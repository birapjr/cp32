#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"
static unsigned char disk[65536],original[65536];
static unsigned writes,fail_at,after,read_fail;
static int raw_read(unsigned at,char *p,int n)
{
  if(read_fail || at>sizeof(disk) || n<0 || (unsigned)n>sizeof(disk)-at)return -1;
  memcpy(p,disk+at,n);return n;
}
static int read_disk(unsigned at,char *p,int n)
{return cp32_cache_read(raw_read,sizeof(disk),at,p,n);}
static int write_disk(unsigned at,const char *p,int n)
{
  assert(n>0 && n<=64 && at+(unsigned)n<=sizeof(disk) && (unsigned)n<=512-at%512);
  writes++;
  if(writes==fail_at && !after)return -1;
  memcpy(disk+at,p,n);
  return writes==fail_at ? -1:n;
}
static void reset(void)
{memcpy(disk,original,sizeof(disk));writes=fail_at=after=read_fail=0;cp32_cache_invalidate();}
static unsigned get16(unsigned at,unsigned swap){return cp32_fs_u16(disk+at,swap);}
static unsigned get32(unsigned at,unsigned swap){return cp32_fs_u32(disk+at,swap);}
static void put16(unsigned at,unsigned n,unsigned swap)
{disk[at+(swap ? 1:0)]=n;disk[at+(swap ? 0:1)]=n>>8;}
static void put32(unsigned at,unsigned n,unsigned swap)
{put16(at,swap ? n>>16:n,swap);put16(at+2,swap ? n:n>>16,swap);}
static void check_file(const char *path,const char *text,unsigned n)
{
  struct cp32_minix_file f;char bytes[64];struct cp32_minix_stat st;
  cp32_cache_invalidate();assert(!cp32_minix_file_open(read_disk,sizeof(disk),path,&f));
  assert(f.size==n && cp32_minix_file_read(&f,bytes,sizeof(bytes))==(int)n && !memcmp(bytes,text,n));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),path,&st));
  assert(st.mode==0100644 && st.links==1 && st.size==n);
  assert(!cp32_fs_allocated(read_disk,2048,st.inode,f.super.swapped));
  assert(!cp32_fs_allocated(read_disk,3072,f.zones[0]-5,f.super.swapped));
  for(unsigned i=n;i<1024;i++)assert(!disk[f.zones[0]*1024+i]);
}
/* All visible entries must reference fully allocated, readable inodes after
 * an injected interruption. Reservations without directory entries may leak. */
