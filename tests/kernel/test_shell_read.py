#!/usr/bin/env python3
"""Execute the production shell's two-stage MINIX device-read adapter."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from test_idle_handoff import extract_function

body = extract_function(ROOT / 'src/kernel/cp32-shell.c', 'cp32_shell_tty_io')
body += '\n' + extract_function(ROOT / 'src/kernel/cp32-shell.c', 'cp32_shell_flush')
body += '\n' + extract_function(ROOT / 'src/kernel/cp32-shell.c', 'cp32_shell_display')
prelude = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define CP32_IRAM_EXT
enum { OK=0, EIO=-5, EFAULT=-14, SUSPEND=-998, DEV_READ=3, DEV_WRITE=4,
       TTY_PROC_NR=-9, FS_PROC_NR=1, TASK_REPLY=68, REVIVE=67 };
typedef struct {
  int m_source, m_type, TTY_LINE, PROC_NR, COUNT, REP_PROC_NR, REP_STATUS;
  char *ADDRESS;
} message;
static message first, second;
static int send_result, receive_result, receives;
static char buffer[64];
static int operation;
static char cp32_shell_output[256], written[1024];
static unsigned cp32_shell_output_len;
static int batch_mode, writes, written_count;
static void panic(const char *reason,int result) {
  (void)reason; (void)result; assert(!"unexpected shell panic");
}
static int _sendrec(int dest, message *m) {
  if (batch_mode) {
    assert(dest==TTY_PROC_NR && m->m_type==DEV_WRITE && m->COUNT<=256);
    assert(m->ADDRESS==cp32_shell_output && m->PROC_NR==FS_PROC_NR);
    memcpy(written+written_count,m->ADDRESS,m->COUNT);
    written_count+=m->COUNT; writes++;
    m->m_source=TTY_PROC_NR; m->m_type=TASK_REPLY;
    m->REP_PROC_NR=FS_PROC_NR; m->REP_STATUS=m->COUNT;
    return OK;
  }
  assert(dest==TTY_PROC_NR && m->m_type==operation && m->TTY_LINE==0);
  assert(m->PROC_NR==FS_PROC_NR && m->ADDRESS==buffer && m->COUNT==64);
  *m=first; return send_result;
}
static int _receive(int source, message *m) {
  assert(source==TTY_PROC_NR); receives++; *m=second; return receive_result;
}
'''
tests = r'''
#define cp32_shell_read(buffer,count) cp32_shell_tty_io(operation,buffer,count)
static void reset(void) {
  first=(message){.m_source=TTY_PROC_NR,.m_type=TASK_REPLY,
                  .REP_PROC_NR=FS_PROC_NR,.REP_STATUS=3};
  second=(message){.m_source=TTY_PROC_NR,.m_type=REVIVE,
                  .REP_PROC_NR=FS_PROC_NR,.REP_STATUS=3};
  send_result=receive_result=receives=0;
}
int main(void) {
 for(operation=DEV_READ;operation<=DEV_WRITE;operation++) {
  reset(); assert(cp32_shell_read(buffer,64)==3 && receives==0);
  reset(); first.REP_STATUS=SUSPEND;
  assert(cp32_shell_read(buffer,64)==3 && receives==1);
  reset(); send_result=EFAULT; assert(cp32_shell_read(buffer,64)==EFAULT);
  reset(); first.REP_STATUS=SUSPEND; receive_result=EFAULT;
  assert(cp32_shell_read(buffer,64)==EFAULT);
  for(int stage=0;stage<2;stage++) {
    for(int field=0;field<5;field++) {
      reset(); if(stage) first.REP_STATUS=SUSPEND;
      message *m=stage ? &second : &first;
      if(field==0) m->m_source=0;
      if(field==1) m->m_type=stage ? TASK_REPLY : REVIVE;
      if(field==2) m->REP_PROC_NR=0;
      if(field==3) m->REP_STATUS=65;
      if(field==4) {
        if(!stage) continue;
        m->REP_STATUS=SUSPEND;
      }
      assert(cp32_shell_read(buffer,64)==EIO);
    }
    reset(); if(stage) first.REP_STATUS=SUSPEND;
    (stage ? &second : &first)->REP_STATUS=0;
    assert(cp32_shell_read(buffer,64)==0);
    reset(); if(stage) first.REP_STATUS=SUSPEND;
    (stage ? &second : &first)->REP_STATUS=EFAULT;
    assert(cp32_shell_read(buffer,64)==EFAULT);
  }
 }
  batch_mode=1;
  cp32_shell_display("abc"); assert(!writes && cp32_shell_output_len==3);
  char large[601]; memset(large,'z',600); large[600]=0;
  cp32_shell_display(large);
  assert(writes==2 && written_count==512 && cp32_shell_output_len==91);
  cp32_shell_flush(); assert(writes==3 && written_count==603 && !cp32_shell_output_len);
  assert(!memcmp(written,"abc",3));
  for(int i=3;i<603;i++) assert(written[i]=='z');
  cp32_shell_flush(); assert(writes==3);
  puts("shell I/O: immediate/suspended completion, reply validation, errors and bounded command batching passed");
}
'''
with tempfile.TemporaryDirectory(prefix='cp32-shell-read-') as folder:
    p = Path(folder)
    (p / 'test.c').write_text(prelude + body + tests)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
