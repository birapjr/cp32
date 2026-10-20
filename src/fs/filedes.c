/* MINIX filedes.c:get_fd/get_filp responsibilities. Bounded inode-backed table
 * beneath fproc namespaces and internal service handles; serialized by FS. */
#include "fs.h"

/* Backend indices/reference counts cannot exceed the eight-slot table.
 * Compact private metadata leaves SRAM for independent process records. */
typedef char cp32_fd_index_bound[(CP32_OPEN_MAX<=255) ? 1:-1];
typedef char cp32_fd_flags_bound[((CP32_O_APPEND|CP32_O_NONBLOCK|3)<=65535) ? 1:-1];
static struct cp32_minix_file files[CP32_OPEN_MAX];
static unsigned char references[CP32_OPEN_MAX];
static unsigned char directories[CP32_OPEN_MAX];
/* Zero means closed; otherwise index+1 into the open-description table. */
static unsigned char descriptors[CP32_OPEN_MAX];
/* MINIX fp_cloexec belongs to each descriptor; filp_flags is shared by
 * duplicated references. F_SETFL preserves the open-time access mode.
 * Application descriptor flags live in fproc; these flags serve internal users. */
static unsigned char descriptor_flags[CP32_OPEN_MAX];
static unsigned short status_flags[CP32_OPEN_MAX];
#if !CP32_ROOT_RAM
static cp32_disk_writer writers[CP32_OPEN_MAX];
static unsigned char pending_unlink[CP32_OPEN_MAX];
#endif

CP32_FS_TEXT static struct cp32_minix_file *get_file(int fd)
{
  if(fd<0 || fd>=CP32_OPEN_MAX || !descriptors[fd]) return 0;
  return &files[descriptors[fd]-1];
}

CP32_FS_TEXT int cp32_fd_open(cp32_disk_reader read,unsigned capacity,const char *path)
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
#if !CP32_ROOT_RAM
  pending_unlink[slot]=0;
#endif
  directories[slot]=directory;
  references[slot]=1;
  descriptors[fd]=slot+1;
  descriptor_flags[fd]=0;
  status_flags[slot]=CP32_O_RDONLY;
  return (int)fd;
}