static void check_publication(void)
{
  struct cp32_minix_dir dir;unsigned ino;char name[15];int r;
  cp32_cache_invalidate();assert(!cp32_minix_root_open(read_disk,sizeof(disk),&dir));
  while((r=cp32_minix_root_next(&dir,&ino,name))>0) {
    if(!strcmp(name,"note"))check_file("/note","hello",5);
    else assert(!strcmp(name,".") || !strcmp(name,"..") || !strcmp(name,"boot") || !strcmp(name,"readme"));
  }
  assert(!r);
}
static void run(unsigned swap)
{
  struct cp32_minix_stat st;
  reset(); /* Test clearing a reused data zone and a stale free inode. */
  memset(disk+9*1024,0xa7,1024);memset(disk+4288,0xb8,64);
  memcpy(original,disk,sizeof(disk));
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5)==5);
  unsigned steps=writes;assert(steps==23);
  check_file("/note","hello",5);
  assert(get32(4096+8,swap)==80);
  assert(!memcmp(disk+4096+64,original+4096+64,128)); /* old inodes unchanged */
  assert(!memcmp(disk+7*1024,original+7*1024,2*1024)); /* old data unchanged */
  unsigned before=writes;
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","bad",3)==-CP32_FILE_EXISTS && writes==before);
  assert(cp32_minix_patch(read_disk,write_disk,sizeof(disk),"/note",0,"HELLO",5)==5);
  check_file("/note","HELLO",5);
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"12345678901234","X",1)==1);
  check_file("/12345678901234","X",1);
  /* Preflight errors must leave every byte unchanged. */
  reset();
  const char *bad[]={"",".","..","too-long-name15","a/b","/name"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
    assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),bad[i],"X",1)<0);
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","X",0)<0);
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","X",65)<0);
  assert(!writes && !memcmp(disk,original,sizeof(disk)));
  memset(disk+2048,255,1024);put16(2052,1,swap);assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","X",1)==-CP32_FILE_NOSPACE && !writes);
  reset();memset(disk+3072,255,1024);put16(3078,0x3ff,swap);assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","X",1)==-CP32_FILE_NOSPACE && !writes);
  reset();read_fail=1;assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","X",1)==-CP32_SUPER_IO && !writes);
  reset();put16(1034,1,swap);put32(1044,31,swap);
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","X",1)==-CP32_SUPER_UNSUPPORTED && !writes);
  reset();memset(disk+2048,255,1024);memset(disk+3072,255,1024);
  put16(2052,0xfffe,swap);put16(3078,0xfdff,swap); /* final inode32, zone62 */
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"last","X",1)==1);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/last",&st) && st.inode==32);
  assert(get32(4096+31*64+24,swap)==62);check_file("/last","X",1);
  /* Deleted directory slot is reused without extending the parent inode. */
  reset();put16(6*1024+48,0,swap);
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5)==5);
  assert(get32(4104,swap)==64 && writes==22);check_file("/note","hello",5);
  /* Full existing directory block: no directory growth allocation yet. */
  reset();put32(4104,1024,swap);
  for(unsigned at=64;at<1024;at+=16) {put16(6*1024+at,3,swap);memcpy(disk+6*1024+at+2,"used",4);}
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5)==-CP32_FILE_NOSPACE && !writes);
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5)==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0; /* simulated reboot: invalidate volatile state */
    check_publication();
    cp32_cache_invalidate();
    int exists=cp32_minix_stat(read_disk,sizeof(disk),"/note",&st)==0;
    int r=cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5);
    assert(r==(exists ? -CP32_FILE_EXISTS:5));check_file("/note","hello",5);
  }
}
static void run_link(unsigned swap)
{
  struct cp32_minix_stat a,b;unsigned steps;
  reset();
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&a));
  assert(!cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/boot/readme","alias"));
  steps=writes;assert(steps==3);
  cp32_cache_invalidate();
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/alias",&b));
  assert(a.inode==b.inode && b.links==a.links+1 && a.size==b.size);
  assert(!memcmp(disk+2048,original+2048,2048)); /* no inode/zone allocation */
  assert(!memcmp(disk+8*1024,original+8*1024,1024));
  assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/alias","!",1)==1);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&a) && a.size==b.size+1);
  assert(!cp32_minix_truncate(read_disk,write_disk,sizeof(disk),"/alias",0));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&a) && !a.size);
  reset();
  const char *bad[]={"",".","..","long-name-12345","a/b","/alias"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
    assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/readme",bad[i])<0);
  assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/boot","alias")<0);
  assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/missing","alias")<0);
  assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/readme","readme")==-CP32_FILE_EXISTS);
  assert(!writes && !memcmp(disk,original,sizeof(disk)));
  put16(4096+2*64+2,127,swap);
  assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/readme","alias")<0 && !writes);
  reset();put32(4104,1024,swap);
  for(unsigned at=64;at<1024;at+=16) {put16(6*1024+at,3,swap);memcpy(disk+6*1024+at+2,"used",4);}
  assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/readme","alias")==-CP32_FILE_NOSPACE && !writes);
  reset();put16(6*1024+48,0,swap);
  assert(!cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/boot/readme","12345678901234"));
  assert(writes==2 && get32(4104,swap)==64);
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/readme","alias")==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;cp32_cache_invalidate();
    assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&a));
    int exists=cp32_minix_stat(read_disk,sizeof(disk),"/alias",&b)==0;
    assert(a.links>=2+(unsigned)exists);
    if(exists)assert(a.inode==b.inode && a.links==b.links);
    assert(!memcmp(disk+2048,original+2048,2048));
    assert(!memcmp(disk+8*1024,original+8*1024,1024));
  }
}
static void run_unlink(unsigned swap)
{
  struct cp32_minix_stat st;unsigned ino,zone;char bytes[8];
  reset();assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5)==5);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/note",&st));ino=st.inode;
  zone=get32(4096+(ino-1)*64+24,swap);
  assert(!cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/note","alias"));
  int fd=cp32_fd_open(read_disk,sizeof(disk),"/note");assert(fd>=0);
  unsigned before=writes;
  assert(cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/alias")==-CP32_FILE_BUSY && writes==before);
  assert(!cp32_fd_close(fd));
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/note"));
  assert(cp32_minix_stat(read_disk,sizeof(disk),"/note",&st)==-CP32_FILE_NOT_FOUND);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/alias",&st) && st.links==1 && st.size==5);
  fd=cp32_fd_open(read_disk,sizeof(disk),"/alias");assert(fd>=0);
  assert(cp32_fd_read(fd,bytes,8)==5 && !memcmp(bytes,"hello",5));assert(!cp32_fd_close(fd));
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/alias"));
  assert(cp32_minix_stat(read_disk,sizeof(disk),"/alias",&st)==-CP32_FILE_NOT_FOUND);
  assert(!(get16(2048+(ino/16)*2,swap)&(1U<<(ino%16))));
  assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","new",3)==3);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/note",&st) && st.inode==ino);
  assert(get32(4096+(ino-1)*64+24,swap)==zone);check_file("/note","new",3);
  reset();const char *bad[]={"","/",".","..","/boot/.","/readme/","/boot","/missing"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
    assert(cp32_minix_unlink(read_disk,write_disk,sizeof(disk),bad[i])<0 && !writes);
  /* Nested deletion preserves the other existing hard link and its data. */
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/boot/readme"));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&st) && st.links==1);
  assert(!memcmp(disk+8*1024,original+8*1024,1024));
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=2;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/boot/readme")==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;cp32_cache_invalidate();
    int exists=cp32_minix_stat(read_disk,sizeof(disk),"/boot/readme",&st)==0;
    assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&st) && st.links>=1+(unsigned)exists);
    assert(!memcmp(disk+2048,original+2048,2048));
    assert(!memcmp(disk+8*1024,original+8*1024,1024));
  }
  puts("unlink: shared names, nested removal, busy/invalid rejection, inode/zone/name reuse and shared-link write failures pass");
}
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,sizeof(original),f)==sizeof(original));fclose(f);
  unsigned char *little=malloc(sizeof(original));assert(little);memcpy(little,original,sizeof(original));
  run(0);run_link(0);run_unlink(0);memcpy(disk,little,sizeof(disk));
  for(unsigned at=1024;at<1036;at+=2)put16(at,get16(at,0),1);
  put32(1036,get32(1036,0),1);put16(1040,get16(1040,0),1);
  put16(1042,get16(1042,0),1);put32(1044,get32(1044,0),1);
  for(unsigned at=2048;at<4096;at+=2)put16(at,get16(at,0),1);
  for(unsigned inode=0;inode<32;inode++) {
    unsigned at=4096+inode*64;
    for(unsigned k=0;k<8;k+=2)put16(at+k,get16(at+k,0),1);
    for(unsigned k=8;k<64;k+=4)put32(at+k,get32(at+k,0),1);
  }
  for(unsigned zone=6;zone<=7;zone++)for(unsigned at=0;at<1024;at+=16)
    put16(zone*1024+at,get16(zone*1024+at,0),1);
  memcpy(original,disk,sizeof(disk));run(1);run_link(1);run_unlink(1);free(little);
  puts("create: little/big endian, allocation, zeroing, remount, duplicate/full/invalid cases and every interrupted-write boundary pass");
}
