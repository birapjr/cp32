#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "exec.h"
static uint32_t word(const unsigned char *p) {
  return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int main(void) {
  unsigned char b[4096], before[4096]; uint32_t sp=123;
  const char *a[]={"hello","one two",""}, *e[]={"PATH=/boot","TERM=cp32"};
  memset(b,0xa5,sizeof(b));
  assert(!cp32_exec_stack(b,256,0x3fca0000,3,a,2,e,&sp));
  unsigned start=sp-0x3fca0000;
  assert(!(sp&15) && word(b+start)==3);
  for(unsigned i=0;i<5;i++) {
    unsigned slot=i<3 ? i+1 : i+2;
    uint32_t ptr=word(b+start+slot*4);
    assert(ptr>=sp+32 && ptr<0x3fca0100);
    assert(!strcmp((char *)b+ptr-0x3fca0000,i<3?a[i]:e[i-3]));
  }
  assert(!word(b+start+16) && !word(b+start+28));
  for(unsigned i=0;i<start;i++) assert(b[i]==0xa5);
  for(unsigned i=256;i<sizeof(b);i++) assert(b[i]==0xa5);
  memcpy(before,b,sizeof(b)); sp=123;
#define FAIL(cap,base,ac,av,ec,ev) do { assert(cp32_exec_stack(b,cap,base,ac,av,ec,ev,&sp)<0); assert(sp==123 && !memcmp(b,before,sizeof(b))); } while(0)
  FAIL(16,0,3,a,2,e); FAIL(255,0,3,a,2,e); FAIL(256,1,3,a,2,e);
  FAIL(256,0xffffff00U,3,a,2,e); FAIL(256,0,33,a,0,0);
  FAIL(256,0,1,0,0,0); FAIL(0,0,0,0,0,0);
  const char *bad[]={0}; FAIL(256,0,1,bad,0,0);
  char huge[4096]; memset(huge,'x',sizeof(huge));
  const char *longarg[]={huge}; FAIL(4096,0,1,longarg,0,0);
  assert(!cp32_exec_stack(b,16,0x1000,0,0,0,0,&sp));
  assert(sp==0x1000 && !word(b) && !word(b+4) && !word(b+8));
  const char *empty[]={""};
  assert(!cp32_exec_stack(b,32,0x1000,0,0,1,empty,&sp));
  assert(!word(b) && !word(b+4) && word(b+8)==0x1010 && !word(b+12));
  puts("exec stack: layout, relocation, alignment, bounds and atomic rejection passed");
}
