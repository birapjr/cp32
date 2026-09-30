/* MINIX read/write expose byte counts, including short transfers. Adapt the
 * bootstrap's bounded callbacks to standard stream numbers, not FS handles. */
#include "io.h"
static const struct cp32_app_services *api;
int cp32_io_init(const struct cp32_app_services *services)
{
  if(api || !services || services->version<5 || !services->read || !services->write)
    return -1;
  api=services;
  return 0;
}
int cp32_read(int fd,void *buffer,unsigned count)
{
  int n;
  if(!api || fd!=0 || count>0x7fffffffU || (!buffer && count)) return -1;
  if(!count) return 0;
  if(count>64) count=64;
  n=api->read(buffer,count);
  return n<0 || (unsigned)n>count ? -1 : n;
}
int cp32_write(int fd,const void *buffer,unsigned count)
{
  unsigned done=0;
  if(!api || (fd!=1 && fd!=2) || count>0x7fffffffU || (!buffer && count)) return -1;
  while(done<count) {
    unsigned chunk=count-done;
    int n;
    if(chunk>256) chunk=256;
    n=api->write((const char *)buffer+done,chunk);
    if(n<0 || (unsigned)n>chunk) return done ? (int)done : -1;
    if(!n) break;
    done+=(unsigned)n;
  }
  return (int)done;
}

int cp32_readline(char *buffer,unsigned capacity)
{
  unsigned used=0;
  if(!buffer || capacity<2 || capacity>0x7fffffffU) return -1;
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
