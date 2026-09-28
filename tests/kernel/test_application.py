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
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "image.h"
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
static int app_busy, services, syncs, publishes, reads, writes, replies, loading, stacking;
static unsigned char stack[4096];
static void panic(const char *s,int n) { (void)s;(void)n; assert(0); }
static int lock_save(void) { return 3; }
static void restore_lock(int n) { assert(n==3); }
static void lock_ready(struct proc *p) {
 assert(p==procs+3 && !p->p_flags && p->p_reg.pc==CP32_APP_TEXT);
 assert(p->p_reg.sp==CP32_APP_TOP-32 && p->p_reg.a[1]==p->p_reg.sp);
 assert(p->p_reg.a[15]==p->p_reg.sp && p->p_reg.a[2]==(reg_t)&services);
 assert(p->p_reg.psw==0x100 && p->p_map[D].mem_vir==CP32_APP_DATA);
 for(unsigned i=0;i<sizeof(stack);i++) assert(!stack[i]);
 publishes++; bill_ptr=p;
}
int cp32_image_load(cp32_image_reader r,void *c,uint32_t sz,unsigned char *t,
 unsigned char *d,struct cp32_image *out) {
 (void)r;(void)c;(void)sz; assert((uintptr_t)t==APP_TEXT_DATA && (uintptr_t)d==CP32_APP_DATA);
 if(loading) return -2; out->entry=CP32_APP_TEXT; return 0;
}
int cp32_exec_stack(unsigned char *b,unsigned cap,uint32_t base,unsigned argc,
 const char *const argv[],unsigned envc,const char *const envp[],uint32_t *sp) {
 (void)b;(void)envp; assert(cap==512 && base==CP32_APP_TOP-512 && argc==1 && !envc);
 assert(!strcmp(argv[0],"hello")); if(stacking) return -1; *sp=CP32_APP_TOP-32;return 0;
}
static int receive(int nr,message *m) {
 assert(nr==APP_NR); struct proc *p=procs+3; p->p_flags=RECEIVING;p->p_getfrom=1;
 switch(reads++%4) {
 case 0: m->m_type=APP_WRITE;m->m1_p1=(char *)CP32_APP_DATA;m->m1_i1=30;break;
 case 1: m->m_type=APP_WRITE;m->m1_p1=(char *)(CP32_APP_TOP-1);m->m1_i1=2;break;
 case 2: m->m_type=999;break;
 default:m->m_type=APP_EXIT;m->m1_i1=7;break;
 } return 0;
}
static int send(int nr,message *m) {
 assert(nr==3); assert(m->m_type==(replies%3==0 ? 30:-1));replies++;return 0;
}
static void output(const char *p,unsigned n) {assert((uintptr_t)p==CP32_APP_DATA && n==30);writes++;}
'''
test=r'''
int main(void) {
 int status=-10;procs[3].p_flags=P_SLOT_FREE;
 loading=1;assert(cp32_application_run(0,0,0,output,&status)==-2);
 assert(!app_busy && !publishes && status==-10 && procs[3].p_flags==P_SLOT_FREE);
 loading=0;stacking=1;assert(cp32_application_run(0,0,0,output,&status)==-1);
 assert(!app_busy && !publishes);stacking=0;
 for(int i=0;i<20;i++) {
   memset(stack,0xa5,sizeof(stack));
   assert(!cp32_application_run(0,0,0,output,&status));assert(status==7);
   assert(procs[3].p_flags==P_SLOT_FREE && procs[3].p_nr==3 && !app_busy);
   assert(bill_ptr==procs+2 && !procs[3].p_reg.pc);
 }
 assert(publishes==20 && syncs==20 && writes==20 && replies==60);
 current_proc=procs+2;assert(cp32_application_run(0,0,0,output,&status)==-1);
 assert(publishes==20);
 puts("application: publish, bounded writes, blocked exit/reap, reload and failures pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-I'+str(root/'src/mm'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
