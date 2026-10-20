/* Application FS requests use an explicitly selected process record.
 * Pointer spans are validated at the kernel IPC boundary before dispatch. */
#include "fproc.h"
#include "../apps/hello/abi.h"
#include <string.h>
CP32_FS_TEXT int cp32_fs_errno(int r)
{
  switch(-r) {
    case CP32_FILE_BUSY:return -16;
    case CP32_FILE_NOT_EMPTY:return -39;
    case CP32_FILE_NOT_FOUND:return -2;
    case CP32_FILE_IS_DIR:return -21;
    case CP32_FILE_NOT_DIR:return -20;
    case CP32_FILE_NAME:return -36;
    case CP32_FILE_BAD_FD:return -9;
    case CP32_FILE_LIMIT:return -24;
    case CP32_FILE_EXISTS:return -17;
    case CP32_FILE_NOSPACE:return -28;
    case CP32_FILE_ACCESS:return -13;
    case CP32_FILE_BIG:return -27;
    case CP32_FILE_LINK_LIMIT:return -31;
    case CP32_FILE_READONLY:return -30;
    case CP32_SUPER_INVALID:return -22;
    case CP32_SUPER_UNSUPPORTED:return -38;
    default:return -5;
  }
}

/* MINIX remove_dir protects every live cwd. CP32's foreground owner supplies
 * the suspended shell record as well as the active application record. */
CP32_FS_TEXT int cp32_fproc_rmdir(struct cp32_fproc *p,const struct cp32_fproc *guard,
    cp32_disk_reader read,cp32_disk_writer write,unsigned capacity,const char *name)
{
#if CP32_ROOT_RAM
  (void)p;(void)guard;(void)read;(void)write;(void)capacity;(void)name;
  return -30;
#else
  char path[256];struct cp32_minix_stat target,cwd;int r;
  if(!p->pid)return -9;
  if(!*name)return -2;
  /* Reject literal terminal dot components before abspath normalizes them. */
  {unsigned i=0,start,end;while(name[i])i++;
   end=i;while(end && name[end-1]=='/')end--;
   start=end;while(start && name[start-1]!='/')start--;
   if((end-start==1 && name[start]=='.') ||
      (end-start==2 && name[start]=='.' && name[start+1]=='.'))return -22;}
  r=cp32_minix_abspath(p->cwd,name,path);if(r)return cp32_fs_errno(r);
  r=cp32_minix_stat(read,capacity,path,&target);if(r)return cp32_fs_errno(r);
  if((target.mode&0170000)!=0040000)return -20;
  if(target.inode==1)return -16;
  if(guard && guard->pid) {
    r=cp32_minix_stat(read,capacity,guard->cwd,&cwd);if(r)return cp32_fs_errno(r);
    if(target.inode==cwd.inode)return -16;
  }
  r=cp32_minix_rmdir(read,write,capacity,path,p->cwd);
  return r ? cp32_fs_errno(r):0;
#endif
}

