"""Production fork image/FS lifecycle with modeled physical RAM and MM IPC."""
from pathlib import Path
import sys, tempfile, subprocess, re
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define CP32_IRAM_EXT
#define CP32_FS_TEXT
#define OK 0
#define EINVAL -22
#define NO_NUM 0
#define NR_PROCS 6
#define NR_TASKS 0
#define NQ 3
#define LOW_USER 2
#define FS_PROC_NR 1
#define MM_PROC_NR 0
#define P_SLOT_FREE 1
#define NO_MAP 2
#define RECEIVING 8
#define NIL_PROC ((struct proc *)0)
#define CLICK_SHIFT 8
#define CP32_APP_DATA 0x3fcec000U
#define CP32_APP_TOP 0x3fcf0000U
#define CP32_MM_ALLOCATE 1001
#define CP32_MM_RELEASE 1002
#define CP32_FPROC_FDS 4
#define CP32_SUPER_INVALID 22
#define IMAGE_BYTES 32768U
struct cp32_fproc {unsigned pid;unsigned short root_inode,uid,euid,gid,egid,mask;char cwd[256];int fds[4];unsigned cloexec;int input;};
typedef struct {int m_type,m_source,m1_i1;} message;
struct proc {
 int p_nr,p_pid,p_flags,p_getfrom,p_alarm,p_pending,p_pendcount;
 unsigned user_time,sys_time,child_utime,child_stime,p_shadow;
 struct {unsigned a[16],pc,sp;} p_reg;
 int p_int_held,p_int_blocked,p_blocked_frame_result;
 struct proc *p_nextready,*p_nextheld,*p_callerq,*p_sendlink;
 void *p_stguard;message *p_messbuf;
};
static struct proc procs[NR_PROCS],*current_proc=procs+1,*proc_ptr=procs+1,*bill_ptr;
static struct proc *held_head,*rdy_head[NQ];
static volatile struct proc *cp32_irq_return_proc,*cp32_blocked_return_proc,*cp32_irq_saved_owner;
#define proc_addr(n) (procs+(n))
static unsigned char live[32768],original[32768];
static union {uint64_t align;unsigned char bytes[33280];} staging;
static struct cp32_fproc cp32_app_state;
static struct cp32_fproc *cp32_fproc_suspended;
static int handles[8],refs,position,fail_dup;
static int cp32_fd_dup(int fd){assert(handles[fd]);if(fail_dup)return -24;for(int n=0;n<8;n++)if(!handles[n]){handles[n]=1;refs++;return n;}return -24;}
static int cp32_fs_errno(int r){return r;}
static int cp32_fd_close(int fd){assert(handles[fd]);handles[fd]=0;refs--;return 0;}
static int locked,alloc_fail,allocated,allocs,frees,syncs;
static int lock_save(void){assert(!locked);locked=1;return 0;}
static void restore_lock(int s){assert(locked && !s);locked=0;}
static void panic(const char *s,int n){(void)n;fprintf(stderr,"%s\n",s);assert(0);}
static int _sendrec(int nr,message *m){
 assert(nr==MM_PROC_NR && !locked);m->m_source=nr;
 if(m->m_type==CP32_MM_ALLOCATE){assert(!allocated && (unsigned)m->m1_i1*256<=sizeof(staging));
  m->m_type=alloc_fail ? -12:0;m->m1_i1=alloc_fail ? 0:123;allocated=!alloc_fail;if(allocated)allocs++;
 }else{assert(allocated && m->m_type==CP32_MM_RELEASE && m->m1_i1==123);allocated=0;frees++;m->m_type=0;}
 return 0;
}
'''
proc='\n'.join(extract_function(root/'src/kernel/process.c',n) for n in ('process_detached','process_pid_free','cp32_proc_clone','cp32_proc_release'))
fs='\n'.join(extract_function(root/'src/fs/fproc.c',n) for n in ('cp32_fproc_exit','cp32_fproc_clone'))
bridge='\n'.join(extract_function(root/'src/kernel/cp32-shell.c',n) for n in ('cp32_shell_app_files_fork','cp32_shell_app_files_restore'))
source=(root/'src/kernel/fork.c').read_text();source=source[source.index('static struct {'):]
source=source.replace('extern int _sendrec(int,message *);','')
source=source.replace('(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT)','staging.bytes')
source=source.replace('(void *)IMAGE_BASE','live')
source=source.replace('__asm__ volatile("memw\\n\\tisync" ::: "memory");','syncs++;')
source=source.replace('memcpy(status,&forked.waits[n].status,sizeof(int))','memcpy(live+16384+p-CP32_APP_DATA,&forked.waits[n].status,sizeof(int))')
test=r'''
static void setup(void){
 memset(procs,0,sizeof(procs));for(int n=3;n<NR_PROCS;n++){procs[n].p_nr=n;procs[n].p_flags=P_SLOT_FREE;}
 struct proc *p=procs+3;p->p_pid=100;p->p_flags=RECEIVING;p->p_getfrom=FS_PROC_NR;
 p->p_messbuf=(message *)42;p->p_reg.pc=0x403d8100;p->p_reg.sp=CP32_APP_TOP-32;
 p->p_reg.a[1]=p->p_reg.sp;p->p_reg.a[2]=22;
 memset(&cp32_app_state,0,sizeof(cp32_app_state));cp32_app_state.pid=100;cp32_app_state.mask=0022;
 strcpy(cp32_app_state.cwd,"/boot");cp32_app_state.fds[0]=1;handles[0]=1;refs=1;position=0;
 for(unsigned n=0;n<sizeof(live);n++)live[n]=(unsigned char)(n*7);
 memcpy(original,live,sizeof(live));
}
int main(void){
 setup();struct proc *active=procs+3;int pp=1;uint32_t floor=10,brk=20;
 alloc_fail=1;assert(cp32_fork_start(&active,101,&pp,&floor,&brk)==-12 && !allocated && refs==1);alloc_fail=0;
 fail_dup=1;assert(cp32_fork_start(&active,101,&pp,&floor,&brk)==-24 && !allocated && refs==1 && cp32_app_state.pid==100);fail_dup=0;
 procs[4].p_flags=procs[5].p_flags=NO_MAP;
 assert(cp32_fork_start(&active,101,&pp,&floor,&brk)==-11 && !allocated && refs==1 && cp32_app_state.pid==100);
 procs[4].p_flags=procs[5].p_flags=P_SLOT_FREE;
 for(int k=0;k<20;k++){
  int pid=101+k,status=-1;int f=allocs;
  assert(!cp32_fork_start(&active,pid,&pp,&floor,&brk));
  assert(cp32_fork_active() && allocated && active==procs+4 && active->p_pid==pid && pp==100 && refs==2);
  assert(cp32_app_state.pid==(unsigned)pid && !strcmp(cp32_app_state.cwd,"/boot") && cp32_app_state.mask==0022);
  assert(cp32_fork_start(&active,999,&pp,&floor,&brk)==-11 && allocs==f+1);
  assert(cp32_fork_wait(-1,0,0)==-10);
  memset(live,0xaa,sizeof(live)); /* Child writes code/data/stack, or execs. */
  strcpy(cp32_app_state.cwd,"/");cp32_app_state.mask=0077;position++;
  floor=30;brk=40;active->user_time=2;active->sys_time=3;bill_ptr=active;
  assert(cp32_fork_finish(&active,7,&pp,&floor,&brk)==pid);
  assert(!cp32_fork_active() && !allocated && refs==1 && active==procs+3 && pp==1 && floor==10 && brk==20);
  assert(!memcmp(live,original,sizeof(live)) && cp32_app_state.pid==100 && !strcmp(cp32_app_state.cwd,"/boot") && cp32_app_state.mask==0022);
  assert(active->child_utime==(unsigned)(k+1)*2 && active->child_stime==(unsigned)(k+1)*3 && !procs[1].child_utime);
  assert(position==k+1 && procs[4].p_flags==P_SLOT_FREE);
  assert(cp32_fork_wait(pid+1,0,0)==-10 && cp32_fork_wait(pid,0,2)==-22);
  assert(cp32_fork_wait(pid,(void *)(CP32_APP_TOP-1),0)==-14);
  assert(cp32_fork_start(&active,pid,&pp,&floor,&brk)==-11);
  assert(cp32_fork_wait(pid,(void *)CP32_APP_DATA,1)==pid);
  memcpy(&status,live+16384,sizeof(status));assert(status==(7<<8));
  assert(cp32_fork_wait(-1,0,0)==-10);
  memcpy(live,original,sizeof(live));
 }
 assert(allocs==frees && syncs==20);
 assert(!cp32_fork_start(&active,555,&pp,&floor,&brk));
 assert(cp32_fork_finish(&active,-1,&pp,&floor,&brk)==555);
 cp32_fork_reset();assert(cp32_fork_wait(-1,0,0)==-10);
 /* Multiple exited children: exhaustion before MM/FS changes, selective
  * waits, failed copyout retained, slot reuse, any-child drain and reset. */
 for(int i=0;i<8;i++){
  assert(!cp32_fork_start(&active,600+i,&pp,&floor,&brk));
  assert(cp32_fork_wait(600,0,0)==-10); /* sibling status is private to parent */
  assert(cp32_fork_finish(&active,i,&pp,&floor,&brk)==600+i);
 }
 int before=allocs;
 assert(cp32_fork_start(&active,700,&pp,&floor,&brk)==-11 && allocs==before);
 assert(cp32_fork_pid_used(603) && !cp32_fork_pid_used(999));
 assert(cp32_fork_wait(603,(void *)(CP32_APP_TOP-1),0)==-14);
 assert(cp32_fork_wait(603,(void *)CP32_APP_DATA,0)==603);
 assert(!cp32_fork_pid_used(603));
 int status;memcpy(&status,live+16384,4);assert(status==(3<<8));
 alloc_fail=1;
 assert(cp32_fork_start(&active,700,&pp,&floor,&brk)==-12 && !allocated && refs==1);
 alloc_fail=0;fail_dup=1;
 assert(cp32_fork_start(&active,700,&pp,&floor,&brk)==-24 && !allocated && refs==1);
 fail_dup=0;
 assert(!cp32_fork_start(&active,700,&pp,&floor,&brk));
 assert(cp32_fork_finish(&active,255,&pp,&floor,&brk)==700);
 assert(cp32_fork_wait(700,0,1)==700);
 unsigned seen=0;
 for(int i=0;i<7;i++){
  int pid=cp32_fork_wait(-1,(void *)CP32_APP_DATA,0);
  assert(pid>=600 && pid<608 && pid!=603 && !(seen&(1U<<(pid-600))));
  seen|=1U<<(pid-600);memcpy(&status,live+16384,4);assert(status==((pid-600)<<8));
 }
 assert(cp32_fork_wait(-1,0,1)==-10 && refs==1 && allocs==frees);
 for(int i=0;i<3;i++){
  assert(!cp32_fork_start(&active,800+i,&pp,&floor,&brk));
  assert(cp32_fork_finish(&active,0,&pp,&floor,&brk)==800+i);
 }
 cp32_fork_reset();assert(cp32_fork_wait(-1,0,0)==-10);

 puts("fork: private image restore, FS inheritance/rollback, child status, usage, bounds and 20 reuse cycles pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+proc+fs+bridge+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
