"""Production MM signal delivery and private-context sigreturn contract."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[2]
source='\n'.join(x for x in (root/'src/mm/signal.c').read_text().splitlines() if not x.startswith('#include'))
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "abi.h"
#include "launch_trace.h"
#define CP32_IRAM_EXT
#define CP32_APP_TEXT 0x403d8000U
#define CP32_APP_TEXT_END 0x403dc000U
#define CP32_APP_STACK 0x3fcef000U
#define CP32_APP_TOP 0x3fcf0000U
#define CP32_APP_DATA 0x3fcec000U
#define APP_FILE 2007
#define APP_EXIT 2002
#define MM_PROC_NR 0
#define LOW_USER 2
#define NR_PROCS 8
#define P_SLOT_FREE 1
#define P_ZOMBIE 0200
#define RECEIVING 010
#define ANY 132
#define NO_NUM 0
#define OK 0
typedef uintptr_t reg_t;
typedef struct {int m_type,m1_i1,m1_i2,m1_i3;char *m1_p1;} message;
struct stackframe_s {reg_t a[16],pc,psw,sp,sar,lbeg,lend,lcount;};
struct cp32_fproc {unsigned uid,euid;};
struct proc {
 struct stackframe_s p_reg;
 int p_nr,p_pid,p_flags,p_app_group,p_getfrom,p_sendto;
 unsigned p_app_pending,p_app_sigmask,p_app_ignore,p_exitstatus;
 int p_blocked_frame_valid,p_blocked_frame_result;
 unsigned p_blocked_frame_pc,p_blocked_frame_psw,p_blocked_frame_sp;
 message *p_messbuf;
 struct cp32_fproc *p_files;
 unsigned p_zombie_uid,p_zombie_euid;
};
struct cp32_fork_state {struct proc *owner;unsigned handlers[16];struct stackframe_s signal_frame;unsigned signal_mask,signal_active;};
static struct proc procs[NR_PROCS];static struct cp32_fork_state states[NR_PROCS];
static struct cp32_fproc files[NR_PROCS];
static unsigned wakes[NR_PROCS],ready,locked;
static unsigned caught_signal,restored_pc;
void cp32_trace_launch(enum cp32_launch_step step,unsigned value){
 if(step==LAUNCH_SIGNAL)caught_signal=value;
 else{assert(step==LAUNCH_SIGRETURN && locked);restored_pc=value;}
}
#define proc_addr(n) (procs+(n))
static struct cp32_fork_state *cp32_runtime_state(struct proc *p){return p && p->p_nr>LOW_USER && p->p_nr<NR_PROCS && states[p->p_nr].owner==p ? states+p->p_nr:0;}
static int cp32_runtime_is_app(struct proc *p){return cp32_runtime_state(p)!=0 && !(p->p_flags&(P_SLOT_FREE|P_ZOMBIE));}
static void cp32_runtime_interrupt(struct proc *p){assert(!locked);wakes[p->p_nr]++;}
static int lock_save(void){locked++;return 0;}
static void restore_lock(int v){assert(!v && locked);locked--;}
static void lock_ready(struct proc *p){assert(locked && !p->p_flags);ready++;}
static void panic(const char *m,int n){(void)m;(void)n;assert(0);}
static int cp32_exec_frame(struct proc *p,reg_t pc,reg_t sp,reg_t arg){
 if(!pc || !sp || (sp&15))return -22;
 memset(&p->p_reg,0,sizeof(p->p_reg));p->p_reg.pc=pc;p->p_reg.sp=sp;p->p_reg.psw=0x100;
 p->p_reg.a[1]=p->p_reg.a[15]=sp;p->p_reg.a[2]=arg;return 0;
}
int _sendrec(int n,message *m){(void)n;(void)m;assert(0);return 0;}
void cp32_ipc_trap(void){}
'''
test=r'''
static void setup(void){memset(procs,0,sizeof(procs));memset(states,0,sizeof(states));memset(files,0,sizeof(files));memset(wakes,0,sizeof(wakes));ready=locked=0;
 for(int n=0;n<NR_PROCS;n++){procs[n].p_nr=n;procs[n].p_flags=P_SLOT_FREE;}
 for(int n=3;n<=4;n++){procs[n].p_flags=0;procs[n].p_pid=100+n;procs[n].p_app_group=103;states[n].owner=procs+n;procs[n].p_files=files+n;}
 procs[5].p_flags=P_ZOMBIE;procs[5].p_pid=105;procs[5].p_app_group=103;
}
static int request(struct proc *p,unsigned op,int selector,unsigned arg){message m={APP_FILE,(int)op,selector,(int)arg,0};int r=999;assert(cp32_runtime_signal_request(p,&m,&r)==1);return r;}
int main(void){setup();struct proc *p=procs+3,*c=procs+4;struct cp32_fork_state *s=states+3,*cs=states+4;
 unsigned bit=CP32_SIGSET(CP32_SIGUSR1);
 assert(request(p,CP32_APP_SIGNAL,9,1)==-22);
 assert(request(p,CP32_APP_SIGNAL,10,CP32_APP_TEXT-1)==-22);
 assert(request(p,CP32_APP_SIGNAL,10,CP32_APP_TEXT_END)==-22);
 assert(request(p,CP32_APP_SIGNAL,10,CP32_APP_TEXT+16)==0);
 p->p_app_pending=bit;assert(request(p,CP32_APP_SIGNAL,10,1)==(int)CP32_APP_TEXT+16 && !p->p_app_pending);
 assert(!request(p,CP32_APP_KILL,p->p_pid,10) && !p->p_app_pending && !wakes[3]);
 assert(request(p,CP32_APP_SIGNAL,10,CP32_APP_TEXT+16)==1);
 assert(!request(p,CP32_APP_SIGMASK,0,bit|CP32_SIGKILL_MASK) && p->p_app_sigmask==bit);
 assert(!request(p,CP32_APP_KILL,p->p_pid,10) && !request(p,CP32_APP_KILL,p->p_pid,10));
 assert(p->p_app_pending==bit && !wakes[3]);assert(request(p,CP32_APP_SIGPENDING,0,0)==(int)bit);
 assert(request(p,CP32_APP_SIGMASK,1,bit)==(int)bit && !p->p_app_sigmask);
 assert(request(p,CP32_APP_KILL,(-2147483647-1),0)==-22);
 assert(request(p,CP32_APP_KILL,105,0)==0 && request(p,CP32_APP_KILL,1,0)==-3);
 assert(!request(p,CP32_APP_KILL,-103,15) && wakes[3]==1 && wakes[4]==1);
 /* Signal and existence probes honor MINIX real/effective UID identities. */
 files[3].uid=100;files[3].euid=101;files[4].uid=200;files[4].euid=201;
 assert(request(p,CP32_APP_KILL,104,0)==-1 && request(p,CP32_APP_KILL,104,15)==-1);
 assert(request(p,CP32_APP_KILL,999,0)==-3);
 assert(!request(p,CP32_APP_KILL,0,0)); /* own permitted member wins over denied peer */
 c->p_app_group=104;assert(request(p,CP32_APP_KILL,-104,0)==-1);c->p_app_group=103;
 for(unsigned a=0;a<2;a++)for(unsigned b=0;b<2;b++) {
  files[4].uid=200;files[4].euid=201;
  if(b)files[4].euid=a ? 101:100;else files[4].uid=a ? 101:100;
  assert(!request(p,CP32_APP_KILL,104,0));
 }
 procs[5].p_zombie_uid=200;procs[5].p_zombie_euid=201;
 assert(request(p,CP32_APP_KILL,105,0)==-1 && request(p,CP32_APP_KILL,105,15)==-3);
 procs[5].p_zombie_euid=100;assert(!request(p,CP32_APP_KILL,105,0));
 files[3].euid=0;files[4].uid=200;files[4].euid=201;
 assert(!request(p,CP32_APP_KILL,104,0));
 p->p_app_pending=bit;p->p_app_sigmask=CP32_SIGSET(12);
 for(unsigned i=0;i<16;i++)p->p_reg.a[i]=0x10203040+i;
 p->p_reg.pc=CP32_APP_TEXT+32;p->p_reg.psw=0x110;p->p_reg.sp=CP32_APP_TOP-64;
 p->p_reg.sar=7;p->p_reg.lbeg=8;p->p_reg.lend=9;p->p_reg.lcount=10;
 struct stackframe_s original=p->p_reg;
 cp32_runtime_signal_return(p);
 assert(caught_signal==10);
 assert(s->signal_active && !memcmp(&s->signal_frame,&original,sizeof(original)));
 assert(p->p_reg.pc==(reg_t)signal_trampoline && p->p_reg.a[2]==CP32_APP_TEXT+16 && p->p_reg.a[3]==10);
 assert(p->p_app_sigmask==(CP32_SIGSET(12)|bit) && !p->p_app_pending);
 /* Fork from within a handler must retain the pending sigreturn context. */
 c->p_app_pending=1;cp32_runtime_signal_fork(c,p);
 assert(cs->signal_active && cs->signal_mask==CP32_SIGSET(12) && !c->p_app_pending);
 assert(!memcmp(&cs->signal_frame,&original,sizeof(original)) && cs->handlers[10]==s->handlers[10]);
 message m={APP_FILE,CP32_APP_SIGRETURN,0,0,0};int r;
 p->p_flags=RECEIVING;p->p_blocked_frame_valid=1;p->p_messbuf=&m;
 assert(cp32_runtime_signal_request(p,&m,&r)==2 && r==0 && ready==1 && !locked);
 assert(restored_pc==original.pc);
 assert(!memcmp(&p->p_reg,&original,sizeof(original)) && p->p_app_sigmask==CP32_SIGSET(12));
 assert(!p->p_flags && !p->p_messbuf && !p->p_blocked_frame_valid && !s->signal_active);
 assert(cp32_runtime_signal_request(p,&m,&r)==1 && r==-22);
 c->p_flags=RECEIVING;assert(cp32_runtime_signal_request(c,&m,&r)==2 && !cs->signal_active);
 assert(!memcmp(&c->p_reg,&original,sizeof(original)));
 /* Defer arbitrary kernel callback PCs, deliver at the completed-IPC gate. */
 p->p_app_pending=bit;p->p_reg.pc=1;cp32_runtime_signal_return(p);assert(!s->signal_active && p->p_app_pending==bit);
 p->p_reg.pc=(reg_t)cp32_ipc_trap+3;cp32_runtime_signal_return(p);assert(s->signal_active);
 p->p_reg.pc=CP32_APP_TEXT;p->p_app_pending=CP32_SIGKILL_MASK;
 cp32_runtime_signal_return(p);assert(p->p_exitstatus==9 && p->p_reg.pc==(reg_t)signal_exit);
 /* Insufficient handler stack fails terminally instead of writing below it. */
 s->signal_active=0;p->p_exitstatus=0;p->p_app_pending=bit;p->p_app_sigmask=0;
 p->p_reg.pc=CP32_APP_TEXT;p->p_reg.sp=CP32_APP_STACK+16;
 cp32_runtime_signal_return(p);assert(p->p_exitstatus==11 && p->p_reg.pc==(reg_t)signal_exit);
 s->handlers[12]=1;s->signal_active=1;cp32_runtime_signal_exec(p);
 assert(!s->signal_active && !s->handlers[10] && s->handlers[12]==1);
 puts("runtime signals: credential permissions, masks/selectors, async frame injection, exact sigreturn, handler-fork inheritance and stack failure PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'src/apps/hello'),'-iquote',str(root/'src/kernel'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
