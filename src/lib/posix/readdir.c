/* MINIX _readdir: return one live entry, skipping deleted slots. The FS
 * decoder retains endian, inode allocation and console-name validation. */
#include "dirent.h"
#include "../../fs/fs.h"

CP32_IRAM_EXT int cp32_readdir(struct cp32_dir *dir,struct cp32_dirent *entry)
{
  struct cp32_dirent next = {0,{0}};
  int result;
  if(!dir || dir->fd<0) return -CP32_FILE_BAD_FD;
  if(!entry) return -CP32_SUPER_INVALID;
  result=cp32_fd_readdir(dir->fd,&next.inode,next.name);
  if(result==1) *entry=next;
  return result;
}
