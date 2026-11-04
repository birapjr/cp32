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
static struct {
  struct proc *parent;
  unsigned base,floor,brk;
  int ppid,slot;
  /* MINIX HANGING children: retain status until wait, not the image or FDs.
   * Only the foreground parent owns this table; an executing child cannot
   * collect siblings. Reserve a slot before allocating or cloning. */
  struct {int pid,status;} waits[8];
} forked;
extern int _sendrec(int,message *);

CP32_IRAM_EXT static void fork_free(unsigned base)
{
  message m;
  memset(&m,0,sizeof(m));m.m_type=CP32_MM_RELEASE;m.m1_i1=base;
  if(_sendrec(MM_PROC_NR,&m)!=OK || m.m_source!=MM_PROC_NR || m.m_type!=OK)
    panic("fork free",NO_NUM);
}
CP32_IRAM_EXT int cp32_fork_pid_used(int pid)
{
  int n;for(n=0;n<8;n++)if(forked.waits[n].pid==pid)return 1;
  return 0;
}
CP32_IRAM_EXT int cp32_fork_active(void){return forked.parent!=NIL_PROC;}
CP32_IRAM_EXT void cp32_fork_reset(void){memset(forked.waits,0,sizeof(forked.waits));}

CP32_IRAM_EXT int cp32_fork_start(struct proc **active,int pid,int *ppid,uint32_t *floor,uint32_t *brk)
{
  struct proc *parent=*active,*child=NIL_PROC;
  struct cp32_fproc *files;
  unsigned base;unsigned char *image;
  int r,n,saved,slot=-1;
  message m;
  if(forked.parent || pid<=0)return -11;
  for(n=0;n<8;n++) {
    if(forked.waits[n].pid==pid)return -11;
    if(!forked.waits[n].pid)slot=n;
  }
  if(slot<0)return -11;
  memset(&m,0,sizeof(m));m.m_type=CP32_MM_ALLOCATE;
  m.m1_i1=(IMAGE_BYTES+sizeof(*files)+(1U<<CLICK_SHIFT)-1)>>CLICK_SHIFT;
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
    forked.slot=slot;forked.parent=parent;forked.base=base;forked.floor=*floor;forked.brk=*brk;forked.ppid=*ppid;
    *ppid=parent->p_pid;*active=child;
    /* SENDREC keeps the child blocked until the broker replies with zero. */
    child->p_flags=RECEIVING;
  }
  restore_lock(saved);
  if(!child){cp32_shell_app_files_restore(files);fork_free(base);return -11;}
  return 0;
}

CP32_IRAM_EXT int cp32_fork_finish(struct proc **active,int status,int *ppid,uint32_t *floor,uint32_t *brk)
{
  struct proc *child=*active,*parent=forked.parent;
  unsigned base=forked.base;
  unsigned char *image=(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT);
  int saved;
  int pid=child->p_pid;
  forked.waits[forked.slot].pid=pid;
  forked.waits[forked.slot].status=((unsigned)status&255U)<<8;
  cp32_shell_app_files_restore((struct cp32_fproc *)(image+IMAGE_BYTES));
  saved=lock_save();
  parent->child_utime+=child->user_time+child->child_utime;
  parent->child_stime+=child->sys_time+child->child_stime;
  child->user_time=child->sys_time=child->child_utime=child->child_stime=0;
  if(cp32_proc_release(child)!=OK)panic("fork reap",NO_NUM);
  memcpy((void *)IMAGE_BASE,image,IMAGE_BYTES);
  __asm__ volatile("memw\n\tisync" ::: "memory");
  *active=parent;*ppid=forked.ppid;*floor=forked.floor;*brk=forked.brk;
  forked.parent=NIL_PROC;forked.base=0;
  restore_lock(saved);
  fork_free(base);
  return pid;
}

CP32_IRAM_EXT int cp32_fork_wait(int pid,void *status,unsigned options)
{
  uintptr_t p=(uintptr_t)status;int child,n;
  if(options&~1U)return -22;
  if(pid!=-1 && pid<=0)return -22;
  if(forked.parent)return -10;
  for(n=0;n<8;n++)if(forked.waits[n].pid &&
      (pid==-1 || pid==forked.waits[n].pid))break;
  if(n==8)return -10;
  if(p && (p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(int)>CP32_APP_TOP-p))return -14;
  if(p)memcpy(status,&forked.waits[n].status,sizeof(int));
  child=forked.waits[n].pid;forked.waits[n].pid=0;
  return child;
}
