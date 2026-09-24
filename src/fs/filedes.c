/* MINIX filedes.c:get_fd/get_filp responsibilities. Bounded read-only table
 * for the single command client; shared descriptions, no process tables yet. */
#include "fs.h"

static struct cp32_minix_file files[CP32_OPEN_MAX];
static unsigned references[CP32_OPEN_MAX];
/* Zero means closed; otherwise index+1 into the open-description table. */
static unsigned descriptors[CP32_OPEN_MAX];

CP32_IRAM_EXT static struct cp32_minix_file *get_file(int fd)
{
  if(fd<0 || fd>=CP32_OPEN_MAX || !descriptors[fd]) return 0;
  return &files[descriptors[fd]-1];
}

CP32_IRAM_EXT int cp32_fd_open(cp32_disk_reader read,unsigned capacity,const char *path)
{
  unsigned fd, slot;
  int result;
  for(fd=0;fd<CP32_OPEN_MAX;fd++) if(!descriptors[fd]) break;
  if(fd==CP32_OPEN_MAX) return -CP32_FILE_LIMIT;
  for(slot=0;slot<CP32_OPEN_MAX;slot++) if(!references[slot]) break;
  if(slot==CP32_OPEN_MAX) return -CP32_FILE_LIMIT;
  result=cp32_minix_file_open(read,capacity,path,&files[slot]);
  if(result) return result;
  references[slot]=1;
  descriptors[fd]=slot+1;
  return (int)fd;
}

CP32_IRAM_EXT int cp32_fd_close(int fd)
{
  if(!get_file(fd)) return -CP32_FILE_BAD_FD;
  references[descriptors[fd]-1]--;
  descriptors[fd]=0;
  return 0;
}

CP32_IRAM_EXT int cp32_fd_read(int fd,char *out,unsigned count)
{
  struct cp32_minix_file *file=get_file(fd);
  return file ? cp32_minix_file_read(file,out,count) : -CP32_FILE_BAD_FD;
}

CP32_IRAM_EXT int cp32_fd_seek(int fd,long offset,int whence,unsigned *position)
{
  struct cp32_minix_file *file=get_file(fd);
  int result;
  if(!file) return -CP32_FILE_BAD_FD;
  result=cp32_minix_file_seek(file,offset,whence);
  if(!result && position) *position=file->position;
  return result;
}

/* Install another reference without copying the offset. -1 selects the
 * lowest free descriptor. Validate everything before replacing a target. */
CP32_IRAM_EXT int cp32_fd_share(int source,int target)
{
  unsigned slot;
  if(!get_file(source)) return -CP32_FILE_BAD_FD;
  if(target==-1) {
    for(target=0;target<CP32_OPEN_MAX;target++) if(!descriptors[target]) break;
    if(target==CP32_OPEN_MAX) return -CP32_FILE_LIMIT;
  } else if(target<0 || target>=CP32_OPEN_MAX) return -CP32_FILE_BAD_FD;
  if(target==source) return target;
  slot=descriptors[source]-1;
  if(descriptors[target]) cp32_fd_close(target);
  descriptors[target]=slot+1;
  references[slot]++;
  return target;
}

/* Internal read-only view for inode-based descriptor operations. */
CP32_IRAM_EXT const struct cp32_minix_file *cp32_fd_file(int fd)
{
  return get_file(fd);
}
