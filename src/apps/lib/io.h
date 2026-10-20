#ifndef CP32_APP_IO_H
#define CP32_APP_IO_H
#include "../hello/abi.h"
/* Bootstrap standard streams: fd 0 reads console; 1/2 write console.
 * ABI v9 supplies readable/writable file descriptors 3..6. Errors set application-local cp32_errno. Return bytes transferred or -1; zero
 * count is a no-op for a valid stream. Read may return at most 64 bytes.
 * Writes collect short transfers and return progress if a later call fails. */
/* Positive MINIX error numbers; scoped names avoid kernel _SIGN macros.
 * Success/EOF does not clear the previous error. Single-threaded runtime. */
#define CP32_ERANGE 34
#define CP32_F_DUPFD 0 /* lowest free file slot >= minimum, skipping 0..2 */
#define CP32_F_GETFD 1
#define CP32_F_SETFD 2
#define CP32_F_GETFL 3
#define CP32_F_SETFL 4
#define CP32_FD_CLOEXEC 1
#define CP32_O_NONBLOCK 04000
/* Regular files only; occupied destinations are never replaced. */
int cp32_rename_noreplace(const char *source,const char *destination);
int cp32_link(const char *source,const char *destination);
/* chmod accepts rwx bits 0000..0777; umask does not apply. */
int cp32_chmod(const char *path,unsigned mode);
int cp32_mkdir(const char *path,unsigned mode);
/* Explicit offsets leave shared position unchanged; pwrite ignores APPEND. */
int cp32_pread(int fd,void *buffer,unsigned count,int offset);
int cp32_pwrite(int fd,const void *buffer,unsigned count,int offset);
int cp32_fchmod(int fd,unsigned mode);
int cp32_ftruncate(int fd,int length);
int cp32_rmdir(const char *path);
int cp32_unlink(const char *path);
int cp32_chdir(const char *path);
char *cp32_getcwd(char *buffer,unsigned size);
int cp32_umask(unsigned mask);
int cp32_fcntl(int fd,int command,unsigned argument);
#define CP32_EEXIST 17
#define CP32_ENOSPC 28
#define CP32_EFBIG 27
#define CP32_EROFS 30
#define CP32_EMLINK 31
#define CP32_EBUSY 16
#define CP32_ENOTEMPTY 39
#define CP32_O_RDONLY 0
#define CP32_O_WRONLY 1
#define CP32_O_RDWR 2
#define CP32_O_CREAT 00100
#define CP32_O_EXCL 00200
#define CP32_O_TRUNC 01000
#define CP32_O_APPEND 02000
#define CP32_E2BIG 7
#define CP32_ENOEXEC 8
#define CP32_ENOMEM 12
#define CP32_ENOENT 2
#define CP32_EACCES 13
#define CP32_ENOTDIR 20
#define CP32_EISDIR 21
#define CP32_EMFILE 24
#define CP32_ENAMETOOLONG 36
#define CP32_ENOSYS 38
#define CP32_ESPIPE 29
#define CP32_EIO 5
#define CP32_EBADF 9
#define CP32_EFAULT 14
#define CP32_EINVAL 22
extern int cp32_errno;
int cp32_io_init(const struct cp32_app_services *services);
/* cp32_open retains read-only compatibility; cp32_open_flags/creat select
 * v9 access/create/exclusive/truncate/append behavior. dup shares offsets.
 * Paths resolve against the launching shell's cwd.
 * Seek offsets/results use signed 32-bit bytes; whence 0=start,1=current,2=end.
 * Only application file descriptors can be closed; exit closes any leftovers. */
/* v8: successful exec never returns; failure preserves image and handles.
 * Up to nine argv entries including argv[0], 256 bytes total. Fixed initial
 * HOME/PATH/USER environment is recreated; this is not general execve. */
int cp32_execv(const char *path,const char *const argv[]);
int cp32_open(const char *path);
int cp32_open_flags(const char *path,unsigned flags,unsigned mode);
int cp32_creat(const char *path,unsigned mode);
int cp32_dup(int fd);
/* Select a file slot 3..6; standard streams are not descriptor-backed yet. */
int cp32_dup2(int source,int target);
/* v7: opendir returns an owned fd; readdir returns 1/0/-1 for entry/EOF/error.
 * Records are unchanged on EOF/error. Close directories with cp32_close;
 * cp32_lseek(fd,0,0) rewinds. stat does not consume a descriptor. */
int cp32_opendir(const char *path);
int cp32_readdir(int fd,struct cp32_app_dirent *entry);
int cp32_fstat(int fd,struct cp32_app_stat *info);
int cp32_stat(const char *path,struct cp32_app_stat *info);
int cp32_close(int fd);
int cp32_lseek(int fd,int offset,int whence);
int cp32_read(int fd,void *buffer,unsigned count);
int cp32_write(int fd,const void *buffer,unsigned count);
/* NUL-terminated line fragment, retaining newline; at most capacity-1 bytes.
 * Capacity must be >=2. Returns length, 0 at EOF, -1 on error (partial text
 * remains terminated). No read-ahead: excess input remains available. */
int cp32_readline(char *buffer,unsigned capacity);
#endif
