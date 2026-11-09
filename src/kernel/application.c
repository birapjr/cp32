/* First trusted foreground exec/wait path. MINIX MM exec/forkexit separates
 * image construction from runnable publication and keeps parent waiting until
 * exit. CP32 currently has one foreground image and no protected syscall ABI. */
#include "kernel.h"
#include "proc.h"
#include "fork.h"
#include "application.h"
#include "../fs/fproc.h"
#include <minix/cp32_mm.h>
#include <minix/com.h>
#include "../apps/hello/abi.h"
static int app_endpoint=-1;
#define APP_WRITE 2001
#define APP_EXIT 2002
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_FILE 2007
#define APP_TEXT_DATA (CP32_APP_TEXT-0x6f0000U)
static int app_busy;
static struct {unsigned signal,cause,pc;} app_fault;
extern struct proc *current_proc;
extern volatile struct proc *cp32_irq_return_proc;
extern int _sendrec(int, message *);

/* MINIX forkexit.c uses bounded PIDs independent of reusable table slots.
 * Single-core caller holds the scheduler lock during allocation/publication. */
CP32_IRAM_EXT static int application_pid(void)
{
  static int next_pid=99;
  int tries,n;
  for(tries=0;tries<29901;tries++) {
    next_pid=next_pid<30000 ? next_pid+1 : 100;
    for(n=0;n<NR_PROCS;n++)
      if(!(proc_addr(n)->p_flags & P_SLOT_FREE) &&
         proc_addr(n)->p_pid==next_pid) break;
    if(n==NR_PROCS && !cp32_fork_pid_used(next_pid)) return next_pid;
  }
  return -1;
}

/* MINIX SYS_TIMES snapshots the actual caller, never a user-selected PID.
 * Nested lock_save in get_uptime preserves this outer lock. Flat Xtensa RAM
 * requires an explicit writable-image range check before copying the record. */
CP32_IRAM_EXT static int application_times(struct proc *caller,message *m)
{
  uintptr_t p=(uintptr_t)m->m1_p1;
  struct cp32_app_times t;
  int saved;
  if(m->m1_i2 || (unsigned)m->m1_i3!=sizeof(t))return -22;
  if(p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(t)>CP32_APP_TOP-p)return -14;
  saved=lock_save();
  t.user=caller->user_time;t.system=caller->sys_time;
  t.children_user=caller->child_utime;t.children_system=caller->child_stime;
  t.uptime=get_uptime();
  restore_lock(saved);
  memcpy((void *)p,&t,sizeof(t));
  return 0;
}

CP32_IRAM_EXT static int application_getpid(void)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(app_endpoint)) return -1;
  memset(&m,0,sizeof(m)); m.m_type=APP_GETPID;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -1;
  return m.m_type;
}

CP32_IRAM_EXT static int application_getppid(void)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(app_endpoint)) return -1;
  memset(&m,0,sizeof(m)); m.m_type=APP_GETPPID;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -1;
  return m.m_type;
}

/* MINIX break.c prevents data/stack collision; CP32 has a fixed stack floor.
 * Compute without signed overflow and leave the break unchanged on failure. */
CP32_IRAM_EXT static int application_break(uint32_t floor,uint32_t *brk,int increment)
{
  uint32_t amount;
  if(*brk<floor || *brk>CP32_APP_STACK) return -1;
  if(increment>=0) {
    amount=(unsigned)increment;
    if(amount>CP32_APP_STACK-*brk) return -1;
    *brk+=amount;
  } else {
    amount=(unsigned)(-(increment+1))+1U;
    if(amount>*brk-floor) return -1;
    *brk-=amount;
  }
  return 0;
}

CP32_IRAM_EXT static void *application_sbrk(int increment)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(app_endpoint)) return (void *)-1;
  memset(&m,0,sizeof(m)); m.m_type=APP_SBRK; m.m1_i1=increment;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR || m.m_type!=OK)
    return (void *)-1;
  return m.m1_p1;
}

