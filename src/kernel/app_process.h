#ifndef CP32_APP_PROCESS_H
#define CP32_APP_PROCESS_H
/* MM-owned application runtime. Include kernel.h/proc.h before this header. */
#include <esp32s3/app_layout.h>
#include "../fs/fproc.h"
#include "../apps/hello/abi.h"
#define CP32_APP_IMAGE_BYTES 32768U
#define CP32_APP_IMAGE_BASE (CP32_APP_TEXT-0x6f0000U)
#define APP_WRITE 2001
#define APP_EXIT 2002
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_FILE 2007
#define CP32_MM_LAUNCH 2100
#define CP32_MM_FS_EXEC 2101
#define CP32_MM_FS_READ 2102
#define CP32_MM_JOB_DONE 2103
#define CP32_MM_FS_CLONE 2104
#define CP32_MM_FS_EXIT 2105
#define CP32_RUNTIME_WAIT 1U
#define CP32_RUNTIME_READ 2U
struct cp32_runtime_launch {
  unsigned entry,sp,heap,services;
  char name[16];
};
struct cp32_runtime_exec {
  struct proc *caller;
  struct cp32_app_exec_request request;
  unsigned backing,entry,sp,heap;
  char name[16];
};
/* Kept behind fproc.lifecycle to preserve the assembly-visible proc layout. */
struct cp32_fork_state {
  struct proc *owner;
  unsigned backing;
  unsigned waiting;
  message request;
  unsigned handlers[16];
  struct stackframe_s signal_frame;
  unsigned signal_mask,signal_active;
};
CP32_IRAM_EXT struct cp32_fork_state *cp32_runtime_state(struct proc *);
CP32_IRAM_EXT int cp32_runtime_is_app(const struct proc *);
CP32_IRAM_EXT int cp32_runtime_activate(struct proc *);
CP32_IRAM_EXT int cp32_runtime_can_run(struct proc *);
CP32_IRAM_EXT int cp32_runtime_needs_tick(void);
CP32_IRAM_EXT int cp32_runtime_read_pending(void);
/* MM dispatch returns1 if it handled the request, including deferred replies. */
CP32_IRAM_EXT int cp32_runtime_dispatch(message *);
CP32_IRAM_EXT void cp32_runtime_tick(void);
CP32_IRAM_EXT void cp32_runtime_interrupt(struct proc *);
CP32_IRAM_EXT void cp32_runtime_reply(struct proc *,int);
CP32_IRAM_EXT void cp32_init_task(void);
/* Implemented by the application/FS bridge. */
CP32_IRAM_EXT int cp32_shell_clone_launch(struct cp32_fproc *,unsigned);
CP32_IRAM_EXT void cp32_runtime_select_fs(struct proc *);
CP32_IRAM_EXT struct proc *cp32_application_window_owner(void);
CP32_IRAM_EXT unsigned cp32_application_window_busy(void);
/* Signal module: MM requests and final user return preparation. */
CP32_IRAM_EXT int cp32_runtime_signal_request(struct proc *,message *,int *);
CP32_IRAM_EXT void cp32_runtime_signal_return(struct proc *);
CP32_IRAM_EXT void cp32_runtime_signal_exec(struct proc *);
CP32_IRAM_EXT void cp32_runtime_signal_fork(struct proc *,struct proc *);
#endif
