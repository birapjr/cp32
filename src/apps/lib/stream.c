/* MINIX stdio semantics over the bootstrap stdin service; caller-owned buffer. */
#include "stream.h"
void cp32_input_init(struct cp32_input *s)
{
  s->next=s->used=0;
  s->eof=s->error=0;s->fd=0;
}
void cp32_input_fd(struct cp32_input *s,int fd)
{
  cp32_input_init(s);s->fd=fd;
}
int cp32_getc(struct cp32_input *s)
{
  int n;
  if(s->eof || s->error) return CP32_EOF;
  if(s->next==s->used) {
    n=cp32_read(s->fd,s->buffer,sizeof(s->buffer));
    if(n<0) {s->error=1;return CP32_EOF;}
    if(!n) {s->eof=1;return CP32_EOF;}
    s->next=0;s->used=(unsigned)n;
  }
  return s->buffer[s->next++];
}
char *cp32_fgets(char *text,unsigned capacity,struct cp32_input *s)
{
  unsigned used=0;
  int c;
  if(!text || capacity<2 || capacity>0x7fffffffU) {
    cp32_errno=!text ? CP32_EFAULT : CP32_EINVAL;
    s->error=1;return 0;
  }
  text[0]=0;
  while(used<capacity-1) {
    c=cp32_getc(s);
    if(c==CP32_EOF) break;
    text[used++]=(char)c;text[used]=0;
    if(c=='\n') break;
  }
  return used && !s->error ? text : 0;
}
int cp32_feof(const struct cp32_input *s) {return s->eof;}
int cp32_ferror(const struct cp32_input *s) {return s->error;}
void cp32_clearerr(struct cp32_input *s) {s->eof=s->error=0;}
