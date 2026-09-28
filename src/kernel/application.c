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
#define APP_TEXT_DATA (CP32_APP_TEXT-0x6f0000U)
static int app_busy;
extern struct proc *current_proc;
extern int _sendrec(int, message *);

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
static const struct cp32_app_services services={1,application_write,application_exit};

CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,unsigned argc,const char *const argv[],cp32_app_output output,int *status)
{
  struct cp32_image image;
  struct proc *child=proc_addr(APP_NR);
  uint32_t sp;
  message m;
  int result,saved;
  if(!argc || !argv || !output || !status || current_proc!=proc_addr(FS_PROC_NR) || app_busy ||
     child->p_flags!=P_SLOT_FREE) return -1;
  app_busy=1;
  result=cp32_image_load(read,context,size,(unsigned char *)APP_TEXT_DATA,
                        (unsigned char *)CP32_APP_DATA,&image);
  if(result) { app_busy=0; return result; }
  memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);
  /* Limit arguments to the top 512 bytes, leaving >3K for C/IPC/IRQ frames. */
  result=cp32_exec_stack((unsigned char *)(CP32_APP_TOP-512),512,
                        CP32_APP_TOP-512,argc,argv,0,0,&sp);
  if(result) { app_busy=0; return result; }
  /* Writes use the internal SRAM data alias, not flash/cache memory. */
  __asm__ volatile("memw\n\tisync" ::: "memory");
  saved=lock_save();
  memset(child,0,sizeof(*child));
  child->p_nr=APP_NR; child->p_pid=APP_NR;
  child->p_reg.pc=image.entry; child->p_reg.psw=0x100;
  child->p_reg.sp=sp; child->p_reg.a[1]=sp; child->p_reg.a[15]=sp;
  child->p_reg.a[2]=(reg_t)&services;
  child->p_map[D].mem_vir=CP32_APP_DATA;
  child->p_map[D].mem_phys=CP32_APP_DATA>>CLICK_SHIFT;
  child->p_map[D].mem_len=(CP32_APP_TOP-CP32_APP_DATA)>>CLICK_SHIFT;
  strcpy(child->p_name,"hello");
  lock_ready(child);
  restore_lock(saved);
  for(;;) {
    if(receive(APP_NR,&m)!=OK) panic("application wait failed",NO_NUM);
    if(m.m_type==APP_EXIT) {
      *status=m.m1_i1;
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
    result=-1;
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
