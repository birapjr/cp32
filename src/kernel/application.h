#ifndef CP32_APPLICATION_H
#define CP32_APPLICATION_H
#include "../mm/image.h"
struct cp32_fproc;
struct proc;
CP32_IRAM_EXT struct proc *cp32_application_window_owner(void);
CP32_IRAM_EXT int cp32_shell_app_poll(char *,unsigned);
CP32_IRAM_EXT int cp32_shell_clone_process(struct proc *,struct proc *);
CP32_IRAM_EXT void cp32_shell_drop_process(struct proc *);
/* Every live-window swap/restore must honor this synchronous I/O pin. */
CP32_IRAM_EXT unsigned cp32_application_window_busy(void);
CP32_IRAM_EXT struct proc *cp32_application_fault(unsigned cause,unsigned pc,unsigned ps);
CP32_IRAM_EXT struct cp32_fproc *cp32_shell_app_files_current(void);
int cp32_shell_app_files_fork(struct cp32_fproc *,unsigned);
void cp32_shell_app_files_exchange(struct cp32_fproc *);
void cp32_shell_app_files_restore(struct cp32_fproc *);
/* Launch an MM-owned foreground job; FS services every live process until
 * the root exits and all adopted descendants finish. Called only by FS. */
typedef void (*cp32_app_output)(const char *,unsigned);
CP32_IRAM_EXT int cp32_application_run(cp32_image_reader read,void *context,
    uint32_t size,unsigned argc,const char *const argv[],cp32_app_output output,int *status);
/* FS-owned foreground console bridge; returns byte count or error. */
CP32_IRAM_EXT int cp32_shell_app_input(char *buffer,unsigned count);
/* Foreground child owns a separate bounded fd namespace; FS context only. */
CP32_IRAM_EXT void cp32_shell_app_files_reset(void);
CP32_IRAM_EXT int cp32_shell_app_files_start(unsigned pid);
CP32_IRAM_EXT int cp32_shell_app_files_owner(unsigned pid);
CP32_IRAM_EXT void cp32_shell_app_files_exec(void);
CP32_IRAM_EXT int cp32_shell_app_file(unsigned op,int fd,void *buffer,unsigned arg);
CP32_IRAM_EXT int cp32_shell_exec_open(const char *path,unsigned *size);
CP32_IRAM_EXT void cp32_shell_exec_close(int fd);
CP32_IRAM_EXT int cp32_shell_app_read(void *context,uint32_t offset,unsigned char *buffer,unsigned count);
#endif
