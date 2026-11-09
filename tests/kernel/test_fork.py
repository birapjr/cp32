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
#define CP32_WAIT_LIVE (-1000)
struct cp32_fproc {struct cp32_fproc *suspended;unsigned pid;unsigned short root_inode,uid,euid,gid,egid,mask;char cwd[256];int fds[4];unsigned cloexec;int input;};
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
static union {uint64_t align;unsigned char bytes[33536];} staging[2];
static int inuse[2];
static unsigned char *stage(unsigned base){assert(base>=123 && base<=124);return staging[base-123].bytes;}
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
 if(m->m_type==CP32_MM_ALLOCATE){assert(allocated<2 && (unsigned)m->m1_i1*256<=sizeof(staging[0]));
  int i=inuse[0] ? 1:0;
  m->m_type=alloc_fail ? -12:0;m->m1_i1=alloc_fail ? 0:123+i;
  if(!alloc_fail){inuse[i]=1;allocated++;allocs++;}
 }else{int i=m->m1_i1-123;assert(i>=0 && i<2 && inuse[i] && m->m_type==CP32_MM_RELEASE);
  inuse[i]=0;allocated--;frees++;m->m_type=0;}

 return 0;
}
'''
proc='\n'.join(extract_function(root/'src/kernel/process.c',n) for n in ('process_detached','process_pid_free','cp32_proc_clone','cp32_proc_release'))
fs='\n'.join(extract_function(root/'src/fs/fproc.c',n) for n in ('cp32_fproc_exit','cp32_fproc_clone'))
bridge='\n'.join(extract_function(root/'src/kernel/cp32-shell.c',n) for n in ('cp32_shell_app_files_fork','cp32_shell_app_files_exchange','cp32_shell_app_files_restore'))
source=(root/'src/kernel/fork.c').read_text();source=source[source.index('struct cp32_fork_state {'):]
source=source.replace('extern int _sendrec(int,message *);','')
source=source.replace('(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT)','stage(base)')
source=source.replace('(void *)IMAGE_BASE','live').replace('(unsigned *)IMAGE_BASE','(unsigned *)live').replace('(unsigned *)(uintptr_t)(base<<CLICK_SHIFT)','(unsigned *)stage(base)')
source=source.replace('((base<<CLICK_SHIFT)+IMAGE_BYTES+sizeof(struct cp32_fproc))','(stage(base)+IMAGE_BYTES+sizeof(struct cp32_fproc))')
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
  assert(allocs==f+1);
  assert(cp32_fork_wait(-1,0,0)==-10);
  memset(live,0xaa,sizeof(live)); /* Child writes code/data/stack, or execs. */
  strcpy(cp32_app_state.cwd,"/");cp32_app_state.mask=0077;position++;
  floor=30;brk=40;active->user_time=2;active->sys_time=3;bill_ptr=active;
  assert(cp32_fork_finish(&active,7,&pp,&floor,&brk,0)==pid);
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
 assert(cp32_fork_finish(&active,-1,&pp,&floor,&brk,0)==555);
 cp32_fork_reset();assert(cp32_fork_wait(-1,0,0)==-10);
 /* Multiple exited children: exhaustion before MM/FS changes, selective
  * waits, failed copyout retained, slot reuse, any-child drain and reset. */
 for(int i=0;i<8;i++){
  assert(!cp32_fork_start(&active,600+i,&pp,&floor,&brk));
  assert(cp32_fork_wait(600,0,0)==-10); /* sibling status is private to parent */
  assert(cp32_fork_finish(&active,i,&pp,&floor,&brk,0)==600+i);
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
 assert(cp32_fork_finish(&active,255,&pp,&floor,&brk,0)==700);
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
  assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==800+i);
 }
 cp32_fork_reset();assert(cp32_fork_wait(-1,0,0)==-10);


 /* Two private parent continuations, independent waits and orphan cleanup. */
 assert(!cp32_fork_start(&active,900,&pp,&floor,&brk));
 assert(cp32_fork_finish(&active,4,&pp,&floor,&brk,0)==900);
 memset(live,0x11,sizeof(live));
 assert(!cp32_fork_start(&active,1000,&pp,&floor,&brk));
 assert(cp32_fork_pid_used(900) && cp32_fork_wait(900,0,0)==-10);
 memset(live,0x22,sizeof(live));strcpy(cp32_app_state.cwd,"/child");
 alloc_fail=1;assert(cp32_fork_start(&active,1001,&pp,&floor,&brk)==-12);alloc_fail=0;
 fail_dup=1;assert(cp32_fork_start(&active,1001,&pp,&floor,&brk)==-24);fail_dup=0;
 assert(allocated==1 && refs==2 && cp32_fproc_suspended && !cp32_fproc_suspended->suspended);
 procs[5].p_flags=NO_MAP;
 assert(cp32_fork_start(&active,1001,&pp,&floor,&brk)==-11);
 procs[5].p_flags=P_SLOT_FREE;
 assert(allocated==1 && refs==2 && cp32_fproc_suspended && !cp32_fproc_suspended->suspended);
 assert(!cp32_fork_start(&active,1001,&pp,&floor,&brk));
 assert(allocated==2 && active==procs+5 && pp==1000 && refs==3);
 assert(cp32_fproc_suspended && cp32_fproc_suspended->suspended);
 before=allocs;assert(cp32_fork_start(&active,1002,&pp,&floor,&brk)==-11 && allocs==before);
 memset(live,0x33,sizeof(live));active->user_time=5;
 assert(cp32_fork_finish(&active,9,&pp,&floor,&brk,0)==1001);
 assert(active==procs+4 && allocated==1 && refs==2 && pp==100);
 assert(live[0]==0x22 && live[32767]==0x22 && !strcmp(cp32_app_state.cwd,"/child"));
 assert(cp32_fork_wait(900,0,0)==-10 && cp32_fork_wait(1001,0,0)==1001);
 assert(cp32_fproc_suspended && !cp32_fproc_suspended->suspended);
 /* The child exits without collecting its second grandchild's result. */
 assert(!cp32_fork_start(&active,1002,&pp,&floor,&brk));
 assert(cp32_fork_finish(&active,8,&pp,&floor,&brk,0)==1002);
 assert(cp32_fork_finish(&active,7,&pp,&floor,&brk,0)==1000);
 assert(active==procs+3 && !allocated && refs==1 && pp==1 && !cp32_fproc_suspended);
 assert(live[0]==0x11 && live[32767]==0x11);
 assert(!cp32_fork_pid_used(1002) && cp32_fork_wait(1002,0,0)==-10);
 assert(cp32_fork_wait(900,0,0)==900 && cp32_fork_wait(1000,0,0)==1000);
 assert(allocs==frees);

 /* MINIX process groups: inherit, create leader, preserve parent, select wait. */
 assert(cp32_process_group(100,0)==1);
 assert(cp32_process_group(100,1)==100 && cp32_process_group(100,1)==-1);
 assert(!cp32_fork_start(&active,1100,&pp,&floor,&brk));
 assert(cp32_process_group(1100,0)==100);
 assert(cp32_process_group(1100,1)==1100 && cp32_process_group(1100,1)==-1);
 assert(!cp32_fork_start(&active,1101,&pp,&floor,&brk));
 assert(cp32_process_group(1101,0)==1100);
 assert(cp32_fork_finish(&active,6,&pp,&floor,&brk,0)==1101);
 assert(cp32_fork_wait(0,0,0)==1101);
 assert(cp32_fork_finish(&active,7,&pp,&floor,&brk,0)==1100);
 assert(cp32_process_group(100,0)==100);
 assert(cp32_fork_wait(0,0,1)==-10 && cp32_fork_wait(-1234,0,0)==-10);
 assert(cp32_fork_wait((-2147483647-1),0,0)==-22);
 assert(cp32_fork_wait(-1100,(void *)(CP32_APP_TOP-1),0)==-14);
 assert(cp32_fork_wait(-1100,0,0)==1100);
 assert(!cp32_fork_start(&active,1102,&pp,&floor,&brk));
 assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==1102);
 assert(cp32_fork_wait(0,0,0)==1102);
 cp32_fork_reset();assert(cp32_process_group(100,0)==1 && allocs==frees);
 /* waitpid(0) in group1 must not accidentally become waitpid(-1). */
 assert(!cp32_fork_start(&active,1200,&pp,&floor,&brk));
 assert(cp32_process_group(1200,1)==1200);
 assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==1200);
 assert(cp32_fork_wait(0,0,1)==-10 && cp32_fork_wait(-1,0,0)==1200);
 /* Fatal signals use the low status bits, restore the same parent image,
  * release child resources and remain waitable exactly once. */
 for(int sig=4;sig<=11;sig++)if(sig==4 || sig==8 || sig==10 || sig==11){
  memset(live,0x44,sizeof(live));int references=refs;
  assert(!cp32_fork_start(&active,1300+sig,&pp,&floor,&brk));
  memset(live,0xcc,sizeof(live));
  assert(cp32_fork_finish(&active,255,&pp,&floor,&brk,(unsigned)sig)==1300+sig);
  for(unsigned i=0;i<sizeof(live);i++)assert(live[i]==0x44);
  assert(refs==references && allocs==frees && cp32_fork_pid_used(1300+sig));
  assert(cp32_fork_wait(1300+sig,(void *)(CP32_APP_TOP-1),0)==-14);
  assert(cp32_fork_wait(1300+sig,(void *)CP32_APP_DATA,0)==1300+sig);
  memcpy(&status,live+16384,4);assert(status==sig);
  assert(!cp32_fork_pid_used(1300+sig) && cp32_fork_wait(1300+sig,0,0)==-10);
 }
 /* Live first-level pair: full images, FS state, groups, breaks and wait
  * ownership survive repeated exchanges in both directions. */
 setup();cp32_fork_reset();active=procs+3;pp=1;floor=CP32_APP_DATA+64;brk=floor+16;
 memset(live,0x11,sizeof(live));
 assert(!cp32_fork_start(&active,1400,&pp,&floor,&brk));
 active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
 memset(live,0x22,sizeof(live));strcpy(cp32_app_state.cwd,"/boot");brk+=32;
 assert(cp32_process_group(1400,1)==1400);
 message request={0};request.m_type=2007;request.m1_i1=45;
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==1400);
 assert(active==procs+3 && pp==1 && live[0]==0x11 && brk==floor+16 && cp32_fork_live());
 assert(cp32_fork_wait(1400,(void *)CP32_APP_DATA,1)==0 && live[16384]==0x11);
 assert(cp32_fork_wait(1400,(void *)(CP32_APP_TOP-1),0)==-14);
 assert(cp32_fork_wait(0,0,0)==-10 && cp32_fork_wait(-1400,0,0)==CP32_WAIT_LIVE);
 assert(cp32_fork_start(&active,1401,&pp,&floor,&brk)==-11 && allocated==1);
 assert(cp32_process_group(100,1)==100 && cp32_fork_pid_used(1400));
 memset(live,0x33,sizeof(live));strcpy(cp32_app_state.cwd,"/parent");brk+=16;
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==0);
 assert(active==procs+4 && pp==100 && live[0]==0x22 && brk==floor+48);
 assert(!strcmp(cp32_app_state.cwd,"/boot") && cp32_process_group(1400,0)==1400);
 memset(live,0x44,sizeof(live));
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==0);
 assert(active==procs+3 && live[0]==0x33 && brk==floor+32 && !strcmp(cp32_app_state.cwd,"/parent"));
 request.m1_i1=41;
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==0);
 assert(live[0]==0x44 && brk==floor+48);
 assert(cp32_fork_finish(&active,7,&pp,&floor,&brk,0)==1400);
 assert(!cp32_fork_live() && brk==floor+32 && live[0]==0x33 && refs==1 && allocs==frees);
 assert(cp32_process_group(100,0)==100 && cp32_fork_wait(1400,(void *)CP32_APP_DATA,0)==1400);
 memcpy(&status,live+16384,4);assert(status==(7<<8));
 /* Second-level exchange keeps the outer ancestor untouched and reserves
  * its group IDs even while the intermediate parent owns a parked child. */
 setup();cp32_fork_reset();active=procs+3;pp=1;floor=CP32_APP_DATA+64;brk=floor+16;
 assert(cp32_process_group(100,1)==100);
 memset(live,0x51,sizeof(live));
 assert(!cp32_fork_start(&active,1500,&pp,&floor,&brk));
 active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
 assert(cp32_process_group(1500,1)==1500);
 memset(live,0x52,sizeof(live));strcpy(cp32_app_state.cwd,"/middle");brk+=16;
 assert(!cp32_fork_start(&active,1501,&pp,&floor,&brk));
 active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
 assert(cp32_process_group(1501,1)==1501);
 memset(live,0x53,sizeof(live));strcpy(cp32_app_state.cwd,"/leaf");brk+=16;
 request.m1_i1=45;
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==1501);
 assert(active==procs+4 && pp==100 && live[0]==0x52 && brk==floor+32);
 assert(cp32_fork_pid_used(100) && cp32_fork_pid_used(1500) && cp32_fork_pid_used(1501));
 assert(!strcmp(cp32_app_state.cwd,"/middle") && cp32_fork_live());
 assert(cp32_fork_wait(1501,0,1)==0 && cp32_fork_wait(-1501,0,0)==CP32_WAIT_LIVE);
 assert(cp32_fork_start(&active,1502,&pp,&floor,&brk)==-11);
 for(int round=0;round<3;round++) {
  assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==0);
  assert(active==procs+5 && pp==1500 && live[0]==0x53 && brk==floor+48);
  assert(!strcmp(cp32_app_state.cwd,"/leaf"));
  assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==0);
  assert(active==procs+4 && live[0]==0x52 && brk==floor+32);
 }
 request.m1_i1=41;
 assert(!cp32_fork_handoff(&active,&pp,&floor,&brk,&request));
 assert(cp32_fork_finish(&active,7,&pp,&floor,&brk,0)==1501);
 assert(active==procs+4 && refs==2 && allocated==1 && live[0]==0x52);
 assert(cp32_fork_wait(1501,0,0)==1501);
 request.m1_i1=45;
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==1500);
 assert(active==procs+3 && live[0]==0x51 && brk==floor+16 && refs==2);
 assert(cp32_fork_wait(1500,0,0)==CP32_WAIT_LIVE);
 assert(!cp32_fork_handoff(&active,&pp,&floor,&brk,&request));
 assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==1500);
 assert(cp32_fork_wait(1500,0,0)==1500 && refs==1 && allocs==frees);
 assert(cp32_fork_signal_target(active,100,0)==0);
 assert(cp32_fork_signal_target(active,100,9)==1);
 assert(cp32_fork_signal_target(active,100,15)==1);
 assert(cp32_fork_signal_target(active,100,16)==-22);
 assert(cp32_fork_signal_target(active,0,9)==-22);
 assert(cp32_fork_signal_target(active,-1,9)==-22);
 assert(cp32_fork_signal_target(active,9999,0)==-3);
 assert(!cp32_fork_start(&active,1600,&pp,&floor,&brk));
 active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
 assert(cp32_fork_signal_target(active,100,0)==0);
 assert(cp32_fork_signal_target(active,100,9)==-1);
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==1600);
 assert(cp32_fork_signal_target(active,1600,0)==0);
 assert(cp32_fork_signal_target(active,1600,9)==2);
 assert(!cp32_fork_handoff(&active,&pp,&floor,&brk,&request));
 assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,9)==1600);
 assert(cp32_fork_signal_target(active,1600,0)==0);
 assert(cp32_fork_signal_target(active,1600,9)==-3);
 assert(cp32_fork_wait(1600,0,0)==1600 && cp32_fork_signal_target(active,1600,0)==-3);
 /* Pending TERM is private, coalesced and inherited only as a mask. */
 assert(!cp32_fork_sigmask(0,(1U<<14)|(1U<<8)));
 assert(cp32_fork_sigmask(4,0)==1<<14 && !cp32_fork_sigpending());
 assert(cp32_fork_sigmask(3,0)==-22 && cp32_fork_sigmask(2,1U<<15)==-22);
 assert(!cp32_fork_signal_target(active,100,15));
 assert(!cp32_fork_signal_target(active,100,15) && cp32_fork_sigpending()==1U<<14);
 assert(!cp32_fork_start(&active,1700,&pp,&floor,&brk));
 active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
 assert(!cp32_fork_sigpending() && cp32_fork_sigmask(4,0)==1<<14);
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==1700);
 assert(!cp32_fork_signal_target(active,1700,15));
 assert(!cp32_fork_handoff(&active,&pp,&floor,&brk,&request));
 assert(cp32_fork_sigpending()==1U<<14);
 assert(cp32_fork_signal_target(active,1700,9)==1);
 assert(cp32_fork_sigmask(1,1U<<14)==-1015);
 assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,15)==1700);
 assert(cp32_fork_sigpending()==1U<<14 && cp32_fork_sigmask(4,0)==1<<14);
 assert(cp32_fork_sigmask(2,0)==-1015);
 cp32_fork_reset();assert(!cp32_fork_sigpending() && !cp32_fork_sigmask(4,0));
 assert(cp32_fork_disposition(9,1)==-22 && cp32_fork_disposition(15,2)==-22);
 assert(!cp32_fork_sigmask(0,1U<<14));
 assert(!cp32_fork_signal_target(active,100,15) && cp32_fork_sigpending());
 assert(cp32_fork_disposition(15,1)==0 && !cp32_fork_sigpending());
 assert(!cp32_fork_signal_target(active,100,15) && !cp32_fork_sigpending());
 assert(cp32_fork_signal_target(active,100,9)==1);
 assert(cp32_fork_sigmask(2,0)==1<<14);
 assert(!cp32_fork_start(&active,1800,&pp,&floor,&brk));
 active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
 assert(!cp32_fork_signal_target(active,1800,15));
 assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==1800);
 assert(!cp32_fork_signal_target(active,1800,15));
 assert(cp32_fork_signal_target(active,1800,9)==2);
 assert(cp32_fork_disposition(15,0)==1);
 assert(!cp32_fork_handoff(&active,&pp,&floor,&brk,&request));
 assert(cp32_fork_disposition(15,0)==1 && cp32_fork_signal_target(active,1800,15)==1);
 assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,15)==1800);
 assert(cp32_fork_disposition(15,0)==0);
 cp32_fork_reset();assert(cp32_fork_disposition(15,0)==0);
 /* Every MINIX standard signal has a separate disposition and mask bit. */
 for(int sig=1;sig<=15;sig++) {
  unsigned bit=1U<<(sig-1);cp32_fork_reset();
  assert(cp32_fork_signal_target(active,100,sig)==1);
  if(sig==9){assert(cp32_fork_disposition(sig,1)==-22);continue;}
  assert(!cp32_fork_disposition(sig,1) && !cp32_fork_signal_target(active,100,sig));
  assert(cp32_fork_disposition(sig,0)==1 && !cp32_fork_sigmask(0,bit));
  assert(!cp32_fork_signal_target(active,100,sig) && cp32_fork_sigpending()==bit);
  assert(cp32_fork_sigmask(1,bit)==-1000-sig);
 }
 cp32_fork_reset();assert(!cp32_fork_sigmask(2,0x7fffU));
 assert(cp32_fork_sigmask(4,0)==(0x7fffU&~(1U<<8)));
 assert(!cp32_fork_signal_target(active,100,12) && !cp32_fork_signal_target(active,100,10));
 assert(cp32_fork_sigpending()==((1U<<11)|(1U<<9)));
 assert(!cp32_fork_disposition(12,1) && cp32_fork_sigpending()==1U<<9);
 assert(cp32_fork_sigmask(2,0)==-1010);
 cp32_fork_reset();assert(!cp32_fork_sigmask(0,(1U<<1)|(1U<<11)));
 assert(!cp32_fork_signal_target(active,100,12) && !cp32_fork_signal_target(active,100,2));
 assert(cp32_fork_sigmask(2,0)==-1002); /* lowest numbered unblocked signal */
 cp32_fork_reset();
 puts("fork: private image restore, FS inheritance/rollback, child status, usage, bounds and 20 reuse cycles pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+proc+fs+bridge+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
