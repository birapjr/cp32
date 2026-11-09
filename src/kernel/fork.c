/* MINIX forkexit.c and the shadow-image model: fixed-address Xtensa images
 * require the parent to remain blocked while its private copy is backed up.
 * Called only by the foreground FS broker. No user pointer chooses a process. */
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
  struct proc *parent;
  unsigned base,floor,brk;
  int ppid,slot,depth,group;
  struct proc *live_child;
  unsigned live_base,own_floor,own_brk;
  int own_ppid;
  unsigned sigmask,pending,ignore;
  message resume; /* parent IPC to complete after the child next yields/exits */
  /* MINIX HANGING children: retain status until wait, not the image or FDs.
   * Only the foreground parent owns this table; an executing child cannot
   * collect siblings. Reserve a slot before allocating or cloning. */
  struct {int pid,status,group;} waits[8];
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
 * that parent's private wait table. At most two suspended ancestors. */
CP32_IRAM_EXT static struct cp32_fork_state *fork_previous(unsigned base)
{
  return (struct cp32_fork_state *)((base<<CLICK_SHIFT)+IMAGE_BYTES+sizeof(struct cp32_fproc));
}
CP32_IRAM_EXT int cp32_fork_pid_used(int pid)
{
  struct cp32_fork_state *f=&forked,*peer;int n;
  for(;;) {
    for(n=0;n<8;n++)if(f->waits[n].pid==pid || (f->waits[n].pid && f->waits[n].group==pid))return 1;
    if(f->group==pid)return 1;
    /* A parked child points back to this parent: inspect it once, then
     * follow the parent's ancestor chain rather than revisiting the child. */
    if(f->live_child) {
      peer=fork_previous(f->live_base);
      if(peer->group==pid)return 1;
      for(n=0;n<8;n++)if(peer->waits[n].pid==pid ||
          (peer->waits[n].pid && peer->waits[n].group==pid))return 1;
    }
    if(!f->parent)return 0;
    f=fork_previous(f->base);
  }
}
CP32_IRAM_EXT int cp32_fork_active(void){return forked.parent!=NIL_PROC;}
CP32_IRAM_EXT void cp32_fork_reset(void){memset(forked.waits,0,sizeof(forked.waits));forked.group=0;forked.sigmask=forked.pending=forked.ignore=0;}

/* MINIX getset.c: all foreground launches inherit the bootstrap shell group1.
 * There is no controlling-terminal/session object in CP32 yet. */
CP32_IRAM_EXT int cp32_process_group(int pid,int create)
{
  int group=forked.group ? forked.group:1;
  if(!create)return group;
  if(group==pid)return -1; /* EPERM: a group leader cannot setsid. */
  forked.group=pid;return pid;
}

CP32_IRAM_EXT int cp32_fork_start(struct proc **active,int pid,int *ppid,uint32_t *floor,uint32_t *brk)
{
  struct proc *parent=*active,*child=NIL_PROC;
  struct cp32_fproc *files;
  unsigned base;unsigned char *image;
  int r,n,saved,slot=-1;
  message m;
  if(forked.live_child || forked.depth==2 || pid<=0 || cp32_fork_pid_used(pid))return -11;
  for(n=0;n<8;n++) {
    if(forked.waits[n].pid==pid)return -11;
    if(!forked.waits[n].pid)slot=n;
  }
  if(slot<0)return -11;
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
    forked.own_ppid=*ppid;forked.own_floor=*floor;forked.own_brk=*brk;
    *fork_previous(base)=forked;forked.pending=0;
    memset(forked.waits,0,sizeof(forked.waits));memset(&forked.resume,0,sizeof(forked.resume));forked.depth++;
    forked.slot=slot;forked.parent=parent;forked.base=base;forked.floor=*floor;forked.brk=*brk;forked.ppid=*ppid;
    *ppid=parent->p_pid;*active=child;
    /* SENDREC keeps the child blocked until the broker replies with zero. */
    child->p_flags=RECEIVING;
  }
  restore_lock(saved);
  if(!child){cp32_shell_app_files_restore(files);fork_free(base);return -11;}
  return 0;
}

/* MINIX check_sig: probe without delivery, never reply after self death.
 * Bounded policy: only self or a parked direct child can be terminated.
 * Ancestors/services are protected; group selection is not implemented. */
CP32_IRAM_EXT int cp32_fork_signal_target(struct proc *active,int pid,unsigned signal)
{
  int n,action;unsigned bit;
  struct cp32_fork_state *target;
  if(pid<=0 || signal>15)return -22;
  target=pid==active->p_pid ? &forked:
    forked.live_child && pid==forked.live_child->p_pid ? fork_previous(forked.live_base):0;
  action=target==&forked ? 1:2;
  if(target) {
    if(!signal)return 0;
    bit=1U<<(signal-1);
    if(target->ignore&bit)return 0;
    if(target->sigmask&bit){target->pending|=bit;return 0;}
    return action;
  }
  for(n=0;n<8;n++)if(forked.waits[n].pid==pid)return signal ? -3:0;
  for(n=0;n<NR_PROCS;n++)if(proc_addr(n)->p_flags!=P_SLOT_FREE && proc_addr(n)->p_pid==pid)
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
  bit=1U<<(signal-1);old=!!(forked.ignore&bit);
  if(disposition){forked.ignore|=bit;forked.pending&=~bit;}
  else forked.ignore&=~bit;
  return (int)old;
}
CP32_IRAM_EXT int cp32_fork_sigmask(int how,unsigned mask)
{
  unsigned old=forked.sigmask,ready;int signal;
  if(how==4)return mask ? -22:(int)old;
  if(how<0 || how>2 || (mask&~0x7fffU))return -22;
  mask&=~(1U<<8);
  if(how==0)forked.sigmask|=mask;
  else if(how==1)forked.sigmask&=~mask;
  else forked.sigmask=mask;
  ready=forked.pending&~forked.sigmask;
  for(signal=1;signal<=15;signal++)if(ready&(1U<<(signal-1)))return -1000-signal;
  return (int)old;
}
CP32_IRAM_EXT unsigned cp32_fork_sigpending(void){return forked.pending;}

