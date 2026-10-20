#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app-update.h"
static unsigned char disk[65536],original[65536],payload[8200];
static unsigned calls,fail;
static int read_disk(unsigned at,char *p,int n) {
 if(at>sizeof(disk) || (unsigned)n>sizeof(disk)-at)return -1;
 memcpy(p,disk+at,n);return n;
}
static int write_disk(unsigned at,const char *p,int n) {
 if(++calls==fail)return -1;
 if(at>sizeof(disk) || (unsigned)n>sizeof(disk)-at)return -1;
 memcpy(disk+at,p,n);return n;
}
static void reset(void) {memcpy(disk,original,sizeof(disk));cp32_cache_invalidate();calls=fail=0;}
#ifdef TEST_PAYLOAD
#include "app-payload.h"
#endif
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
 assert(fread(original,1,sizeof(original),f)==sizeof(original));fclose(f);
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(unsigned char)(i*13);
 struct cp32_update_entry entry={"/boot/new",payload,sizeof(payload)};
 struct cp32_minix_stat st;
 reset();assert(!cp32_update_one(read_disk,write_disk,sizeof(disk),&entry));
 assert(!cp32_minix_stat(read_disk,sizeof(disk),entry.path,&st) && st.size==sizeof(payload) && (st.mode&0777)==0555);
 unsigned total=calls;calls=0;
 assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&entry)==1 && !calls);
 /* Shrink replacement, then repair interrupted write from the same payload. */
 entry.size=1200;assert(!cp32_update_one(read_disk,write_disk,sizeof(disk),&entry));
 assert(!cp32_minix_stat(read_disk,sizeof(disk),entry.path,&st) && st.size==1200);
 assert(!cp32_minix_link(read_disk,write_disk,sizeof(disk),entry.path,"/alias"));
 calls=0;assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&entry)==-CP32_FILE_BUSY && !calls);
 entry.path="/boot";calls=0;assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&entry)==-CP32_FILE_BUSY && !calls);
 entry.path="/boot/new";entry.size=sizeof(payload);
 /* Sample allocation, data, metadata and final chmod failures. Reboot/retry
  * must not report success for incomplete contents or leak descriptors. */
 for(unsigned stop=1;stop<=total;stop+=17) {
  reset();fail=stop;assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&entry)<0);
  fail=0;cp32_cache_invalidate();
  int r=cp32_update_one(read_disk,write_disk,sizeof(disk),&entry);assert(r>=0);
  calls=0;assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&entry)==1 && !calls);
 }
 reset();entry.path="/absent/new";assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&entry)<0 && !calls);
#ifdef TEST_PAYLOAD
 reset();
 for(unsigned i=0;i<sizeof(cp32_update_entries)/sizeof(cp32_update_entries[0]);i++)
   assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&cp32_update_entries[i])>=0);
 cp32_cache_invalidate();calls=0;
 for(unsigned i=0;i<sizeof(cp32_update_entries)/sizeof(cp32_update_entries[0]);i++)
   assert(cp32_update_one(read_disk,write_disk,sizeof(disk),&cp32_update_entries[i])==1);
 assert(!calls);
#endif
 puts("application updater: install, verify/skip, replacement, hardlink/directory rejection and fault retry pass");
}
