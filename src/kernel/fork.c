/* MINIX forkexit.c and the shadow-image model: fixed-address Xtensa images
 * require the parent to remain blocked while its private copy is backed up.
 * Called only by the foreground FS broker. No user pointer chooses a process.
 * p_shadow is the MM click base of the complete saved image plus FS/family
 * metadata; zero means resident. Unlike the M68000 MINIX data-only shadow,
 * this includes text. ESP32 SHADOWING remains disabled: address translation
 * does not implicitly redirect to backing memory. Ownership transfers under
 * the scheduler lock before either process can receive a runnable reply. */
#include "kernel.h"
#include "proc.h"
#include "application.h"
#include "fork.h"
#include "../fs/fproc.h"
#include <minix/com.h>
#include <minix/cp32_mm.h>
#define IMAGE_BYTES 32768U
#define IMAGE_BASE (CP32_APP_TEXT-0x6f0000U)
struct cp32_fork_state {
  struct proc *parent,*owner;
  int depth;
  struct proc *live_child;
  message resume; /* parent IPC to complete after the child next yields/exits */

};
static struct cp32_fork_state forked;
extern int _sendrec(int,message *);

CP32_IRAM_EXT static void fork_free(unsigned base)
{
  message m;
  memset(&m,0,sizeof(m));m.m_type=CP32_MM_RELEASE;m.m1_i1=base;
  if(_sendrec(MM_PROC_NR,&m)!=OK || m.m_source!=MM_PROC_NR || m.m_type!=OK)
    panic("fork free",NO_NUM);
}
/* Each MM backing image also stores the previous broker continuation and
 * that parent's continuation. Exit records live in the process table.
 * Process slots and MM allocation bound
 * the ancestor chain; it does not grow on the broker's C stack. */
CP32_IRAM_EXT static struct cp32_fork_state *fork_previous(unsigned base)
{
  return (struct cp32_fork_state *)((base<<CLICK_SHIFT)+IMAGE_BYTES+sizeof(struct cp32_fproc));
}
CP32_IRAM_EXT int cp32_fork_pid_used(int pid)
{
  int n;struct proc *p;
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    p=proc_addr(n);
    if(p->p_flags!=P_SLOT_FREE && (p->p_pid==pid || p->p_app_group==pid))return 1;
  }
  return 0;
}
CP32_IRAM_EXT static int fork_capacity(void)
{
  int n;
  for(n=LOW_USER+1;n<NR_PROCS;n++)if(proc_addr(n)->p_flags==P_SLOT_FREE)return 1;
  return 0;
}
/* Called under the scheduler lock. Until INIT adoption exists, preserve the
 * established foreground policy: discard an exiting parent's retained exits. */
CP32_IRAM_EXT static void fork_discard(int parent)
{
  int n;struct proc *p;
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    p=proc_addr(n);
    if(p->p_flags==P_ZOMBIE && p->p_parent_pid==parent && cp32_proc_release(p)!=OK)
      panic("fork state",NO_NUM);
  }
}
CP32_IRAM_EXT int cp32_fork_active(void){return forked.parent!=NIL_PROC;}
/* Bind only a fresh foreground root. Descendants inherit in proc_clone. */
CP32_IRAM_EXT void cp32_fork_bind(struct proc *owner)
{
  if(forked.parent || forked.live_child)panic("fork state",NO_NUM);
  memset(&forked,0,sizeof(forked));forked.owner=owner;
  owner->p_app_group=1;
}
CP32_IRAM_EXT void cp32_fork_reset(void)
{
  int saved=lock_save();
  if(forked.owner) {
    fork_discard(forked.owner->p_pid);
    forked.owner->p_app_group=0;
    forked.owner->p_app_sigmask=forked.owner->p_app_pending=forked.owner->p_app_ignore=0;
  }
  restore_lock(saved);
}

/* MINIX getset.c: all foreground launches inherit the bootstrap shell group1.
 * There is no controlling-terminal/session object in CP32 yet. */
CP32_IRAM_EXT int cp32_process_group(int pid,int create)
{
  int group=forked.owner->p_app_group ? forked.owner->p_app_group:1;
  if(!create)return group;
  if(group==pid)return -1; /* EPERM: a group leader cannot setsid. */
  forked.owner->p_app_group=pid;return pid;
}

