#!/usr/bin/env python3
"""Execute production input transfer against translated, guarded destinations."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from test_idle_handoff import extract_function
body = extract_function(ROOT / 'src/kernel/tty.c', 'in_transfer')
prelude = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define buflen(a) (sizeof(a)/sizeof((a)[0]))
#define bufend(a) ((a)+buflen(a))
#define vir2phys(a) ((uintptr_t)(a))
enum {IN_CHAR=255,IN_EOT=256,IN_EOF=512,ICANON=1,EFAULT=-14};
typedef uintptr_t phys_bytes;
typedef struct {
 int tty_inleft,tty_eotct,tty_min,tty_incount,tty_incum;
 int tty_inproc,tty_inrepcode,tty_incaller;
 uintptr_t tty_in_vir;
 unsigned short tty_inbuf[256], *tty_intail;
 struct {int c_lflag;} tty_termios;
} tty_t;
static unsigned char memory[260];
static int capacity, copies, replies, status;
static phys_bytes numap(int proc,uintptr_t address,unsigned count) {
 assert(proc==1);
 if(address<0x1000 || address-0x1000>(unsigned)capacity ||
    count>(unsigned)capacity-(address-0x1000)) return 0;
 return (uintptr_t)(memory+2+(address-0x1000));
}
static void phys_copy(phys_bytes src,phys_bytes dst,unsigned count) {
 assert(count<=64 && dst>=(uintptr_t)(memory+2));
 assert(dst+count<=(uintptr_t)(memory+2+capacity));
 copies++; memcpy((void*)dst,(void*)src,count);
}
static void tty_reply(int type,int caller,int proc,int result) {
 assert(type==100 && caller==2 && proc==1); replies++; status=result;
}
'''
tests = r'''
static tty_t setup(int requested,int available,int canonical,int wrap) {
 tty_t t={0}; memset(memory,0xA5,sizeof(memory));
 capacity=requested; copies=replies=status=0;
 t.tty_inleft=requested; t.tty_incount=available; t.tty_min=1;
 t.tty_inproc=1; t.tty_incaller=2; t.tty_inrepcode=100; t.tty_in_vir=0x1000;
 t.tty_termios.c_lflag=canonical?ICANON:0;
 for(int i=0;i<available;i++) {
  unsigned short ch=(unsigned char)('a'+i%26);
  if(!canonical || i==available-1) {ch|=IN_EOT; t.tty_eotct++;}
  t.tty_inbuf[(wrap+i)%256]=ch;
 }
 return t;
}
static void guards(int n) {
 assert(memory[0]==0xA5 && memory[1]==0xA5);
 for(unsigned i=n+2;i<sizeof(memory);i++) assert(memory[i]==0xA5);
}
int main(void) {
 int lengths[]={1,63,64,65,150};
 for(unsigned j=0;j<buflen(lengths);j++) {
  int n=lengths[j]; tty_t t=setup(n,n,0,240); t.tty_intail=t.tty_inbuf+240;
  in_transfer(&t);
  assert(replies==1 && status==n && copies==(n+63)/64);
  assert(t.tty_in_vir==0x1000+(unsigned)n && !t.tty_incount && !t.tty_eotct);
  assert(!t.tty_inleft && !t.tty_incum);
  for(int i=0;i<n;i++) assert(memory[i+2]=='a'+i%26);
  guards(n);
 }
 tty_t t=setup(12,4,1,0); t.tty_intail=t.tty_inbuf;
 in_transfer(&t); assert(replies==1 && status==4 && t.tty_in_vir==0x1004); guards(4);
 t=setup(12,4,1,0); t.tty_intail=t.tty_inbuf; t.tty_eotct=0;
 in_transfer(&t); assert(!copies && !replies && t.tty_incount==4); guards(0);
 t=setup(12,1,1,0); t.tty_intail=t.tty_inbuf; t.tty_inbuf[0]=IN_EOT|IN_EOF;
 in_transfer(&t); assert(replies==1 && status==0 && !copies && !t.tty_incount); guards(0);
 t=setup(12,4,0,0); t.tty_intail=t.tty_inbuf;
 in_transfer(&t); assert(!replies && t.tty_incum==4 && t.tty_inleft==8); guards(4);
 t.tty_incount=t.tty_eotct=8;
 for(int i=4;i<12;i++) t.tty_inbuf[i]=('a'+i)|IN_EOT;
 in_transfer(&t); assert(replies==1 && status==12 && t.tty_in_vir==0x100C); guards(12);
 t=setup(4,4,0,0); t.tty_intail=t.tty_inbuf; capacity=3;
 in_transfer(&t); assert(replies==1 && status==EFAULT && !copies && t.tty_incount==4);
 assert(t.tty_intail==t.tty_inbuf && !t.tty_inleft && !t.tty_incum); guards(0);
 puts("TTY input transfer: mapped copies, bounds, chunking, wrap, canonical/EOF, partial and fault cases passed");
}
'''
with tempfile.TemporaryDirectory(prefix='cp32-tty-read-') as folder:
    p = Path(folder)
    (p/'test.c').write_text(prelude + body + tests)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
