#include <assert.h>
#include <string.h>
#include "output.h"
static unsigned char written[512];
static unsigned used,calls,limit=512;
static int fail,zero;
static int input(char *p,unsigned n) {(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n) {
  calls++;
  if(fail && used>=limit)return -1;
  if(zero)return 0;
  if(n>3)n=3; /* force repeated short writes */
  if(fail && n>limit-used)n=limit-used;
  assert(used+n<=sizeof(written));
  memcpy(written+used,p,n);used+=n;return n;
}
int main(void) {
  struct cp32_app_services api={5,output,0,0,0,0,input,0};
  struct cp32_output s;
  assert(!cp32_io_init(&api));
  assert(!cp32_output_init(&s,1));
  assert(!cp32_flush(&s) && calls==0);
  for(unsigned i=0;i<64;i++)assert(cp32_putc(i+128,&s)==(int)i+128);
  assert(used==0 && s.used==64);
  assert(cp32_putc(0,&s)==0 && used==64 && s.used==1);
  assert(!cp32_flush(&s) && used==65);
  for(unsigned i=0;i<64;i++)assert(written[i]==i+128);
  assert(written[64]==0);
  unsigned oldcalls=calls;assert(!cp32_flush(&s) && calls==oldcalls);
  used=calls=0;fail=1;limit=5;
  for(unsigned i=0;i<12;i++)assert(cp32_putc(i,&s)==(int)i);
  assert(cp32_flush(&s)==-1 && used==5 && s.used==7);
  assert(cp32_output_error(&s) && cp32_errno==CP32_EIO);
  oldcalls=calls;
  assert(cp32_putc(99,&s)==-1 && cp32_flush(&s)==-1 && calls==oldcalls);
  fail=0;cp32_output_clearerr(&s);
  assert(!cp32_output_error(&s) && !cp32_flush(&s));
  assert(used==12);for(unsigned i=0;i<12;i++)assert(written[i]==i);
  assert(!cp32_output_init(&s,2));zero=1;
  assert(cp32_putc('x',&s)=='x' && cp32_flush(&s)==-1 && s.used==1);
  assert(cp32_errno==CP32_EIO);
  zero=0;cp32_output_clearerr(&s);assert(!cp32_flush(&s));
  assert(written[12]=='x');
  assert(cp32_output_init(&s,0)==-1 && cp32_errno==CP32_EBADF);
  assert(cp32_putc('x',&s)==-1);
  assert(!cp32_output_init(&s,1));
  for(unsigned i=0;i<64;i++)assert(cp32_putc('a',&s)=='a');
  zero=1;assert(cp32_putc('b',&s)==-1 && s.used==64);
  zero=0;cp32_output_clearerr(&s);
  assert(cp32_putc('b',&s)=='b' && s.used==1 && !cp32_flush(&s));
  assert(written[used-1]=='b');
  return 0;
}