CP32_IRAM_EXT int cp32_fork_live(void){return forked.live_child!=NIL_PROC;}
CP32_IRAM_EXT void cp32_fork_request(message *m){*m=forked.resume;}

/* Cooperative shadow-image exchange, at either supported fork level. Both
 * descriptors are blocked receiving FS; no other app image is runnable.
 * Caller is FS on its own stack. No allocation or disk I/O under the lock. */
CP32_IRAM_EXT int cp32_fork_handoff(struct proc **active,int *ppid,
    uint32_t *floor,uint32_t *brk,const message *request)
{
  struct proc *from=*active,*to;
  unsigned base,i,*saved_image,*running=(unsigned *)IMAGE_BASE;
  struct cp32_fork_state old;
  int lock,result;
  if(forked.live_child){to=forked.live_child;base=forked.live_base;result=0;}
  else if(forked.parent && forked.depth<=2) {
    to=forked.parent;base=forked.base;result=forked.resume.m_type ? 0:from->p_pid;
  } else return 0;
  if(from->p_flags!=RECEIVING || to->p_flags!=RECEIVING ||
     from->p_getfrom!=FS_PROC_NR || to->p_getfrom!=FS_PROC_NR)panic("swap state",NO_NUM);
  forked.own_ppid=*ppid;forked.own_floor=*floor;forked.own_brk=*brk;
  old=forked;
  saved_image=(unsigned *)(uintptr_t)(base<<CLICK_SHIFT);
  lock=lock_save();
  for(i=0;i<IMAGE_BYTES/sizeof(unsigned);i++) {
    unsigned word=running[i];running[i]=saved_image[i];saved_image[i]=word;
  }
  cp32_shell_app_files_exchange((struct cp32_fproc *)((unsigned char *)saved_image+IMAGE_BYTES));
  forked=*fork_previous(base);
  if(old.live_child) {
    old.live_child=NIL_PROC;old.live_base=0;
    forked.resume=*request;forked.floor=old.own_floor;forked.brk=old.own_brk;forked.ppid=old.own_ppid;
  } else {forked.live_child=from;forked.live_base=base;}
  *fork_previous(base)=old;
  *active=to;*ppid=forked.own_ppid;*floor=forked.own_floor;*brk=forked.own_brk;
  __asm__ volatile("memw\n\tisync" ::: "memory");
  restore_lock(lock);
  return result;
}

CP32_IRAM_EXT int cp32_fork_finish(struct proc **active,int status,int *ppid,uint32_t *floor,uint32_t *brk,unsigned signal)
{
  struct proc *child=*active,*parent=forked.parent;
  unsigned base=forked.base;
  unsigned char *image=(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT);
  int saved;
  int pid=child->p_pid,group=cp32_process_group(child->p_pid,0);
  int slot=forked.slot;
  cp32_shell_app_files_restore((struct cp32_fproc *)(image+IMAGE_BYTES));
  saved=lock_save();
  parent->child_utime+=child->user_time+child->child_utime;
  parent->child_stime+=child->sys_time+child->child_stime;
  child->user_time=child->sys_time=child->child_utime=child->child_stime=0;
  if(cp32_proc_release(child)!=OK)panic("fork reap",NO_NUM);
  memcpy((void *)IMAGE_BASE,image,IMAGE_BYTES);
  __asm__ volatile("memw\n\tisync" ::: "memory");
  *active=parent;*ppid=forked.ppid;*floor=forked.floor;*brk=forked.brk;
  forked=*fork_previous(base);
  forked.waits[slot].pid=pid;forked.waits[slot].group=group;
  forked.waits[slot].status=signal ? (signal&127U):(((unsigned)status&255U)<<8);
  restore_lock(saved);
  fork_free(base);
  return pid;
}

CP32_IRAM_EXT int cp32_fork_wait(int pid,void *status,unsigned options)
{
  uintptr_t p=(uintptr_t)status;int child,n,bygroup=pid==0 || pid < -1;
  if(options&~1U)return -22;
  if(pid==(-2147483647-1))return -22;
  if(pid==0)pid=-cp32_process_group(0,0);
  for(n=0;n<8;n++)if(forked.waits[n].pid &&
      ((!bygroup && (pid==-1 || pid==forked.waits[n].pid)) ||
       (bygroup && -pid==forked.waits[n].group)))break;
  if(n==8) {
    if(forked.live_child) {
      struct cp32_fork_state *child=fork_previous(forked.live_base);
      int group=child->group ? child->group:1;
      if((!bygroup && (pid==-1 || pid==forked.live_child->p_pid)) || (bygroup && -pid==group)) {
        if(p && (p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(int)>CP32_APP_TOP-p))return -14;
        return options ? 0:CP32_WAIT_LIVE;
      }
    }
    return -10;
  }
  if(p && (p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(int)>CP32_APP_TOP-p))return -14;
  if(p)memcpy(status,&forked.waits[n].status,sizeof(int));
  child=forked.waits[n].pid;forked.waits[n].pid=0;
  return child;
}
