/* MINIX misc.c:do_dup responsibilities, on the bounded single-client table. */
#include "fs.h"

CP32_IRAM_EXT int cp32_fd_dup(int fd)
{
  return cp32_fd_share(fd,-1);
}

CP32_IRAM_EXT int cp32_fd_dup2(int fd,int target)
{
  if(target<0 || target>=CP32_OPEN_MAX) return -CP32_FILE_BAD_FD;
  return cp32_fd_share(fd,target);
}
