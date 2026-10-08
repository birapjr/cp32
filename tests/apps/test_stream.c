#include <assert.h>
#include <string.h>
#include "stream.h"
static const unsigned char *data;
static unsigned size, pos, calls;
static int fail;
static int input(char *p,unsigned n) {
  calls++;
  if(fail) return -1;
  if(n>size-pos)n=size-pos;
  memcpy(p,data+pos,n);pos+=n;return n;
}
static int output(const char *p,unsigned n) {(void)p;return n;}
static void reset(struct cp32_input *s,const unsigned char *p,unsigned n) {
  data=p;size=n;pos=calls=0;fail=0;cp32_input_init(s);
}
int main(void) {
  struct cp32_app_services api={5,output,0,0,0,0,input,0};
  struct cp32_input s;char line[8];unsigned char all[130];
  assert(!cp32_io_init(&api));
  for(unsigned i=0;i<sizeof(all);i++)all[i]=(unsigned char)(i+128);
  reset(&s,all,sizeof(all));
  for(unsigned i=0;i<sizeof(all);i++)assert(cp32_getc(&s)==all[i]);
  assert(calls==3 && !cp32_feof(&s));
  assert(cp32_getc(&s)==CP32_EOF && cp32_feof(&s) && !cp32_ferror(&s));
  assert(cp32_getc(&s)==CP32_EOF && calls==4);
  cp32_clearerr(&s);assert(!cp32_feof(&s));
  assert(cp32_getc(&s)==CP32_EOF && calls==5);
  reset(&s,(const unsigned char *)"abc\ndefghijk",12);
  assert(cp32_fgets(line,sizeof(line),&s)==line && !strcmp(line,"abc\n"));
  assert(cp32_fgets(line,sizeof(line),&s)==line && !strcmp(line,"defghij"));
  assert(cp32_fgets(line,sizeof(line),&s)==line && !strcmp(line,"k"));
  assert(cp32_feof(&s) && !cp32_fgets(line,sizeof(line),&s));
  reset(&s,all,1);fail=1;
  assert(cp32_getc(&s)==CP32_EOF && cp32_ferror(&s) && !cp32_feof(&s));
  assert(cp32_errno==CP32_EIO);
  fail=0;assert(cp32_getc(&s)==CP32_EOF && calls==1);
  cp32_clearerr(&s);assert(cp32_getc(&s)==128);
  assert(!cp32_fgets(line,1,&s) && cp32_errno==CP32_EINVAL);
  reset(&s,(const unsigned char *)"ab",2);
  assert(cp32_getc(&s)=='a');fail=1;
  assert(!cp32_fgets(line,sizeof(line),&s) && !strcmp(line,"b"));
  assert(cp32_ferror(&s) && !cp32_feof(&s));
  return 0;
}
