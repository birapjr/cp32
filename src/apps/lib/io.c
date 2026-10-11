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
  if(fd!=1 && fd!=2) return io_error(CP32_EBADF);
  if(count>0x7fffffffU) return io_error(CP32_EINVAL);
  if(!buffer && count) return io_error(CP32_EFAULT);
  while(done<count) {
    unsigned chunk=count-done;
    int n;
    if(chunk>256) chunk=256;
    n=api->write((const char *)buffer+done,chunk);
    if(n<0 || (unsigned)n>chunk) {
      io_error(CP32_EIO);return done ? (int)done : -1;
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
