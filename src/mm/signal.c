/* MINIX2 mm/signal.c: dispositions, pending sets, interrupted waits and
 * sigreturn. Xtensa has no protected signal frame: the authoritative complete
 * call0 context stays in MM-owned process storage, never in user memory. */
#include "mm.h"
#include "../kernel/app_process.h"
#include "../kernel/launch_trace.h"
extern int _sendrec(int,message *);
extern void cp32_ipc_trap(void);

/* Runs on the selected application's stack after RFE. Handler return resumes
 * through a validated MM request, never through an application-provided frame. */
CP32_IRAM_EXT static void signal_trampoline(void (*handler)(int),int signo)
{
  message m;
  handler(signo);
  memset(&m,0,sizeof(m));m.m_type=APP_FILE;m.m1_i1=CP32_APP_SIGRETURN;
  _sendrec(MM_PROC_NR,&m);
  panic("signal return",NO_NUM);
}
CP32_IRAM_EXT static void signal_exit(void)
{
  message m;
  memset(&m,0,sizeof(m));m.m_type=APP_EXIT;
  _sendrec(MM_PROC_NR,&m);
  panic("signal exit",NO_NUM);
}
CP32_IRAM_EXT static int signal_match(int selector,const struct proc *p,int group)
{
  return selector>0 ? p->p_pid==selector:selector==-1 ||
    p->p_app_group==(selector==0 ? group:-selector);
}
CP32_IRAM_EXT static int signal_permitted(const struct proc *sender,const struct proc *target)
{
  unsigned uid,euid;
  const struct cp32_fproc *s=sender->p_files;
  if(target->p_flags==P_ZOMBIE) {
    uid=target->p_zombie_uid;euid=target->p_zombie_euid;
  } else {uid=target->p_files->uid;euid=target->p_files->euid;}
  /* MINIX2 check_sig permits either sender identity to match either target
   * identity; signal0 performs the same permission check without delivery. */
  return !s->euid || s->uid==uid || s->euid==uid || s->uid==euid || s->euid==euid;
}
CP32_IRAM_EXT static int signal_queue(struct proc *p,unsigned signo)
{
  unsigned bit=1U<<(signo-1);
  struct cp32_fork_state *state=cp32_runtime_state(p);
  int saved,wake;
  if(!state || state->handlers[signo]==CP32_SIG_IGN)return 0;
  saved=lock_save();
  p->p_app_pending|=bit;
  wake=!(p->p_app_sigmask&bit);
  restore_lock(saved);
  if(wake)cp32_runtime_interrupt(p);
  return 0;
}
CP32_IRAM_EXT void cp32_runtime_signal_fork(struct proc *child,struct proc *parent)
{
  struct cp32_fork_state *c=cp32_runtime_state(child),*p=cp32_runtime_state(parent);
  memcpy(c->handlers,p->handlers,sizeof(c->handlers));
  c->signal_active=p->signal_active;c->signal_mask=p->signal_mask;
  if(p->signal_active)c->signal_frame=p->signal_frame;
  child->p_app_pending=0;
}
CP32_IRAM_EXT void cp32_runtime_signal_exec(struct proc *p)
{
  unsigned sig;struct cp32_fork_state *state=cp32_runtime_state(p);
  for(sig=1;sig<=15;sig++)if(state->handlers[sig]>CP32_SIG_IGN)state->handlers[sig]=CP32_SIG_DFL;
  state->signal_active=0;state->signal_mask=0;
}
CP32_IRAM_EXT int cp32_runtime_signal_request(struct proc *p,message *m,int *result)
{
  struct cp32_fork_state *state=cp32_runtime_state(p);
  unsigned op=(unsigned)m->m1_i1,arg=(unsigned)m->m1_i3,old,bit;
  int signo=m->m1_i2,n,found,saved,error;
  struct proc *target;
  if(!state || m->m_type!=APP_FILE)return 0;
  if(op!=CP32_APP_SIGNAL && op!=CP32_APP_SIGMASK && op!=CP32_APP_SIGPENDING &&
     op!=CP32_APP_KILL && op!=CP32_APP_SIGRETURN)return 0;
  *result=-22;
  if(m->m1_p1)return 1;
  if(op==CP32_APP_SIGNAL) {
    if(signo<1 || signo>15 || signo==CP32_SIGKILL ||
       (arg>CP32_SIG_IGN && (arg<CP32_APP_TEXT || arg>=CP32_APP_TEXT_END)))return 1;
    old=state->handlers[signo];state->handlers[signo]=arg;bit=1U<<(signo-1);
    if(arg==CP32_SIG_IGN){p->p_app_ignore|=bit;p->p_app_pending&=~bit;}
    else p->p_app_ignore&=~bit;
    *result=(int)old;return 1;
  }
  if(op==CP32_APP_SIGPENDING) {
    if(!signo && !arg)*result=p->p_app_pending;
    return 1;
  }
  if(op==CP32_APP_SIGMASK) {
    old=p->p_app_sigmask;
    if(signo==4){if(!arg)*result=(int)old;return 1;}
    if(signo<0 || signo>2 || (arg&~CP32_SIGNAL_MASK))return 1;
    arg&=~CP32_SIGKILL_MASK;
    if(signo==CP32_SIG_BLOCK)p->p_app_sigmask|=arg;
    else if(signo==CP32_SIG_UNBLOCK)p->p_app_sigmask&=~arg;
    else p->p_app_sigmask=arg;
    *result=(int)old;return 1;
  }
  if(op==CP32_APP_KILL) {
    if(signo==(-2147483647-1) || arg>15)return 1;
    found=0;error=-3;
    for(n=LOW_USER+1;n<NR_PROCS;n++) {
      target=proc_addr(n);
      if((target->p_flags&P_SLOT_FREE) || !signal_match(signo,target,p->p_app_group))continue;
      if(target->p_flags==P_ZOMBIE) {if(arg)continue;}
      else if(!cp32_runtime_is_app(target))continue;
      if(!signal_permitted(p,target)){error=-1;continue;}
      if(target->p_flags==P_ZOMBIE){found=1;continue;}
      found=1;if(arg)signal_queue(target,arg);
    }
    *result=found ? 0:error;return 1;
  }
  /* A handler cannot fabricate or nest a return frame. IPC parameters and
   * the MM-owned active bit are the entire return authorization. */
  if(signo || arg || !state->signal_active)return 1;
  saved=lock_save();
  p->p_reg=state->signal_frame;
  p->p_app_sigmask=state->signal_mask&~CP32_SIGKILL_MASK;
  state->signal_active=0;state->signal_mask=0;
  p->p_flags=0;p->p_getfrom=ANY;p->p_sendto=0;p->p_messbuf=0;
  p->p_blocked_frame_valid=0;p->p_blocked_frame_result=0;
  p->p_blocked_frame_pc=p->p_blocked_frame_psw=p->p_blocked_frame_sp=0;
  lock_ready(p);
  cp32_trace_launch(LAUNCH_SIGRETURN,p->p_reg.pc);
  restore_lock(saved);*result=0;return 2;
}