CP32_IRAM_EXT static int application_write(const char *buffer,unsigned count)
{
  message m;
  int r;
  if(!app_busy || current_proc!=proc_addr(app_endpoint) || count>256) return -1;
  memset(&m,0,sizeof(m));
  m.m_type=APP_WRITE; m.m1_p1=(char *)buffer; m.m1_i1=count;
  r=_sendrec(FS_PROC_NR,&m);
  return r==OK && m.m_source==FS_PROC_NR ? m.m_type : -1;
}
CP32_IRAM_EXT static int application_read(char *buffer,unsigned count)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(app_endpoint) || count>64) return -1;
  memset(&m,0,sizeof(m)); m.m_type=APP_READ;
  m.m1_p1=buffer; m.m1_i1=count;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -1;
  return m.m_type;
}
CP32_IRAM_EXT static int application_file(unsigned op,int fd,void *buffer,unsigned arg)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(app_endpoint)) return -9;
  memset(&m,0,sizeof(m));m.m_type=APP_FILE;
  m.m1_i1=op;m.m1_i2=fd;m.m1_i3=arg;m.m1_p1=buffer;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -5;
  return m.m_type;
}
/* Validate the complete user span before the FS bridge dereferences it. */
CP32_IRAM_EXT __attribute__((noinline)) static int application_file_request(message *m)
{
  int r=cp32_fproc_validate(m->m1_i1,m->m1_p1,(unsigned)m->m1_i3,CP32_APP_DATA,CP32_APP_TOP);
  return r ? r:cp32_shell_app_file(m->m1_i1,m->m1_i2,m->m1_p1,(unsigned)m->m1_i3);
}

CP32_IRAM_EXT static void application_exit(int status)
{
  message m;
  memset(&m,0,sizeof(m)); m.m_type=APP_EXIT; m.m1_i1=status;
  /* Parent consumes the exit request but never replies: reap the blocked
   * child before it can return through this continuation. */
  _sendrec(FS_PROC_NR,&m);
  panic("app exit",NO_NUM);
  for(;;) {}
}
/* MINIX exception.c routes user faults to default signal termination. Values
 * below are ESP32-S3 core.h/corebits.h EXCCAUSE, not x86 vector numbers.
 * Called by irq.S with EXCM set, on a fresh stack inside the dying app slot.
 * Faults in callbacks/kernel text, nested handlers and unknown causes halt. */
CP32_IRAM_EXT struct proc *cp32_application_fault(unsigned cause,unsigned pc,unsigned ps)
{
  struct proc *owner;
  unsigned signal;
  if(!app_busy || app_endpoint<=LOW_USER || app_endpoint>=NR_PROCS ||
     pc<CP32_APP_TEXT || pc>=CP32_APP_TEXT_END || k_reenter ||
     (ps&15U) || !(ps&16U) || app_fault.signal)return NIL_PROC;
  owner=proc_addr(app_endpoint);
  if(owner!=current_proc || owner!=proc_ptr || owner->p_flags)return NIL_PROC;
  switch(cause) {
    case 0: signal=CP32_SIGILL;break;  /* illegal instruction: SIGILL */
    case 6: signal=CP32_SIGFPE;break;  /* integer divide by zero: SIGFPE */
    case 9: signal=CP32_SIGBUS;break; /* unaligned access: SIGBUS (MINIX number) */
    case 28:case 29:signal=CP32_SIGSEGV;break; /* prohibited load/store: SIGSEGV */
    default:return NIL_PROC;
  }
  if(cp32_exec_frame(owner,(reg_t)application_exit,CP32_APP_TOP-16,0)!=OK)return NIL_PROC;
  app_fault.signal=signal;app_fault.cause=cause;app_fault.pc=pc;
  cp32_irq_return_proc=owner;
  return owner;
}

