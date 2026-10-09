#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rootdisk.h"
#include "sdcard.h"
#include "../fs/fs.h"
#include "../mm/image.h"
static unsigned char *disk;
static unsigned size,reads,fail,init_fail;
static uint64_t reported;
int cp32_sdcard_init(void) {return init_fail ? -1:0;}
uint64_t cp32_sdcard_sectors(void) {return reported;}
int cp32_sdcard_read(uint32_t lba,unsigned char *p) {
 reads++;if(fail || lba>=size/512)return -1;memcpy(p,disk+lba*512,512);return 0;
}
static int reader(unsigned offset,char *p,int n)
{return cp32_root_read(offset,p,n) ? -1:n;}
static int image_reader(void *ctx,uint32_t offset,unsigned char *p,unsigned n)
{
 int fd=*(int *)ctx;unsigned done=0;
 if(cp32_fd_seek(fd,offset,0,0))return -1;
 while(done<n) {int got=cp32_fd_read(fd,(char *)p+done,n-done);if(got<=0)return -1;done+=got;}
 return 0;
}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size=ftell(f);rewind(f);
 disk=malloc(size);assert(disk && fread(disk,1,size,f)==size);fclose(f);reported=size/512;
 assert(!cp32_root_init() && cp32_root_capacity()==65536);
 unsigned char buf[1024],old[512];
 assert(!cp32_root_read(511,buf,1024) && !memcmp(buf,disk+2048*512+511,1024));
 assert(cp32_root_read(65535,buf,2)<0 && cp32_root_read(0,0,1)<0);
 memcpy(old,disk,512);assert(cp32_root_write(0,buf,10)<0 && !memcmp(old,disk,512));
 /* Existing FS and ELF loader operate on SD-backed byte reads unchanged. */
 int fd=cp32_fd_open(reader,cp32_root_capacity(),"/readme");assert(fd>=0);
 assert(cp32_fd_read(fd,(char *)buf,5)==5 && !memcmp(buf,"CP32 ",5));assert(!cp32_fd_close(fd));
 static unsigned char text[16384],data[12288];struct cp32_image image;
 const char *names[]={"hello","echo","cat","wc","ls","exec"};
 for(unsigned i=0;i<6;i++) {
  char path[32];snprintf(path,sizeof(path),"/boot/%s",names[i]);
  fd=cp32_fd_open(reader,cp32_root_capacity(),path);assert(fd>=0);
  struct cp32_minix_stat st;assert(!cp32_fd_fstat(fd,&st));
  assert(!cp32_image_load(image_reader,&fd,st.size,text,data,&image));assert(!cp32_fd_close(fd));
 }
 fail=1;assert(cp32_root_read(65000,buf,512)<0);fail=0;
 assert(!cp32_root_read(65000,buf,512)); /* failed sector must not be cached */
 init_fail=1;assert(cp32_root_init()<0 && !cp32_root_capacity());init_fail=0;
 disk[450]=0x0c;assert(cp32_root_init()==-6);disk[450]=0x81; /* FAT not MINIX */
 disk[466]=0x81;assert(cp32_root_init()==-6);disk[466]=0;
 reported=2048;assert(cp32_root_init()==-6);reported=size/512;
 disk[2048*512+1040]=0;assert(cp32_root_init()==-8 && !cp32_root_capacity());disk[2048*512+1040]=0x68;
 fail=1;assert(cp32_root_init()==-5 && !cp32_root_capacity());fail=0;
 assert(!cp32_root_init());
 /* Out-of-range and oversized primary partition spans must not wrap. */
 unsigned char entry[16];memcpy(entry,disk+446,16);
 memset(disk+454,0xff,4);assert(cp32_root_init()==-6);
 memcpy(disk+446,entry,16);reported=0x100000000ULL;
 disk[458]=0;disk[459]=0;disk[460]=0x40;disk[461]=0;
 assert(cp32_root_init()==-7);memcpy(disk+446,entry,16);
 /* Raw MINIX volume on a large physical card uses the superblock's size. */
 memmove(disk,disk+2048*512,65536);size=65536;
 assert(!cp32_root_init() && cp32_root_capacity()==63*1024);
 assert(!cp32_root_read(8192,buf,5) && !memcmp(buf,"CP32 ",5));
 puts("SD root: MBR bounds/type/ambiguity, read-only I/O, recovery, MINIX paths and six ELF loads pass");free(disk);
}
