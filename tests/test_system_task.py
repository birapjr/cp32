#!/usr/bin/env python3
"""Execute the SYS loop and actual read-only handlers; other services stubbed."""
from pathlib import Path
import re
import subprocess
import tempfile
import sys
sys.dont_write_bytecode=True
from test_idle_handoff import extract_function
ROOT=Path(__file__).resolve().parents[1]
loop=extract_function(ROOT/'src/kernel/system.c','sys_task')
handlers=[extract_function(ROOT/'src/kernel/system.c',n) for n in ('do_getsp','do_times')]
client=extract_function(ROOT/'src/kernel/cp32-shell.c','cp32_shell_sys_query')
opcodes=sorted(set(re.findall(r'case (SYS_\w+)',loop)))
stubs=sorted(set(re.findall(r'r = (do_\w+)\(&m\)',loop))-{'do_getsp','do_times'})
PRELUDE=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define PUBLIC
#define TRUE 1
#define SHADOWING 0
#define NO_NUM 0
#define ANY 132
#define FS_PROC_NR 1
#define SYSTASK -2
#define OK 0
#define E_BAD_PROC -1
#define E_BAD_FCN -2
#define EIO -3
#define P_SLOT_FREE 1
#define isoksusern(n) ((n)>=0 && (n)<4)
typedef struct { int m_source,m_type,PROC1; char *STACK_PTR;
  long USER_TIME,SYSTEM_TIME,CHILD_UTIME,CHILD_STIME,BOOT_TICKS; } message;
struct proc { int p_flags; struct { uintptr_t sp; } p_reg;
  long user_time,sys_time,child_utime,child_stime; };
static struct proc proc[4];
#define proc_addr(n) (&proc[n])
static message m;
static int receives,sends,traces,mask=3;
static jmp_buf stopped;
static int lock_save(void) { int old=mask; mask=15; return old; }
static void restore_lock(int old) { mask=old; }
static long get_uptime(void) { return 1234; }
static void cp32_trace_sys_receive(void) { traces++; }
static void panic(const char *s,int n) { (void)s; (void)n; assert(!"unexpected panic"); }
'''
IO=r'''
static int receive(int source,message *req) {
  assert(source==ANY && mask==3);
  if (receives==250) longjmp(stopped,1);
  memset(req,0,sizeof(*req)); req->m_source=FS_PROC_NR;
  req->PROC1=FS_PROC_NR;
  switch (receives++%5) {
    case 0: req->m_type=SYS_GETSP; break;
    case 1: req->m_type=SYS_TIMES; break;
    case 2: req->m_type=SYS_GETSP; req->PROC1=2; break;
    case 3: req->m_type=SYS_TIMES; req->PROC1=4; break;
    case 4: req->m_type=9999; break;
  }
  return OK;
}
static int send(int dest,message *reply) {
  assert(dest==FS_PROC_NR && mask==3);
  switch (sends++%5) {
    case 0: assert(reply->m_type==OK && (uintptr_t)reply->STACK_PTR==0x3fcd7000); break;
    case 1:
      assert(reply->m_type==OK && reply->USER_TIME==12 && reply->SYSTEM_TIME==34);
      assert(reply->CHILD_UTIME==56 && reply->CHILD_STIME==78 && reply->BOOT_TICKS==1234); break;
    case 2: case 3: assert(reply->m_type==E_BAD_PROC); break;
    case 4: assert(reply->m_type==E_BAD_FCN); break;
  }
  return OK;
}
'''
TESTS=r'''
static int _sendrec(int endpoint,message *req) {
  assert(endpoint==SYSTASK && req->PROC1==FS_PROC_NR);
  req->m_type=req->m_type==SYS_GETSP ? do_getsp(req) : do_times(req);
  req->m_source=SYSTASK; return OK;
}
@CLIENT@
int main(void) {
  proc[FS_PROC_NR]=(struct proc){0,{0x3fcd7000},12,34,56,78};
  proc[2].p_flags=P_SLOT_FREE;
  if (!setjmp(stopped)) sys_task();
  assert(receives==250 && sends==250 && traces==250);
  message request={0};
  request.PROC1=2; assert(do_times(&request)==E_BAD_PROC);
  request.PROC1=-1; assert(do_getsp(&request)==E_BAD_PROC);
  request.PROC1=FS_PROC_NR; mask=7; assert(do_times(&request)==OK && mask==7);
  uint32_t ticks=0; assert(cp32_shell_sys_query(&ticks)==OK && ticks==1234);
  puts("SYS: receive/dispatch/reply, saved SP, uptime/accounting, invalid targets and client queries passed");
  return 0;
}
'''
defines='\n'.join('#define '+name+' '+str(i+100) for i,name in enumerate(opcodes))+'\n'
fakes='\n'.join('static int '+name+'(message *p) { (void)p; return E_BAD_FCN; }' for name in stubs if name!='do_fresh')
source=PRELUDE+defines+fakes+'\n'+IO+'\n'+'\n'.join(handlers)+'\n'+loop+TESTS.replace('@CLIENT@',client)
with tempfile.TemporaryDirectory(prefix='cp32-sys-task-') as folder:
 p=Path(folder); (p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
