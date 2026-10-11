/* First trusted foreground exec/wait path. MINIX MM exec/forkexit separates
 * image construction from runnable publication and keeps parent waiting until
 * exit. CP32 currently has one fixed app slot and no protected syscall ABI. */
#include "kernel.h"
#include "proc.h"
#include "application.h"
#include <minix/cp32_mm.h>
#include <minix/com.h>
#include "../apps/hello/abi.h"
#define APP_NR (LOW_USER+1)
#define APP_WRITE 2001
#define APP_EXIT 2002
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_FILE 2007
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
CP32_IRAM_EXT static int application_file(unsigned op,int fd,void *buffer,unsigned arg)
{
  message m;
  if(!app_busy || current_proc!=proc_addr(APP_NR)) return -9;
  memset(&m,0,sizeof(m));m.m_type=APP_FILE;
  m.m1_i1=op;m.m1_i2=fd;m.m1_i3=arg;m.m1_p1=buffer;
  if(_sendrec(FS_PROC_NR,&m)!=OK || m.m_source!=FS_PROC_NR) return -5;
  return m.m_type;
}
/* Validate the complete user span before the FS bridge dereferences it. */
CP32_IRAM_EXT static int application_file_request(message *m)
{
  unsigned op=m->m1_i1,n=(unsigned)m->m1_i3;
  uintptr_t p=(uintptr_t)m->m1_p1;
  if(op>CP32_APP_STAT) return -22;
  if(op==CP32_APP_OPEN || op==CP32_APP_OPENDIR || op==CP32_APP_READ ||
     op==CP32_APP_FSTAT || op==CP32_APP_READDIR || op==CP32_APP_STAT) {
    if((op==CP32_APP_OPEN || op==CP32_APP_OPENDIR) && (!n || n>256)) return -36;
    if(op==CP32_APP_READ && n>64) return -22;
    if(op==CP32_APP_FSTAT && n!=sizeof(struct cp32_app_stat))return -22;
    if(op==CP32_APP_READDIR && n!=sizeof(struct cp32_app_dirent))return -22;
    if(op==CP32_APP_STAT && n!=sizeof(struct cp32_app_stat_request))return -22;
    if(n && (p<CP32_APP_DATA || p>CP32_APP_TOP || n>CP32_APP_TOP-p)) return -14;
    if((op==CP32_APP_OPEN || op==CP32_APP_OPENDIR) && m->m1_p1[n-1]) return -22;
    if(op==CP32_APP_STAT && m->m1_p1[255])return -36;
  }
  return cp32_shell_app_file(op,m->m1_i2,m->m1_p1,n);
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
static const struct cp32_app_services services={8,application_write,application_exit,application_getpid,application_getppid,application_sbrk,application_read,application_file};

/* Transactional foreground exec. Stage all bytes in MM-owned temporary memory
 * while the child is blocked in SENDREC; publish only after every read succeeds.
 * No target-memory writes or fd-table changes occur on a recoverable failure. */
CP32_IRAM_EXT static int application_replace(struct proc *child,message *request,
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
  cp32_shell_app_files_reset();
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
      cp32_shell_app_files_reset();
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
    if(m.m_type==APP_FILE && m.m1_i1==CP32_APP_EXEC) {
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
    if(send(APP_NR,&m)!=OK) panic("application reply failed",NO_NUM);
  }
}
