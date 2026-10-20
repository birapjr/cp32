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
  if(f.zones[0]) {
    assert(!cp32_fs_allocated(read_disk,3072,f.zones[0]-5,f.super.swapped));
    for(unsigned i=n;i<1024;i++)assert(!disk[f.zones[0]*1024+i]);
  } else assert(!n);
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
  assert(cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/readme","alias")==-CP32_FILE_LINK_LIMIT && !writes);
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
static void close_all(void){for(int i=0;i<CP32_OPEN_MAX;i++)cp32_fd_close(i);}
static void run_position(void)
{
  char b[4];unsigned pos;int fd,alias;
  for(unsigned commit=0;commit<2;commit++) {
    close_all();reset();
    fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/pos",CP32_O_RDWR|CP32_O_CREAT,0600);assert(fd>=0);
    assert(cp32_fd_write(fd,"abc",3)==3);alias=cp32_fd_dup(fd);assert(alias>=0);
    assert(cp32_fd_pread(alias,b,3,0)==3 && !memcmp(b,"abc",3));
    cp32_cache_invalidate();read_fail=1;assert(cp32_fd_pread(alias,b,1,0)==-CP32_SUPER_IO);read_fail=0;
    writes=0;fail_at=1;after=commit;assert(cp32_fd_pwrite(alias,"X",1,0)==-CP32_SUPER_IO);fail_at=0;
    assert(!cp32_fd_seek(fd,0,1,&pos) && pos==3);
    assert(cp32_fd_pread(alias,b,1,0)==1 && b[0]==(commit ? 'X':'a'));
    assert(cp32_fd_pread(alias,b,1,100)==0);
    assert(!cp32_fd_pwrite(alias,0,0,100) && !cp32_fd_pread(alias,0,0,100));
    assert(!cp32_fd_close(fd) && !cp32_fd_close(alias));
  }
  reset();puts("positional IO: shared offset unchanged on read/write faults, EOF and zero counts pass");
}
static void run_resize(unsigned swap)
{
  unsigned steps,ino,zone,pos;int fd;char b[64];struct cp32_minix_stat st;
  unsigned char prepared[65536];
  close_all();reset();
  fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/resize",CP32_O_CREAT|CP32_O_RDWR,0600);assert(fd>=0);
  assert(cp32_fd_write(fd,"abc",3)==3 && !cp32_fd_fstat(fd,&st));ino=st.inode;
  zone=get32(4096+(ino-1)*64+24,swap);
  memset(disk+zone*1024+3,'X',1021);cp32_cache_invalidate();
  memcpy(prepared,disk,sizeof(disk));writes=0;
  assert(!cp32_fd_truncate(fd,8193));steps=writes;
  assert(!cp32_fd_seek(fd,0,1,&pos) && pos==3);
  assert(!memcmp(disk+2048,prepared+2048,2048)); /* sparse growth allocates nothing */
  assert(!cp32_fd_seek(fd,0,0,0) && cp32_fd_read(fd,b,64)==64 && !memcmp(b,"abc",3));
  for(unsigned i=3;i<64;i++)assert(!b[i]);
  assert(!cp32_fd_truncate(fd,263*1024));
  assert(!cp32_fd_seek(fd,263*1024-1,0,0) && cp32_fd_read(fd,b,1)==1 && !b[0]);
  assert(cp32_fd_truncate(fd,263*1024+1)==-CP32_FILE_BIG);
  assert(!cp32_fd_close(fd));
  for(unsigned commit=0;commit<2;commit++)for(unsigned step=1;step<=steps;step++) {
    memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=fail_at=0;
    fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/resize",CP32_O_RDWR,0);assert(fd>=0);
    fail_at=step;after=commit;
    assert(cp32_fd_truncate(fd,8193)==-CP32_SUPER_IO);fail_at=0;
    assert(!cp32_fd_fstat(fd,&st));assert(st.size==3 || (commit && step==steps && st.size==8193));
    assert(cp32_fd_read(fd,b,64)==(st.size==3 ? 3:64) && !memcmp(b,"abc",3));
    if(st.size>3)for(unsigned i=3;i<64;i++)assert(!b[i]);
    assert(!memcmp(disk+2048,prepared+2048,2048));assert(!cp32_fd_close(fd));
  }
  reset();puts("resize: both endian sparse growth, stale tail clearing, bounds, offsets and every growth write fault pass");
}
static void run_writable(unsigned swap)
{
  int fd,alias,other,r;unsigned pos,steps;char bytes[64];
  struct cp32_minix_stat st;unsigned char prepared[65536];
  close_all();reset();
  fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_CREAT|CP32_O_EXCL|CP32_O_RDWR,0600);assert(fd==0);
  alias=cp32_fd_dup(fd);assert(alias==1);
  assert(cp32_fd_write(fd,"abc",3)==3);
  assert(!cp32_fd_seek(alias,0,1,&pos) && pos==3);
  other=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_RDWR,0);assert(other==2);
  assert(cp32_fd_read(other,bytes,64)==3 && !memcmp(bytes,"abc",3));
  assert(!cp32_fd_seek(fd,510,0,0));assert(cp32_fd_write(fd,"ABCD",4)==4);
  assert(!cp32_fd_seek(other,500,0,0));assert(cp32_fd_read(other,bytes,14)==14);
  for(unsigned i=0;i<10;i++)assert(!bytes[i]);assert(!memcmp(bytes+10,"ABCD",4));
  assert(!cp32_fd_seek(fd,7*1024-2,0,0));assert(cp32_fd_write(fd,"WXYZ",4)==4);
  assert(!cp32_fd_seek(other,7*1024-2,0,0));assert(cp32_fd_read(other,bytes,4)==2 && !memcmp(bytes,"WX",2));
  assert(cp32_fd_read(other,bytes,2)==2 && !memcmp(bytes,"YZ",2));
  assert(!cp32_fd_seek(fd,1500,0,0));assert(cp32_fd_write(fd,"q",1)==1); /* fill a hole */
  assert(!cp32_fd_seek(other,1498,0,0));assert(cp32_fd_read(other,bytes,4)==4 && !bytes[0] && !bytes[1] && bytes[2]=='q' && !bytes[3]);
  assert(cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_CREAT|CP32_O_EXCL|CP32_O_RDWR,0)==-CP32_FILE_EXISTS);
  assert(!cp32_fd_fstat(fd,&st) && st.size==7170);
  assert(!cp32_fd_fcntl(alias,CP32_F_SETFL,CP32_O_APPEND));
  assert((cp32_fd_fcntl(fd,CP32_F_GETFL,0)&3)==CP32_O_RDWR);
  assert(!cp32_fd_seek(fd,0,0,0) && cp32_fd_write(alias,"!",1)==1);
  assert(!cp32_fd_seek(other,0,2,&pos) && pos==7171);
  assert(!cp32_fd_close(alias));
  alias=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_WRONLY|CP32_O_TRUNC,0);assert(alias==1);
  assert(!cp32_fd_fstat(other,&st) && !st.size);
  assert(cp32_fd_read(alias,bytes,1)==-CP32_FILE_BAD_FD);
  assert(cp32_fd_write(alias,"z",1)==1);
  assert(!cp32_fd_seek(other,0,0,0));assert(cp32_fd_read(other,bytes,1)==1 && bytes[0]=='z');
  assert(cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_RDONLY|CP32_O_TRUNC,0)==-CP32_SUPER_INVALID);
  assert(cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/boot",CP32_O_RDWR,0)==-CP32_FILE_IS_DIR);
  assert(cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/readme",CP32_O_WRONLY,0)==-CP32_FILE_ACCESS);
  assert(cp32_fd_write(alias,0,0)==0);
  assert(!cp32_fd_seek(alias,263*1024,0,0));assert(cp32_fd_write(alias,"x",1)==-CP32_FILE_BIG);
  close_all();
  fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_RDONLY,0);assert(fd==0);
  assert(cp32_fd_write(fd,"x",1)==-CP32_FILE_BAD_FD);close_all();
  /* Saturated descriptors must not create/truncate anything. */
  for(unsigned i=0;i<CP32_OPEN_MAX;i++)assert(cp32_fd_open(read_disk,sizeof(disk),"/w")== (int)i);
  writes=0;assert(cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/none",CP32_O_CREAT|CP32_O_RDWR,0600)==-CP32_FILE_LIMIT && !writes);close_all();
  /* Interrupted mapped writes: exact confirmed prefix, unchanged offset on
   * negative returns; every visible mapping remains allocated/decodable. */
  reset();assert(!cp32_minix_create(read_disk,write_disk,sizeof(disk),"/w",0,0));
  memcpy(prepared,disk,sizeof(disk));fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_RDWR,0);
  writes=0;assert(!cp32_fd_seek(fd,500,0,0));memset(bytes,'x',64);assert(cp32_fd_write(fd,bytes,64)==64);steps=writes;close_all();
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=0;fail_at=fail;after=commit;
    fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_RDWR,0);assert(fd==0);
    assert(!cp32_fd_seek(fd,500,0,0));r=cp32_fd_write(fd,bytes,64);
    assert(r==-CP32_SUPER_IO || r==12);assert(writes==fail);fail_at=0;
    assert(!cp32_fd_seek(fd,0,1,&pos) && pos==500+(r>0 ? (unsigned)r:0));
    assert(!cp32_fd_fstat(fd,&st) && st.size<=564);
    struct cp32_minix_file f;assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/w",&f));
    close_all();
  }
  reset();assert(!cp32_minix_create(read_disk,write_disk,sizeof(disk),"/w",0,0));
  memset(disk+3072,255,1024);cp32_cache_invalidate();
  fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/w",CP32_O_RDWR,0);
  assert(cp32_fd_write(fd,"x",1)==-CP32_FILE_NOSPACE);
  assert(!cp32_fd_fstat(fd,&st) && st.size==0);close_all();
  (void)swap;puts("writable descriptors: modes, coherence, dup/append, holes, indirect boundary, truncation, limits and every write fault pass");
}
static void run_empty(unsigned swap)
{
  struct cp32_minix_stat st;struct cp32_minix_file f;char c;
  unsigned char prepared[65536];unsigned steps;
  reset();
  assert(!cp32_minix_create(read_disk,write_disk,sizeof(disk),"/e",0,0));steps=writes;
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/e",&st) && !st.size && st.links==1);
  for(unsigned i=24;i<64;i++)assert(!disk[4096+(st.inode-1)*64+i]);
  assert(!memcmp(disk+3072,original+3072,1024));
  assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/e",&f));
  assert(cp32_minix_file_read(&f,&c,1)==0);
  assert(!cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/e","/f"));
  assert(!memcmp(disk+3072,original+3072,1024));
  assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/e","x",1)==1);
  check_file("/e","x",1);check_file("/f","",0);
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/e"));
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/f"));
  assert(!memcmp(disk+2048,original+2048,2048));
  reset();memset(disk+3072,255,1024);memcpy(prepared,disk,sizeof(disk));
  assert(!cp32_minix_create(read_disk,write_disk,sizeof(disk),"/e",0,0));
  assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/e","x",1)==-CP32_FILE_NOSPACE);
  check_file("/e","",0);
  assert(!memcmp(disk+3072,prepared+3072,1024));
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/e",0,0)==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;cp32_cache_invalidate();
    if(!cp32_minix_stat(read_disk,sizeof(disk),"/e",&st)) {
      assert(!st.size && (get16(2048+st.inode/16*2,swap)&(1U<<(st.inode%16))));
    }
    assert(!memcmp(disk+3072,original+3072,1024));
  }
  puts("empty create: zero-zone inode, copy, first append, full data bitmap and interrupted writes pass");
}
static void run_copy(void)
{
  struct cp32_minix_stat a,b;struct cp32_minix_file f;char block[64],out[64];
  unsigned char prepared[65536];unsigned steps;
  reset();memset(block,'x',64);
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/a",block,64)==64);
  for(unsigned i=0;i<16;i++)assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/a",block,64)==64);
  assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/copies"));
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/a",0751));
  memcpy(prepared,disk,sizeof(disk));writes=0;
  assert(!cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/a","/copies/b"));steps=writes;
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/a",&a));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/copies/b",&b));
  assert(a.inode!=b.inode && b.links==1 && b.size==1088 && b.mode==a.mode);
  assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/copies/b",&f));
  for(unsigned i=0;i<17;i++)assert(cp32_minix_file_read(&f,out,64)==64 && !memcmp(out,block,64));
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/copies/b",0644));
  assert(cp32_minix_patch(read_disk,write_disk,sizeof(disk),"/copies/b",0,"Y",1)==1);
  assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/a",&f));
  assert(cp32_minix_file_read(&f,out,64)==64 && !memcmp(out,block,64));
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/a"));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/copies/b",&b) && b.size==1088);
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/copies/b",0644));
  assert(!cp32_minix_truncate(read_disk,write_disk,sizeof(disk),"/copies/b",0));
  assert(!cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/copies/b","/empty"));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/empty",&b) && b.size==0);
  writes=0;
  assert(cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/empty","/empty")==-CP32_FILE_EXISTS);
  assert(cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/empty","/copies/b")==-CP32_FILE_EXISTS);
  assert(cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/copies","/bad")==-CP32_FILE_IS_DIR);
  assert(cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/empty","/missing/b")<0);
  assert(!writes);
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=0;fail_at=fail;after=commit;
    assert(cp32_minix_copy(read_disk,write_disk,sizeof(disk),"/a","/copies/b")==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;
    assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/a",&f));
    for(unsigned i=0;i<17;i++)assert(cp32_minix_file_read(&f,out,64)==64 && !memcmp(out,block,64));
    if(!cp32_minix_stat(read_disk,sizeof(disk),"/copies/b",&b)) {
      assert(b.size<=1088 && b.inode!=a.inode);
      if(b.mode&0111)assert(b.size==1088);
    }
  }
  puts("copy: independent inode/data, nested paths, modes, empty files, rejection and every write failure pass");
}
static void run_patch_span(unsigned swap)
{
  char text[64],bytes[1100];unsigned char prepared[65536];
  struct cp32_minix_file f;struct cp32_minix_stat st;
  memset(text,'a',sizeof(text));reset();
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/span",text,64)==64);
  /* Occupy the next zone so logical neighbors are physically disjoint. */
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/gap","gap",3)==3);
  for(unsigned i=0;i<16;i++)assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/span",text,64)==64);
  assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/span",&f));
  assert(f.zones[1]!=f.zones[0]+1);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/span",&st));
  memcpy(prepared,disk,sizeof(disk));memset(text,'Z',sizeof(text));
  const unsigned offsets[]={480,511,512,1000,1023,1024};
  for(unsigned j=0;j<sizeof(offsets)/sizeof(*offsets);j++) {
    memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=0;
    unsigned offset=offsets[j];
    assert(cp32_minix_patch(read_disk,write_disk,sizeof(disk),"/span",offset,text,64)==64);
    assert(writes==(offset%512>448 ? 2:1));
    assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/span",&f));
    for(unsigned at=0;at<1088;at+=64)
      assert(cp32_minix_file_read(&f,bytes+at,64)==64);
    for(unsigned k=0;k<1088;k++)assert(bytes[k]==(k>=offset && k<offset+64 ? 'Z':'a'));
    assert(!memcmp(disk,prepared,6*1024)); /* metadata and bitmaps unchanged */
    check_file("/gap","gap",3);
  }
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=2;fail++) {
    memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=0;fail_at=fail;after=commit;
    assert(cp32_minix_patch(read_disk,write_disk,sizeof(disk),"/span",1000,text,64)==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;
    assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/span",&f));
    for(unsigned at=0;at<1088;at+=64)
      assert(cp32_minix_file_read(&f,bytes+at,64)==64);
    unsigned changed=fail==1 ? (commit ? 24:0):(commit ? 64:24);
    for(unsigned k=0;k<1088;k++)assert(bytes[k]==(k>=1000 && k<1000+changed ? 'Z':'a'));
  }
  memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=0;
  put32(4096+(st.inode-1)*64+28,0,swap); /* missing second data zone */
  assert(cp32_minix_patch(read_disk,write_disk,sizeof(disk),"/span",1000,text,64)==-CP32_SUPER_UNSUPPORTED);
  assert(!writes); /* validate the second mapping before changing the first */
  memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();fail_at=0;
  memset(text,'a',64);
  for(unsigned j=0;j<96;j++)assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/span",text,64)==64);
  memset(text,'Z',64);writes=0;
  assert(cp32_minix_patch(read_disk,write_disk,sizeof(disk),"/span",7160,text,64)==64 && writes==2);
  assert(!cp32_minix_file_open(read_disk,sizeof(disk),"/span",&f));f.position=7104;
  assert(cp32_minix_file_read(&f,bytes,64)==64);
  assert(cp32_minix_file_read(&f,bytes+64,64)==64);
  for(unsigned j=0;j<128;j++)assert(bytes[j]==(j>=56 && j<120 ? 'Z':'a'));
  puts("patch span: sector/noncontiguous zone boundaries, preserved bytes, preflight and partial I/O failures pass");
}
static void run_link_path(void)
{
  struct cp32_minix_stat a,b,parent;
  unsigned char prepared[65536];
  reset();assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/links"));
  memcpy(prepared,disk,sizeof(disk));writes=0;
  const char *bad[]={"/missing/a","/readme/a","/links/","/links/.","/links/.."};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
    assert(cp32_minix_link(read_disk,write_disk,sizeof(disk),"/readme",bad[i])<0);
  assert(!writes);
  assert(!cp32_minix_link(read_disk,write_disk,sizeof(disk),"/readme","/links/alias"));
  assert(writes==3);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&a));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/links/alias",&b));
  assert(a.inode==b.inode && a.links==3 && b.links==3);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/links",&parent) && parent.links==2 && parent.size==48);
  assert(cp32_minix_link(read_disk,write_disk,sizeof(disk),"/readme","/links/alias")==-CP32_FILE_EXISTS);
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/links/alias"));
  writes=0;
  assert(!cp32_minix_link(read_disk,write_disk,sizeof(disk),"/readme","/links/reused"));
  assert(writes==2); /* Reuse tombstone without extending parent. */
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=3;fail++) {
    memcpy(disk,prepared,sizeof(disk));cp32_cache_invalidate();writes=0;fail_at=fail;after=commit;
    assert(cp32_minix_link(read_disk,write_disk,sizeof(disk),"/readme","/links/alias")==-CP32_SUPER_IO);
    fail_at=0;cp32_cache_invalidate();
    assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&a));
    int visible=!cp32_minix_stat(read_disk,sizeof(disk),"/links/alias",&b);
    assert(a.links>=2+(unsigned)visible);
    if(visible)assert(a.inode==b.inode);
    assert(!memcmp(disk+2048,prepared+2048,2048));
  }
  puts("nested link: parent lookup, shared inode/count, tombstone reuse and write failures pass");
}
static void run_unlink(unsigned swap)
{
  struct cp32_minix_stat st;unsigned ino,zone;char bytes[8];
  reset();assert(cp32_minix_create_root(read_disk,write_disk,sizeof(disk),"note","hello",5)==5);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/note",&st));ino=st.inode;
  zone=get32(4096+(ino-1)*64+24,swap);
  assert(!cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/note","alias"));
  int fd=cp32_fd_open(read_disk,sizeof(disk),"/note");assert(fd>=0);
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/alias"));
  assert(!cp32_fd_fstat(fd,&st) && st.links==1);
  assert(!cp32_minix_link_root(read_disk,write_disk,sizeof(disk),"/note","alias"));
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
  reset();fd=cp32_fd_open_mode(read_disk,write_disk,sizeof(disk),"/gone",CP32_O_CREAT|CP32_O_RDWR,0600);assert(fd>=0);
  assert(cp32_fd_write(fd,"old",3)==3 && !cp32_fd_fstat(fd,&st));ino=st.inode;
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/gone"));
  assert(!cp32_fd_fstat(fd,&st) && !st.links);
  assert(!cp32_fd_seek(fd,7168,0,0) && cp32_fd_write(fd,"tail",4)==4);
  assert(!cp32_fd_close(fd));assert(!(get16(2048+(ino/16)*2,swap)&(1U<<(ino%16))));
  assert(!memcmp(disk+2048,original+2048,2048));
  puts("unlink: shared names, open-inode lifetime, indirect growth, byte orders and reclamation pass");
}
static void run_rename(unsigned swap)
{
  struct cp32_minix_stat old,now;char bytes[4];
  const char *destinations[]={"/renamed","/boot/renamed"};
  for(unsigned d=0;d<2;d++) {
    reset();assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&old));
    int fd=cp32_fd_open(read_disk,sizeof(disk),"/readme");assert(fd>=0);
    assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme",destinations[d]));
    unsigned steps=writes;assert(steps==(d ? 5U:1U));
    cp32_cache_invalidate();
    assert(cp32_minix_stat(read_disk,sizeof(disk),"/readme",&now)==-CP32_FILE_NOT_FOUND);
    assert(!cp32_minix_stat(read_disk,sizeof(disk),destinations[d],&now));
    assert(old.inode==now.inode && old.links==now.links && old.size==now.size);
    assert(cp32_fd_read(fd,bytes,4)==4 && !memcmp(bytes,"CP32",4));
    assert(!cp32_fd_close(fd));
    assert(!memcmp(disk+2048,original+2048,2048));
    assert(!memcmp(disk+4224,original+4224,64));
    assert(!memcmp(disk+8*1024,original+8*1024,1024));
    for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
      reset();fail_at=fail;after=commit;
      assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme",destinations[d])==-CP32_SUPER_IO);
      assert(writes==fail);fail_at=0;cp32_cache_invalidate();
      unsigned visible=0;
      if(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&now)) {visible++;assert(now.inode==old.inode);}
      if(!cp32_minix_stat(read_disk,sizeof(disk),destinations[d],&now)) {visible++;assert(now.inode==old.inode);}
      assert(visible>=1);
      assert(!cp32_minix_stat(read_disk,sizeof(disk),"/boot/readme",&now));
      assert(now.links>=visible+1 && now.size==old.size);
      assert(!memcmp(disk+2048,original+2048,2048));
      assert(!memcmp(disk+8*1024,original+8*1024,1024));
    }
  }
  reset();
  assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/readme") && !writes);
  assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/boot/readme") && !writes);
  const char *bad[]={"","/","/boot/.","/boot/..","/renamed/","/123456789012345","/missing/file"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
    assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme",bad[i])<0 && !writes);
  assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/missing","/new")<0 && !writes);
  assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/boot","/new")<0 && !writes);
  assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/boot")==-CP32_FILE_EXISTS && !writes);
  put16(4226,127,swap);cp32_cache_invalidate();
  assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/boot/new")<0 && !writes);
  assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/12345678901234") && writes==1);
  reset();put32(4104,1024,swap);
  for(unsigned at=64;at<1024;at+=16) {put16(6*1024+at,3,swap);memcpy(disk+6*1024+at+2,"used",4);}
  assert(cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/boot/readme","/new")==-CP32_FILE_NOSPACE && !writes);
  /* In-place rename needs no free directory slot, even in a full directory. */
  assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/new") && writes==1);
  reset(); /* Move back from /boot into a deleted root slot without growing it. */
  assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/readme","/boot/moved"));
  writes=0;assert(!cp32_minix_rename(read_disk,write_disk,sizeof(disk),"/boot/moved","/back"));
  assert(writes==4 && get32(4104,swap)==64);
  puts("rename: both parents, no-op aliases, shared/open inode identity, full/deleted slots, invalid inputs and all write failures pass");
}
static void run_chmod(unsigned swap)
{
  struct cp32_minix_stat info;unsigned inode_at=4224;
  reset();int fd=cp32_fd_open(read_disk,sizeof(disk),"/readme");assert(fd>=0);
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/boot/readme",0755));
  assert(writes==1 && get16(inode_at,swap)==0100755);
  assert(!cp32_fd_fstat(fd,&info) && info.mode==0100755 && info.links==2);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/readme",&info) && info.mode==0100755);
  assert(!memcmp(disk,original,inode_at) && !memcmp(disk+inode_at+2,original+inode_at+2,sizeof(disk)-inode_at-2));
  assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/readme","X",1)==-CP32_SUPER_UNSUPPORTED && writes==1);
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",0755) && writes==1);
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",0));
  assert(!cp32_fd_fstat(fd,&info) && info.mode==0100000);assert(!cp32_fd_close(fd));
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",0644));
  assert(cp32_minix_append(read_disk,write_disk,sizeof(disk),"/readme","X",1)==1);
  reset();assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/boot",0700));
  assert(get16(4160,swap)==0040700 && writes==1);
  reset();put16(inode_at,0107744,swap);cp32_cache_invalidate();
  assert(!cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",0644));
  assert(get16(inode_at,swap)==0100644); /* unsupported special bits cleared */
  reset();
  assert(cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",01000)<0);
  assert(cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/missing",0644)<0);
  assert(cp32_minix_chmod(read_disk,write_disk,sizeof(disk),0,0644)<0);
  assert(!writes && !memcmp(disk,original,sizeof(disk)));
  read_fail=1;assert(cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",0644)==-CP32_SUPER_IO && !writes);
  for(unsigned commit=0;commit<2;commit++) {
    reset();fail_at=1;after=commit;
    assert(cp32_minix_chmod(read_disk,write_disk,sizeof(disk),"/readme",0755)==-CP32_SUPER_IO && writes==1);
    assert(get16(inode_at,swap)==(commit ? 0100755:0100444));
    assert(!memcmp(disk,original,inode_at) && !memcmp(disk+inode_at+2,original+inode_at+2,sizeof(disk)-inode_at-2));
  }
  puts("chmod: endian modes, aliases/fstat, directories, no-op, special-bit clearing, execute guard and write failures pass");
}
static void run_create_path(unsigned swap)
{
  struct cp32_minix_stat st;char path[256];
  reset();assert(!cp32_minix_abspath("/boot","note",path));
  assert(!strcmp(path,"/boot/note"));
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),path,"nested",6)==6);
  unsigned steps=writes;assert(steps==23);
  check_file("/boot/note","nested",6);
  assert(!memcmp(disk+4096,original+4096,64)); /* root inode must not grow */
  assert(!memcmp(disk+6*1024,original+6*1024,1024)); /* root entries unchanged */
  assert(get32(4160+8,swap)==64); /* fixture boot48 ->64 */
  unsigned before=writes;
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),path,"replace",7)==-CP32_FILE_EXISTS && writes==before);
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/note","root",4)==4);
  check_file("/note","root",4);check_file("/boot/note","nested",6);
  reset();const char *bad[]={"","/","/boot/.","/boot/..","/boot/file/","/missing/file","/readme/file","/boot/123456789012345"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)
    assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),bad[i],"X",1)<0 && !writes);
  /* Reuse a deleted nested slot, retaining directory size and root metadata. */
  reset();put16(7*1024+32,0,swap);
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/boot/note","nested",6)==6);
  assert(writes==22 && get32(4168,swap)==48);check_file("/boot/note","nested",6);
  reset();put32(4168,1024,swap);
  for(unsigned at=48;at<1024;at+=16) {put16(7*1024+at,3,swap);memcpy(disk+7*1024+at+2,"used",4);}
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/boot/note","nested",6)==-CP32_FILE_NOSPACE && !writes);
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/boot/note","nested",6)==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;cp32_cache_invalidate();
    if(!cp32_minix_stat(read_disk,sizeof(disk),"/boot/note",&st))check_file("/boot/note","nested",6);
    assert(!memcmp(disk+4096,original+4096,64));
    assert(!memcmp(disk+6*1024,original+6*1024,1024));
  }
  puts("create path: nested/cwd names, parent-only growth, independent names, reuse/full/rejection and all write failures pass");
}
static void check_directory(const char *path,unsigned parent)
{
  struct cp32_minix_dir dir;struct cp32_minix_stat st;unsigned ino;char name[15];
  cp32_cache_invalidate();assert(!cp32_minix_stat(read_disk,sizeof(disk),path,&st));
  assert(st.mode==0040755 && st.links==2 && st.size==32);
  assert(!cp32_minix_dir_open(read_disk,sizeof(disk),path,&dir));
  assert(cp32_minix_root_next(&dir,&ino,name)==1 && ino==st.inode && !strcmp(name,"."));
  assert(cp32_minix_root_next(&dir,&ino,name)==1 && ino==parent && !strcmp(name,".."));
  assert(!cp32_minix_root_next(&dir,&ino,name));
}
static void run_mkdir(unsigned swap)
{
  struct cp32_minix_stat st;char cwd[256]="/";
  reset();assert(!cp32_minix_mkdir_mode(read_disk,write_disk,sizeof(disk),"/boot/work",0700));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/boot/work",&st) && st.mode==0040700);
  reset();assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work"));
  unsigned steps=writes;assert(steps==24);check_directory("/boot/work",2);
  assert(get16(4162,swap)==3);assert(!memcmp(disk+4096,original+4096,64));
  assert(!cp32_minix_chdir(read_disk,sizeof(disk),cwd,"/boot/work"));
  assert(!strcmp(cwd,"/boot/work"));
  assert(!cp32_minix_chdir(read_disk,sizeof(disk),cwd,"..") && !strcmp(cwd,"/boot"));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/boot/work",&st));unsigned parent=st.inode;
  assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work/sub"));
  check_directory("/boot/work/sub",parent);
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/boot/work",&st) && st.links==3);
  assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/boot/work/sub/note","hello",5)==5);
  check_file("/boot/work/sub/note","hello",5);
  unsigned before=writes;
  assert(cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work")==-CP32_FILE_EXISTS && writes==before);
  reset();const char *bad[]={"","/","/boot/.","/boot/..","/boot/new/","/missing/new","/readme/new","/123456789012345"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),bad[i])<0 && !writes);
  put16(4162,127,swap);cp32_cache_invalidate();
  assert(cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work")==-CP32_SUPER_UNSUPPORTED && !writes);
  reset();memset(disk+2048,255,1024);assert(cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work")==-CP32_FILE_NOSPACE && !writes);
  reset();memset(disk+3072,255,1024);assert(cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work")==-CP32_FILE_NOSPACE && !writes);
  reset();put16(7*1024+32,0,swap);
  assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work"));
  assert(writes==23 && get32(4168,swap)==48);check_directory("/boot/work",2);
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/boot/work")==-CP32_SUPER_IO);
    assert(writes==fail);fail_at=0;cp32_cache_invalidate();
    int exists=cp32_minix_stat(read_disk,sizeof(disk),"/boot/work",&st)==0;
    if(exists)check_directory("/boot/work",2);
    assert(get16(4162,swap)>=2+(unsigned)exists);
    assert(!memcmp(disk+4096,original+4096,64));
    assert(!memcmp(disk+6*1024,original+6*1024,1024));
  }
  puts("mkdir: dot links, nested traversal/file creation, parent counts, allocation errors and every publication fault pass");
}
static void run_rmdir(unsigned swap)
{
  unsigned char saved[65536],prepared[65536];struct cp32_minix_stat st;
  memcpy(saved,original,sizeof(saved));reset();
  assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/work"));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/work",&st));unsigned ino=st.inode;
  unsigned zone=get32(4096+(ino-1)*64+24,swap),links=get16(4098,swap);
  memcpy(prepared,disk,sizeof(prepared));memcpy(original,disk,sizeof(disk));reset();
  assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/work")==-CP32_FILE_BUSY && !writes);
  int fd=cp32_fd_open(read_disk,sizeof(disk),"/work");assert(fd>=0);
  assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/")==-CP32_FILE_BUSY && !writes);
  assert(!cp32_fd_close(fd));
  assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/readme","/")==-CP32_FILE_NOT_DIR && !writes);
  const char *bad[]={"/",".","..","/work/.","/work/..","/missing"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),bad[i],"/")<0 && !writes);
  assert(!cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/"));
  unsigned steps=writes;assert(steps==5 && get16(4098,swap)==links-1);
  assert(cp32_minix_stat(read_disk,sizeof(disk),"/work",&st)==-CP32_FILE_NOT_FOUND);
  assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/work"));
  assert(!cp32_minix_stat(read_disk,sizeof(disk),"/work",&st) && st.inode==ino);
  assert(get32(4096+(ino-1)*64+24,swap)==zone);check_directory("/work",1);
  reset();assert(cp32_minix_create(read_disk,write_disk,sizeof(disk),"/work/note","x",1)==1);
  unsigned before=writes;assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/")==-CP32_FILE_NOT_EMPTY && writes==before);
  assert(!cp32_minix_unlink(read_disk,write_disk,sizeof(disk),"/work/note"));
  assert(!cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/")); /* tombstones are empty */
  reset();assert(!cp32_minix_mkdir(read_disk,write_disk,sizeof(disk),"/work/sub"));
  before=writes;assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/")==-CP32_FILE_NOT_EMPTY && writes==before);
  assert(!cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work/sub","/"));
  assert(!cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/"));
  reset();put16(zone*1024+16,2,swap);cp32_cache_invalidate();
  assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/")==-CP32_SUPER_INVALID && !writes);
  for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
    reset();fail_at=fail;after=commit;
    assert(cp32_minix_rmdir(read_disk,write_disk,sizeof(disk),"/work","/")==-CP32_SUPER_IO && writes==fail);
    fail_at=0;cp32_cache_invalidate();
    if(!cp32_minix_stat(read_disk,sizeof(disk),"/work",&st)) {
      check_directory("/work",1);assert(get16(4098,swap)==links);
      assert(!memcmp(disk+2048,prepared+2048,2048));
    }
    if(get16(4096+(ino-1)*64,swap)) {
      assert(get16(4098,swap)==links);
      assert(!memcmp(disk+2048,prepared+2048,2048));
    }
    assert(get16(4098,swap)>=links-1);
  }
  memcpy(original,saved,sizeof(saved));reset();
  puts("rmdir: emptiness, dot validation, cwd/open/root protection, counts/reuse and every write failure pass");
}
int main(int argc,char **argv)
{
  assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
  assert(fread(original,1,sizeof(original),f)==sizeof(original));fclose(f);
  unsigned char *little=malloc(sizeof(original));assert(little);memcpy(little,original,sizeof(original));
  run(0);run_link(0);run_link_path();run_patch_span(0);run_copy();run_empty(0);run_writable(0);run_resize(0);run_position();run_unlink(0);run_rename(0);run_chmod(0);run_create_path(0);run_mkdir(0);run_rmdir(0);memcpy(disk,little,sizeof(disk));
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
  memcpy(original,disk,sizeof(disk));run(1);run_link(1);run_link_path();run_patch_span(1);run_copy();run_empty(1);run_writable(1);run_resize(1);run_position();run_unlink(1);run_rename(1);run_chmod(1);run_create_path(1);run_mkdir(1);run_rmdir(1);free(little);
  puts("create: little/big endian, allocation, zeroing, remount, duplicate/full/invalid cases and every interrupted-write boundary pass");
}