CP32_FS_TEXT int cp32_fproc_file(struct cp32_fproc *p,cp32_disk_reader read,cp32_disk_writer write,unsigned capacity,unsigned op,int fd,void *buffer,unsigned arg)
{
  int r,slot;
  int *cp32_app_fds=p->fds;
  unsigned position;
  if(!p->pid)return -9;
  if(op==CP32_APP_DUPFD) {
    r=cp32_fproc_dupfd(p,fd,arg);return r<0 ? cp32_fs_errno(r):r;
  }
  if(op==CP32_APP_DUP2) {
    if(arg<3 || arg>=3+CP32_APP_FILE_MAX)return -9;
    r=cp32_fproc_dup2(p,fd,(int)arg);return r<0 ? cp32_fs_errno(r):r;
  }
  if(op==CP32_APP_RMDIR)return cp32_fproc_rmdir(p,0,read,write,capacity,buffer);
  if(op==CP32_APP_LINK || op==CP32_APP_RENAME_NOREPLACE) {
#if CP32_ROOT_RAM
    return -30;
#else
    struct cp32_app_link_request *q=buffer;char source[256],destination[256];
    if(!q->source[0] || !q->destination[0])return -2;
    r=cp32_minix_abspath(p->cwd,q->source,source);
    if(!r)r=cp32_minix_abspath(p->cwd,q->destination,destination);
    if(!r)r=op==CP32_APP_LINK ? cp32_minix_link(read,write,capacity,source,destination):
      cp32_minix_rename(read,write,capacity,source,destination);
    return r ? cp32_fs_errno(r):0;
#endif
  }
  if(op==CP32_APP_MKDIR || op==CP32_APP_CHMOD) {
#if CP32_ROOT_RAM
    return -30;
#else
    struct cp32_app_open_request *q=buffer;char path[256];
    if(q->flags)return -22;
    if(!q->path[0])return -2;
    r=cp32_minix_abspath(p->cwd,q->path,path);
    if(!r)r=op==CP32_APP_CHMOD ? cp32_minix_chmod(read,write,capacity,path,q->mode):
      cp32_minix_mkdir_mode(read,write,capacity,path,q->mode&~p->mask);
    return r ? cp32_fs_errno(r):0;
#endif
  }
  if(op==CP32_APP_UNLINK) {
#if CP32_ROOT_RAM
    return -30;
#else
    char path[256];
    if(!*(char *)buffer)return -2;
    r=cp32_minix_abspath(p->cwd,buffer,path);
    if(!r)r=cp32_minix_unlink(read,write,capacity,path);
    return r ? cp32_fs_errno(r):0;
#endif
  }
  if(op==CP32_APP_CHDIR) {
    r=cp32_fproc_chdir(p,read,capacity,buffer);
    return r ? cp32_fs_errno(r):0;
  }
  if(op==CP32_APP_GETCWD) {
    unsigned n=0;while(p->cwd[n])n++;n++;
    if(arg<n)return -34;
    memcpy(buffer,p->cwd,n);return 0;
  }
  if(op==CP32_APP_UMASK) {
    unsigned old=p->mask;p->mask=arg&0777;return old;
  }
  if(op>=CP32_APP_GETFD && op<=CP32_APP_SETFL) {
    r=cp32_fproc_flags(p,fd,CP32_F_GETFD+op-CP32_APP_GETFD,arg);
    return r<0 ? cp32_fs_errno(r):r;
  }
  if(op==CP32_APP_STAT) {
    char path[CP32_MINIX_PATH_MAX+1];
    struct cp32_minix_stat info;
    if(!*(char *)buffer)return -2;
    r=cp32_minix_abspath(p->cwd,buffer,path);
    if(!r)r=cp32_minix_stat(read,capacity,path,&info);
    if(!r)memcpy((char *)buffer+256,&info,sizeof(info));
    return r ? cp32_fs_errno(r) : 0;
  }
  if(op==CP32_APP_OPEN_MODE) {
    struct cp32_app_open_request *request=buffer;
    char path[CP32_MINIX_PATH_MAX+1];
    if(!request->path[0])return -2;
    for(slot=0;slot<CP32_APP_FILE_MAX;slot++)if(!cp32_app_fds[slot])break;
    if(slot==CP32_APP_FILE_MAX)return -24;
    r=cp32_minix_abspath(p->cwd,request->path,path);
    if(r)return cp32_fs_errno(r);
    r=cp32_fd_open_mode(read,write,capacity,path,request->flags,request->mode&~p->mask);
    if(r<0)return cp32_fs_errno(r);
    cp32_app_fds[slot]=r+1;p->cloexec&=~(1U<<slot);return slot+3;
  }
  if(op==CP32_APP_OPEN || op==CP32_APP_OPENDIR) {
    char path[CP32_MINIX_PATH_MAX+1];
    struct cp32_minix_stat info;
    if(!*(char *)buffer)return -2;
    for(slot=0;slot<CP32_APP_FILE_MAX;slot++)if(!cp32_app_fds[slot])break;
    if(slot==CP32_APP_FILE_MAX)return -24;
    r=cp32_minix_abspath(p->cwd,buffer,path);
    if(r)return cp32_fs_errno(r);
    r=op==CP32_APP_OPENDIR ? cp32_fd_open(read,capacity,path) :
                           cp32_fd_open_mode(read,write,capacity,path,CP32_O_RDONLY,0);
    if(r<0)return cp32_fs_errno(r);
    fd=r;r=cp32_fd_fstat(fd,&info);
    if(!r && op==CP32_APP_OPENDIR && (info.mode & 0170000)!=0040000) {
      cp32_fd_close(fd);return -20;
    }
    if(!r && op==CP32_APP_OPEN && (info.mode&0170000)!=0100000) {
      cp32_fd_close(fd);return -21;
    }
    if(r || !(info.mode & 0444)) {
      cp32_fd_close(fd);return r ? cp32_fs_errno(r) : -13;
    }
    cp32_app_fds[slot]=fd+1;return slot+3;
  }
  if(fd<3 || fd>=3+CP32_APP_FILE_MAX || !cp32_app_fds[fd-3])return -9;
  slot=fd-3;fd=cp32_app_fds[slot]-1;
  if(op==CP32_APP_DUP) {
    for(slot=0;slot<CP32_APP_FILE_MAX;slot++)if(!cp32_app_fds[slot])break;
    if(slot==CP32_APP_FILE_MAX)return -24;
    r=cp32_fd_dup(fd);if(r<0)return cp32_fs_errno(r);
    cp32_app_fds[slot]=r+1;p->cloexec&=~(1U<<slot);return slot+3;
  }
  if(op==CP32_APP_PREAD || op==CP32_APP_PWRITE) {
    struct cp32_app_position_request *q=buffer;char data[64];
    if(q->count>64 || q->offset>0x7fffffffU || q->count>0x7fffffffU-q->offset)return -22;
    if(op==CP32_APP_PWRITE) {
      if(q->count)memcpy(data,q->data,q->count);
      r=cp32_fd_pwrite(fd,data,q->count,q->offset);
    } else {
      r=cp32_fd_pread(fd,data,q->count,q->offset);
      if(r>0)memcpy(q->data,data,r);
    }
  } else if(op==CP32_APP_FCHMOD) {
#if CP32_ROOT_RAM
    r=-CP32_FILE_READONLY;
#else
    const struct cp32_minix_file *f=cp32_fd_file(fd);
    r=f ? cp32_fs_chmod_inode(f->read,write,&f->super,f->inode,arg):-CP32_FILE_BAD_FD;
#endif
  } else if(op==CP32_APP_FTRUNCATE) {
    if(arg>0x7fffffffU)return -22;
    r=cp32_fd_truncate(fd,arg);
  } else if(op==CP32_APP_WRITE) {
    char data[64];if(arg>64)return -22;
    if(arg)memcpy(data,buffer,arg);
    r=cp32_fd_write(fd,data,arg);
  } else if(op==CP32_APP_FSTAT) {
    struct cp32_minix_stat info;
    r=cp32_fd_fstat(fd,&info);
    if(!r)memcpy(buffer,&info,sizeof(info));
  } else if(op==CP32_APP_READDIR) {
    struct cp32_app_dirent entry;
    memset(&entry,0,sizeof(entry));
    r=cp32_fd_readdir(fd,&entry.inode,entry.name);
    if(r==1)memcpy(buffer,&entry,sizeof(entry));
  } else if(op==CP32_APP_CLOSE) {
    r=cp32_fd_close(fd);
    cp32_app_fds[slot]=0;p->cloexec&=~(1U<<slot);
  } else if(op==CP32_APP_READ) {
    char data[64];
    if(arg>sizeof(data))return -22;
    r=cp32_fd_read(fd,data,arg);
    if(r>0)memcpy(buffer,data,r);
  } else if(op>=CP32_APP_SEEK_SET && op<=CP32_APP_SEEK_END) {
    r=cp32_fd_seek(fd,(int)arg,op-CP32_APP_SEEK_SET,&position);
    if(!r)return position;
    if(r==-CP32_SUPER_INVALID)return -22;
  } else return -22;
  return r<0 ? cp32_fs_errno(r) : r;
}

