/* MINIX read/write expose byte counts, including short transfers. Adapt the
 * bootstrap's bounded callbacks to standard streams and owned file handles. */
#include "io.h"
int cp32_errno;
static int io_error(int code) {cp32_errno=code;return -1;}
static const struct cp32_app_services *api;
int cp32_io_init(const struct cp32_app_services *services)
{
  if(api || !services || services->version<5 || !services->read || !services->write)
    return io_error(CP32_EINVAL);
  api=services;
  return 0;
}
int cp32_read(int fd,void *buffer,unsigned count)
{
  int n;
  if(!api) return io_error(CP32_EINVAL);
  if(fd!=0 && (fd<3 || fd>=3+CP32_APP_FILE_MAX || api->version<6 || !api->file)) return io_error(CP32_EBADF);
  if(count>0x7fffffffU) return io_error(CP32_EINVAL);
  if(!buffer && count) return io_error(CP32_EFAULT);
  if(!count && !fd) return 0;
  if(count>64) count=64;
  if(fd) {
    n=api->file(CP32_APP_READ,fd,buffer,count);
    return n<0 ? io_error(-n) : (unsigned)n>count ? io_error(CP32_EIO) : n;
  }
  n=api->read(buffer,count);
  return n<0 || (unsigned)n>count ? io_error(CP32_EIO) : n;
}
int cp32_write(int fd,const void *buffer,unsigned count)
{
  unsigned done=0;
  if(!api) return io_error(CP32_EINVAL);
  if(fd!=1 && fd!=2 && (fd<3 || fd>=3+CP32_APP_FILE_MAX || api->version<9 || !api->file)) return io_error(CP32_EBADF);
  if(count>0x7fffffffU) return io_error(CP32_EINVAL);
  if(!buffer && count) return io_error(CP32_EFAULT);
  if(!count && fd>=3) {
    int r=api->file(CP32_APP_WRITE,fd,(void *)buffer,0);return r<0 ? io_error(-r):r;
  }
  while(done<count) {
    unsigned chunk=count-done;
    int n;
    if(chunk>(fd>=3 ? 64U:256U))chunk=fd>=3 ? 64:256;
    n=fd>=3 ? api->file(CP32_APP_WRITE,fd,(char *)buffer+done,chunk):api->write((const char *)buffer+done,chunk);
    if(n<0 || (unsigned)n>chunk) {
      io_error(n<0 && fd>=3 ? -n:CP32_EIO);return done ? (int)done : -1;
    }
    if(!n) break;
    done+=(unsigned)n;
  }
  return (int)done;
}

int cp32_readline(char *buffer,unsigned capacity)
{
  unsigned used=0;
  if(!buffer) return io_error(CP32_EFAULT);
  if(capacity<2 || capacity>0x7fffffffU) return io_error(CP32_EINVAL);
  buffer[0]=0;
  while(used<capacity-1) {
    char c;
    int n=cp32_read(0,&c,1);
    if(n<0) return -1;
    if(!n) break;
    buffer[used++]=c;buffer[used]=0;
    if(c=='\n') break;
  }
  return (int)used;
}

static int file_call(unsigned op,int fd,void *buffer,unsigned arg)
{
  int r;
  if(!api || api->version<6 || !api->file)return io_error(CP32_ENOSYS);
  r=api->file(op,fd,buffer,arg);
  return r<0 ? io_error(-r) : r;
}
static int open_path(const char *path,unsigned op)
{
  unsigned n=0;
  if(!path)return io_error(CP32_EFAULT);
  while(n<256 && path[n])n++;
  if(n==256)return io_error(CP32_ENAMETOOLONG);
  return file_call(op,0,(void *)path,n+1);
}
int cp32_close(int fd)
{
  if(fd<3)return io_error(CP32_EBADF);
  return file_call(CP32_APP_CLOSE,fd,0,0);
}
int cp32_lseek(int fd,int offset,int whence)
{
  if(fd>=0 && fd<3)return io_error(CP32_ESPIPE);
  if(fd<0)return io_error(CP32_EBADF);
  if(whence<0 || whence>2)return io_error(CP32_EINVAL);
  return file_call(CP32_APP_SEEK_SET+whence,fd,0,(unsigned)offset);
}

