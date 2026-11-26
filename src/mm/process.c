/* MINIX MM forkexit/exec/wait policy, with software-resident Xtensa images.
 * All lifecycle requests execute in MM. FS owns descriptor/cache mutations;
 * scheduler activation only copies owned images under the scheduler lock. */
#include "mm.h"
#include "../kernel/app_process.h"
#include "../kernel/launch_trace.h"
#include "../kernel/application.h"

extern int _sendrec(int,message *);
extern struct proc *current_proc;
static struct proc *resident;
static unsigned live_apps,runtime_services;
/* Only MM changes these. The clock IRQ samples them to decide whether a
 * terminal-read retry is due. The TTY also uses read_waiters for its cursor. */
static volatile unsigned read_waiters,read_poll_active;
static int root_pid,root_status,root_exited;
static int init_waiting;
static message init_request;

CP32_IRAM_EXT struct cp32_fork_state *cp32_runtime_state(struct proc *p)
{
  struct cp32_fork_state *s;
  if(!p || p->p_nr<=LOW_USER || p->p_nr>=NR_PROCS ||
     proc_addr(p->p_nr)!=p || (p->p_flags&(P_SLOT_FREE|P_ZOMBIE)) || !p->p_files)return 0;
  s=p->p_files->lifecycle;
  return s && s->owner==p ? s:0;
}
CP32_IRAM_EXT int cp32_runtime_is_app(const struct proc *p)
{ return cp32_runtime_state((struct proc *)p)!=0; }
CP32_IRAM_EXT int cp32_runtime_can_run(struct proc *p)
{
  struct proc *pin=cp32_application_window_owner();
  return !cp32_runtime_is_app(p) || !cp32_application_window_busy() || p==pin;
}
/* Code reaches the internal instruction window through its writable data
 * alias. Complete the writes before this core can fetch the resident image. */
static inline __attribute__((always_inline)) void sync_application_text(void)
{
#if defined(__XTENSA__)
  __asm__ volatile("memw\n\tisync" ::: "memory");
#endif
}

/* The execution window and every backing are distinct, page-aligned SRAM.
 * A switch copies two complete images with level-1 interrupts masked. Use
 * word transfers so the next timer deadline leaves time to run the new PC. */
CP32_IRAM_EXT static void copy_application_image(void *destination, const void *source)
{
  enum { chunk = 16 };
  _Static_assert(CP32_APP_IMAGE_BYTES % chunk == 0, "image must use whole copy chunks");
  unsigned char *out = __builtin_assume_aligned(destination, sizeof(uint32_t));
  const unsigned char *in = __builtin_assume_aligned(source, sizeof(uint32_t));
  unsigned offset;

  for (offset = 0; offset < CP32_APP_IMAGE_BYTES; offset += chunk)
    __builtin_memcpy(out + offset, in + offset, chunk);
}

CP32_IRAM_EXT int cp32_runtime_activate(struct proc *p)
{
  struct cp32_fork_state *to=cp32_runtime_state(p),*from;
  /* Slots above INIT can execute only with an owned application image.
   * Missing metadata must never silently reuse another process's window. */
  if(!to)return p && p->p_nr<=LOW_USER ? 0:-22;
  if(!to->backing ||
     !cp32_mem_owned(to->backing,CP32_APP_IMAGE_BYTES>>CLICK_SHIFT,MM_PROC_NR))return -22;
  if(resident==p)return p->p_shadow ? -22:0;
  if(cp32_application_window_busy())return -16;
  if(p->p_shadow!=to->backing)return -22;
  if(resident) {
    from=cp32_runtime_state(resident);
    if(!from || resident->p_shadow || !from->backing ||
       !cp32_mem_owned(from->backing,CP32_APP_IMAGE_BYTES>>CLICK_SHIFT,MM_PROC_NR))return -22;
    cp32_trace_swap_begin();
    copy_application_image((void *)(uintptr_t)(from->backing<<CLICK_SHIFT),
                           (void *)CP32_APP_IMAGE_BASE);
    resident->p_shadow=from->backing;
  }
  copy_application_image((void *)CP32_APP_IMAGE_BASE,
                         (void *)(uintptr_t)(to->backing<<CLICK_SHIFT));
  sync_application_text();
  p->p_shadow=0;resident=p;
  cp32_trace_swap_end();
  return 0;
}