CP32_IRAM_EXT int cp32_fork_start(struct proc **active,int pid)
{
  struct proc *parent=*active,*child=NIL_PROC;
  struct cp32_fproc *files;
  unsigned base;unsigned char *image;
  int r,n,saved;
  message m;
  if(parent->p_shadow || forked.live_child || forked.depth>=NR_PROCS-LOW_USER-2 || pid<=0 || cp32_fork_pid_used(pid))return -11;
  if(!fork_capacity())return -11;
  memset(&m,0,sizeof(m));m.m_type=CP32_MM_ALLOCATE;
  m.m1_i1=(IMAGE_BYTES+sizeof(*files)+sizeof(forked)+(1U<<CLICK_SHIFT)-1)>>CLICK_SHIFT;
  if(_sendrec(MM_PROC_NR,&m)!=OK || m.m_source!=MM_PROC_NR || m.m_type!=OK || !m.m1_i1)return -12;
  base=(unsigned)m.m1_i1;image=(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT);
  files=(struct cp32_fproc *)(image+IMAGE_BYTES);
  /* Parent is blocked in SENDREC, and no application can run during staging. */
  memcpy(image,(void *)IMAGE_BASE,IMAGE_BYTES);
  r=cp32_shell_app_files_fork(files,(unsigned)pid);
  if(r){fork_free(base);return r;}
  saved=lock_save();
  for(n=LOW_USER+1;n<NR_PROCS;n++)if(proc_addr(n)->p_flags==P_SLOT_FREE) {
    if(cp32_proc_clone(parent->p_nr,n,pid,FS_PROC_NR)==OK){child=proc_addr(n);break;}
  }
  if(child) {
    parent->p_shadow=base;
    *fork_previous(base)=forked;forked.owner=child;
    memset(&forked.resume,0,sizeof(forked.resume));forked.depth++;
    forked.parent=parent;
    *active=child;
    /* SENDREC keeps the child blocked until the broker replies with zero. */
    child->p_flags=RECEIVING;
  }
  restore_lock(saved);
  if(!child){cp32_shell_app_files_restore(files);fork_free(base);return -11;}
  return 0;
}

/* MINIX check_sig selectors. Only the current application family owns shadow
 * images here; kernel/server processes are never broadcast targets. Queue every
 * group member before cleanup can change the tree. Ancestors receive pending
 * delivery before their saved continuation is published. */
CP32_IRAM_EXT static int fork_signal_ready(struct cp32_fork_state *f)
{
  unsigned ready;int sig;
  if(!f->owner)return 0;
  ready=f->owner->p_app_pending&~f->owner->p_app_sigmask;
  for(sig=1;sig<=15;sig++)if(ready&(1U<<(sig-1)))return sig;
  return 0;
}
CP32_IRAM_EXT int cp32_fork_signal_ready(int child)
{
  return child ? (forked.live_child ? fork_signal_ready(fork_previous(forked.live_child->p_shadow)):0):
    fork_signal_ready(&forked);
}
CP32_IRAM_EXT static int fork_signal_match(int selector,int pid,int group,int caller_group)
{
  return selector>0 ? selector==pid:selector==-1 ||
    group==(selector==0 ? caller_group:-selector);
}
CP32_IRAM_EXT static int fork_signal_queue(struct cp32_fork_state *f,int pid,
    int selector,unsigned signal,int group)
{
  int n,count=0;unsigned bit=signal ? 1U<<(signal-1):0;
  if(fork_signal_match(selector,pid,f->owner->p_app_group ? f->owner->p_app_group:1,group)) {
    count=1;
    if(bit && !(f->owner->p_app_ignore&bit))f->owner->p_app_pending|=bit;
  }
  /* HANGING children participate in existence probes, never delivery. */
  if(!signal)for(n=LOW_USER+1;n<NR_PROCS;n++) {
    struct proc *p=proc_addr(n);
    if(p->p_flags==P_ZOMBIE && p->p_parent_pid==pid &&
       fork_signal_match(selector,p->p_pid,p->p_app_group,group))count++;
  }
  return count;
}
CP32_IRAM_EXT int cp32_fork_signal_target(struct proc *active,int pid,unsigned signal)
{
  int n,action,count=0,owner=active->p_pid,group=cp32_process_group(owner,0);
  unsigned bit;struct cp32_fork_state *f=&forked,*target;
  if(pid==(-2147483647-1) || signal>15)return -22;
  /* Retain immediate self/direct-child delivery for positive PID calls. */
  target=pid==active->p_pid ? &forked:
    forked.live_child && pid==forked.live_child->p_pid ? fork_previous(forked.live_child->p_shadow):0;
  action=target==&forked ? 1:2;
  if(pid>0 && target) {
    if(!signal)return 0;
    bit=1U<<(signal-1);
    if(target->owner->p_app_ignore&bit)return 0;
    if(target->owner->p_app_sigmask&bit){target->owner->p_app_pending|=bit;return 0;}
    return action;
  }
  for(;;) {
    count+=fork_signal_queue(f,owner,pid,signal,group);
    if(f->live_child)count+=fork_signal_queue(fork_previous(f->live_child->p_shadow),
        f->live_child->p_pid,pid,signal,group);
    if(!f->parent)break;
    owner=f->parent->p_pid;f=fork_previous(f->parent->p_shadow);
  }
  if(count) {
    if(!signal)return 0;
    if(cp32_fork_signal_ready(1))return 2;
    return cp32_fork_signal_ready(0) ? 1:0;
  }
  if(pid>0)for(n=0;n<NR_PROCS;n++)if(!(proc_addr(n)->p_flags&(P_SLOT_FREE|P_ZOMBIE)) && proc_addr(n)->p_pid==pid)
    return signal || n<=LOW_USER ? -1:0;
  return -3;
}

