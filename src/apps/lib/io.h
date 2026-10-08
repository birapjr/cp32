#ifndef CP32_APP_IO_H
#define CP32_APP_IO_H
#include "../hello/abi.h"
/* Bootstrap standard streams: fd 0 reads console; 1/2 write console.
 * ABI v6 also supplies read-only file descriptors 3..6. Errors set application-local cp32_errno. Return bytes transferred or -1; zero
 * count is a no-op for a valid stream. Read may return at most 64 bytes.
 * Writes collect short transfers and return progress if a later call fails. */
/* Positive MINIX error numbers; scoped names avoid kernel _SIGN macros.
 * Success/EOF does not clear the previous error. Single-threaded runtime. */
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
/* Read-only open; paths resolve against the launching shell's cwd.
 * Seek offsets/results use signed 32-bit bytes; whence 0=start,1=current,2=end.
 * Only application file descriptors can be closed; exit closes any leftovers. */
int cp32_open(const char *path);
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
