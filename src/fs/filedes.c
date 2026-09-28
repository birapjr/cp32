/* MINIX filedes.c:get_fd/get_filp responsibilities. Bounded read-only table
 * for the single command client; shared descriptions, no process tables yet. */
#include "fs.h"

static struct cp32_minix_file files[CP32_OPEN_MAX];
static unsigned references[CP32_OPEN_MAX];
static unsigned directories[CP32_OPEN_MAX];
/* Zero means closed; otherwise index+1 into the open-description table. */
static unsigned descriptors[CP32_OPEN_MAX];
/* MINIX fp_cloexec belongs to each descriptor; filp_flags is shared by
 * duplicated references. Neither flag set changes the read-only access mode.
 * CLOEXEC is recorded here for future exec integration; no exec exists yet. */
static unsigned descriptor_flags[CP32_OPEN_MAX];
static unsigned status_flags[CP32_OPEN_MAX];

CP32_IRAM_EXT static struct cp32_minix_file *get_file(int fd)
{
  if(fd<0 || fd>=CP32_OPEN_MAX || !descriptors[fd]) return 0;
  return &files[descriptors[fd]-1];
}

CP32_IRAM_EXT int cp32_fd_open(cp32_disk_reader read,unsigned capacity,const char *path)
{
  unsigned fd, slot;
  unsigned number, i, directory = 0;
  struct cp32_minix_super super;
  int result;
  for(fd=0;fd<CP32_OPEN_MAX;fd++) if(!descriptors[fd]) break;
  if(fd==CP32_OPEN_MAX) return -CP32_FILE_LIMIT;
  for(slot=0;slot<CP32_OPEN_MAX;slot++) if(!references[slot]) break;
  if(slot==CP32_OPEN_MAX) return -CP32_FILE_LIMIT;
  result=cp32_fs_resolve_path(read,capacity,path,&super,&number);
  if(result) return result;
  result=cp32_fs_file_inode(read,&super,number,&files[slot]);
  if(result==-CP32_FILE_IS_DIR) {
    struct cp32_minix_dir dir;
    result=cp32_fs_open_directory(read,&super,number,&dir);
    if(result) return result;
    /* MINIX common_open permits read-only directories. Reuse their strict
     * inode validation, then store the same shared byte offset as files. */
    files[slot].read=read;
    files[slot].super=super;
    files[slot].size=dir.size;
    files[slot].position=0;
    files[slot].zone_bytes=1024U << super.log_zone_size;
    for(i=0;i<7;i++) files[slot].zones[i]=dir.zones[i];
    files[slot].indirect=dir.indirect;
    files[slot].double_indirect=0;
    files[slot].inode=number;
    directory=1;
  }
  if(result) return result;
  directories[slot]=directory;
  references[slot]=1;
  descriptors[fd]=slot+1;
  descriptor_flags[fd]=0;
  status_flags[slot]=CP32_O_RDONLY;
  return (int)fd;
}

CP32_IRAM_EXT int cp32_fd_close(int fd)
{
  if(!get_file(fd)) return -CP32_FILE_BAD_FD;
  references[descriptors[fd]-1]--;
  descriptors[fd]=0;
  descriptor_flags[fd]=0;
  return 0;
}

CP32_IRAM_EXT int cp32_fd_read(int fd,char *out,unsigned count)
{
  struct cp32_minix_file *file=get_file(fd);
  if(file && directories[descriptors[fd]-1] && count && file->position<file->size) {
    unsigned zone;
    int result;
    if(!out) return -CP32_SUPER_INVALID;
    result=cp32_fs_read_map(file,file->position/file->zone_bytes,&zone);
    if(result) return result;
    if(!zone) return -CP32_SUPER_INVALID; /* no sparse directories */
  }
  return file ? cp32_minix_file_read(file,out,count) : -CP32_FILE_BAD_FD;
}

/* Command-client directory iterator over a shared descriptor offset.
 * MINIX libc readdir similarly consumes an open directory. Keep disk name
 * validation in the existing decoder; this helper is not a new syscall. */
CP32_IRAM_EXT int cp32_fd_readdir(int fd,unsigned *inode,char name[15])
{
  struct cp32_minix_file *file=get_file(fd);
  struct cp32_minix_dir dir;
  unsigned i;
  int result;
  if(!file) return -CP32_FILE_BAD_FD;
  if(!directories[descriptors[fd]-1]) return -CP32_FILE_NOT_DIR;
  if(!inode || !name || file->position%16) return -CP32_SUPER_INVALID;
  dir.super=file->super;
  dir.read=file->read;
  dir.size=file->size;
  dir.position=file->position;
  for(i=0;i<7;i++) dir.zones[i]=file->zones[i];
  dir.indirect=file->indirect;
  result=cp32_minix_root_next(&dir,inode,name);
  /* Publish only successful progress (including skipped deleted entries).
   * Errors leave the shared offset and caller outputs unchanged. */
  if(result>=0) file->position=dir.position;
  return result;
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
  descriptor_flags[target]=0;
  references[slot]++;
  return target;
}

/* MINIX misc.c:do_fcntl and filedes.c:get_fd(start). Keep table mutation
 * beside the descriptor owner. Regular files/directories accept APPEND and
 * NONBLOCK as shared status metadata; CP32 still has no writes or pipes. */
CP32_IRAM_EXT int cp32_fd_fcntl(int fd,int command,long argument)
{
  unsigned slot;
  int target;
  if(!get_file(fd)) return -CP32_FILE_BAD_FD;
  slot=descriptors[fd]-1;
  switch(command) {
    case CP32_F_DUPFD:
      if(argument<0 || argument>=CP32_OPEN_MAX) return -CP32_SUPER_INVALID;
      for(target=(int)argument;target<CP32_OPEN_MAX;target++)
        if(!descriptors[target]) return cp32_fd_share(fd,target);
      return -CP32_FILE_LIMIT;
    case CP32_F_GETFD:
      return (int)descriptor_flags[fd];
    case CP32_F_SETFD:
      descriptor_flags[fd]=(unsigned long)argument & CP32_FD_CLOEXEC;
      return 0;
    case CP32_F_GETFL:
      return (int)status_flags[slot];
    case CP32_F_SETFL:
      status_flags[slot]=(unsigned long)argument & (CP32_O_APPEND | CP32_O_NONBLOCK);
      return 0;
    case CP32_F_GETLK:
    case CP32_F_SETLK:
    case CP32_F_SETLKW:
      return -CP32_SUPER_UNSUPPORTED;
    default:
      return -CP32_SUPER_INVALID;
  }
}

/* Internal read-only view for inode-based descriptor operations. */
CP32_IRAM_EXT const struct cp32_minix_file *cp32_fd_file(int fd)
{
  return get_file(fd);
}