int cp32_open(const char *path) {return open_path(path,CP32_APP_OPEN);}
static int directory_api(void)
{
  return api && api->version>=7 && api->file ? 0 : io_error(CP32_ENOSYS);
}
int cp32_opendir(const char *path)
{
  return directory_api() ? -1 : open_path(path,CP32_APP_OPENDIR);
}
int cp32_readdir(int fd,struct cp32_app_dirent *entry)
{
  if(!entry)return io_error(CP32_EFAULT);
  if(directory_api())return -1;
  return file_call(CP32_APP_READDIR,fd,entry,sizeof(*entry));
}
int cp32_fstat(int fd,struct cp32_app_stat *info)
{
  if(!info)return io_error(CP32_EFAULT);
  if(directory_api())return -1;
  return file_call(CP32_APP_FSTAT,fd,info,sizeof(*info));
}
int cp32_stat(const char *path,struct cp32_app_stat *info)
{
  struct cp32_app_stat_request request;
  unsigned i=0;
  if(!path || !info)return io_error(CP32_EFAULT);
  if(directory_api())return -1;
  while(i<255 && path[i]) {request.path[i]=path[i];i++;}
  if(path[i])return io_error(CP32_ENAMETOOLONG);
  request.path[i]=0;request.path[255]=0;
  if(file_call(CP32_APP_STAT,0,&request,sizeof(request)))return -1;
  *info=request.info;return 0;
}

int cp32_execv(const char *path,const char *const argv[])
{
  struct cp32_app_exec_request request;
  unsigned n=0,used=0,i;
  if(!api || api->version<8 || !api->file)return io_error(CP32_ENOSYS);
  if(!path || !argv)return io_error(CP32_EFAULT);
  while(n<255 && path[n]) {request.path[n]=path[n];n++;}
  if(path[n])return io_error(CP32_ENAMETOOLONG);
  request.path[n]=0;request.path[255]=0;
  for(i=0;i<9 && argv[i];i++) {
    n=0;
    do {
      if(used==sizeof(request.args))return io_error(CP32_E2BIG);
      request.args[used++]=argv[i][n];
    }while(argv[i][n++]);
  }
  if(!i || argv[i])return io_error(CP32_E2BIG);
  request.argc=i;
  return file_call(CP32_APP_EXEC,0,&request,sizeof(request));
}

int cp32_open_flags(const char *path,unsigned flags,unsigned mode)
{
  struct cp32_app_open_request request;unsigned n=0;
  if(!api || api->version<9)return io_error(CP32_ENOSYS);
  if(!path)return io_error(CP32_EFAULT);
  while(n<255 && path[n]){request.path[n]=path[n];n++;}
  if(path[n])return io_error(CP32_ENAMETOOLONG);
  request.path[n]=0;request.path[255]=0;request.flags=flags;request.mode=mode;
  return file_call(CP32_APP_OPEN_MODE,0,&request,sizeof(request));
}
int cp32_creat(const char *path,unsigned mode)
{return cp32_open_flags(path,CP32_O_WRONLY|CP32_O_CREAT|CP32_O_TRUNC,mode);}
int cp32_dup(int fd)
{
  if(!api || api->version<9)return io_error(CP32_ENOSYS);
  return file_call(CP32_APP_DUP,fd,0,0);
}

int cp32_chdir(const char *path)
{
  if(!api || api->version<10)return io_error(CP32_ENOSYS);
  return open_path(path,CP32_APP_CHDIR);
}
char *cp32_getcwd(char *buffer,unsigned size)
{
  if(!api || api->version<10){io_error(CP32_ENOSYS);return 0;}
  if(!buffer){io_error(CP32_EFAULT);return 0;}
  if(!size){io_error(CP32_EINVAL);return 0;}
  if(size>256)size=256;
  return file_call(CP32_APP_GETCWD,0,buffer,size)<0 ? 0:buffer;
}
int cp32_umask(unsigned mask)
{
  if(!api || api->version<10)return io_error(CP32_ENOSYS);
  return file_call(CP32_APP_UMASK,0,0,mask);
}
int cp32_fcntl(int fd,int command,unsigned argument)
{
  if(!api || api->version<10)return io_error(CP32_ENOSYS);
  if(command==CP32_F_DUPFD) {
    if(api->version<19)return io_error(CP32_ENOSYS);
    return file_call(CP32_APP_DUPFD,fd,0,argument);
  }
  if(command<CP32_F_GETFD || command>CP32_F_SETFL)return io_error(CP32_EINVAL);
  return file_call(CP32_APP_GETFD+command-CP32_F_GETFD,fd,0,argument);
}

