/* First trusted foreground exec/wait path. MINIX MM exec/forkexit separates
 * image construction from runnable publication and keeps parent waiting until
 * exit. CP32 currently has one fixed app slot and no protected syscall ABI. */
#include "kernel.h"
#include "proc.h"
#include "application.h"
#include "../apps/hello/abi.h"
#define APP_NR (LOW_USER+1)
#define APP_WRITE 2001
#define APP_EXIT 2002
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_TEXT_DATA (CP32_APP_TEXT-0x6f0000U)
static int app_busy;
extern struct proc *current_proc;
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
    if(n==NR_PROCS) return next_pid;
  }
  return -1;
}

CP32_IRAM_EXT static int application_getpid(void)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(APP_NR)) return -1;
  memset(&m,0,sizeof(m)); m.m_type=APP_GETPID;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -1;
  return m.m_type;
}

CP32_IRAM_EXT static int application_getppid(void)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(APP_NR)) return -1;
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
  if(!app_busy || current_proc!=proc_addr(APP_NR)) return (void *)-1;
  memset(&m,0,sizeof(m)); m.m_type=APP_SBRK; m.m1_i1=increment;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR || m.m_type!=OK)
    return (void *)-1;
  return m.m1_p1;
}

CP32_IRAM_EXT static int application_write(const char *buffer,unsigned count)
{
  message m;
  int r;
  if(!app_busy || current_proc!=proc_addr(APP_NR) || count>256) return -1;
  memset(&m,0,sizeof(m));
  m.m_type=APP_WRITE; m.m1_p1=(char *)buffer; m.m1_i1=count;
  r=_sendrec(FS_PROC_NR,&m);
  return r==OK && m.m_source==FS_PROC_NR ? m.m_type : -1;
}
CP32_IRAM_EXT static int application_read(char *buffer,unsigned count)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(APP_NR) || count>64) return -1;
  memset(&m,0,sizeof(m)); m.m_type=APP_READ;
  m.m1_p1=buffer; m.m1_i1=count;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -1;
  return m.m_type;
}
CP32_IRAM_EXT static void application_exit(int status)
{
  message m;
  memset(&m,0,sizeof(m)); m.m_type=APP_EXIT; m.m1_i1=status;
  /* Parent consumes the exit request but never replies: reap the blocked
   * child before it can return through this continuation. */
  _sendrec(FS_PROC_NR,&m);
  panic("application exit returned",NO_NUM);
  for(;;) {}
}
static const struct cp32_app_services services={5,application_write,application_exit,application_getpid,application_getppid,application_sbrk,application_read};

CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,unsigned argc,const char *const argv[],cp32_app_output output,int *status)
{
  static const char *const environment[]={"HOME=/", "PATH=/boot", "USER=root"};
  struct cp32_image image;
  struct proc *child=proc_addr(APP_NR);
  uint32_t sp,heap_floor,heap_break;
  message m;
  int result,saved,pid,parent_pid;
  if(!argc || !argv || !output || !status || current_proc!=proc_addr(FS_PROC_NR) || app_busy ||
     child->p_flags!=P_SLOT_FREE) return -1;
  parent_pid=current_proc->p_pid;
  if(parent_pid<=0) return -1;
  app_busy=1;
  result=cp32_image_load(read,context,size,(unsigned char *)APP_TEXT_DATA,
                        (unsigned char *)CP32_APP_DATA,&image);
  if(result) { app_busy=0; return result; }
  heap_floor=(CP32_APP_DATA+image.data.memsz+15U)&~15U;
  heap_break=heap_floor;
  memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);
  /* Limit arguments to the top 512 bytes, leaving >3K for C/IPC/IRQ frames. */
  result=cp32_exec_stack((unsigned char *)(CP32_APP_TOP-512),512,
                        CP32_APP_TOP-512,argc,argv,
                        sizeof(environment)/sizeof(environment[0]),environment,&sp);
  if(result) { app_busy=0; return result; }
  /* Writes use the internal SRAM data alias, not flash/cache memory. */
  __asm__ volatile("memw\n\tisync" ::: "memory");
  saved=lock_save();
  pid=application_pid();
  if(pid<0) { restore_lock(saved); app_busy=0; return -1; }
  memset(child,0,sizeof(*child));
  child->p_nr=APP_NR; child->p_pid=pid;
  if(cp32_exec_frame(child,image.entry,sp,(reg_t)&services)!=OK)
    panic("application initial frame",NO_NUM);
  child->p_map[D].mem_vir=CP32_APP_DATA;
  child->p_map[D].mem_phys=CP32_APP_DATA>>CLICK_SHIFT;
  child->p_map[D].mem_len=(CP32_APP_TOP-CP32_APP_DATA)>>CLICK_SHIFT;
  {
    const char *name=argv[0], *scan;
    unsigned i=0;
    for(scan=name;*scan;scan++) if(*scan=='/') name=scan+1;
    while(name[i] && i<sizeof(child->p_name)-1) { child->p_name[i]=name[i]; i++; }
    child->p_name[i]=0;
  }
  lock_ready(child);
  restore_lock(saved);
  for(;;) {
    if(receive(APP_NR,&m)!=OK) panic("application wait failed",NO_NUM);
    if(m.m_type==APP_EXIT) {
      /* MINIX MM retains an eight-bit exit status. This bootstrap API
       * returns that value directly, not a POSIX encoded wait status. */
      *status=(unsigned)m.m1_i1 & 255U;
      saved=lock_save();
      /* SENDREC must leave the child waiting for FS's reply. */
      if(child->p_flags!=RECEIVING || child->p_getfrom!=FS_PROC_NR ||
         child->p_callerq!=NIL_PROC || child->p_sendlink!=NIL_PROC)
        panic("application reap state",NO_NUM);
      memset(child,0,sizeof(*child)); child->p_nr=APP_NR;
      child->p_flags=P_SLOT_FREE;
      if(bill_ptr==child) bill_ptr=proc_addr(LOW_USER);
      restore_lock(saved);
      app_busy=0;
      return 0;
    }
    result=m.m_type==APP_GETPID ? child->p_pid :
           m.m_type==APP_GETPPID ? parent_pid : -1;
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
    if(send(APP_NR,&m)!=OK) panic("application reply failed",NO_NUM);
  }
}
