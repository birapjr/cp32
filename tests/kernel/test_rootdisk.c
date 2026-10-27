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
static unsigned writes,write_fail,corrupt,sync_fail;
int cp32_sdcard_sync(void) {return sync_fail ? -1:0;}
int cp32_sdcard_write(uint32_t lba,const unsigned char *p) {
 writes++;if(write_fail || lba>=size/512)return -1;
 memcpy(disk+lba*512,p,512);if(corrupt)disk[lba*512]^=1;return 0;
}
int cp32_sdcard_init(void) {return init_fail ? -1:0;}
uint64_t cp32_sdcard_sectors(void) {return reported;}
int cp32_sdcard_read(uint32_t lba,unsigned char *p) {
 reads++;if(fail || lba>=size/512)return -1;memcpy(p,disk+lba*512,512);return 0;
}
static int reader(unsigned offset,char *p,int n)
{return cp32_root_read(offset,p,n) ? -1:n;}
static int cached_reader(unsigned offset,char *p,int n)
{return cp32_cache_read(reader,cp32_root_capacity(),offset,p,n);}
static int writer(unsigned offset,const char *p,int n)
{return cp32_root_write(offset,p,n) ? -1:n;}
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
 assert(!cp32_root_sync());sync_fail=1;assert(cp32_root_sync()<0 && !cp32_root_writable());
 sync_fail=0;assert(cp32_root_sync()<0);assert(!cp32_root_init());
 unsigned char buf[1024],old[512];
 assert(!cp32_root_read(511,buf,1024) && !memcmp(buf,disk+2048*512+511,1024));
 assert(cp32_root_read(65535,buf,2)<0 && cp32_root_read(0,0,1)<0);
 memcpy(old,disk,512);
 unsigned char saved[512];memcpy(saved,disk+2048*512+63*1024,512);
 assert(!cp32_root_write(63*1024+9,"persist",7) && writes==1);
 assert(!cp32_root_read(63*1024,buf,512) && !memcmp(buf+9,"persist",7));
 assert(!memcmp(buf,saved,9) && !memcmp(buf+16,saved+16,496) && !memcmp(old,disk,512));
 assert(cp32_root_write(511,buf,2)<0 && writes==1);
 assert(!cp32_root_init() && !cp32_root_read(63*1024+9,buf,7) && !memcmp(buf,"persist",7));
 write_fail=1;assert(cp32_root_write(63*1024,"bad",3)<0 && !cp32_root_writable());
 write_fail=0;unsigned before=writes;assert(cp32_root_write(63*1024,"bad",3)<0 && writes==before);
 assert(!cp32_root_read(63*1024+9,buf,7) && !memcmp(buf,"persist",7));
 assert(!cp32_root_init());corrupt=1;assert(cp32_root_write(63*1024,"bad",3)<0 && !cp32_root_writable());
 corrupt=0;assert(!cp32_root_init());
 assert(!cp32_root_write(63*1024,saved,512));
 /* Existing FS and ELF loader operate on SD-backed byte reads unchanged. */
 int fd=cp32_fd_open(reader,cp32_root_capacity(),"/readme");assert(fd>=0);
 assert(cp32_fd_read(fd,(char *)buf,5)==5 && !memcmp(buf,"CP32 ",5));assert(!cp32_fd_close(fd));
 /* The production FS patch -> root -> SD sector path, with a warm cache. */
 unsigned char *snapshot=malloc(size);assert(snapshot);memcpy(snapshot,disk,size);
 struct cp32_minix_file file;
 assert(!cp32_minix_file_open(cached_reader,65536,"/readme",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,5)==5 && !memcmp(buf,"CP32 ",5));
 assert(cp32_minix_patch(cached_reader,writer,65536,"/readme",0,"SD125",5)==5);
 for(unsigned i=0;i<size;i++)
   if(i<2048*512+8192 || i>=2048*512+8197)assert(disk[i]==snapshot[i]);
 assert(!cp32_minix_file_open(cached_reader,65536,"/boot/readme",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,5)==5 && !memcmp(buf,"SD125",5));
 assert(!cp32_root_init());cp32_cache_invalidate(); /* reboot: no cache survives */
 assert(!cp32_minix_file_open(cached_reader,65536,"/readme",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,5)==5 && !memcmp(buf,"SD125",5));
 assert(cp32_minix_patch(cached_reader,writer,65536,"/readme",0,"CP32 ",5)==5);
 assert(!memcmp(disk,snapshot,size));
 before=writes;
 assert(cp32_minix_patch(cached_reader,writer,65536,"/readme",file.size,"X",1)<0);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/readme",0,"X",65)<0);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/boot/double",0,"X",1)<0);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/boot/hello",0,"X",1)<0);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/boot",0,"X",1)<0);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/missing",0,"X",1)<0);
 assert(writes==before && !memcmp(disk,snapshot,size));
 assert(cp32_minix_patch(cached_reader,writer,65536,"/boot/indirect",7*1024,"ABC",3)==3);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/boot/double",263*1024,"XYZ",3)==3);
 assert(!memcmp(disk+2048*512+17*1024,"ABC",3));
 assert(!memcmp(disk+2048*512+20*1024,"XYZ",3));
 write_fail=1;assert(cp32_minix_patch(cached_reader,writer,65536,"/readme",0,"bad",3)==-CP32_SUPER_IO);
 write_fail=0;assert(!cp32_minix_file_open(cached_reader,65536,"/readme",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,5)==5 && !memcmp(buf,"CP32 ",5));
 memcpy(disk,snapshot,size);free(snapshot);assert(!cp32_root_init());cp32_cache_invalidate();
 /* New file allocation through the actual root sector writer and remount. */
 assert(cp32_minix_create_root(cached_reader,writer,65536,"note","created on SD",13)==13);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==13 && !memcmp(buf,"created on SD",13));
 assert(cp32_minix_append(cached_reader,writer,65536,"/note"," + text",7)==7);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==20 && !memcmp(buf,"created on SD + text",20));
 unsigned previous_zone=file.zones[0];
 assert(!cp32_minix_truncate(cached_reader,writer,65536,"/note",7));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file));
 assert(file.size==7 && cp32_minix_file_read(&file,(char *)buf,64)==7 && !memcmp(buf,"created",7));
 assert(!cp32_minix_truncate(cached_reader,writer,65536,"/note",0));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file) && !file.size && !file.zones[0]);
 assert(cp32_minix_append(cached_reader,writer,65536,"/note","reused",6)==6);
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file) && file.zones[0]==previous_zone);
 /* Hard link survives remount and shares subsequent data/size updates. */
 assert(!cp32_minix_link_root(cached_reader,writer,65536,"/note","alias"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 struct cp32_minix_stat linked,source;
 assert(!cp32_minix_stat(cached_reader,65536,"/note",&source));
 assert(!cp32_minix_stat(cached_reader,65536,"/alias",&linked));
 assert(source.inode==linked.inode && source.links==2 && linked.links==2);
 assert(cp32_minix_append(cached_reader,writer,65536,"/alias","!",1)==1);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==7 && !memcmp(buf,"reused!",7));
 unsigned count_before=writes;
 assert(cp32_minix_create_root(cached_reader,writer,65536,"note","replace",7)==-CP32_FILE_EXISTS && writes==count_before);
 /* Remove one name, remount, then remove last name and reuse its inode/zone. */
 unsigned reused_inode=source.inode;
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/alias"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/alias",&linked)==-CP32_FILE_NOT_FOUND);
 assert(!cp32_minix_stat(cached_reader,65536,"/note",&source) && source.links==1);
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==7 && !memcmp(buf,"reused!",7));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/note"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/note",&source)==-CP32_FILE_NOT_FOUND);
 assert(cp32_minix_create_root(cached_reader,writer,65536,"note","again",5)==5);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_stat(cached_reader,65536,"/note",&source) && source.inode==reused_inode && source.links==1);
 assert(!cp32_minix_file_open(cached_reader,65536,"/note",&file) && file.zones[0]==previous_zone);
 assert(cp32_minix_file_read(&file,(char *)buf,64)==5 && !memcmp(buf,"again",5));
 /* Persist same-directory rename, then cross-directory move without copying. */
 assert(!cp32_minix_rename(cached_reader,writer,65536,"/note","/renamed"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/note",&linked)==-CP32_FILE_NOT_FOUND);
 assert(!cp32_minix_stat(cached_reader,65536,"/renamed",&linked) && linked.inode==reused_inode);
 assert(!cp32_minix_rename(cached_reader,writer,65536,"/renamed","/boot/moved"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/renamed",&linked)==-CP32_FILE_NOT_FOUND);
 assert(!cp32_minix_stat(cached_reader,65536,"/boot/moved",&linked));
 assert(linked.inode==reused_inode && linked.links==1);
 assert(!cp32_minix_file_open(cached_reader,65536,"/boot/moved",&file) && file.zones[0]==previous_zone);
 assert(cp32_minix_file_read(&file,(char *)buf,64)==5 && !memcmp(buf,"again",5));
 /* Mode changes through an alias survive remount and preserve file bytes. */
 assert(!cp32_minix_link_root(cached_reader,writer,65536,"/boot/moved","modealias"));
 assert(!cp32_minix_chmod(cached_reader,writer,65536,"/modealias",0755));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_stat(cached_reader,65536,"/boot/moved",&linked) && linked.mode==0100755);
 assert(!cp32_minix_stat(cached_reader,65536,"/modealias",&source));
 assert(source.inode==linked.inode && source.mode==linked.mode && source.links==2);
 assert(!cp32_minix_chmod(cached_reader,writer,65536,"/boot/moved",0644));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_stat(cached_reader,65536,"/modealias",&source) && source.mode==0100644);
 assert(!cp32_minix_file_open(cached_reader,65536,"/modealias",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==5 && !memcmp(buf,"again",5));
 /* Release this completed scenario's inode before the directory tests. */
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/modealias"));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/boot/moved"));
 assert(!cp32_root_sync());
 /* Create directly under /boot, remount, then exercise its normal lifecycle. */
 assert(cp32_minix_create(cached_reader,writer,65536,"/boot/nested","nested",6)==6);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/boot/nested",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==6 && !memcmp(buf,"nested",6));
 assert(cp32_minix_append(cached_reader,writer,65536,"/boot/nested","!",1)==1);
 assert(!cp32_minix_rename(cached_reader,writer,65536,"/boot/nested","/boot/newname"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/boot/newname",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==7 && !memcmp(buf,"nested!",7));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/boot/newname"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/boot/newname",&source)==-CP32_FILE_NOT_FOUND);
 assert(!cp32_minix_mkdir(cached_reader,writer,65536,"/work"));
 assert(!cp32_minix_mkdir(cached_reader,writer,65536,"/work/sub"));
 assert(cp32_minix_create(cached_reader,writer,65536,"/work/sub/note","tree",4)==4);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_stat(cached_reader,65536,"/work",&source) && source.links==3 && source.mode==0040755);
 assert(!cp32_minix_stat(cached_reader,65536,"/work/sub/..",&linked) && linked.inode==source.inode);
 assert(!cp32_minix_file_open(cached_reader,65536,"/work/sub/note",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==4 && !memcmp(buf,"tree",4));
 assert(cp32_minix_rmdir(cached_reader,writer,65536,"/work/sub","/")==-CP32_FILE_NOT_EMPTY);
 assert(!cp32_minix_link(cached_reader,writer,65536,"/work/sub/note","/work/alias"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_stat(cached_reader,65536,"/work/alias",&linked) && linked.links==2);
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/work/sub/note"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/work/alias",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,64)==4 && !memcmp(buf,"tree",4));
 assert(!cp32_minix_stat(cached_reader,65536,"/work/alias",&linked) && linked.links==1);
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/work/alias"));
 assert(!cp32_minix_rmdir(cached_reader,writer,65536,"/work/sub","/"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/work/sub",&linked)==-CP32_FILE_NOT_FOUND);
 assert(!cp32_minix_stat(cached_reader,65536,"/work",&linked) && linked.links==2);
 assert(!cp32_minix_rmdir(cached_reader,writer,65536,"/work","/"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/work",&linked)==-CP32_FILE_NOT_FOUND);
 assert(!cp32_minix_mkdir(cached_reader,writer,65536,"/work"));
 /* Full FS -> root -> SD path across a sector boundary, then remount. */
 memset(buf,'q',64);
 assert(cp32_minix_create(cached_reader,writer,65536,"/span",(char *)buf,64)==64);
 for(unsigned i=0;i<8;i++)assert(cp32_minix_append(cached_reader,writer,65536,"/span",(char *)buf,64)==64);
 assert(cp32_minix_patch(cached_reader,writer,65536,"/span",510,"ABCD",4)==4);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/span",&file));
 for(unsigned at=0;at<576;at+=64)assert(cp32_minix_file_read(&file,(char *)buf+at,64)==64);
 for(unsigned i=0;i<576;i++)assert(buf[i]==(i>=510 && i<514 ? "ABCD"[i-510]:'q'));
 assert(!cp32_minix_copy(cached_reader,writer,65536,"/span","/copy"));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/span"));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/copy",&file));
 for(unsigned at=0;at<576;at+=64)assert(cp32_minix_file_read(&file,(char *)buf+at,64)==64);
 for(unsigned i=0;i<576;i++)assert(buf[i]==(i>=510 && i<514 ? "ABCD"[i-510]:'q'));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/copy"));

 assert(!cp32_minix_create(cached_reader,writer,65536,"/e",0,0));
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_stat(cached_reader,65536,"/e",&source) && !source.size);
 assert(!cp32_minix_file_open(cached_reader,65536,"/e",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,1)==0);
 assert(cp32_minix_append(cached_reader,writer,65536,"/e","hi",2)==2);
 assert(!cp32_root_init());cp32_cache_invalidate();
 assert(!cp32_minix_file_open(cached_reader,65536,"/e",&file));
 assert(cp32_minix_file_read(&file,(char *)buf,2)==2 && !memcmp(buf,"hi",2));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/e"));
 /* Application descriptor writes survive root-driver remount/cache reset. */
 fd=cp32_fd_open_mode(cached_reader,writer,65536,"/wtest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
 assert(fd>=0 && cp32_fd_write(fd,"persist",7)==7);
 assert(!cp32_fd_close(fd));assert(!cp32_root_init());cp32_cache_invalidate();
 fd=cp32_fd_open(cached_reader,65536,"/wtest");assert(fd>=0);
 assert(cp32_fd_read(fd,(char *)buf,7)==7 && !memcmp(buf,"persist",7));
 assert(!cp32_minix_unlink(cached_reader,writer,65536,"/wtest"));
 struct cp32_minix_stat orphan;
 assert(!cp32_fd_fstat(fd,&orphan) && !orphan.links);
 assert(!cp32_fd_seek(fd,0,0,0));
 assert(cp32_fd_read(fd,(char *)buf,7)==7 && !memcmp(buf,"persist",7));
 assert(!cp32_fd_close(fd));assert(!cp32_root_init());cp32_cache_invalidate();
 assert(cp32_minix_stat(cached_reader,65536,"/wtest",&orphan)==-CP32_FILE_NOT_FOUND);
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
 puts("SD root: MBR bounds/type/ambiguity, verified writes/reboot/readback faults, recovery, MINIX paths and six ELF loads pass");free(disk);
}
