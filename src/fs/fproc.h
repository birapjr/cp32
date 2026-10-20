#ifndef CP32_FPROC_H
#define CP32_FPROC_H
#include "fs.h"
#include <stdint.h>
#define CP32_FPROC_FDS 4
/* MINIX fproc: client-owned namespace, separate from shared open descriptions.
 * Root is inode1 on the sole mounted volume; chroot/credentials are not exposed.
 * A zero PID is an inactive record. Slots are bound by the process lifecycle,
 * never selected by an application-provided PID. */
struct cp32_fproc {
  unsigned pid;
  unsigned short root_inode,uid,euid,gid,egid,mask; /* MINIX V2 widths */
  char cwd[256];
  int fds[CP32_FPROC_FDS]; /* backend descriptor+1, zero is closed */
  unsigned cloexec;
  int input; /* inherited input backend descriptor+1; zero means console */
};
CP32_FS_TEXT int cp32_fproc_clone(struct cp32_fproc *,const struct cp32_fproc *,unsigned);
CP32_FS_TEXT void cp32_fproc_exit(struct cp32_fproc *);
CP32_FS_TEXT void cp32_fproc_exec(struct cp32_fproc *);
CP32_FS_TEXT int cp32_fproc_chdir(struct cp32_fproc *,cp32_disk_reader,unsigned,const char *);
CP32_FS_TEXT int cp32_fproc_flags(struct cp32_fproc *,int,int,unsigned);
CP32_FS_TEXT int cp32_fproc_file(struct cp32_fproc *,cp32_disk_reader,cp32_disk_writer,unsigned,unsigned,int,void *,unsigned);
CP32_FS_TEXT int cp32_fs_errno(int);
CP32_FS_TEXT int cp32_fproc_exec_open(struct cp32_fproc *,cp32_disk_reader,unsigned,const char *,unsigned *);
CP32_FS_TEXT int cp32_fproc_validate(unsigned,char *,unsigned,uintptr_t,uintptr_t);
CP32_FS_TEXT int cp32_fproc_rmdir(struct cp32_fproc *,const struct cp32_fproc *,cp32_disk_reader,cp32_disk_writer,unsigned,const char *);
CP32_FS_TEXT int cp32_fproc_dup2(struct cp32_fproc *,int,int);
CP32_FS_TEXT int cp32_fproc_dupfd(struct cp32_fproc *,int,unsigned);
#endif
