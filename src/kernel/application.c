/* MINIX FS services files while MM owns process creation, execution and exit.
 * Trusted call0 applications use a fixed execution window with private backing. */
#include "kernel.h"
#include "proc.h"
#include "application.h"
#include "app_process.h"
#include "launch_trace.h"
#include <minix/com.h>
static int app_busy;
static volatile unsigned application_io_pins;
static struct proc *application_io_owner;
extern struct proc *current_proc;
extern volatile struct proc *cp32_irq_return_proc;
extern int _sendrec(int,message *);
CP32_IRAM_EXT unsigned cp32_application_window_busy(void){return application_io_pins;}
CP32_IRAM_EXT struct proc *cp32_application_window_owner(void){return application_io_owner;}
/* Select and pin atomically: an intervening timer must not substitute another
 * process between selecting a buffer and dereferencing it in FS. */
CP32_IRAM_EXT static int application_pin(struct proc *p)
{
  int saved=lock_save(),r;
  if(application_io_pins){restore_lock(saved);return -16;}
  r=cp32_runtime_activate(p);
  if(!r){application_io_owner=p;application_io_pins=1;}
  restore_lock(saved);
  if(!r)cp32_runtime_select_fs(p);
  return r;
}
CP32_IRAM_EXT static void application_unpin(void)
{
  int saved=lock_save();application_io_pins=0;application_io_owner=NIL_PROC;restore_lock(saved);
}
CP32_IRAM_EXT static int application_getpid(void)
{
  message m;if(!cp32_runtime_is_app(current_proc))return -1;
  memset(&m,0,sizeof(m));m.m_type=APP_GETPID;
  return _sendrec(MM_PROC_NR,&m)==OK && m.m_source==MM_PROC_NR ? m.m_type:-1;
}
CP32_IRAM_EXT static int application_getppid(void)
{
  message m;if(!cp32_runtime_is_app(current_proc))return -1;
  memset(&m,0,sizeof(m));m.m_type=APP_GETPPID;
  return _sendrec(MM_PROC_NR,&m)==OK && m.m_source==MM_PROC_NR ? m.m_type:-1;
}
CP32_IRAM_EXT static void *application_sbrk(int increment)
{
  message m;if(!cp32_runtime_is_app(current_proc))return (void *)-1;
  memset(&m,0,sizeof(m));m.m_type=APP_SBRK;m.m1_i1=increment;
  if(_sendrec(MM_PROC_NR,&m)!=OK || m.m_source!=MM_PROC_NR || m.m_type!=OK)return (void *)-1;
  return m.m1_p1;
}
CP32_IRAM_EXT static int application_write(const char *buffer,unsigned count)
{
  message m;if(!cp32_runtime_is_app(current_proc) || count>256)return -1;
  memset(&m,0,sizeof(m));m.m_type=APP_WRITE;m.m1_p1=(char *)buffer;m.m1_i1=count;
  return _sendrec(FS_PROC_NR,&m)==OK && m.m_source==FS_PROC_NR ? m.m_type:-5;
}
CP32_IRAM_EXT static int application_read(char *buffer,unsigned count)
{
  message m;if(!cp32_runtime_is_app(current_proc) || count>64)return -1;
  memset(&m,0,sizeof(m));m.m_type=APP_READ;m.m1_p1=buffer;m.m1_i1=count;
  return _sendrec(MM_PROC_NR,&m)==OK && m.m_source==MM_PROC_NR ? m.m_type:-5;
}
CP32_IRAM_EXT static int application_file(unsigned op,int fd,void *buffer,unsigned arg)
{
  message m;int server;
  if(!cp32_runtime_is_app(current_proc))return -9;
  server=op==CP32_APP_EXEC || op>=CP32_APP_FORK ? MM_PROC_NR:FS_PROC_NR;
  memset(&m,0,sizeof(m));m.m_type=APP_FILE;m.m1_i1=op;m.m1_i2=fd;m.m1_i3=arg;m.m1_p1=buffer;
  return _sendrec(server,&m)==OK && m.m_source==server ? m.m_type:-5;
}
/* Borrowed live-window span is pinned by the caller before validation. */
CP32_IRAM_EXT static int application_file_request(message *m)
{
  int r=cp32_fproc_validate(m->m1_i1,m->m1_p1,(unsigned)m->m1_i3,CP32_APP_DATA,CP32_APP_TOP);
  return r ? r:cp32_shell_app_file(m->m1_i1,m->m1_i2,m->m1_p1,(unsigned)m->m1_i3);
}
CP32_IRAM_EXT static void application_exit(int status)
{
  message m;memset(&m,0,sizeof(m));m.m_type=APP_EXIT;m.m1_i1=status;
  m.m1_i2=current_proc->p_exitstatus;
  _sendrec(MM_PROC_NR,&m);panic("app exit",NO_NUM);for(;;){}
}
CP32_IRAM_EXT struct proc *cp32_application_fault(unsigned cause,unsigned pc,unsigned ps)
{
  struct proc *owner=current_proc;unsigned signal;
  if(!cp32_runtime_is_app(owner) || pc<CP32_APP_TEXT || pc>=CP32_APP_TEXT_END ||
     k_reenter || (ps&15U) || !(ps&16U) || owner!=proc_ptr || owner->p_flags || owner->p_exitstatus)return NIL_PROC;
  switch(cause){case 0:signal=CP32_SIGILL;break;case 6:signal=CP32_SIGFPE;break;
    case 9:signal=CP32_SIGBUS;break;case 28:case 29:signal=CP32_SIGSEGV;break;default:return NIL_PROC;}
  if(cp32_exec_frame(owner,(reg_t)application_exit,CP32_APP_TOP-16,0)!=OK)return NIL_PROC;
  owner->p_fault_pc=pc;owner->p_fault_cause=cause;owner->p_exitstatus=signal;
  cp32_irq_return_proc=owner;return owner;
}
static const struct cp32_app_services services={37,application_write,application_exit,application_getpid,application_getppid,application_sbrk,application_read,application_file};
/* FS stages a complete executable into MM's allocation. It does not allocate
 * through MM (which is awaiting this reply), change a frame, or publish a task. */