/* Called only on the kernel exception stack after image activation, with
 * scheduling excluded. Never inject into the middle of a kernel callback.
 * Completed IPC (trap+3) and application instruction PCs are safe boundaries. */
CP32_IRAM_EXT void cp32_runtime_signal_return(struct proc *p)
{
  struct cp32_fork_state *state=cp32_runtime_state(p);
  unsigned ready,handler,sp,bit;int sig;
  if(!state || p->p_flags || (p->p_reg.psw&15U))return;
  if((p->p_reg.pc<CP32_APP_TEXT || p->p_reg.pc>=CP32_APP_TEXT_END) &&
     p->p_reg.pc!=(reg_t)cp32_ipc_trap+3U)return;
  ready=p->p_app_pending&~p->p_app_sigmask;
  if(!ready)return;
  sig=CP32_SIGKILL;
  if(!(ready&CP32_SIGKILL_MASK))for(sig=1;sig<=15;sig++) {
    if(!(ready&(1U<<(sig-1))))continue;
    if(state->signal_active && state->handlers[sig]>CP32_SIG_IGN)continue;
    break;
  }
  if(sig>15)return;
  bit=1U<<(sig-1);handler=state->handlers[sig];p->p_app_pending&=~bit;
  if(handler==CP32_SIG_IGN)return;
  sp=p->p_reg.sp;
  if(handler>CP32_SIG_IGN && sp>=CP32_APP_STACK+1024U && sp<=CP32_APP_TOP && !(sp&15U)) {
    state->signal_frame=p->p_reg;state->signal_mask=p->p_app_sigmask;state->signal_active=1;
    p->p_app_sigmask|=bit;
    if(cp32_exec_frame(p,(reg_t)signal_trampoline,sp-32U,handler)!=OK)panic("signal frame",NO_NUM);
    p->p_reg.a[3]=(reg_t)sig;
    cp32_trace_launch(LAUNCH_SIGNAL,sig);
  } else {
    p->p_exitstatus=handler>CP32_SIG_IGN ? CP32_SIGSEGV:(unsigned)sig;
    if(cp32_exec_frame(p,(reg_t)signal_exit,CP32_APP_TOP-16U,0)!=OK)panic("signal frame",NO_NUM);
  }
}
