/* MINIX lib/posix/_opendir.c: own a directory fd, validate with fstat,
 * set NONBLOCK/CLOEXEC. CP32 uses caller-owned storage instead of malloc. */
#include "dirent.h"
#include "../../fs/fs.h"

CP32_IRAM_EXT int cp32_opendir(cp32_disk_reader read,unsigned capacity,
                               const char *path,struct cp32_dir *dir)
{
  struct cp32_minix_stat info;
  int fd,result,flags;
  if(!dir || dir->fd!=-1) return -CP32_SUPER_INVALID;
  fd=cp32_fd_open(read,capacity,path);
  if(fd<0) return fd;
  result=cp32_fd_fstat(fd,&info);
  if(!result && (info.mode & 0170000)!=0040000) result=-CP32_FILE_NOT_DIR;
  if(!result) {
    flags=cp32_fd_fcntl(fd,CP32_F_GETFD,0);
    result=flags<0 ? flags : cp32_fd_fcntl(fd,CP32_F_SETFD,flags|CP32_FD_CLOEXEC);
  }
  if(!result) {
    flags=cp32_fd_fcntl(fd,CP32_F_GETFL,0);
    result=flags<0 ? flags : cp32_fd_fcntl(fd,CP32_F_SETFL,flags|CP32_O_NONBLOCK);
  }
  if(result) { cp32_fd_close(fd); return result; }
  dir->fd=fd;
  return 0;
}