CP32_IRAM_EXT static unsigned allocate(unsigned bytes)
{
  int lock=lock_save();unsigned base=cp32_mem_alloc((bytes+(1U<<CLICK_SHIFT)-1)>>CLICK_SHIFT,MM_PROC_NR);
  restore_lock(lock);return base;
}
CP32_IRAM_EXT static void release(unsigned base)
{
  int lock=lock_save();
  if(cp32_mem_free(base,MM_PROC_NR)!=OK)panic("MM image free",NO_NUM);
  restore_lock(lock);
}
CP32_IRAM_EXT static int new_pid(void)
{
  static int next=99;int tries,n;
  for(tries=0;tries<29901;tries++) {
    next=next<30000 ? next+1:100;
    /* The shell retains this job identity until its orphan descendants drain,
     * even after the original root's process slot has been released. */
    if(next==root_pid)continue;
    for(n=0;n<NR_PROCS;n++)if(!(proc_addr(n)->p_flags&P_SLOT_FREE) &&
       (proc_addr(n)->p_pid==next || proc_addr(n)->p_app_group==next))break;
    if(n==NR_PROCS)return next;
  }
  return -1;
}
CP32_IRAM_EXT static struct proc *by_pid(int pid)
{
  int n;for(n=0;n<NR_PROCS;n++)if(!(proc_addr(n)->p_flags&P_SLOT_FREE) && proc_addr(n)->p_pid==pid)return proc_addr(n);
  return NIL_PROC;
}
CP32_IRAM_EXT static struct cp32_fproc *files_at(unsigned base)
{ return (struct cp32_fproc *)(uintptr_t)((base<<CLICK_SHIFT)+CP32_APP_IMAGE_BYTES); }
CP32_IRAM_EXT static unsigned process_allocate(void)
{
  unsigned base=allocate(CP32_APP_IMAGE_BYTES+sizeof(struct cp32_fproc)+sizeof(struct cp32_fork_state));
  if(base)memset(files_at(base),0,sizeof(struct cp32_fproc)+sizeof(struct cp32_fork_state));
  return base;
}
CP32_IRAM_EXT static void bind(struct proc *p,unsigned base)
{
  struct cp32_fproc *f=files_at(base);
  struct cp32_fork_state *s=(struct cp32_fork_state *)(f+1);
  p->p_files=f;f->lifecycle=s;s->owner=p;s->backing=base;
}
CP32_IRAM_EXT void cp32_runtime_reply(struct proc *p,int result)
{
  message reply;memset(&reply,0,sizeof(reply));reply.m_type=result;
  if(send(p->p_nr,&reply)!=OK)panic("MM app reply",NO_NUM);
}
CP32_IRAM_EXT static int fs_request(int operation,int first,int second,void *pointer)
{
  message m;memset(&m,0,sizeof(m));m.m_type=operation;m.m1_i1=first;m.m1_i2=second;m.m1_p1=pointer;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR)return -5;
  return m.m_type;
}
CP32_IRAM_EXT static void map_image(struct proc *p)
{
  p->p_map[D].mem_vir=CP32_APP_DATA;p->p_map[D].mem_phys=CP32_APP_DATA>>CLICK_SHIFT;
  p->p_map[D].mem_len=(CP32_APP_TOP-CP32_APP_DATA)>>CLICK_SHIFT;
  p->p_map[S].mem_vir=CP32_APP_STACK;p->p_map[S].mem_phys=CP32_APP_STACK>>CLICK_SHIFT;
  p->p_map[S].mem_len=(CP32_APP_TOP-CP32_APP_STACK)>>CLICK_SHIFT;
}
CP32_IRAM_EXT static int launch(message *m)
{
  struct cp32_runtime_launch copy;struct proc *p;unsigned base;int pid,lock,r;
  phys_bytes address;
  if(m->m_source!=FS_PROC_NR || live_apps || root_pid)return -16;
  address=numap(FS_PROC_NR,(vir_bytes)m->m1_p1,sizeof(copy));if(!address)return -14;
  memcpy(&copy,(void *)(uintptr_t)address,sizeof(copy));
  if(copy.entry<CP32_APP_TEXT || copy.entry>=CP32_APP_TEXT_END ||
     copy.sp<CP32_APP_STACK || copy.sp>=CP32_APP_TOP || (copy.sp&15) ||
     copy.heap<CP32_APP_DATA || copy.heap>CP32_APP_STACK || !copy.services)return -22;
  cp32_trace_launch(LAUNCH_ACCEPT,copy.sp);
  base=process_allocate();
  cp32_trace_launch(LAUNCH_ALLOC,base);
  if(!base)return -12;
  lock=lock_save();pid=new_pid();p=pid<0 ? NIL_PROC:cp32_proc_reserve(pid);restore_lock(lock);
  if(!p){release(base);return -11;}
  bind(p,base);
  cp32_trace_launch(LAUNCH_SLOT,p->p_nr);
  r=cp32_shell_clone_launch(p->p_files,(unsigned)pid);
  cp32_trace_launch(LAUNCH_FILES,r);
  /* FS cloning deliberately clears the private continuation pointer. */
  p->p_files->lifecycle=(struct cp32_fork_state *)(p->p_files+1);
  if(r) {lock=lock_save();cp32_proc_release(p);restore_lock(lock);release(base);return r;}
  lock=lock_save();
  p->p_parent_pid=proc_addr(FS_PROC_NR)->p_pid;p->p_app_group=p->p_parent_pid;
  p->p_heap_floor=p->p_heap_break=copy.heap;map_image(p);
  memcpy(p->p_name,copy.name,sizeof(p->p_name));p->p_name[sizeof(p->p_name)-1]=0;
  cp32_exec_frame(p,copy.entry,copy.sp,copy.services);
  copy_application_image((void *)(uintptr_t)(base<<CLICK_SHIFT),(void *)CP32_APP_IMAGE_BASE);
  /* FS loaded this first image directly into the execution window. Its first
   * activation takes the resident fast path, so synchronize it here. */
  sync_application_text();
  runtime_services=copy.services;resident=p;root_pid=pid;root_exited=0;root_status=0;live_apps=1;
  r=cp32_proc_publish(p);
  if(r!=OK)panic("MM app publish",r);
  cp32_trace_launch(LAUNCH_PUBLISH,pid);
  restore_lock(lock);m->m1_i1=p->p_nr;return 0;
}

