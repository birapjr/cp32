/* MINIX lib/other/_seekdir.c: reset the directory position. No read-ahead
 * buffer exists in CP32, so the descriptor already holds the logical offset. */
#include "../posix/dirent.h"
#include "../../fs/fs.h"

CP32_IRAM_EXT int cp32_seekdir(struct cp32_dir *dir,long position)
{
  if(!dir || dir->fd<0) return -CP32_FILE_BAD_FD;
  if(position<0 || position%16) return -CP32_SUPER_INVALID;
  return cp32_fd_seek(dir->fd,position,0,0);
}
