/* MINIX system.c:do_fork/do_xit lifecycle boundary. The caller holds the
 * scheduler lock. CP32 reserves unmapped slots until the Xtensa context is
 * complete; image allocation and parent/wait policy stay with the caller. */
#include "kernel.h"
#include "proc.h"
#include <minix/com.h>
extern struct proc *current_proc, *held_head;
extern volatile struct proc *cp32_irq_return_proc,*cp32_blocked_return_proc;
extern volatile struct proc *cp32_irq_saved_owner;

CP32_IRAM_EXT static int process_detached(struct proc *p)
{
  struct proc *q;int i,hops;
  if(p==current_proc || p==proc_ptr || p->p_nextready || p->p_nextheld ||
     p->p_int_held || p->p_int_blocked || p->p_callerq || p->p_sendlink)return 0;
  for(i=0;i<NQ;i++) {
    for(q=rdy_head[i],hops=0;q && hops++<NR_TASKS+NR_PROCS;q=q->p_nextready)
      if(q==p)return 0;
    if(q)return 0;
  }
  for(q=held_head,hops=0;q && hops++<NR_TASKS+NR_PROCS;q=q->p_nextheld)
    if(q==p)return 0;
  if(q)return 0;
  return 1;
}

CP32_IRAM_EXT static int process_pid_free(int pid)
{
  int n;
  if(pid<=0)return 0;
  for(n=0;n<NR_PROCS;n++)
    if(!(proc_addr(n)->p_flags&P_SLOT_FREE) && proc_addr(n)->p_pid==pid)return 0;
  return 1;
}

CP32_IRAM_EXT struct proc *cp32_proc_reserve(int pid)
{
  int n;struct proc *p;
  if(!process_pid_free(pid))return NIL_PROC;
  /* LOW_USER is the boot/init role; kernel tasks and servers are never reused. */
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    p=proc_addr(n);
    if(p->p_flags!=P_SLOT_FREE || !process_detached(p))continue;
    memset(p,0,sizeof(*p));p->p_nr=n;p->p_pid=pid;p->p_flags=NO_MAP;
    p->p_getfrom=ANY;return p;
  }
  return NIL_PROC;
}

/* Kernel half of MINIX SYS_FORK. MM must still install a distinct image/map
 * and complete the inherited receive before the child can execute. */
CP32_IRAM_EXT int cp32_proc_clone(int parent,int child,int pid,int broker)
{
  struct proc *p,*c;
  if(broker!=MM_PROC_NR && broker!=FS_PROC_NR)return EINVAL;
  if(parent<=LOW_USER || parent>=NR_PROCS || child<=LOW_USER ||
     child>=NR_PROCS || parent==child || pid<=0)return EINVAL;
  p=proc_addr(parent);c=proc_addr(child);
  if(p->p_pid<=0 || p->p_flags!=RECEIVING || p->p_getfrom!=broker ||
     !p->p_messbuf || !p->p_reg.pc || !p->p_reg.sp ||
     c->p_flags!=P_SLOT_FREE || !process_detached(c))return EINVAL;
  if(!process_pid_free(pid))return EINVAL;
  *c=*p;c->p_nr=child;c->p_pid=pid;c->p_flags=NO_MAP|RECEIVING;
  c->p_reg.a[2]=0;c->p_alarm=0;c->p_pending=0;c->p_pendcount=0;
  c->p_nextready=c->p_nextheld=c->p_callerq=c->p_sendlink=NIL_PROC;
  c->p_int_held=c->p_int_blocked=0;
  c->p_stguard=0;c->p_shadow=0;
  c->p_blocked_frame_result=0;
  c->user_time=c->sys_time=c->child_utime=c->child_stime=0;
  return OK;
}