CP32_IRAM_EXT static int fork_process(struct proc *parent)
{
  struct cp32_fork_state *ps=cp32_runtime_state(parent),*cs;
  struct proc *child=NIL_PROC;unsigned base;int n,pid,lock,r;
  if(!ps)return -22;
  base=process_allocate();if(!base)return -12;
  lock=lock_save();pid=new_pid();
  if(pid>0)for(n=LOW_USER+1;n<NR_PROCS;n++)if(proc_addr(n)->p_flags==P_SLOT_FREE &&
      cp32_proc_clone(parent->p_nr,n,pid,MM_PROC_NR)==OK){child=proc_addr(n);break;}
  if(child) {
    bind(child,base);cs=cp32_runtime_state(child);
    copy_application_image((void *)(uintptr_t)(base<<CLICK_SHIFT),
      parent->p_shadow ? (void *)(uintptr_t)(ps->backing<<CLICK_SHIFT):(void *)CP32_APP_IMAGE_BASE);
    child->p_shadow=base;
    (void)cs;
  }
  restore_lock(lock);
  if(!child){release(base);return -11;}
  r=fs_request(CP32_MM_FS_CLONE,parent->p_nr,child->p_nr,0);
  child->p_files->lifecycle=(struct cp32_fork_state *)(child->p_files+1);
  if(r) {
    lock=lock_save();child->p_shadow=0;child->p_flags=NO_MAP;
    if(cp32_proc_release(child)!=OK)panic("MM fork rollback",NO_NUM);
    restore_lock(lock);release(base);return r;
  }
  lock=lock_save();cp32_runtime_signal_fork(child,parent);
  child->p_flags=RECEIVING;live_apps++;restore_lock(lock);
  cp32_trace_launch(LAUNCH_FORK,child->p_nr);
  cp32_runtime_reply(child,0);
  return pid;
}

