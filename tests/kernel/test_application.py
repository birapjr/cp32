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
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "image.h"
#define NR_PROCS 4
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_FILE 2007
#define CP32_APP_EXEC 10
#define APP_NR 3
#define LOW_USER 2
#define FS_PROC_NR 1
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
typedef struct {int m_type,m1_i1; char *m1_p1;} message;
struct proc {
 int p_nr,p_pid,p_flags,p_getfrom;
 struct {reg_t pc,psw,sp,a[16];} p_reg;
 struct {uint32_t mem_vir,mem_phys,mem_len;} p_map[3];
 struct proc *p_callerq,*p_sendlink;
 char p_name[16];
};
static struct proc procs[4], *current_proc=procs+1, *bill_ptr;
#define proc_addr(n) (procs+(n))
static const char *args[]={"hello","one","two"};
static int requested_status;
static int app_busy, services, syncs, publishes, reads, writes, replies, loading, stacking;
static unsigned char stack[4096],heap[12288];
static void panic(const char *s,int n) { (void)s;(void)n; assert(0); }
static int lock_save(void) { return 3; }
static void restore_lock(int n) { assert(n==3); }
static void lock_ready(struct proc *p) {
 assert(p==procs+3 && !p->p_flags && p->p_reg.pc==CP32_APP_TEXT);
 assert(p->p_reg.sp==CP32_APP_TOP-32 && p->p_reg.a[1]==p->p_reg.sp);
 assert(p->p_reg.a[15]==p->p_reg.sp && p->p_reg.a[2]==(reg_t)&services);
 assert(p->p_reg.psw==0x100 && p->p_map[D].mem_vir==CP32_APP_DATA);
 for(unsigned i=0;i<sizeof(stack);i++) assert(!stack[i]);
 assert(!strcmp(p->p_name,strrchr(args[0],'/') ? strrchr(args[0],'/')+1 : args[0]));
 assert(p->p_pid==101+publishes);
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
static int resets;
static void cp32_shell_app_files_reset(void) {resets++;}
static int application_replace(struct proc *p,message *m,uint32_t *f,uint32_t *b) {(void)p;(void)m;(void)f;(void)b;return -8;}
static int application_file_request(message *m) {(void)m;return -22;}
static int receive(int nr,message *m) {
 assert(nr==APP_NR); struct proc *p=procs+3; p->p_flags=RECEIVING;p->p_getfrom=1;
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
 default:m->m_type=APP_EXIT;m->m1_i1=requested_status;break;
 } return 0;
}
static int send(int nr,message *m) {
 assert(nr==3);
 if(replies%9==5) {
   assert((uintptr_t)m->m1_p1==CP32_APP_DATA+128);
   for(unsigned i=0;i<sizeof(heap);i++)
     assert(heap[i]==(i>=128 && i<160 ? 0 : 0xa5));
 }
 assert(m->m_type==(replies%9==0 ? 30 : replies%9==3 ? procs[3].p_pid : replies%9==4 ? 1 : replies%9==5 ? 0 : replies%9==7 ? 5 : -1));replies++;return 0;
}
static int cp32_shell_app_input(char *p,unsigned n) {assert((uintptr_t)p==CP32_APP_DATA && n==64);return 5;}
static void output(const char *p,unsigned n) {assert((uintptr_t)p==CP32_APP_DATA && n==30);writes++;}
'''
test=r'''
int main(void) {
 procs[0].p_pid=100; /* Occupied identity must be skipped. */
 procs[1].p_pid=1;
 int status=-10;procs[3].p_flags=P_SLOT_FREE;
 loading=1;assert(cp32_application_run(0,0,0,3,args,output,&status)==-2);
 assert(!app_busy && !publishes && status==-10 && procs[3].p_flags==P_SLOT_FREE);
 loading=0;stacking=1;assert(cp32_application_run(0,0,0,3,args,output,&status)==-1);
 assert(!app_busy && !publishes);stacking=0;
 for(int i=0;i<20;i++) {
   requested_status=(int[]){0,7,255,256,65535,-1,-256}[i%7];
   args[0]=i%2 ? "/boot/echo":"hello";
   memset(stack,0xa5,sizeof(stack));
   assert(!cp32_application_run(0,0,0,3,args,output,&status));assert(status==(int)((unsigned)requested_status & 255U));
   assert(procs[3].p_flags==P_SLOT_FREE && procs[3].p_nr==3 && !app_busy);
   assert(bill_ptr==procs+2 && !procs[3].p_reg.pc);
 }
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
 int wrapped=0,last=120;
 for(int i=0;i<30000;i++) {
   int pid=application_pid();assert(pid>=101 && pid<=30000);
   if(pid<last) {assert(last==30000 && pid==101);wrapped++;}
   last=pid;
 }
 assert(wrapped==1);
 puts("application: publish, bounded writes, blocked exit/reap, reload and failures pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extract_function(root/'src/kernel/context.c','cp32_exec_frame')+extract_function(root/'src/kernel/application.c','application_pid')+extract_function(root/'src/kernel/application.c','application_break')+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-I'+str(root/'src/mm'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