CP32_IRAM_EXT int cp32_proc_publish(struct proc *p)
{
  unsigned base,bytes,sp;int segment;
  if(!p || p->p_nr<=LOW_USER || p->p_nr>=NR_PROCS || proc_addr(p->p_nr)!=p ||
     p->p_flags!=NO_MAP || p->p_pid<=0 || !process_detached(p))return EINVAL;
  sp=p->p_reg.sp;
  /* Flat internal SRAM: D and S must describe identity-mapped storage. */
  for(segment=D;segment<=S;segment++) {
    base=p->p_map[segment].mem_vir;
    if((base&((1U<<CLICK_SHIFT)-1)) ||
       p->p_map[segment].mem_phys!=(base>>CLICK_SHIFT) ||
       p->p_map[segment].mem_len>(0xffffffffU>>CLICK_SHIFT))return EINVAL;
    bytes=p->p_map[segment].mem_len<<CLICK_SHIFT;
    if(!bytes || bytes-1>0xffffffffU-base || sp<base || sp-base>=bytes)return EINVAL;
  }
  if(!p->p_reg.pc || (sp&15) ||
     p->p_reg.a[1]!=sp || p->p_reg.a[15]!=sp || p->p_reg.psw!=0x100)return EINVAL;
  p->p_flags=0;lock_ready(p);return OK;
}

CP32_IRAM_EXT int cp32_proc_release(struct proc *p)
{
  int nr;
  if(!p || p->p_nr<=LOW_USER || p->p_nr>=NR_PROCS || proc_addr(p->p_nr)!=p ||
     !process_detached(p))return EINVAL;
  if(p->p_flags!=NO_MAP &&
     !(p->p_flags==RECEIVING && p->p_getfrom==FS_PROC_NR))return EINVAL;
  nr=p->p_nr;
  /* This foreground broker is the parent. Cancellation of NO_MAP is not
   * an exit and must not contribute usage. MINIX system.c do_xit includes
   * descendants' accumulated times before clearing the child descriptor. */
  if(p->p_flags==RECEIVING) {
    struct proc *parent=proc_addr(FS_PROC_NR);
    parent->child_utime+=p->user_time+p->child_utime;
    parent->child_stime+=p->sys_time+p->child_stime;
  }
  if(bill_ptr==p)bill_ptr=proc_addr(FS_PROC_NR);
  if(cp32_irq_return_proc==p)cp32_irq_return_proc=NIL_PROC;
  if(cp32_blocked_return_proc==p)cp32_blocked_return_proc=NIL_PROC;
  if(cp32_irq_saved_owner==p)cp32_irq_saved_owner=NIL_PROC;
  memset(p,0,sizeof(*p));p->p_nr=nr;p->p_flags=P_SLOT_FREE;
  return OK;
}

/* MINIX exposes process-table state to diagnostic tools. CP32 has no /dev/kmem
 * interface: copy only public summary fields with local interrupts masked.
 * Never hold that lock while formatting or sending TTY output. */
CP32_IRAM_EXT int cp32_proc_info(int endpoint,struct cp32_proc_info *out)
{
  struct proc *p;int saved;unsigned i;
  if(!out || endpoint < -NR_TASKS || endpoint >= NR_PROCS)return EINVAL;
  saved=lock_save();p=proc_addr(endpoint);
  memset(out,0,sizeof(*out));
  if(p->p_flags&P_SLOT_FREE){restore_lock(saved);return 0;}
  out->endpoint=endpoint;out->pid=p->p_pid;out->flags=p->p_flags;
  out->running=p==current_proc;
  out->user_ticks=(unsigned)p->user_time;
  out->system_ticks=(unsigned)p->sys_time;
  out->child_user_ticks=(unsigned)p->child_utime;
  out->child_system_ticks=(unsigned)p->child_stime;
  for(i=0;i<sizeof(out->name)-1 && p->p_name[i];i++) {
    unsigned char c=p->p_name[i];
    out->name[i]=(c>=32 && c<127) ? c:'?';
  }
  if(!out->name[0]) {
    const char *name=endpoint==TTY_PROC_NR ? "tty":endpoint==MEM ? "disk":
      endpoint==CLOCK ? "clock":endpoint==SYSTASK ? "system":
      endpoint==MM_PROC_NR ? "mm":endpoint==FS_PROC_NR ? "shell":
      endpoint==IDLE ? "idle":endpoint==HARDWARE ? "hardware":"reserved";
    strcpy(out->name,name);
  }
  restore_lock(saved);return 1;
}