int cp32_unlink(const char *path)
{
  if(!api || api->version<11)return io_error(CP32_ENOSYS);
  return open_path(path,CP32_APP_UNLINK);
}

int cp32_mkdir(const char *path,unsigned mode)
{
  struct cp32_app_open_request q;unsigned n=0;
  if(!api || api->version<12)return io_error(CP32_ENOSYS);
  if(!path)return io_error(CP32_EFAULT);
  while(n<255 && path[n]){q.path[n]=path[n];n++;}
  if(path[n])return io_error(CP32_ENAMETOOLONG);
  q.path[n]=0;q.path[255]=0;q.flags=0;q.mode=mode;
  return file_call(CP32_APP_MKDIR,0,&q,sizeof(q));
}

int cp32_link(const char *source,const char *destination)
{
  struct cp32_app_link_request q;unsigned i;
  if(!api || api->version<13)return io_error(CP32_ENOSYS);
  if(!source || !destination)return io_error(CP32_EFAULT);
  for(i=0;i<255 && source[i];i++)q.source[i]=source[i];
  if(source[i])return io_error(CP32_ENAMETOOLONG);
  q.source[i]=q.source[255]=0;
  for(i=0;i<255 && destination[i];i++)q.destination[i]=destination[i];
  if(destination[i])return io_error(CP32_ENAMETOOLONG);
  q.destination[i]=q.destination[255]=0;
  return file_call(CP32_APP_LINK,0,&q,sizeof(q));
}

int cp32_chmod(const char *path,unsigned mode)
{
  struct cp32_app_open_request q;unsigned n=0;
  if(!api || api->version<14)return io_error(CP32_ENOSYS);
  if(!path)return io_error(CP32_EFAULT);
  if(mode&~0777U)return io_error(CP32_EINVAL);
  while(n<255 && path[n]){q.path[n]=path[n];n++;}
  if(path[n])return io_error(CP32_ENAMETOOLONG);
  q.path[n]=q.path[255]=0;q.flags=0;q.mode=mode;
  return file_call(CP32_APP_CHMOD,0,&q,sizeof(q));
}

int cp32_rmdir(const char *path)
{
  if(!api || api->version<15)return io_error(CP32_ENOSYS);
  return open_path(path,CP32_APP_RMDIR);
}

int cp32_rename_noreplace(const char *source,const char *destination)
{
  struct cp32_app_link_request q;unsigned i;
  if(!api || api->version<16)return io_error(CP32_ENOSYS);
  if(!source || !destination)return io_error(CP32_EFAULT);
  for(i=0;i<255 && source[i];i++)q.source[i]=source[i];
  if(source[i])return io_error(CP32_ENAMETOOLONG);
  q.source[i]=q.source[255]=0;
  for(i=0;i<255 && destination[i];i++)q.destination[i]=destination[i];
  if(destination[i])return io_error(CP32_ENAMETOOLONG);
  q.destination[i]=q.destination[255]=0;
  return file_call(CP32_APP_RENAME_NOREPLACE,0,&q,sizeof(q));
}

int cp32_rename(const char *source,const char *destination)
{
  struct cp32_app_link_request q;unsigned i;
  if(!api || api->version<23)return io_error(CP32_ENOSYS);
  if(!source || !destination)return io_error(CP32_EFAULT);
  for(i=0;i<255 && source[i];i++)q.source[i]=source[i];
  if(source[i])return io_error(CP32_ENAMETOOLONG);
  q.source[i]=q.source[255]=0;
  for(i=0;i<255 && destination[i];i++)q.destination[i]=destination[i];
  if(destination[i])return io_error(CP32_ENAMETOOLONG);
  q.destination[i]=q.destination[255]=0;
  return file_call(CP32_APP_RENAME,0,&q,sizeof(q));
}

int cp32_ftruncate(int fd,int length)
{
  if(!api || api->version<17)return io_error(CP32_ENOSYS);
  if(length<0)return io_error(CP32_EINVAL);
  return file_call(CP32_APP_FTRUNCATE,fd,0,(unsigned)length);
}