CP32_IRAM_EXT static int child_matches(struct proc *parent,struct proc *child,int selector)
{
  if(child->p_parent_pid!=parent->p_pid || (child->p_flags&P_SLOT_FREE))return 0;
  if(selector>0)return child->p_pid==selector;
  if(selector==-1)return 1;
  return child->p_app_group==(selector ? -selector:parent->p_app_group);
}
CP32_IRAM_EXT static int wait_result(struct proc *parent,message *request)
{
  int n,found=0,selector=request->m1_i2,lock,result;phys_bytes out=0;struct proc *child;
  if((unsigned)request->m1_i3&~1U || selector==(-2147483647-1))return -22;
  if(request->m1_p1) {
    out=numap(parent->p_nr,(vir_bytes)request->m1_p1,sizeof(int));if(!out)return -14;
  }
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    child=proc_addr(n);if(!child_matches(parent,child,selector))continue;
    found=1;if(child->p_flags!=P_ZOMBIE)continue;
    lock=lock_save();
    /* Resolve again while copies and resident ownership cannot change. */
    if(request->m1_p1) {
      out=numap(parent->p_nr,(vir_bytes)request->m1_p1,sizeof(int));
      if(!out){restore_lock(lock);return -14;}
      result=child->p_exitstatus;memcpy((void *)(uintptr_t)out,&result,sizeof(result));
    }
    result=child->p_pid;
    if(cp32_proc_release(child)!=OK)panic("MM wait release",NO_NUM);
    restore_lock(lock);return result;
  }
  if(!found && parent->p_nr!=LOW_USER)return -10;
  return request->m1_i3 ? 0:-1000;
}
CP32_IRAM_EXT static void wake_waiters(void)
{
  int n,r;struct proc *p;struct cp32_fork_state *s;
  if(init_waiting) {
    p=proc_addr(LOW_USER);r=wait_result(p,&init_request);
    if(r!=-1000){init_waiting=0;cp32_runtime_reply(p,r);}
  }
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    p=proc_addr(n);s=cp32_runtime_state(p);
    if(!s || s->waiting!=CP32_RUNTIME_WAIT)continue;
    r=wait_result(p,&s->request);
    if(r!=-1000){s->waiting=0;cp32_runtime_reply(p,r);}
  }
}
CP32_IRAM_EXT void cp32_runtime_interrupt(struct proc *p)
{
  struct cp32_fork_state *s=cp32_runtime_state(p);
  if(s && s->waiting) {
    if(s->waiting==CP32_RUNTIME_READ)read_waiters--;
    s->waiting=0;
    cp32_runtime_reply(p,-4);
  }
}
CP32_IRAM_EXT static int park(struct proc *p,struct cp32_fork_state *s,message *m,unsigned reason)
{
  int saved=lock_save();
  /* A signal may have arrived while this syscall was queued at MM. Its
   * return hook cannot run while the receive is blocked: do not strand it. */
  if(p->p_app_pending&~p->p_app_sigmask&~p->p_app_ignore) {
    restore_lock(saved);return -4;
  }
  s->request=*m;s->waiting=reason;
  if(reason==CP32_RUNTIME_READ)read_waiters++;
  restore_lock(saved);return 0;
}
CP32_IRAM_EXT static void job_complete(void)
{
  message m;int n;
  if(!root_pid || !root_exited || live_apps)return;
  /* INIT may have collected only one of several newly adopted zombies.
   * Keep the foreground job until every retained process slot is reaped. */
  for(n=LOW_USER+1;n<NR_PROCS;n++)if(proc_addr(n)->p_flags==P_ZOMBIE)return;
  memset(&m,0,sizeof(m));m.m_type=CP32_MM_JOB_DONE;m.m1_i1=root_status;
  root_pid=0;root_exited=0;
  if(send(FS_PROC_NR,&m)!=OK)panic("MM job completion",NO_NUM);
}
CP32_IRAM_EXT static void exit_process(struct proc *p,int status)
{
  struct cp32_fork_state *s=cp32_runtime_state(p);struct proc *parent;
  unsigned base=s->backing;int n,lock,pid=p->p_pid,ppid=p->p_parent_pid,group=p->p_app_group;
  unsigned uid=p->p_files->uid,euid=p->p_files->euid;
  unsigned encoded=p->p_exitstatus ? p->p_exitstatus&127U:((unsigned)status&255U)<<8;
  int root=pid==root_pid;
  if(fs_request(CP32_MM_FS_EXIT,p->p_nr,0,0))panic("MM FS exit",NO_NUM);
  lock=lock_save();
  parent=by_pid(ppid);
  if(parent){parent->child_utime+=p->user_time+p->child_utime;parent->child_stime+=p->sys_time+p->child_stime;}
  for(n=LOW_USER+1;n<NR_PROCS;n++)if(!(proc_addr(n)->p_flags&P_SLOT_FREE) && proc_addr(n)->p_parent_pid==pid)
    proc_addr(n)->p_parent_pid=1;
  if(resident==p)resident=NIL_PROC;
  p->p_shadow=0;p->p_flags=NO_MAP;
  if(cp32_proc_release(p)!=OK)panic("MM exit release",NO_NUM);
  if(!root){p->p_pid=pid;p->p_parent_pid=ppid;p->p_app_group=group;p->p_exitstatus=encoded;
    p->p_zombie_uid=uid;p->p_zombie_euid=euid;p->p_flags=P_ZOMBIE;}
  else {root_exited=1;root_status=(encoded&127U) ? 128+(encoded&127U):(encoded>>8)&255U;}
  live_apps--;restore_lock(lock);release(base);wake_waiters();job_complete();
}