/* MINIX do_sigprocmask/do_sigpending: SIGKILL is never blocked. Signals
 * 1..15 use independent pending bits; first unblocked signal terminates. */
/* MINIX do_sigaction: ignoring a signal discards its pending instance. */
CP32_IRAM_EXT int cp32_fork_disposition(int signal,unsigned disposition)
{
  unsigned bit,old;
  if(signal<1 || signal>15 || signal==9 || disposition>1)return -22;
  bit=1U<<(signal-1);old=!!(forked.owner->p_app_ignore&bit);
  if(disposition){forked.owner->p_app_ignore|=bit;forked.owner->p_app_pending&=~bit;}
  else forked.owner->p_app_ignore&=~bit;
  return (int)old;
}
CP32_IRAM_EXT int cp32_fork_sigmask(int how,unsigned mask)
{
  unsigned old=forked.owner->p_app_sigmask,ready;int signal;
  if(how==4)return mask ? -22:(int)old;
  if(how<0 || how>2 || (mask&~0x7fffU))return -22;
  mask&=~(1U<<8);
  if(how==0)forked.owner->p_app_sigmask|=mask;
  else if(how==1)forked.owner->p_app_sigmask&=~mask;
  else forked.owner->p_app_sigmask=mask;
  ready=forked.owner->p_app_pending&~forked.owner->p_app_sigmask;
  for(signal=1;signal<=15;signal++)if(ready&(1U<<(signal-1)))return -1000-signal;
  return (int)old;
}
CP32_IRAM_EXT unsigned cp32_fork_sigpending(void){return forked.owner->p_app_pending;}

CP32_IRAM_EXT int cp32_fork_live(void){return forked.live_child!=NIL_PROC;}
CP32_IRAM_EXT void cp32_fork_request(message *m){*m=forked.resume;}

/* Cooperative shadow-image exchange, at any allocated fork depth. Both
 * descriptors are blocked receiving FS; no other app image is runnable.
 * Caller is FS on its own stack. No allocation or disk I/O under the lock. */
