/* Optional maintenance utility using MINIX open/truncate/write semantics.
 * In-place replacement: clear execute permission until complete readback.
 * A power cut can leave an incomplete file; the embedded payload allows retry.
 * No heap allocation, private MMIO or application execution is involved. */
#include "app-update.h"

CP32_IRAM_EXT static int matches(int fd,const struct cp32_update_entry *entry)
{
  char data[64];unsigned at=0,i;int n;
  while(at<entry->size) {
    n=cp32_fd_read(fd,data,entry->size-at>64 ? 64:entry->size-at);
    if(n<0)return n;
    if(!n)return 0;
    for(i=0;i<(unsigned)n;i++)if((unsigned char)data[i]!=entry->bytes[at+i])return 0;
    at+=n;
  }
  return 1;
}
CP32_IRAM_EXT int cp32_update_one(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const struct cp32_update_entry *entry)
{
  struct cp32_minix_stat st;int r,fd;unsigned at,n;
  if(!entry || !entry->path || !entry->bytes || !entry->size || !write)
    return -CP32_SUPER_INVALID;
  r=cp32_minix_stat(read,capacity,entry->path,&st);
  if(!r) {
    /* Never truncate a directory or modify another name through a hard link. */
    if((st.mode&0170000)!=0100000 || st.links!=1)return -CP32_FILE_BUSY;
    fd=cp32_fd_open(read,capacity,entry->path);if(fd<0)return fd;
    r=st.size==entry->size ? matches(fd,entry):0;
    cp32_fd_close(fd);if(r<0)return r;
    if(r==1) {
      if((st.mode&0777)==0555)return 1; /* byte-identical, no writes */
      return cp32_minix_chmod(read,write,capacity,entry->path,0555);
    }
    r=cp32_minix_chmod(read,write,capacity,entry->path,0600);if(r)return r;
  } else if(r!=-CP32_FILE_NOT_FOUND)return r;
  fd=cp32_fd_open_mode(read,write,capacity,entry->path,
      CP32_O_RDWR|CP32_O_CREAT|CP32_O_TRUNC,0600);
  if(fd<0)return fd;
  for(at=0;at<entry->size;at+=n) {
    n=entry->size-at;if(n>64)n=64;
    r=cp32_fd_write(fd,(const char *)entry->bytes+at,n);
    if(r!=(int)n){if(r>=0)r=-CP32_SUPER_IO;goto done;}
  }
  r=cp32_fd_seek(fd,0,0,0);if(r)goto done;
  r=matches(fd,entry);if(r==1)r=0;else if(!r)r=-CP32_SUPER_IO;
done:
  cp32_fd_close(fd);
  if(r)return r;
  return cp32_minix_chmod(read,write,capacity,entry->path,0555);
}
