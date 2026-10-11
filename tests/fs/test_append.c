#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"
static unsigned char disk[65536],base[65536],start[65536],prefix[263*1024];
static unsigned writes,fail_at,after,swap;
static int raw_read(unsigned at,char *p,int n)
{assert(n>=0 && at+(unsigned)n<=sizeof(disk));memcpy(p,disk+at,n);return n;}
static int reader(unsigned at,char *p,int n)
{return cp32_cache_read(raw_read,sizeof(disk),at,p,n);}
static int writer(unsigned at,const char *p,int n)
{
 assert(n>0 && n<=64 && at+(unsigned)n<=sizeof(disk) && (unsigned)n<=512-at%512);
 writes++;if(writes==fail_at && !after)return -1;
 memcpy(disk+at,p,n);return writes==fail_at ? -1:n;
}
static unsigned get16(unsigned at){return cp32_fs_u16(disk+at,swap);}
static unsigned get32(unsigned at){return cp32_fs_u32(disk+at,swap);}
static void put16(unsigned at,unsigned v)
{disk[at+(swap ? 1:0)]=v;disk[at+(swap ? 0:1)]=v>>8;}
static void put32(unsigned at,unsigned v)
{put16(at,swap ? v>>16:v);put16(at+2,swap ? v:v>>16);}
static void mark(unsigned zone)
{unsigned bit=zone-5,at=3072+bit/16*2;put16(at,get16(at)|(1U<<(bit%16)));}
static void reset(void)
{memcpy(disk,start,sizeof(disk));writes=fail_at=after=0;cp32_cache_invalidate();}
static void read_all(unsigned char *out,unsigned size)
{
 struct cp32_minix_file f;assert(!cp32_minix_file_open(reader,sizeof(disk),"/readme",&f));
 assert(f.size==size);unsigned done=0;while(done<size) {
   int n=cp32_minix_file_read(&f,(char *)out+done,size-done);assert(n>0);done+=n;
 }
}
static void prepare(unsigned size)
{
 memcpy(disk,base,sizeof(disk));
 unsigned used=(size+1023)/1024;
 memset(disk+4224+24,0,40);put32(4232,size);
 for(unsigned i=0;i<used && i<7;i++) {
   unsigned z=9+i;put32(4224+24+i*4,z);mark(z);memset(disk+z*1024,'A'+i,1024);
 }
 if(used>7) {
   mark(16);mark(17);put32(4224+52,16);memset(disk+16*1024,0,1024);
   put32(16*1024+(used-8)*4,17);memset(disk+17*1024,'Z',1024);
 }
 for(unsigned z=6;z<63;z++) {
   unsigned bit=z-5;if(!(get16(3072+(bit/16)*2)&(1U<<(bit%16))))memset(disk+z*1024,0xa7,1024);
 }
 cp32_cache_invalidate();read_all(prefix,size);
 memcpy(start,disk,sizeof(disk));reset();
}
static void check(unsigned old,unsigned n,const char *text)
{
 struct cp32_minix_file f;char buf[64];
 cp32_cache_invalidate();assert(!cp32_minix_file_open(reader,sizeof(disk),"/readme",&f));
 assert(f.size==old || f.size==old+n);
 unsigned done=0;
 while(done<old) {
   unsigned want=old-done;if(want>64)want=64;
   int r=cp32_minix_file_read(&f,buf,want);assert(r>0 && !memcmp(buf,prefix+done,r));done+=r;
 }
 if(f.size==old+n) {
   done=0;while(done<n) {
     int r=cp32_minix_file_read(&f,buf,n-done);assert(r>0 && !memcmp(buf,text+done,r));done+=r;
   }
 }
 /* Other inodes, root directory and pre-existing readme zone8 stay intact. */
 assert(!memcmp(disk+4096,start+4096,128));
 assert(!memcmp(disk+6*1024,start+6*1024,3*1024));
}
static void run(void)
{
 static const char text[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!!";
 unsigned sizes[]={0,8,500,1000,1024,7160,7168,8190,8192,262*1024,263*1024-64};
 for(unsigned k=0;k<sizeof(sizes)/sizeof(*sizes);k++) {
   unsigned size=sizes[k];prepare(size);
   assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,64)==64);
   unsigned steps=writes;assert(get32(4232)==size+64);check(size,64,text);
   if((size+64+1023)/1024>(size+1023)/1024) {
     struct cp32_minix_file file;unsigned zone;
     assert(!cp32_minix_file_open(reader,sizeof(disk),"/readme",&file));
     assert(!cp32_fs_read_map(&file,(size+63)/1024,&zone) && zone);
     for(unsigned i=(size+64)%1024;i && i<1024;i++)assert(!disk[zone*1024+i]);
   }
   for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
     reset();fail_at=fail;after=commit;
     assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,64)==-CP32_SUPER_IO && writes==fail);
     fail_at=0;check(size,64,text); /* remount/readback: old EOF or complete append */
   }
 }
 prepare(1000);memset(disk+3072,255,1024);cp32_cache_invalidate();
 assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,64)==-CP32_FILE_NOSPACE && !writes);
 prepare(7168);memset(disk+3072,255,1024);put16(3074,get16(3074)&~1U);cp32_cache_invalidate();
 assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,64)==-CP32_FILE_NOSPACE && !writes); /* one zone free, needs two */
 prepare(263*1024);assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,1)==-CP32_SUPER_UNSUPPORTED && !writes);
 prepare(8);put16(4224,0100555);cp32_cache_invalidate();assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,1)<0 && !writes);
 prepare(8);put32(4224+24,0);cp32_cache_invalidate();assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,1)<0 && !writes);
 prepare(8);assert(cp32_minix_append(reader,writer,sizeof(disk),"/boot",text,1)<0 && !writes);
 assert(cp32_minix_append(reader,writer,sizeof(disk),"/missing",text,1)<0 && !writes);
 assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,65)<0 && !writes);
 assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme",text,0)<0 && !writes);
}
static unsigned allocated_zones(void)
{
 unsigned total=0;for(unsigned bit=1;bit<=57;bit++)
   if(get16(3072+(bit/16)*2)&(1U<<(bit%16)))total++;
 return total;
}
static void live_zone(unsigned zone)
{
 if(!zone)return;assert(zone>=6 && zone<63);
 unsigned bit=zone-5;assert(get16(3072+(bit/16)*2)&(1U<<(bit%16)));
}
static void check_shrink(unsigned old,unsigned length)
{
 struct cp32_minix_file f;char buf[64];unsigned done=0;
 cp32_cache_invalidate();assert(!cp32_minix_file_open(reader,sizeof(disk),"/readme",&f));
 assert(f.size==old || f.size==length);
 while(done<f.size) {
   int r=cp32_minix_file_read(&f,buf,f.size-done);
   assert(r>0 && !memcmp(buf,prefix+done,r));done+=r;
 }
 for(unsigned i=0;i<7;i++)live_zone(get32(4224+24+i*4));
 unsigned table=get32(4224+52);live_zone(table);
 if(table)for(unsigned i=0;i<256;i++)live_zone(get32(table*1024+i*4));
 assert(!memcmp(disk+4096,start+4096,128));
 assert(!memcmp(disk+6*1024,start+6*1024,3*1024));
}
static void run_truncate(void)
{
 unsigned cases[][2]={{0,0},{8,8},{8,0},{1000,513},{2000,1000},{7168,1000},
   {8192,7168},{8192,7169},{9000,7200},{263*1024,8192},{263*1024,0}};
 for(unsigned k=0;k<sizeof(cases)/sizeof(*cases);k++) {
   unsigned old=cases[k][0],length=cases[k][1];prepare(old);
   unsigned count=allocated_zones();
   assert(!cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",length));
   unsigned steps=writes;assert(get32(4232)==length);check_shrink(old,length);
   if(old==length)assert(!steps);
   if(!length) {
     assert(allocated_zones()==3); /* original fixture root/boot/readme zone8 */
     assert(cp32_minix_append(reader,writer,sizeof(disk),"/readme","reused",6)==6);
     assert(allocated_zones()==4 && get32(4248)==9); /* lowest reclaimed zone */
   } else {
     assert(allocated_zones()<=count);
     struct cp32_minix_file f;unsigned zone;
     assert(!cp32_minix_file_open(reader,sizeof(disk),"/readme",&f));
     assert(!cp32_fs_read_map(&f,(length-1)/1024,&zone));
     if(zone && old!=length)for(unsigned i=length%1024;i && i<1024;i++)assert(!disk[zone*1024+i]);
   }
   for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
     reset();fail_at=fail;after=commit;
     assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",length)==-CP32_SUPER_IO);
     assert(writes==fail);fail_at=0;check_shrink(old,length);
   }
 }
 prepare(8);assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",9)<0 && !writes);
 assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/boot",0)<0 && !writes);
 assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/missing",0)<0 && !writes);
 put16(4224,0100555);cp32_cache_invalidate();assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",0)<0 && !writes);
 prepare(8192);put32(16*1024,9);cp32_cache_invalidate(); /* indirect aliases a direct zone */
 assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",0)==-CP32_SUPER_INVALID && !writes);
 prepare(8192);put32(16*1024,63);cp32_cache_invalidate(); /* outside FS */
 assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",0)==-CP32_SUPER_INVALID && !writes);
 prepare(8192);put16(3072,get16(3072)&~(1U<<12));cp32_cache_invalidate(); /* data zone17 unallocated */
 assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",0)==-CP32_SUPER_INVALID && !writes);
 prepare(8);put32(4224+56,16);cp32_cache_invalidate();
 assert(cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",0)==-CP32_SUPER_UNSUPPORTED && !writes);
 /* Interrupted append's mapped zone beyond EOF can be detached and freed. */
 prepare(8192);put32(4232,7168);cp32_cache_invalidate();
 assert(!cp32_minix_truncate(reader,writer,sizeof(disk),"/readme",7168));
 assert(!get32(4224+52));assert(!(get16(3072)&(1U<<11)) && !(get16(3072)&(1U<<12)));
 puts("truncate: byte orders, partial/zero/indirect shrink, tail zeroing, space reuse, corrupt mapping rejection and interruption ordering pass");
}
static void run_unlink(void)
{
 const unsigned sizes[]={0,8,8192,263*1024};
 for(unsigned c=0;c<sizeof(sizes)/sizeof(*sizes);c++) {
   prepare(sizes[c]);
   assert(!cp32_minix_unlink(reader,writer,sizeof(disk),"/boot/readme"));
   memcpy(start,disk,sizeof(disk));reset();
   assert(!cp32_minix_unlink(reader,writer,sizeof(disk),"/readme"));
   unsigned steps=writes;
   assert(!(get16(2048)&8));
   for(unsigned i=0;i<64;i++)assert(!disk[4224+i]);
   for(unsigned z=9;z<=17;z++)assert(!(get16(3072)&(1U<<(z-5))));
   for(unsigned commit=0;commit<2;commit++)for(unsigned fail=1;fail<=steps;fail++) {
     reset();fail_at=fail;after=commit;
     assert(cp32_minix_unlink(reader,writer,sizeof(disk),"/readme")==-CP32_SUPER_IO);
     assert(writes==fail);fail_at=0;cp32_cache_invalidate();
     struct cp32_minix_stat st;
     if(!cp32_minix_stat(reader,sizeof(disk),"/readme",&st)) {
       assert(st.links==1 && st.size==sizes[c]);
       assert(!memcmp(disk+2048,start+2048,2048));
       assert(!memcmp(disk+4224,start+4224,64));
       read_all(prefix,sizes[c]);
     }
     /* Any inode still containing references retains every allocation. */
     if(get16(4224))assert(!memcmp(disk+2048,start+2048,2048));
     if(!(get16(2048)&8))for(unsigned i=0;i<64;i++)assert(!disk[4224+i]);
   }
 }
 prepare(8192);assert(!cp32_minix_unlink(reader,writer,sizeof(disk),"/boot/readme"));
 writes=0;put32(16*1024,9);cp32_cache_invalidate();
 assert(cp32_minix_unlink(reader,writer,sizeof(disk),"/readme")==-CP32_SUPER_INVALID && !writes);
 prepare(8);assert(!cp32_minix_unlink(reader,writer,sizeof(disk),"/boot/readme"));
 writes=0;put32(4224+56,16);cp32_cache_invalidate();
 assert(cp32_minix_unlink(reader,writer,sizeof(disk),"/readme")==-CP32_SUPER_UNSUPPORTED && !writes);
 puts("unlink: zero/direct/indirect/sparse last-link reclamation and every complete-write interruption pass");
}
int main(int argc,char **argv)
{
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(base,1,sizeof(base),f)==sizeof(base));fclose(f);
 run();run_truncate();run_unlink();memcpy(disk,base,sizeof(disk));
 /* Swap the minimal fixture's metadata in place; data bytes are unchanged. */
 for(unsigned at=1024;at<1036;at+=2) {unsigned v=get16(at);swap=1;put16(at,v);swap=0;}
 unsigned longs[]={1036,1044};for(unsigned i=0;i<2;i++){unsigned v=get32(longs[i]);swap=1;put32(longs[i],v);swap=0;}
 for(unsigned at=1040;at<=1042;at+=2){unsigned v=get16(at);swap=1;put16(at,v);swap=0;}
 for(unsigned at=2048;at<4096;at+=2){unsigned v=get16(at);swap=1;put16(at,v);swap=0;}
 for(unsigned inode=0;inode<32;inode++) {
   unsigned at=4096+inode*64;
   for(unsigned k=0;k<8;k+=2){unsigned v=get16(at+k);swap=1;put16(at+k,v);swap=0;}
   for(unsigned k=8;k<64;k+=4){unsigned v=get32(at+k);swap=1;put32(at+k,v);swap=0;}
 }
 for(unsigned z=6;z<=7;z++)for(unsigned at=0;at<1024;at+=16){unsigned v=get16(z*1024+at);swap=1;put16(z*1024+at,v);swap=0;}
 memcpy(base,disk,sizeof(base));swap=1;run();run_truncate();run_unlink();
 puts("append: both byte orders, empty/sector/zone/direct-indirect boundaries, EOF publication, space limits and all write interruptions pass");
}
