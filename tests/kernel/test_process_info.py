#!/usr/bin/env python3
"""Exercise production process snapshots and ps formatting with modeled IRQ lock."""
from pathlib import Path
import subprocess, sys, tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CP32_IRAM_EXT
#define NR_TASKS 2
#define NR_PROCS 5
#define TTY_PROC_NR -2
#define MEM -3
#define CLOCK -4
#define SYSTASK -5
#define MM_PROC_NR 0
#define FS_PROC_NR 1
#define IDLE -6
#define HARDWARE -7
#define P_SLOT_FREE 1
#define NO_MAP 2
#define SENDING 4
#define RECEIVING 8
#define P_STOP 64
#define EINVAL -22
struct proc {int p_pid,p_flags;unsigned user_time,sys_time,child_utime,child_stime;char p_name[16];};
struct cp32_proc_info {int endpoint,pid,flags,running;unsigned user_ticks,system_ticks,child_user_ticks,child_system_ticks;char name[16];};
static struct proc table[NR_TASKS+NR_PROCS],*current_proc;
#define proc_addr(n) (&table[(n)+NR_TASKS])
static int locked,locks;
static int lock_save(void){int old=locked;locked=1;locks++;return old;}
static void restore_lock(int old){assert(locked);locked=old;}
static char output[4096];
static void cp32_shell_print(const char *s){assert(!locked);assert(strlen(output)+strlen(s)<sizeof(output));strcat(output,s);}
static void cp32_shell_print_u32(unsigned n){char s[16];sprintf(s,"%u",n);cp32_shell_print(s);}
'''
test=r'''
int main(void){
 struct cp32_proc_info p;
 memset(&p,0x55,sizeof(p));
 assert(cp32_proc_info(-NR_TASKS-1,&p)==EINVAL && !locks);
 assert(cp32_proc_info(NR_PROCS,&p)==EINVAL && !locks);
 assert(cp32_proc_info(0,0)==EINVAL && !locks);
 for(int n=-NR_TASKS;n<NR_PROCS;n++)proc_addr(n)->p_flags=P_SLOT_FREE;
 assert(!cp32_proc_info(0,&p) && !locked && !p.name[0] && !p.pid);
 struct proc *q=proc_addr(-2);q->p_flags=RECEIVING;q->user_time=123;q->sys_time=45;
 memcpy(q->p_name,"clock\n",7);
 assert(cp32_proc_info(-2,&p)==1 && p.endpoint==-2 && p.user_ticks==123 && p.system_ticks==45);
 assert(!strcmp(p.name,"clock?") && !locked);
 q->user_time=456;assert(p.user_ticks==123); /* Owned copy, not table alias. */
 q=proc_addr(1);q->child_utime=7;q->child_stime=11;q->p_flags=0;q->p_pid=1;current_proc=q;
 memset(q->p_name,'x',16);
 locked=1;assert(cp32_proc_info(1,&p)==1 && locked && p.running);locked=0;
 assert(strlen(p.name)==15 && p.child_user_ticks==7 && p.child_system_ticks==11);
 proc_addr(2)->p_flags=NO_MAP;
 proc_addr(3)->p_flags=SENDING|RECEIVING;
 proc_addr(4)->p_flags=P_STOP;
 cp32_shell_ps();
 assert(strstr(output," children 7/11\r\n"));
 assert(strstr(output,"-2 0 RCV 456/45 clock?\r\n"));
 assert(strstr(output,"1 1 RUN 0/0 xxxxxxxxxxxxxxx\r\n"));
 assert(strstr(output,"2 0 MAP") && strstr(output,"3 0 SR") && strstr(output,"4 0 STP"));
 assert(!strstr(output,"-1 0") && !strstr(output,"\r\n0 0"));
 output[0]=0;proc_addr(2)->p_flags=SENDING;proc_addr(3)->p_flags=0;proc_addr(4)->p_flags=32;
 cp32_shell_ps();assert(strstr(output,"2 0 SND") && strstr(output,"3 0 RDY") && strstr(output,"4 0 BLK"));
 puts("process info: bounds, free slots, stable copies, name sanitization, lock restoration and ps states pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extract_function(root/'src/kernel/process.c','cp32_proc_info')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_ticks')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_ps')+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
