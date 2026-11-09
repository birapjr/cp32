#!/usr/bin/env python3
"""Production launch/wait/reap logic with modeled hardware and IPC boundaries."""
from pathlib import Path
import sys, tempfile, subprocess
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
source=extract_function(root/'src/kernel/application.c','cp32_application_run')
source=source.replace('__asm__ volatile("memw\\n\\tisync" ::: "memory");','syncs++;')
source=source.replace('memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);','memset(stack,0,sizeof(stack));')
source=source.replace('memset((void *)old,0,heap_break-old);','memset(heap+old-CP32_APP_DATA,0,heap_break-old);')
pre=r'''
static struct {unsigned signal,cause,pc;} app_fault;
static unsigned application_fault_take(int pid){(void)pid;return 0;}
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "image.h"
#define NR_PROCS 5
#define NR_TASKS 0
#define NQ 3
#define NO_MAP 2
#define ANY -999
#define S 2
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_FILE 2007
#define CP32_APP_EXEC 10
#define CP32_APP_YIELD 45
#define CP32_APP_KILL 46
#define CP32_APP_SIGMASK 47
#define CP32_APP_SIGPENDING 48
#define CP32_APP_SIGNAL 49
#define CP32_SIGTERM 15
#define CP32_WAIT_LIVE (-1000)
#define CP32_APP_FORK 40
#define CP32_APP_WAITPID 41
#define CP32_APP_TIMES 44
#define CP32_APP_GETPGRP 42
#define CP32_APP_SETSID 43
#define APP_NR 3
#define LOW_USER 2
#define FS_PROC_NR 1
#define MM_PROC_NR 0
#define APP_WRITE 2001
#define APP_EXIT 2002
#define APP_TEXT_DATA (CP32_APP_TEXT-0x6f0000U)
#define P_SLOT_FREE 1
#define RECEIVING 4
#define D 1
#define CLICK_SHIFT 8
#define OK 0
#define EINVAL -22
#define NO_NUM 0
#define NIL_PROC ((struct proc *)0)
typedef uintptr_t reg_t;
typedef void (*cp32_app_output)(const char *,unsigned);
typedef struct {int m_type,m1_i1,m1_i2,m1_i3; char *m1_p1;} message;
struct proc {
 int p_nr,p_pid,p_flags,p_getfrom;
 unsigned user_time,sys_time,child_utime,child_stime;
 int p_alarm,p_pending,p_pendcount,p_blocked_frame_result;
 void *p_stguard;unsigned p_shadow;message *p_messbuf;
 struct {reg_t pc,psw,sp,a[16];} p_reg;
 struct {uint32_t mem_vir,mem_phys,mem_len;} p_map[3];
 struct proc *p_callerq,*p_sendlink,*p_nextready,*p_nextheld;
 int p_int_held,p_int_blocked;
 char p_name[16];
};
static struct proc procs[5], *current_proc=procs+1, *proc_ptr=procs+1, *bill_ptr;
static struct proc *rdy_head[NQ], *held_head;
static volatile struct proc *cp32_irq_return_proc,*cp32_blocked_return_proc,*cp32_irq_saved_owner;
#define proc_addr(n) (procs+(n))
static const char *args[]={"hello","one","two"};
static int requested_status;
static int app_endpoint=-1;
static int app_busy, services, syncs, publishes, reads, writes, replies, loading, stacking;
static unsigned char stack[4096],heap[12288];
static void panic(const char *s,int n) { (void)s;(void)n; assert(0); }
static int lock_save(void) { return 3; }
static void restore_lock(int n) { assert(n==3); }
static void lock_ready(struct proc *p) {
 assert(p==procs+app_endpoint && !p->p_flags && p->p_reg.pc==CP32_APP_TEXT);
 assert(p->p_reg.sp==CP32_APP_TOP-32 && p->p_reg.a[1]==p->p_reg.sp);
 assert(p->p_reg.a[15]==p->p_reg.sp && p->p_reg.a[2]==(reg_t)&services);
 assert(p->p_reg.psw==0x100 && p->p_map[D].mem_vir==CP32_APP_DATA);
 for(unsigned i=0;i<sizeof(stack);i++) assert(!stack[i]);
 assert(!strcmp(p->p_name,strrchr(args[0],'/') ? strrchr(args[0],'/')+1 : args[0]));
 assert(p->p_pid==103+publishes);
 publishes++; bill_ptr=p;
}
int cp32_image_load(cp32_image_reader r,void *c,uint32_t sz,unsigned char *t,
 unsigned char *d,struct cp32_image *out) {
 (void)r;(void)c;(void)sz; assert((uintptr_t)t==APP_TEXT_DATA && (uintptr_t)d==CP32_APP_DATA);
 if(loading) return -2; memset(heap,0xa5,sizeof(heap)); out->entry=CP32_APP_TEXT;out->data.memsz=128; return 0;
}
int cp32_exec_stack(unsigned char *b,unsigned cap,uint32_t base,unsigned argc,
 const char *const argv[],unsigned envc,const char *const envp[],uint32_t *sp) {
 assert(!strcmp(envp[0],"HOME=/") && !strcmp(envp[1],"PATH=/boot") && !strcmp(envp[2],"USER=root"));
 (void)b; assert(cap==512 && base==CP32_APP_TOP-512 && argc==3 && envc==3);
 assert(!strcmp(argv[0],args[0]) && !strcmp(argv[1],"one") && !strcmp(argv[2],"two")); if(stacking) return -1; *sp=CP32_APP_TOP-32;return 0;
}
static int resets,fs_pid,fs_fail;
static int cp32_shell_app_files_start(unsigned pid) {assert(!fs_pid);if(fs_fail)return -1;fs_pid=pid;return 0;}
static int cp32_shell_app_files_owner(unsigned pid) {return pid==(unsigned)fs_pid;}
static void cp32_shell_app_files_reset(void) {resets++;fs_pid=0;}
static int reserve_wait_pid;
static int cp32_fork_pid_used(int pid){return reserve_wait_pid && pid==101;}
static int cp32_process_group(int p,int c){(void)p;(void)c;return 1;}
static int cp32_fork_signal_target(struct proc *p,int pid,unsigned sig){(void)p;(void)pid;(void)sig;return -3;}
static int cp32_fork_sigmask(int how,unsigned mask){(void)how;(void)mask;return 0;}
static unsigned cp32_fork_sigpending(void){return 0;}
static int cp32_fork_disposition(int sig,unsigned action){(void)sig;(void)action;return 0;}
static int cp32_fork_live(void){return 0;}
static void cp32_fork_request(message *m){memset(m,0,sizeof(*m));}
static int cp32_fork_handoff(struct proc **p,int *pp,uint32_t *f,uint32_t *b,const message *m){(void)p;(void)pp;(void)f;(void)b;(void)m;return 0;}
static int cp32_fork_active(void){return 0;}
static void cp32_fork_reset(void){}
static int cp32_fork_start(struct proc **p,int pid,int *pp,uint32_t *f,uint32_t *b){(void)p;(void)pid;(void)pp;(void)f;(void)b;return -11;}
static int cp32_fork_finish(struct proc **p,int st,int *pp,uint32_t *f,uint32_t *b,unsigned sig){(void)sig;(void)p;(void)st;(void)pp;(void)f;(void)b;assert(0);return -1;}
static int cp32_fork_wait(int pid,void *p,unsigned opts){(void)pid;(void)p;(void)opts;return -10;}
static int application_times(struct proc *p,message *m){(void)p;(void)m;return -22;}
static int application_replace(struct proc *p,message *m,uint32_t *f,uint32_t *b) {(void)p;(void)m;(void)f;(void)b;return -8;}
static int application_file_request(message *m) {(void)m;return -22;}
static int receive(int nr,message *m) {
 assert(nr==app_endpoint); struct proc *p=procs+nr; p->p_flags=RECEIVING;p->p_getfrom=1;
 switch(reads++%10) {
 case 0: m->m_type=APP_WRITE;m->m1_p1=(char *)CP32_APP_DATA;m->m1_i1=30;break;
 case 1: m->m_type=APP_WRITE;m->m1_p1=(char *)(CP32_APP_TOP-1);m->m1_i1=2;break;
 case 2: m->m_type=999;break;
 case 3: m->m_type=APP_GETPID;break;
 case 4: m->m_type=APP_GETPPID;break;
 case 5: m->m_type=APP_SBRK;m->m1_i1=32;break;
 case 6: m->m_type=APP_SBRK;m->m1_i1=0x7fffffff;break;
 case 7: m->m_type=APP_READ;m->m1_p1=(char *)CP32_APP_DATA;m->m1_i1=64;break;
 case 8: m->m_type=APP_READ;m->m1_p1=(char *)(CP32_APP_TOP-1);m->m1_i1=64;break;
 default:p->user_time=2;p->sys_time=3;p->child_utime=5;p->child_stime=7;m->m_type=APP_EXIT;m->m1_i1=requested_status;break;
 } return 0;
}
static int send(int nr,message *m) {
 assert(nr==app_endpoint);
 if(replies%9==5) {
   assert((uintptr_t)m->m1_p1==CP32_APP_DATA+128);
   for(unsigned i=0;i<sizeof(heap);i++)
     assert(heap[i]==(i>=128 && i<160 ? 0 : 0xa5));
 }
 assert(m->m_type==(replies%9==0 ? 30 : replies%9==3 ? procs[app_endpoint].p_pid : replies%9==4 ? 1 : replies%9==5 ? 0 : replies%9==7 ? 5 : -1));replies++;return 0;
}
static int cp32_shell_app_input(char *p,unsigned n) {assert((uintptr_t)p==CP32_APP_DATA && n==64);return 5;}
static void output(const char *p,unsigned n) {assert((uintptr_t)p==CP32_APP_DATA && n==30);writes++;}
'''
test=r'''
int main(void) {
 procs[0].p_pid=100; /* Occupied identity must be skipped. */
 procs[1].p_pid=1;
 int status=-10;
 for(int n=3;n<NR_PROCS;n++){procs[n].p_nr=n;procs[n].p_flags=P_SLOT_FREE;}
 loading=1;assert(cp32_application_run(0,0,0,3,args,output,&status)==-2);
 assert(!app_busy && !publishes && status==-10 && procs[3].p_flags==P_SLOT_FREE);
 loading=0;stacking=1;assert(cp32_application_run(0,0,0,3,args,output,&status)==-1);
 assert(!app_busy && !publishes);stacking=0;
 for(int i=0;i<20;i++) {
   procs[3].p_flags=i%2 ? NO_MAP:P_SLOT_FREE;
   procs[3].p_pid=i%2 ? 90:0;
   requested_status=(int[]){0,7,255,256,65535,-1,-256}[i%7];
   args[0]=i%2 ? "/boot/echo":"hello";
   memset(stack,0xa5,sizeof(stack));
   assert(!cp32_application_run(0,0,0,3,args,output,&status));assert(status==(int)((unsigned)requested_status & 255U));
   assert(procs[i%2 ? 4:3].p_flags==P_SLOT_FREE && !app_busy);
   assert(bill_ptr==procs+1 && !procs[i%2 ? 4:3].p_reg.pc);
 }
 assert(procs[1].child_utime==140 && procs[1].child_stime==200);
 assert(publishes==20 && syncs==20 && writes==20 && replies==180 && resets==40);
 current_proc=procs+2;assert(cp32_application_run(0,0,0,3,args,output,&status)==-1);
 assert(publishes==20);
 uint32_t brk=CP32_APP_DATA+128, floor=brk;
 assert(!application_break(floor,&brk,32) && brk==floor+32);
 assert(application_break(floor,&brk,(-2147483647-1))==-1 && brk==floor+32);
 assert(!application_break(floor,&brk,-32) && brk==floor);
 assert(application_break(floor,&brk,-1)==-1 && brk==floor);
 assert(!application_break(floor,&brk,CP32_APP_STACK-floor));
 assert(application_break(floor,&brk,1)==-1 && brk==CP32_APP_STACK);
 assert(!application_break(floor,&brk,0));
 procs[3].p_flags=P_SLOT_FREE;
 int wrapped=0,last=122;reserve_wait_pid=1;
 for(int i=0;i<30000;i++) {
   int pid=application_pid();assert(pid>=102 && pid<=30000);
   if(pid<last) {assert(last==30000 && pid==102);wrapped++;}
   last=pid;
 }
 assert(wrapped==1);
 current_proc=procs+1;
 struct proc *p=cp32_proc_reserve(500);
 assert(p==procs+3 && p->p_flags==NO_MAP);
 assert(!cp32_proc_reserve(500));
 assert(cp32_proc_publish(p)==EINVAL); /* No frame/map may run. */
 p->p_reg.pc=CP32_APP_TEXT;p->p_reg.psw=0x100;
 p->p_reg.sp=p->p_reg.a[1]=p->p_reg.a[15]=CP32_APP_TOP-32;
 for(int n=D;n<=S;n++) {
   p->p_map[n].mem_vir=CP32_APP_DATA;
   p->p_map[n].mem_phys=CP32_APP_DATA>>CLICK_SHIFT;
   p->p_map[n].mem_len=(CP32_APP_TOP-CP32_APP_DATA)>>CLICK_SHIFT;
 }
 p->p_map[S].mem_phys++;assert(cp32_proc_publish(p)==EINVAL);p->p_map[S].mem_phys--;
 p->p_reg.a[15]--;assert(cp32_proc_publish(p)==EINVAL);p->p_reg.a[15]++;
 p->p_map[S].mem_len=0;assert(cp32_proc_publish(p)==EINVAL);
 assert(p->p_flags==NO_MAP);
 assert(cp32_proc_release(procs+1)==EINVAL); /* Never recycle a server. */
 rdy_head[0]=p;assert(cp32_proc_release(p)==EINVAL);rdy_head[0]=0;
 held_head=p;assert(cp32_proc_release(p)==EINVAL);held_head=0;
 current_proc=p;assert(cp32_proc_release(p)==EINVAL);current_proc=procs+1;
 assert(cp32_proc_reserve(501)==procs+4);
 assert(!cp32_proc_reserve(502));
 procs[4].user_time=99;procs[4].child_stime=99;
 assert(!cp32_proc_release(procs+4));
 assert(procs[1].child_utime==140 && procs[1].child_stime==200);
 p->p_flags=RECEIVING;p->p_getfrom=2;assert(cp32_proc_release(p)==EINVAL);
 p->p_getfrom=FS_PROC_NR;
 cp32_irq_return_proc=cp32_blocked_return_proc=cp32_irq_saved_owner=p;
 assert(!cp32_proc_release(p));
 assert(!cp32_irq_return_proc && !cp32_blocked_return_proc && !cp32_irq_saved_owner);
 assert(cp32_proc_release(p)==EINVAL); /* Double release is rejected. */
 assert(procs[1].child_utime==140 && procs[1].child_stime==200);
 fs_fail=1;
 assert(cp32_application_run(0,0,0,3,args,output,&status)==-1);
 assert(!app_busy && app_endpoint==-1 && !fs_pid && procs[3].p_flags==P_SLOT_FREE);
 fs_fail=0;
 struct proc *parent=procs+3,*child=procs+4;
 memset(parent,0,sizeof(*parent));parent->p_nr=3;parent->p_pid=700;
 parent->p_flags=RECEIVING;parent->p_getfrom=MM_PROC_NR;
 message msg;parent->p_messbuf=&msg;
 parent->p_reg.pc=CP32_APP_TEXT;parent->p_reg.sp=CP32_APP_TOP-32;
 parent->p_reg.a[1]=parent->p_reg.a[15]=parent->p_reg.sp;
 parent->p_reg.a[2]=99;parent->p_reg.psw=0x100;
 parent->p_alarm=9;parent->user_time=8;parent->p_pending=4;
 parent->p_pendcount=3;parent->p_int_held=1;parent->p_int_blocked=1;
 parent->p_stguard=&msg;parent->p_shadow=123;parent->p_blocked_frame_result=9;
 parent->p_callerq=procs;parent->p_nextheld=procs;
 struct proc before=*child;
 parent->p_getfrom=FS_PROC_NR;
 assert(cp32_proc_clone(3,4,701,MM_PROC_NR)==EINVAL && !memcmp(child,&before,sizeof(before)));
 parent->p_getfrom=MM_PROC_NR;parent->p_flags=0;
 assert(cp32_proc_clone(3,4,701,MM_PROC_NR)==EINVAL && !memcmp(child,&before,sizeof(before)));
 parent->p_flags=RECEIVING;
 assert(cp32_proc_clone(3,4,0,MM_PROC_NR)==EINVAL && !memcmp(child,&before,sizeof(before)));
 assert(cp32_proc_clone(1,4,701,MM_PROC_NR)==EINVAL && !memcmp(child,&before,sizeof(before)));
 assert(cp32_proc_clone(3,3,701,MM_PROC_NR)==EINVAL);
 assert(cp32_proc_clone(3,4,700,MM_PROC_NR)==EINVAL);
 rdy_head[0]=child;assert(cp32_proc_clone(3,4,701,MM_PROC_NR)==EINVAL);rdy_head[0]=0;
 struct proc parent_before=*parent;
 assert(!cp32_proc_clone(3,4,701,MM_PROC_NR));
 assert(!memcmp(parent,&parent_before,sizeof(*parent)));
 assert(child->p_nr==4 && child->p_pid==701 && child->p_flags==(NO_MAP|RECEIVING));
 assert(child->p_reg.sp==parent->p_reg.sp && !child->p_reg.a[2]);
 assert(child->p_messbuf==&msg && child->p_getfrom==MM_PROC_NR);
 assert(!child->p_alarm && !child->user_time && !child->p_pending);
 assert(!child->p_callerq && !child->p_nextheld);
 assert(!child->p_pendcount && !child->p_int_held && !child->p_int_blocked);
 assert(!child->p_stguard && !child->p_shadow && !child->p_blocked_frame_result);
 before=*child;assert(cp32_proc_clone(3,4,702,MM_PROC_NR)==EINVAL && !memcmp(child,&before,sizeof(before)));
 assert(parent->p_reg.a[2]==99 && parent->p_callerq==procs);
 puts("application: publish, bounded writes, blocked exit/reap, reload and failures pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+''.join(extract_function(root/'src/kernel/process.c',n) for n in ('process_detached','process_pid_free','cp32_proc_reserve','cp32_proc_clone','cp32_proc_publish','cp32_proc_release'))+extract_function(root/'src/kernel/context.c','cp32_exec_frame')+extract_function(root/'src/kernel/application.c','application_pid')+extract_function(root/'src/kernel/application.c','application_break')+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-I'+str(root/'src/mm'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