CP32_IRAM_EXT int cp32_fork_handoff(struct proc **active,const message *request)
{
  struct proc *from=*active,*to;
  unsigned base,i,*saved_image,*running=(unsigned *)IMAGE_BASE;
  struct cp32_fork_state old;
  int lock,result;
  if(forked.live_child){to=forked.live_child;base=forked.live_child->p_shadow;result=0;}
  else if(forked.parent) {
    to=forked.parent;base=forked.parent->p_shadow;result=forked.resume.m_type ? 0:from->p_pid;
  } else return 0;
  if(from->p_shadow || !base)panic("fork state",NO_NUM);
  if(from->p_flags!=RECEIVING || to->p_flags!=RECEIVING ||
     from->p_getfrom!=FS_PROC_NR || to->p_getfrom!=FS_PROC_NR)panic("swap state",NO_NUM);
  old=forked;
  saved_image=(unsigned *)(uintptr_t)(base<<CLICK_SHIFT);
  lock=lock_save();
  for(i=0;i<IMAGE_BYTES/sizeof(unsigned);i++) {
    unsigned word=running[i];running[i]=saved_image[i];saved_image[i]=word;
  }
  cp32_shell_app_files_exchange((struct cp32_fproc *)((unsigned char *)saved_image+IMAGE_BYTES));
  forked=*fork_previous(base);
  if(old.live_child) {
    old.live_child=NIL_PROC;
    forked.resume=*request;
  } else {forked.live_child=from;}
  *fork_previous(base)=old;
  from->p_shadow=base;to->p_shadow=0;
  *active=to;
  __asm__ volatile("memw\n\tisync" ::: "memory");
  restore_lock(lock);
  return result;
}

CP32_IRAM_EXT int cp32_fork_finish(struct proc **active,int status,unsigned signal)
{
  struct proc *child=*active,*parent=forked.parent;
  unsigned base=forked.parent->p_shadow;
  unsigned char *image=(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT);
  int saved;
  int pid=child->p_pid,group=cp32_process_group(child->p_pid,0);
  if(child->p_shadow || !base)panic("fork state",NO_NUM);
  cp32_shell_app_files_restore((struct cp32_fproc *)(image+IMAGE_BYTES));
  saved=lock_save();
  parent->child_utime+=child->user_time+child->child_utime;
  parent->child_stime+=child->sys_time+child->child_stime;
  child->user_time=child->sys_time=child->child_utime=child->child_stime=0;
  fork_discard(child->p_pid);
  if(cp32_proc_release(child)!=OK)panic("fork state",NO_NUM);
  child->p_pid=pid;child->p_parent_pid=parent->p_pid;child->p_app_group=group;
  child->p_exitstatus=signal ? (signal&127U):(((unsigned)status&255U)<<8);
  child->p_flags=P_ZOMBIE;
  memcpy((void *)IMAGE_BASE,image,IMAGE_BYTES);
  __asm__ volatile("memw\n\tisync" ::: "memory");
  parent->p_shadow=0;
  *active=parent;
  forked=*fork_previous(base);
  restore_lock(saved);
  fork_free(base);
  return pid;
}

CP32_IRAM_EXT int cp32_fork_wait(int pid,void *status,unsigned options)
{
  uintptr_t p=(uintptr_t)status;struct proc *z;int child,n,saved,value,bygroup=pid==0 || pid < -1;
  if(options&~1U)return -22;
  if(pid==(-2147483647-1))return -22;
  if(pid==0)pid=-cp32_process_group(0,0);
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    z=proc_addr(n);
    if(z->p_flags==P_ZOMBIE && z->p_parent_pid==forked.owner->p_pid &&
       ((!bygroup && (pid==-1 || pid==z->p_pid)) || (bygroup && -pid==z->p_app_group)))break;
  }
  if(n==NR_PROCS) {
    if(forked.live_child) {
      struct cp32_fork_state *child=fork_previous(forked.live_child->p_shadow);
      int group=child->owner->p_app_group ? child->owner->p_app_group:1;
      if((!bygroup && (pid==-1 || pid==forked.live_child->p_pid)) || (bygroup && -pid==group)) {
        if(p && (p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(int)>CP32_APP_TOP-p))return -14;
        return options ? 0:CP32_WAIT_LIVE;
      }
    }
    return -10;
  }
  if(p && (p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(int)>CP32_APP_TOP-p))return -14;
  saved=lock_save();
  value=z->p_exitstatus;
  if(p)memcpy(status,&value,sizeof(value));
  child=z->p_pid;
  if(cp32_proc_release(z)!=OK)panic("fork state",NO_NUM);
  restore_lock(saved);
  return child;
}
