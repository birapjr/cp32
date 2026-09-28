#include "../posix/dirent.h"
#include "../../fs/fs.h"

CP32_IRAM_EXT long cp32_telldir(struct cp32_dir *dir)
{
  unsigned position;
  int result;
  if(!dir || dir->fd<0) return -CP32_FILE_BAD_FD;
  result=cp32_fd_seek(dir->fd,0,1,&position);
  return result ? result : (long)position;
}
