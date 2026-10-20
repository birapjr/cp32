/* MINIX misc.c do_fork/do_exec/do_exit, bounded for CP32's serialized service. */
#include "fproc.h"
CP32_FS_TEXT void cp32_fproc_exit(struct cp32_fproc *p)
{
  unsigned i;
  for(i=0;i<CP32_FPROC_FDS;i++) {
    if(p->fds[i])cp32_fd_close(p->fds[i]-1);
    p->fds[i]=0;
  }
  if(p->input)cp32_fd_close(p->input-1);
  p->input=0;p->cloexec=0;p->pid=0;
}
CP32_FS_TEXT int cp32_fproc_clone(struct cp32_fproc *child,const struct cp32_fproc *parent,unsigned pid)
{
  unsigned i;int fd;
  if(child==parent || child->pid || !parent->pid || !pid)return -CP32_SUPER_INVALID;
  *child=*parent;
  for(i=0;i<CP32_FPROC_FDS;i++)child->fds[i]=0;
  child->input=0;
  for(i=0;i<CP32_FPROC_FDS;i++)if(parent->fds[i]) {
    fd=cp32_fd_dup(parent->fds[i]-1);
    if(fd<0)goto failed;
    child->fds[i]=fd+1;
  }
  if(parent->input) {
    fd=cp32_fd_dup(parent->input-1);if(fd<0)goto failed;
    child->input=fd+1;
  }
  child->pid=pid;return 0;
failed:
  cp32_fproc_exit(child);return fd;
}
CP32_FS_TEXT void cp32_fproc_exec(struct cp32_fproc *p)
{
  unsigned i;
  for(i=0;i<CP32_FPROC_FDS;i++)if((p->cloexec&(1U<<i)) && p->fds[i]) {
    cp32_fd_close(p->fds[i]-1);p->fds[i]=0;
  }
  p->cloexec=0;
}
CP32_FS_TEXT int cp32_fproc_chdir(struct cp32_fproc *p,cp32_disk_reader read,unsigned capacity,const char *path)
{
  if(!p->pid || p->root_inode!=1)return -CP32_SUPER_INVALID;
  if(!path || !*path)return -CP32_FILE_NOT_FOUND;
  return cp32_minix_chdir(read,capacity,p->cwd,path);
}
/* Descriptor flags are local; open-description flags are shared by dup/clone. */
CP32_FS_TEXT int cp32_fproc_flags(struct cp32_fproc *p,int fd,int command,unsigned arg)
{
  unsigned slot;
  if(!p->pid || fd<3 || fd>=3+CP32_FPROC_FDS || !p->fds[fd-3])return -CP32_FILE_BAD_FD;
  slot=fd-3;
  if(command==CP32_F_GETFD)return !!(p->cloexec&(1U<<slot));
  if(command==CP32_F_SETFD) {
    if(arg&~CP32_FD_CLOEXEC)return -CP32_SUPER_INVALID;
    p->cloexec=(p->cloexec&~(1U<<slot))|((arg&1)<<slot);return 0;
  }
  if(command!=CP32_F_GETFL && command!=CP32_F_SETFL)return -CP32_SUPER_INVALID;
  return cp32_fd_fcntl(p->fds[slot]-1,command,arg);
}

/* MINIX do_dup: validate both local descriptors before closing any target.
 * Backend dup2 can reuse the occupied target even when all slots are full. */
CP32_FS_TEXT int cp32_fproc_dup2(struct cp32_fproc *p,int source,int target)
{
  int r;
  if(!p->pid || source<3 || source>=3+CP32_FPROC_FDS ||
     target<3 || target>=3+CP32_FPROC_FDS || !p->fds[source-3])return -CP32_FILE_BAD_FD;
  if(source==target)return target;
  r=p->fds[target-3] ? cp32_fd_dup2(p->fds[source-3]-1,p->fds[target-3]-1):
                      cp32_fd_dup(p->fds[source-3]-1);
  if(r<0)return r;
  p->fds[target-3]=r+1;p->cloexec&=~(1U<<(target-3));return target;
}

/* F_DUPFD searches the caller's namespace, never the backend's numbering. */
CP32_FS_TEXT int cp32_fproc_dupfd(struct cp32_fproc *p,int source,unsigned minimum)
{
  unsigned slot;int r;
  if(!p->pid || source<3 || source>=3+CP32_FPROC_FDS || !p->fds[source-3])return -CP32_FILE_BAD_FD;
  if(minimum>=3+CP32_FPROC_FDS)return -CP32_SUPER_INVALID;
  if(minimum<3)minimum=3; /* reserved callback-backed standard streams */
  for(slot=minimum-3;slot<CP32_FPROC_FDS;slot++)if(!p->fds[slot])break;
  if(slot==CP32_FPROC_FDS)return -CP32_FILE_LIMIT;
  r=cp32_fd_dup(p->fds[source-3]-1);if(r<0)return r;
  p->fds[slot]=r+1;p->cloexec&=~(1U<<slot);return slot+3;
}
