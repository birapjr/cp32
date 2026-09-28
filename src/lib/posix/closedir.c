#include "dirent.h"
#include "../../fs/fs.h"

CP32_IRAM_EXT int cp32_closedir(struct cp32_dir *dir)
{
  int result;
  if(!dir || dir->fd<0) return -CP32_FILE_BAD_FD;
  result=cp32_fd_close(dir->fd);
  if(!result) dir->fd=-1;
  return result;
}
