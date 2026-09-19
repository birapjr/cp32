#!/usr/bin/env python3
"""Run production DEV_WRITE validation, console copy and output processing."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode=True
from test_idle_handoff import extract_function
ROOT=Path(__file__).resolve().parents[1]
bodies=[extract_function(ROOT/'src/kernel/tty.c',n) for n in
        ['out_process','cp32_console_write','do_write']]
PRELUDE=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define PUBLIC
#define TAB_SIZE 8
#define TAB_MASK 7
enum {OK=0,EIO=-1,EFAULT=-2,EINVAL=-3,EAGAIN=-4,SUSPEND=-998,
      O_NONBLOCK=1,OPOST=2,ONLCR=4,XTABS=8,TASK_REPLY=100,REVIVE=101};
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
typedef struct { int m_source,COUNT,PROC_NR,TTY_FLAGS; char *ADDRESS; } message;
typedef struct { int tty_outleft,tty_outcum,tty_inhibited,tty_position;
  int tty_outrepcode,tty_outcaller,tty_outproc; vir_bytes tty_out_vir;
  struct { unsigned c_oflag; } tty_termios; } tty_t;
static char output[2048];
static int used,depth,replies,status,fault,copies,maxcopy;
static const char *allowed;
static unsigned allowed_size;
static phys_bytes numap(int endpoint,vir_bytes address,unsigned n) {
  if(endpoint!=1 || address<(uintptr_t)allowed || address-(uintptr_t)allowed>allowed_size ||
     n>allowed_size-(address-(uintptr_t)allowed)) return 0;
  return address;
}
#define vir2phys(p) ((phys_bytes)(p))
static void phys_copy(phys_bytes a,phys_bytes b,unsigned n) {
  assert(n<=64); copies++; if((int)n>maxcopy) maxcopy=n;
  memcpy((void*)b,(void*)a,n);
}
static void cardputer_display_begin_batch(void) { depth++; }
static void cardputer_display_end_batch(void) { assert(depth>0); depth--; }
static unsigned cardputer_display_faulted(void) { return fault; }
static void cardputer_display_putc(char c) { assert(depth>0); output[used++]=c; }
static void tty_reply(int type,int dest,int process,int result) {
  assert(type==TASK_REPLY && dest==1 && process==1 && depth==0);
  replies++; status=result;
}
static void handle_events(tty_t *tp);
'''
TESTS=r'''
static void handle_events(tty_t *tp) { cp32_console_write(tp); }
static void reset(void) { used=depth=replies=status=fault=copies=maxcopy=0; memset(output,0,sizeof(output)); }
static void write_text(tty_t *tp,char *text,int count) {
  allowed=text; allowed_size=count>0?(unsigned)count:0;
  message request={1,count,1,0,text}; do_write(tp,&request);
}
int main(void) {
  tty_t tty={0}; char large[150]; memset(large,'x',sizeof(large));
  reset(); write_text(&tty,large,sizeof(large));
  assert(replies==1 && status==150 && used==150 && copies==3 && maxcopy==64);
  assert(!memcmp(output,large,sizeof(large)) && !tty.tty_outleft && !tty.tty_outcum);
  reset(); tty.tty_termios.c_oflag=OPOST|ONLCR|XTABS; tty.tty_position=0;
  char formatted[]="A\tB\n"; write_text(&tty,formatted,4);
  assert(replies==1 && status==4 && used==11 && !memcmp(output,"A       B\r\n",11));
  reset(); write_text(&tty,large,0); assert(status==EINVAL && !copies);
  reset(); write_text(&tty,large,-1); assert(status==EINVAL && !copies);
  reset(); allowed=large; allowed_size=2;
  message request={1,3,1,0,large}; do_write(&tty,&request);
  assert(status==EFAULT && !copies && !used);
  reset(); tty.tty_outleft=1; write_text(&tty,large,3);
  assert(status==EIO && !copies); tty.tty_outleft=0;
  reset(); fault=1; write_text(&tty,large,3);
  assert(status==EIO && !copies && !tty.tty_outleft && !tty.tty_outcum);
  reset(); tty.tty_inhibited=1; allowed=large; allowed_size=3;
  request=(message){1,3,1,O_NONBLOCK,large}; do_write(&tty,&request);
  assert(status==EAGAIN && !used && !tty.tty_outleft);
  reset(); request.TTY_FLAGS=0; do_write(&tty,&request);
  assert(status==SUSPEND && tty.tty_outleft==3 && tty.tty_outrepcode==REVIVE);
  puts("TTY DEV_WRITE: chunked copy, byte-count reply, termios, busy/invalid/fault/flow-control checks passed");
  return 0;
}
'''
prototypes='\n'.join(b[:b.index('{')].rstrip()+';' for b in bodies)
with tempfile.TemporaryDirectory(prefix='cp32-tty-write-') as folder:
 p=Path(folder); (p/'test.c').write_text(PRELUDE+prototypes+'\n'+'\n'.join(bodies)+TESTS)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