/* Removable, bounded fault diagnostic in normal broker context, not an IRQ. */
CP32_IRAM_EXT static void cp32_trace_app_fault(int pid)
{
  static unsigned count;
  if(++count<=4 || count%64==0) {
    usbj_print("[CTX V180 fault pid=");usbj_print_u32((unsigned)pid);
    usbj_print(" signal=");usbj_print_u32(app_fault.signal);
    usbj_print(" cause=");usbj_print_u32(app_fault.cause);
    usbj_print(" pc=");usbj_print_hex32(app_fault.pc);usbj_print("]\r\n");
  }
}
/* Consume before restoration so a child's fault cannot leak into the parent. */
CP32_IRAM_EXT static unsigned application_fault_take(int pid)
{
  unsigned signal=app_fault.signal;
  if(signal)cp32_trace_app_fault(pid);
  app_fault.signal=0;
  return signal;
}

static const struct cp32_app_services services={33,application_write,application_exit,application_getpid,application_getppid,application_sbrk,application_read,application_file};

/* Transactional foreground exec. Stage all bytes in MM-owned temporary memory
 * while the child is blocked in SENDREC; publish only after every read succeeds.
 * No target-memory writes or fd-table changes occur on a recoverable failure. */
/* Size optimization also bounds this nested server frame; it contains no
 * interrupt entry/exit assembly and uses the ordinary call0 ABI. */
#if defined(__XTENSA__)
#pragma GCC push_options
#pragma GCC optimize ("Os")
#endif
CP32_IRAM_EXT __attribute__((noinline)) static int application_replace(struct proc *child,message *request,
                                            uint32_t *floor,uint32_t *brk)
{
  struct cp32_app_exec_request copy;
  struct cp32_image image;
  const char *argv[9],*env[]={"HOME=/","PATH=/boot","USER=root"};
  uintptr_t p=(uintptr_t)request->m1_p1;
  unsigned i,n=0,size,base;
  const char *name,*scan;
  uint32_t sp;
  unsigned char *stage;
  message mm;
  int fd,r,saved;
  if((unsigned)request->m1_i3!=sizeof(copy))return -22;
  if(p<CP32_APP_DATA || p>CP32_APP_TOP || sizeof(copy)>CP32_APP_TOP-p)return -14;
  memcpy(&copy,request->m1_p1,sizeof(copy));
  if(copy.path[255])return -36;
  if(!copy.argc || copy.argc>9)return -7;
  for(i=0;i<copy.argc;i++) {
    if(n>=sizeof(copy.args))return -7;
    argv[i]=copy.args+n;
    while(n<sizeof(copy.args) && copy.args[n])n++;
    if(n==sizeof(copy.args))return -7;
    n++;
  }
  fd=cp32_shell_exec_open(copy.path,&size);
  if(fd<0)return fd;
  memset(&mm,0,sizeof(mm));mm.m_type=CP32_MM_ALLOCATE;mm.m1_i1=32768U>>CLICK_SHIFT;
  r=_sendrec(MM_PROC_NR,&mm);
  if(r!=OK || mm.m_source!=MM_PROC_NR || mm.m_type!=OK || !mm.m1_i1) {
    cp32_shell_exec_close(fd);return -12;
  }
  base=(unsigned)mm.m1_i1;stage=(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT);
  r=cp32_image_load(cp32_shell_app_read,&fd,size,stage,stage+16384,&image);
  cp32_shell_exec_close(fd);
  if(r)r=r==-1 ? -8 : -5;
  if(!r) {
    memset(stage+28672,0,4096);
    if(cp32_exec_stack(stage+32768-512,512,CP32_APP_TOP-512,copy.argc,argv,3,env,&sp))r=-7;
  }
  if(!r) {
    /* Child cannot run until the new frame is complete. No IPC reply goes to
     * the old message buffer, which will cease to exist after these copies. */
    if(child->p_flags!=RECEIVING || child->p_getfrom!=FS_PROC_NR ||
       child->p_callerq!=NIL_PROC || child->p_sendlink!=NIL_PROC)
      panic("exec blocked state",NO_NUM);
    cp32_shell_app_files_exec(); /* commit only after successful staging */
    saved=lock_save();
    memcpy((void *)APP_TEXT_DATA,stage,16384);
    memcpy((void *)CP32_APP_DATA,stage+16384,16384);
    __asm__ volatile("memw\n\tisync" ::: "memory");
    *floor=(CP32_APP_DATA+image.data.memsz+15U)&~15U;*brk=*floor;
    if(cp32_exec_frame(child,image.entry,sp,(reg_t)&services)!=OK)panic("exec frame",NO_NUM);
    child->p_blocked_frame_valid=0;child->p_blocked_frame_result=0;
    child->p_blocked_frame_pc=child->p_blocked_frame_psw=child->p_blocked_frame_sp=0;
    child->p_messbuf=0;child->p_getfrom=ANY;child->p_sendto=0;
    name=copy.path;
    for(scan=name;*scan;scan++)if(*scan=='/')name=scan+1;
    for(i=0;name[i] && i<sizeof(child->p_name)-1;i++)child->p_name[i]=name[i];
    child->p_name[i]=0;
    child->p_flags=0;lock_ready(child);
    restore_lock(saved);
  }
  memset(&mm,0,sizeof(mm));mm.m_type=CP32_MM_RELEASE;mm.m1_i1=base;
  if(_sendrec(MM_PROC_NR,&mm)!=OK || mm.m_source!=MM_PROC_NR || mm.m_type!=OK)
    panic("exec staging release",NO_NUM);
  return r;
}

CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,unsigned argc,const char *const argv[],cp32_app_output output,int *status)
{
  static const char *const environment[]={"HOME=/", "PATH=/boot", "USER=root"};
  struct cp32_image image;
  struct proc *child;
  uint32_t sp,heap_floor,heap_break;
  message m,resume;
  int result,saved,pid,parent_pid;
  if(!argc || !argv || !output || !status || current_proc!=proc_addr(FS_PROC_NR) || app_busy) return -1;
  parent_pid=current_proc->p_pid;
  if(parent_pid<=0) return -1;
  saved=lock_save();pid=application_pid();
  child=pid<0 ? NIL_PROC:cp32_proc_reserve(pid);
  if(!child){restore_lock(saved);return -1;}
  app_endpoint=child->p_nr;app_busy=1;app_fault.signal=0;restore_lock(saved);
  result=cp32_image_load(read,context,size,(unsigned char *)APP_TEXT_DATA,
                        (unsigned char *)CP32_APP_DATA,&image);
  if(result)goto failed;
  heap_floor=(CP32_APP_DATA+image.data.memsz+15U)&~15U;
  heap_break=heap_floor;
  memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);
  /* Limit arguments to the top 512 bytes, leaving >3K for C/IPC/IRQ frames. */
  result=cp32_exec_stack((unsigned char *)(CP32_APP_TOP-512),512,
                        CP32_APP_TOP-512,argc,argv,
                        sizeof(environment)/sizeof(environment[0]),environment,&sp);
  if(result)goto failed;
  /* Writes use the internal SRAM data alias, not flash/cache memory. */
  __asm__ volatile("memw\n\tisync" ::: "memory");
  cp32_shell_app_files_reset();
  if(cp32_shell_app_files_start((unsigned)pid)) {result=-1;goto failed;}
  saved=lock_save();
  if(cp32_exec_frame(child,image.entry,sp,(reg_t)&services)!=OK)
    panic("app frame",NO_NUM);
  child->p_map[D].mem_vir=CP32_APP_DATA;
  child->p_map[D].mem_phys=CP32_APP_DATA>>CLICK_SHIFT;
  child->p_map[D].mem_len=(CP32_APP_TOP-CP32_APP_DATA)>>CLICK_SHIFT;
  child->p_map[S].mem_vir=CP32_APP_STACK;
  child->p_map[S].mem_phys=CP32_APP_STACK>>CLICK_SHIFT;
  child->p_map[S].mem_len=(CP32_APP_TOP-CP32_APP_STACK)>>CLICK_SHIFT;
  {
    const char *name=argv[0], *scan;
    unsigned i=0;
    for(scan=name;*scan;scan++) if(*scan=='/') name=scan+1;
    while(name[i] && i<sizeof(child->p_name)-1) { child->p_name[i]=name[i]; i++; }
    child->p_name[i]=0;
  }
  if(cp32_proc_publish(child)!=OK)panic("app publish",NO_NUM);
  restore_lock(saved);
  for(;;) {
    if(receive(app_endpoint,&m)!=OK) panic("app wait",NO_NUM);
    if(!cp32_shell_app_files_owner((unsigned)child->p_pid))panic("FS process owner",NO_NUM);
    result=m.m_type==APP_EXIT ? application_fault_take(child->p_pid):0;
app_exit:
    if(m.m_type==APP_EXIT && cp32_fork_live()) {
      m.m1_i2=result; /* preserve a foreground fault while its child drains */
      cp32_fork_handoff(&child,&parent_pid,&heap_floor,&heap_break,&m);
      app_endpoint=child->p_nr;m.m_type=0;
      if(send(app_endpoint,&m)!=OK)panic("yield exit",NO_NUM);
      continue;
    }
    if(m.m_type==APP_EXIT && cp32_fork_active()) {
      cp32_fork_request(&resume);
      m.m_type=cp32_fork_finish(&child,m.m1_i1,&parent_pid,&heap_floor,&heap_break,(unsigned)result);
      app_endpoint=child->p_nr;
      if(resume.m_type==APP_EXIT){m=resume;result=m.m1_i2;goto app_exit;}
      if(resume.m_type) m.m_type=resume.m1_i1==CP32_APP_WAITPID ?
        cp32_fork_wait(resume.m1_i2,resume.m1_p1,(unsigned)resume.m1_i3):0;
      if(send(app_endpoint,&m)!=OK)panic("fork reply",NO_NUM);
      continue;
    }
    if(m.m_type==APP_EXIT) {
      cp32_fork_reset();
      /* MINIX MM retains an eight-bit exit status. This bootstrap API
       * returns that value directly, not a POSIX encoded wait status. */
      *status=result ? 128+result:((unsigned)m.m1_i1 & 255U);
      cp32_shell_app_files_reset();
      saved=lock_save();
      /* SENDREC must leave the child waiting for FS's reply. */
      if(child->p_flags!=RECEIVING || child->p_getfrom!=FS_PROC_NR ||
         child->p_callerq!=NIL_PROC || child->p_sendlink!=NIL_PROC)
        panic("app reap state",NO_NUM);
      if(cp32_proc_release(child)!=OK)panic("app release",NO_NUM);
      restore_lock(saved);
      app_busy=0;app_endpoint=-1;
      return 0;
    }
    result=m.m_type==APP_GETPID ? child->p_pid :
           m.m_type==APP_GETPPID ? parent_pid : -1;
    if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_SIGNAL) {
      result=m.m1_p1 ? -22:cp32_fork_disposition(m.m1_i2,(unsigned)m.m1_i3);
    } else if(m.m_type==APP_FILE && (m.m1_i1==CP32_APP_SIGMASK || m.m1_i1==CP32_APP_SIGPENDING)) {
      result=m.m1_p1 ? -22:m.m1_i1==CP32_APP_SIGMASK ?
        cp32_fork_sigmask(m.m1_i2,(unsigned)m.m1_i3):
        (m.m1_i2 || m.m1_i3 ? -22:(int)cp32_fork_sigpending());
      if(result<=-1001 && result>=-1015){result=-1000-result;m.m_type=APP_EXIT;m.m1_i1=0;goto app_exit;}
    } else if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_KILL) {
      result=m.m1_p1 ? -22:cp32_fork_signal_target(child,m.m1_i2,(unsigned)m.m1_i3);
      if(result==1) {
        result=m.m1_i3;m.m_type=APP_EXIT;m.m1_i1=0;
        goto app_exit; /* normal exit cleanup, signal status, no caller reply */
      }
      if(result==2) {
        unsigned signal=(unsigned)m.m1_i3;
        /* The target is parked in FS receive. Restore it for the existing
         * cleanup path, without publishing a reply that could run its code. */
        cp32_fork_handoff(&child,&parent_pid,&heap_floor,&heap_break,&m);
        cp32_fork_finish(&child,0,&parent_pid,&heap_floor,&heap_break,signal);
        app_endpoint=child->p_nr;result=0;
      }
    } else if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_FORK) {
      saved=lock_save();pid=application_pid();restore_lock(saved);
      result=(m.m1_i2 || m.m1_p1 || m.m1_i3) ? -22:
        cp32_fork_start(&child,pid,&parent_pid,&heap_floor,&heap_break);
      app_endpoint=child->p_nr;
    } else if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_YIELD) {
      cp32_fork_request(&resume);
      result=(m.m1_i2 || m.m1_p1 || m.m1_i3) ? -22:0;
      if(!result && (cp32_fork_live() || !resume.m_type ||
          (resume.m_type==APP_FILE && resume.m1_i1==CP32_APP_YIELD)))
        result=cp32_fork_handoff(&child,&parent_pid,&heap_floor,&heap_break,&m);
      app_endpoint=child->p_nr;
    } else if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_WAITPID) {
      result=cp32_fork_wait(m.m1_i2,m.m1_p1,(unsigned)m.m1_i3);
      if(result==CP32_WAIT_LIVE) {
        result=cp32_fork_handoff(&child,&parent_pid,&heap_floor,&heap_break,&m);
        app_endpoint=child->p_nr;
      }
    } else if(m.m_type==APP_FILE && (m.m1_i1==CP32_APP_GETPGRP || m.m1_i1==CP32_APP_SETSID)) {
      result=(m.m1_i2 || m.m1_p1 || m.m1_i3) ? -22:
        cp32_process_group(child->p_pid,m.m1_i1==CP32_APP_SETSID);
    } else if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_TIMES) {
      result=application_times(child,&m);
    } else if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_EXEC) {
      result=application_replace(child,&m,&heap_floor,&heap_break);
      if(!result)continue; /* new image resumes at entry, never through send */
    } else if(m.m_type==APP_FILE) result=application_file_request(&m);
    if(m.m_type==APP_SBRK) {
      uint32_t old=heap_break;
      result=application_break(heap_floor,&heap_break,m.m1_i1);
      if(!result) {
        if(heap_break>old) memset((void *)old,0,heap_break-old);
        m.m1_p1=(char *)(uintptr_t)old;
      }
    }
    if(m.m_type==APP_READ && m.m1_i1>=0 && m.m1_i1<=64) {
      uint32_t p=(uint32_t)m.m1_p1,n=(unsigned)m.m1_i1;
      if(p>=CP32_APP_DATA && p<=CP32_APP_TOP && n<=CP32_APP_TOP-p)
        result=n ? cp32_shell_app_input(m.m1_p1,n) : 0;
    }
    if(m.m_type==APP_WRITE && m.m1_i1>=0 && m.m1_i1<=256) {
      uint32_t p=(uint32_t)m.m1_p1, n=(unsigned)m.m1_i1;
      if(p>=CP32_APP_DATA && p<=CP32_APP_TOP && n<=CP32_APP_TOP-p) {
        output(m.m1_p1,n); result=n;
      }
    }
    m.m_type=result;
    if(send(app_endpoint,&m)!=OK) panic("app reply",NO_NUM);
  }
failed:
  saved=lock_save();
  if(cp32_proc_release(child)!=OK)panic("app cancel",NO_NUM);
  app_busy=0;app_endpoint=-1;restore_lock(saved);
  return result;

}

#if defined(__XTENSA__)
#pragma GCC pop_options
#endif