int cp32_dup2(int source,int target)
{
  if(!api || api->version<18)return io_error(CP32_ENOSYS);
  if(source<3 || source>=3+CP32_APP_FILE_MAX || target<3 || target>=3+CP32_APP_FILE_MAX)
    return io_error(CP32_EBADF);
  return file_call(CP32_APP_DUP2,source,0,(unsigned)target);
}

int cp32_fchmod(int fd,unsigned mode)
{
  if(!api || api->version<20)return io_error(CP32_ENOSYS);
  if(mode&~0777U)return io_error(CP32_EINVAL);
  return file_call(CP32_APP_FCHMOD,fd,0,mode);
}

static int position_io(int fd,void *buffer,unsigned count,int offset,int writing)
{
  struct cp32_app_position_request q;unsigned done=0,i;int r;
  if(!api || api->version<21)return io_error(CP32_ENOSYS);
  if(!buffer && count)return io_error(CP32_EFAULT);
  if(offset<0 || count>0x7fffffffU-(unsigned)offset)return io_error(CP32_EINVAL);
  if(!writing && count>64)count=64;
  do {
    q.offset=(unsigned)offset+done;q.count=count-done;if(q.count>64)q.count=64;
    if(writing)for(i=0;i<q.count;i++)q.data[i]=((const char *)buffer)[done+i];
    r=file_call(writing ? CP32_APP_PWRITE:CP32_APP_PREAD,fd,&q,sizeof(q));
    if(r<0)return done ? (int)done:-1;
    if((unsigned)r>q.count){io_error(CP32_EIO);return done ? (int)done:-1;}
    if(!writing)for(i=0;i<(unsigned)r;i++)((char *)buffer)[done+i]=q.data[i];
    done+=r;if(!r || !writing)break;
  }while(done<count);
  return done;
}
int cp32_pread(int fd,void *buffer,unsigned count,int offset)
{return position_io(fd,buffer,count,offset,0);}
int cp32_pwrite(int fd,const void *buffer,unsigned count,int offset)
{return position_io(fd,(void *)buffer,count,offset,1);}

int cp32_fsinfo(struct cp32_app_fsinfo *info)
{
  if(!api || api->version<22)return io_error(CP32_ENOSYS);
  if(!info)return io_error(CP32_EFAULT);
  return file_call(CP32_APP_FSINFO,0,info,sizeof(*info));
}

int cp32_sync(void)
{
  if(!api || api->version<24)return io_error(CP32_ENOSYS);
  return file_call(CP32_APP_SYNC,0,0,0);
}
int cp32_fsync(int fd)
{
  if(!api || api->version<24)return io_error(CP32_ENOSYS);
  return file_call(CP32_APP_FSYNC,fd,0,0);
}

int cp32_time(unsigned *seconds)
{
  unsigned value;int r;
  if(!api || api->version<25)return io_error(CP32_ENOSYS);
  if(!seconds)return io_error(CP32_EFAULT);
  r=file_call(CP32_APP_TIME,0,&value,sizeof(value));if(!r)*seconds=value;return r;
}
int cp32_stime(unsigned seconds)
{
  if(!api || api->version<25)return io_error(CP32_ENOSYS);
  if(seconds>0x7fffffffU)return io_error(CP32_EINVAL);
  return file_call(CP32_APP_STIME,0,&seconds,sizeof(seconds));
}
int cp32_utime(const char *path,const struct cp32_utimbuf *times)
{
  struct cp32_app_utime_request q;unsigned i;
  if(!api || api->version<25)return io_error(CP32_ENOSYS);
  if(!path)return io_error(CP32_EFAULT);
  for(i=0;i<256 && path[i];i++)q.path[i]=path[i];
  if(i==256)return io_error(CP32_ENAMETOOLONG);
  q.path[i]=0;q.path[255]=0;q.current=times==0;
  q.atime=times ? times->actime:0;q.mtime=times ? times->modtime:0;
  if(q.atime>0x7fffffffU || q.mtime>0x7fffffffU)return io_error(CP32_EINVAL);
  return file_call(CP32_APP_UTIME,0,&q,sizeof(q));
}