/* Refresh inode content without replacing the shared open-file offset. */
CP32_FS_TEXT static int refresh(int fd)
{
  struct cp32_minix_file *f=get_file(fd),next;unsigned position;int r;
  if(!f)return -CP32_FILE_BAD_FD;
  if(directories[descriptors[fd]-1])return 0;
  position=f->position;r=cp32_fs_file_inode(f->read,&f->super,f->inode,&next);
  if(!r){*f=next;f->position=position;}
  return r;
}
CP32_FS_TEXT int cp32_fd_open_mode(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned flags,unsigned mode)
{
#if CP32_ROOT_RAM
  if(flags!=CP32_O_RDONLY)return -CP32_FILE_READONLY;
  return cp32_fd_open(read,capacity,path);
#else
  struct cp32_minix_stat st;int r,fd;unsigned access=flags&3,i;
  if(access==3 || mode&~0777U || flags&~(3U|CP32_O_CREAT|CP32_O_EXCL|CP32_O_TRUNC|CP32_O_APPEND|CP32_O_NONBLOCK) ||
     ((flags&CP32_O_TRUNC) && !access) || ((flags&CP32_O_EXCL) && !(flags&CP32_O_CREAT)))return -CP32_SUPER_INVALID;
  for(i=0;i<CP32_OPEN_MAX;i++)if(!descriptors[i])break;
  if(i==CP32_OPEN_MAX)return -CP32_FILE_LIMIT;
  if((access || (flags&CP32_O_CREAT)) && !write)return -CP32_FILE_READONLY;
  r=cp32_minix_stat(read,capacity,path,&st);
  if(!r) {
    if((flags&(CP32_O_CREAT|CP32_O_EXCL))==(CP32_O_CREAT|CP32_O_EXCL))return -CP32_FILE_EXISTS;
    if((st.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
    if((access!=CP32_O_WRONLY && !(st.mode&0444)) || (access && !(st.mode&0222)))return -CP32_FILE_ACCESS;
  } else if(r==-CP32_FILE_NOT_FOUND && (flags&CP32_O_CREAT)) {
    r=cp32_minix_create(read,write,capacity,path,0,0);if(r)return r;
    r=cp32_minix_chmod(read,write,capacity,path,mode);if(r)return r;
  } else return r;
  fd=cp32_fd_open(read,capacity,path);if(fd<0)return fd;
  if(flags&CP32_O_TRUNC) {
    r=cp32_fs_truncate_inode(read,write,capacity,files[descriptors[fd]-1].inode,0);
    if(r){cp32_fd_close(fd);return r;}
  }
  i=descriptors[fd]-1;writers[i]=write;
  status_flags[i]=flags&(3U|CP32_O_APPEND|CP32_O_NONBLOCK);
  return fd;
#endif
}
CP32_FS_TEXT int cp32_fd_write(int fd,const char *bytes,unsigned count)
{
#if CP32_ROOT_RAM
  return get_file(fd) ? -CP32_FILE_READONLY:-CP32_FILE_BAD_FD;
#else
  struct cp32_minix_file *f=get_file(fd);unsigned slot,position;int r;
  if(!f)return -CP32_FILE_BAD_FD;
  slot=descriptors[fd]-1;
  if(!(status_flags[slot]&3) || !writers[slot])return -CP32_FILE_BAD_FD;
  if(count>64 || (count && !bytes))return -CP32_SUPER_INVALID;
  r=refresh(fd);if(r)return r;
  if(!count)return 0;
  position=(status_flags[slot]&CP32_O_APPEND) ? f->size:f->position;
  r=cp32_fs_write_inode(f->read,writers[slot],&f->super,f->inode,position,bytes,count);
  if(r>0)f->position=position+r;
  return r;
#endif
}

CP32_FS_TEXT int cp32_fd_truncate(int fd,unsigned length)
{
  struct cp32_minix_file *f=get_file(fd);
  if(!f)return -CP32_FILE_BAD_FD;
#if CP32_ROOT_RAM
  (void)length;return -CP32_FILE_READONLY;
#else
  unsigned slot=descriptors[fd]-1;
  if(!(status_flags[slot]&3) || !writers[slot])return -CP32_FILE_BAD_FD;
  if(directories[slot])return -CP32_FILE_IS_DIR;
  /* Geometry was capacity-checked at open. Do not alter the shared offset. */
  return cp32_fs_resize_inode(f->read,writers[slot],f->super.zones*f->zone_bytes,f->inode,length);
#endif
}

/* Positional I/O never publishes a new shared offset. */
CP32_FS_TEXT int cp32_fd_pread(int fd,char *bytes,unsigned count,unsigned offset)
{
  struct cp32_minix_file *f=get_file(fd),local;unsigned slot;int r;
  if(!f)return -CP32_FILE_BAD_FD;
  slot=descriptors[fd]-1;
  if((status_flags[slot]&3)==CP32_O_WRONLY)return -CP32_FILE_BAD_FD;
  if(directories[slot])return -CP32_FILE_IS_DIR;
  if(count>64 || offset>0x7fffffffU || count>0x7fffffffU-offset)return -CP32_SUPER_INVALID;
  r=refresh(fd);if(r)return r;local=*f;local.position=offset;
  return cp32_minix_file_read(&local,bytes,count);
}
CP32_FS_TEXT int cp32_fd_pwrite(int fd,const char *bytes,unsigned count,unsigned offset)
{
  struct cp32_minix_file *f=get_file(fd);
  if(!f)return -CP32_FILE_BAD_FD;
#if CP32_ROOT_RAM
  (void)bytes;(void)count;(void)offset;return -CP32_FILE_READONLY;
#else
  unsigned slot=descriptors[fd]-1;
  if(!(status_flags[slot]&3) || !writers[slot])return -CP32_FILE_BAD_FD;
  if(directories[slot])return -CP32_FILE_IS_DIR;
  if(count>64 || offset>0x7fffffffU || count>0x7fffffffU-offset || (count && !bytes))return -CP32_SUPER_INVALID;
  if(!count)return 0;
  /* Explicit position takes precedence over APPEND, without changing flags. */
  return cp32_fs_write_inode(f->read,writers[slot],&f->super,f->inode,offset,bytes,count);
#endif
}

/* Match a mounted reader and inode, not only a numeric inode. */
CP32_FS_TEXT int cp32_fd_inode_open(cp32_disk_reader read,unsigned number)
{
  unsigned i,n=0;
  for(i=0;i<CP32_OPEN_MAX;i++)if(references[i] && files[i].read==read && files[i].inode==number)n+=references[i];
  return n;
}
#if !CP32_ROOT_RAM
CP32_FS_TEXT int cp32_fd_defer_unlink(cp32_disk_reader read,cp32_disk_writer write,unsigned number)
{
  unsigned i;int count=cp32_fd_inode_open(read,number);
  for(i=0;i<CP32_OPEN_MAX;i++)if(references[i] && files[i].read==read && files[i].inode==number) {
    pending_unlink[i]=1;writers[i]=write;
  }
  return count;
}
#endif
CP32_FS_TEXT int cp32_fd_close(int fd)
{
  struct cp32_minix_file *f=get_file(fd);unsigned slot;int r=0;
  if(!f)return -CP32_FILE_BAD_FD;
  slot=descriptors[fd]-1;
#if !CP32_ROOT_RAM
  if(pending_unlink[slot] && cp32_fd_inode_open(f->read,f->inode)==1)
    r=cp32_fs_reap_inode(f->read,writers[slot],&f->super,f->inode);
#endif
  /* Close consumes the descriptor even if final reclamation reports an error.
   * Uncertain reclamation leaks reservations rather than retrying/freeing twice. */
  references[slot]--;descriptors[fd]=0;descriptor_flags[fd]=0;
  return r;
}

CP32_FS_TEXT int cp32_fd_read(int fd,char *out,unsigned count)
{
  struct cp32_minix_file *file=get_file(fd);
  int refreshed;
  if(!file || (status_flags[descriptors[fd]-1]&3)==CP32_O_WRONLY)return -CP32_FILE_BAD_FD;
  refreshed=refresh(fd);if(refreshed)return refreshed;
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
CP32_FS_TEXT int cp32_fd_readdir(int fd,unsigned *inode,char name[15])
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

CP32_FS_TEXT int cp32_fd_seek(int fd,long offset,int whence,unsigned *position)
{
  struct cp32_minix_file *file=get_file(fd);
  int result;
  if(!file) return -CP32_FILE_BAD_FD;
  if(whence==2){result=refresh(fd);if(result)return result;}
  result=cp32_minix_file_seek(file,offset,whence);
  if(!result && position) *position=file->position;
  return result;
}

/* Install another reference without copying the offset. -1 selects the
 * lowest free descriptor. Validate everything before replacing a target. */
CP32_FS_TEXT int cp32_fd_share(int source,int target)
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
 * NONBLOCK as shared status metadata; blocking pipes remain unported. */
CP32_FS_TEXT int cp32_fd_fcntl(int fd,int command,long argument)
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
      status_flags[slot]=(status_flags[slot]&3) | ((unsigned long)argument & (CP32_O_APPEND | CP32_O_NONBLOCK));
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
CP32_FS_TEXT const struct cp32_minix_file *cp32_fd_file(int fd)
{
  return get_file(fd);
}