CP32_IRAM_EXT static int application_stage(struct cp32_runtime_exec *q)
{
  struct cp32_image image;const char *argv[9],*env[]={"HOME=/","PATH=/boot","USER=root"};
  struct cp32_app_exec_request *copy=&q->request;
  uint32_t sp;unsigned i,n=0,size;int fd,r;const char *name,*scan;
  unsigned char *stage=(unsigned char *)(uintptr_t)(q->backing<<CLICK_SHIFT);
  if(copy->path[255])return -36;
  if(!copy->argc || copy->argc>9)return -7;
  for(i=0;i<copy->argc;i++){
    if(n>=sizeof(copy->args))return -7;argv[i]=copy->args+n;
    while(n<sizeof(copy->args) && copy->args[n])n++;
    if(n==sizeof(copy->args))return -7;n++;
  }
  fd=cp32_shell_exec_open(copy->path,&size);if(fd<0)return fd;
  r=cp32_image_load(cp32_shell_app_read,&fd,size,stage,stage+16384,&image);
  cp32_shell_exec_close(fd);
  if(r)return r==-1 ? -8:-5;
  memset(stage+28672,0,4096);
  if(cp32_exec_stack(stage+32768-512,512,CP32_APP_TOP-512,copy->argc,argv,3,env,&sp))return -7;
  q->sp=sp;q->entry=image.entry;q->heap=(CP32_APP_DATA+image.data.memsz+15U)&~15U;
  name=copy->path;for(scan=name;*scan;scan++)if(*scan=='/')name=scan+1;
  for(i=0;name[i] && i<sizeof(q->name)-1;i++)q->name[i]=name[i];q->name[i]=0;
  cp32_shell_app_files_exec();return 0;
}
CP32_IRAM_EXT static int application_span(const void *buffer,unsigned n)
{
  uintptr_t p=(uintptr_t)buffer;
  return p>=CP32_APP_DATA && p<=CP32_APP_TOP && n<=CP32_APP_TOP-p;
}
CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,unsigned argc,const char *const argv[],cp32_app_output output,int *status)
{
  static const char *const environment[]={"HOME=/","PATH=/boot","USER=root"};
  struct cp32_image image;struct cp32_runtime_launch launch;message m;
  uint32_t sp;unsigned i;int r,source;struct proc *caller;const char *name,*scan;
  if(!argc || !argv || !output || !status || current_proc!=proc_addr(FS_PROC_NR) || app_busy)return -1;
  app_busy=1;
  r=cp32_image_load(read,context,size,(unsigned char *)CP32_APP_IMAGE_BASE,(unsigned char *)CP32_APP_DATA,&image);
  if(r)goto failed;
  memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);
  memset(&launch,0,sizeof(launch));
  r=cp32_exec_stack((unsigned char *)(CP32_APP_TOP-512),512,CP32_APP_TOP-512,argc,argv,3,environment,&sp);
  if(r)goto failed;
  launch.sp=sp;launch.entry=image.entry;launch.heap=(CP32_APP_DATA+image.data.memsz+15U)&~15U;launch.services=(unsigned)&services;
  name=argv[0];for(scan=name;*scan;scan++)if(*scan=='/')name=scan+1;
  for(i=0;name[i] && i<sizeof(launch.name)-1;i++)launch.name[i]=name[i];
  memset(&m,0,sizeof(m));m.m_type=CP32_MM_LAUNCH;m.m1_p1=(char *)&launch;
  cp32_trace_launch(LAUNCH_LOAD,launch.entry);
  r=_sendrec(MM_PROC_NR,&m);
  cp32_trace_launch(LAUNCH_FS_RESUME,r==OK ? m.m_type:r);
  if(r!=OK || m.m_source!=MM_PROC_NR){r=-5;goto failed;}
  if(m.m_type<0){r=m.m_type;goto failed;}
  for(;;){
    if(receive(ANY,&m)!=OK)panic("FS receive",NO_NUM);source=m.m_source;
    if(source==MM_PROC_NR && m.m_type==CP32_MM_JOB_DONE){
      cp32_trace_launch(LAUNCH_DONE,m.m1_i1);
      *status=m.m1_i1;app_busy=0;return 0;
    }
    if(source==MM_PROC_NR && m.m_type==CP32_MM_FS_CLONE){
      r=cp32_shell_clone_process(proc_addr(m.m1_i1),proc_addr(m.m1_i2));
    }else if(source==MM_PROC_NR && m.m_type==CP32_MM_FS_EXIT){
      cp32_shell_drop_process(proc_addr(m.m1_i1));r=0;
      cp32_trace_launch(LAUNCH_FS_DROP,m.m1_i1);
    }else{
      caller=source==MM_PROC_NR ? (m.m_type==CP32_MM_FS_EXEC ?
        ((struct cp32_runtime_exec *)m.m1_p1)->caller:proc_addr(m.m1_i1)):proc_addr(source);
      r=cp32_runtime_is_app(caller) ? application_pin(caller):-3;
      if(!r){
        if(source==MM_PROC_NR && m.m_type==CP32_MM_FS_EXEC)r=application_stage((struct cp32_runtime_exec *)m.m1_p1);
        else if(source==MM_PROC_NR && m.m_type==CP32_MM_FS_READ)
          r=m.m1_i2<0 || m.m1_i2>64 || !application_span(m.m1_p1,m.m1_i2) ? -14:cp32_shell_app_poll(m.m1_p1,m.m1_i2);
        else if(source!=MM_PROC_NR && m.m_type==APP_FILE)r=application_file_request(&m);
        else if(source!=MM_PROC_NR && m.m_type==APP_WRITE){
          r=m.m1_i1<0 || m.m1_i1>256 || !application_span(m.m1_p1,m.m1_i1) ? -14:m.m1_i1;
          if(r>=0)output(m.m1_p1,r);
        }else r=-22;
        application_unpin();
      }
    }
    m.m_type=r;if(send(source,&m)!=OK)panic("FS reply",NO_NUM);
  }
failed:
  app_busy=0;return r;
}