/* Exec loader owns a private descriptor, outside the application fd namespace. */
CP32_FS_TEXT int cp32_fproc_exec_open(struct cp32_fproc *p,cp32_disk_reader read,unsigned capacity,const char *name,unsigned *size)
{
  char path[256];struct cp32_minix_stat info;int r,fd;
  if(!p->pid)return -9;
  if(!*name)return -2;
  r=cp32_minix_abspath(p->cwd,name,path);if(r)return cp32_fs_errno(r);
  fd=cp32_fd_open_mode(read,0,capacity,path,CP32_O_RDONLY,0);
  if(fd<0)return fd==-CP32_FILE_IS_DIR ? -13:cp32_fs_errno(fd);
  r=cp32_fd_fstat(fd,&info);
  if(r || (info.mode&0170000)!=0100000 || !(info.mode&0111) || !(info.mode&0444)){cp32_fd_close(fd);return r ? -5:-13;}
  *size=info.size;return fd;
}

/* Kernel supplies the fixed application address range before FS dispatch. */
CP32_FS_TEXT int cp32_fproc_validate(unsigned op,char *buffer,unsigned n,uintptr_t base,uintptr_t top)
{
  uintptr_t p=(uintptr_t)buffer;
  if(op>CP32_APP_PWRITE || op==CP32_APP_EXEC) return -22;
  if(op==CP32_APP_PREAD || op==CP32_APP_PWRITE || op==CP32_APP_OPEN || op==CP32_APP_OPENDIR || op==CP32_APP_READ ||
     op==CP32_APP_FSTAT || op==CP32_APP_READDIR || op==CP32_APP_STAT ||
     op==CP32_APP_OPEN_MODE || op==CP32_APP_MKDIR || op==CP32_APP_CHMOD || op==CP32_APP_LINK || op==CP32_APP_RENAME_NOREPLACE || op==CP32_APP_WRITE || op==CP32_APP_CHDIR || op==CP32_APP_GETCWD || op==CP32_APP_UNLINK || op==CP32_APP_RMDIR) {
    if((op==CP32_APP_GETCWD || op==CP32_APP_CHDIR) && (!n || n>256))return -22;
    if((op==CP32_APP_OPEN || op==CP32_APP_OPENDIR || op==CP32_APP_UNLINK || op==CP32_APP_RMDIR) && (!n || n>256)) return -36;
    if((op==CP32_APP_READ || op==CP32_APP_WRITE) && n>64) return -22;
    if((op==CP32_APP_OPEN_MODE || op==CP32_APP_MKDIR || op==CP32_APP_CHMOD) && (n!=sizeof(struct cp32_app_open_request) || (p&3)))return -22;
    if((op==CP32_APP_PREAD || op==CP32_APP_PWRITE) && (n!=sizeof(struct cp32_app_position_request) || (p&3)))return -22;
    if(op==CP32_APP_FSTAT && n!=sizeof(struct cp32_app_stat))return -22;
    if(op==CP32_APP_READDIR && n!=sizeof(struct cp32_app_dirent))return -22;
    if(op==CP32_APP_STAT && n!=sizeof(struct cp32_app_stat_request))return -22;
    if((op==CP32_APP_LINK || op==CP32_APP_RENAME_NOREPLACE) && n!=sizeof(struct cp32_app_link_request))return -22;
    if(n && (p<base || p>top || n>top-p)) return -14;
    if((op==CP32_APP_OPEN || op==CP32_APP_OPENDIR || op==CP32_APP_CHDIR || op==CP32_APP_UNLINK || op==CP32_APP_RMDIR) && buffer[n-1]) return -22;
    if((op==CP32_APP_LINK || op==CP32_APP_RENAME_NOREPLACE) && (buffer[255] || buffer[511]))return -36;
    if(op==CP32_APP_STAT && buffer[255])return -36;
    if((op==CP32_APP_OPEN_MODE || op==CP32_APP_MKDIR || op==CP32_APP_CHMOD) && buffer[255])return -22;
  }
  return 0;
}
