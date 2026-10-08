/* MINIX fflush-style completion/error reporting over bounded IPC writes. */
#include "output.h"
int cp32_output_init(struct cp32_output *s,int fd)
{
  s->used=0;s->fd=fd;s->error=0;
  if(fd!=1 && fd!=2) {
    cp32_errno=CP32_EBADF;s->error=1;return -1;
  }
  return 0;
}
int cp32_flush(struct cp32_output *s)
{
  int n;
  unsigned i;
  if(s->error) return -1;
  if(!s->used) return 0;
  n=cp32_write(s->fd,s->buffer,s->used);
  if(n>0) {
    s->used-=(unsigned)n;
    for(i=0;i<s->used;i++)s->buffer[i]=s->buffer[i+(unsigned)n];
  }
  if(n<0 || s->used) {
    if(n>=0)cp32_errno=CP32_EIO;
    s->error=1;return -1;
  }
  return 0;
}
int cp32_putc(int c,struct cp32_output *s)
{
  if(s->error) return -1;
  /* Flush before accepting another byte, so failure never consumes c. */
  if(s->used==sizeof(s->buffer) && cp32_flush(s))return -1;
  s->buffer[s->used++]=(unsigned char)c;
  return (unsigned char)c;
}
int cp32_output_error(const struct cp32_output *s) {return s->error;}
void cp32_output_clearerr(struct cp32_output *s) {s->error=0;}