CP32_IRAM_EXT static int exec_process(struct proc *p,message *request)
{
  struct cp32_runtime_exec stage;struct cp32_fork_state *s=cp32_runtime_state(p);
  phys_bytes address;unsigned base;int lock,r;
  if((unsigned)request->m1_i3!=sizeof(stage.request))return -22;
  lock=lock_save();address=numap(p->p_nr,(vir_bytes)request->m1_p1,sizeof(stage.request));
  if(address)memcpy(&stage.request,(void *)(uintptr_t)address,sizeof(stage.request));
  restore_lock(lock);if(!address)return -14;
  base=allocate(CP32_APP_IMAGE_BYTES);if(!base)return -12;
  stage.caller=p;stage.backing=base;
  r=fs_request(CP32_MM_FS_EXEC,0,0,&stage);
  if(!r) {
    lock=lock_save();
    copy_application_image((void *)(uintptr_t)(s->backing<<CLICK_SHIFT),(void *)(uintptr_t)(base<<CLICK_SHIFT));
    if(resident==p){resident=NIL_PROC;p->p_shadow=s->backing;}else p->p_shadow=s->backing;
    p->p_heap_floor=p->p_heap_break=stage.heap;
    memcpy(p->p_name,stage.name,sizeof(p->p_name));p->p_name[sizeof(p->p_name)-1]=0;
    /* All executables use the same trusted kernel service-table entry. */
    cp32_exec_frame(p,stage.entry,stage.sp,runtime_services);
    p->p_messbuf=0;p->p_getfrom=ANY;p->p_sendto=0;p->p_blocked_frame_valid=0;
    p->p_blocked_frame_pc=p->p_blocked_frame_psw=p->p_blocked_frame_sp=0;
    p->p_exitstatus=0;cp32_runtime_signal_exec(p);p->p_flags=0;lock_ready(p);
    restore_lock(lock);
  }
  release(base);return r;
}
CP32_IRAM_EXT static int process_break(struct proc *p,int increment,message *request)
{
  unsigned old=p->p_heap_break,next=old,amount;int lock;phys_bytes address;
  if(increment>=0) {amount=(unsigned)increment;if(old>CP32_APP_STACK || amount>CP32_APP_STACK-old)return -12;next+=amount;}
  else {amount=(unsigned)(-(increment+1))+1U;if(old<p->p_heap_floor || amount>old-p->p_heap_floor)return -12;next-=amount;}
  lock=lock_save();
  if(next>old){address=numap(p->p_nr,old,next-old);if(!address){restore_lock(lock);return -14;}memset((void *)(uintptr_t)address,0,next-old);}
  p->p_heap_break=next;request->m1_p1=(char *)(uintptr_t)old;restore_lock(lock);return 0;
}
CP32_IRAM_EXT static int process_times(struct proc *p,message *request)
{
  struct cp32_app_times t;phys_bytes address;int lock;
  if(request->m1_i2 || (unsigned)request->m1_i3!=sizeof(t))return -22;
  lock=lock_save();address=numap(p->p_nr,(vir_bytes)request->m1_p1,sizeof(t));
  if(!address){restore_lock(lock);return -14;}
  t.user=p->user_time;t.system=p->sys_time;t.children_user=p->child_utime;t.children_system=p->child_stime;t.uptime=get_uptime();
  memcpy((void *)(uintptr_t)address,&t,sizeof(t));restore_lock(lock);return 0;
}
CP32_IRAM_EXT int cp32_runtime_needs_tick(void)
{ return read_waiters!=0 && !read_poll_active; }
CP32_IRAM_EXT int cp32_runtime_read_pending(void)
{ return read_waiters!=0; }
CP32_IRAM_EXT void cp32_runtime_tick(void)
{
  int n,r;struct proc *p;struct cp32_fork_state *s;
  /* An MM poll can block in FS for several ticks. Keep the next MM receive
   * available for queued callers instead of accumulating a retry in flight. */
  read_poll_active=1;
  for(n=LOW_USER+1;n<NR_PROCS;n++) {
    p=proc_addr(n);s=cp32_runtime_state(p);if(!s || s->waiting!=CP32_RUNTIME_READ)continue;
    r=fs_request(CP32_MM_FS_READ,p->p_nr,s->request.m1_i1,s->request.m1_p1);
    if(r!=-11){read_waiters--;s->waiting=0;cp32_runtime_reply(p,r);}
  }
  read_poll_active=0;
  job_complete();
}
CP32_IRAM_EXT int cp32_runtime_dispatch(message *m)
{
  struct proc *p;struct cp32_fork_state *s;int r=-22,handled;
  if(m->m_type==CP32_MM_LAUNCH) {
    r=launch(m);m->m_type=r;
    cp32_trace_launch(LAUNCH_REPLY,r);
    if(send(m->m_source,m)!=OK)panic("MM launch reply",NO_NUM);return 1;
  }
  if(m->m_source==HARDWARE && m->m_type==HARD_INT){cp32_runtime_tick();return 1;}
  if(m->m_source==LOW_USER && m->m_type==APP_FILE && m->m1_i1==CP32_APP_WAITPID) {
    r=wait_result(proc_addr(LOW_USER),m);
    if(r==-1000){init_waiting=1;init_request=*m;}else cp32_runtime_reply(proc_addr(LOW_USER),r);
    /* With no live applications, no tick notification remains to finish the
     * job: the final INIT reap must publish completion itself. */
    job_complete();
    return 1;
  }
  if(m->m_source<=LOW_USER || m->m_source>=NR_PROCS)return 0;
  p=proc_addr(m->m_source);s=cp32_runtime_state(p);if(!s)return 0;
  if(m->m_type==APP_EXIT){
    cp32_trace_launch(LAUNCH_EXIT,m->m1_i1);
    exit_process(p,m->m1_i1);return 1;
  }
  if(m->m_type==APP_GETPID)r=p->p_pid;
  else if(m->m_type==APP_GETPPID)r=p->p_parent_pid;
  else if(m->m_type==APP_SBRK) {
    r=process_break(p,m->m1_i1,m);m->m_type=r;
    if(send(p->p_nr,m)!=OK)panic("MM break reply",NO_NUM);return 1;
  } else if(m->m_type==APP_READ) {
    if(m->m1_i1<0 || m->m1_i1>64)r=-22;
    else if(m->m1_i1 && !numap(p->p_nr,(vir_bytes)m->m1_p1,m->m1_i1))r=-14;
    else if(!m->m1_i1)r=0;
    else {r=park(p,s,m,CP32_RUNTIME_READ);if(!r){cp32_runtime_tick();return 1;}}
  } else if(m->m_type==APP_FILE) {
    handled=cp32_runtime_signal_request(p,m,&r);if(handled==2)return 1;
    if(!handled)switch(m->m1_i1) {
      case CP32_APP_FORK:
        r=(m->m1_i2 || m->m1_i3 || m->m1_p1) ? -22:fork_process(p);break;
      case CP32_APP_WAITPID:
        r=wait_result(p,m);if(r==-1000){r=park(p,s,m,CP32_RUNTIME_WAIT);if(!r)return 1;}break;
      case CP32_APP_YIELD:r=(m->m1_i2 || m->m1_i3 || m->m1_p1) ? -22:0;break;
      case CP32_APP_GETPGRP:r=(m->m1_i2 || m->m1_i3 || m->m1_p1) ? -22:p->p_app_group;break;
      case CP32_APP_SETSID:r=(m->m1_i2 || m->m1_i3 || m->m1_p1) ? -22:
        p->p_app_group==p->p_pid ? -1:(p->p_app_group=p->p_pid);break;
      case CP32_APP_TIMES:r=process_times(p,m);break;
      case CP32_APP_EXEC:r=exec_process(p,m);if(!r)return 1;break;
      default:r=-38;
    }
  }
  cp32_runtime_reply(p,r);return 1;
}
CP32_IRAM_EXT void cp32_init_task(void)
{
  message m;int status;
  for(;;) {
    memset(&m,0,sizeof(m));m.m_type=APP_FILE;m.m1_i1=CP32_APP_WAITPID;m.m1_i2=-1;m.m1_p1=(char *)&status;
    if(_sendrec(MM_PROC_NR,&m)!=OK)panic("INIT wait",NO_NUM);
  }
}
