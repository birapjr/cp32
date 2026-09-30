#ifndef CP32_APP_IO_H
#define CP32_APP_IO_H
#include "../hello/abi.h"
/* Bootstrap standard streams: fd 0 reads console; 1/2 write console.
 * No file descriptors or errno yet. Return bytes transferred or -1; zero
 * count is a no-op for a valid stream. Read may return at most 64 bytes.
 * Writes collect short transfers and return progress if a later call fails. */
int cp32_io_init(const struct cp32_app_services *services);
int cp32_read(int fd,void *buffer,unsigned count);
int cp32_write(int fd,const void *buffer,unsigned count);
/* NUL-terminated line fragment, retaining newline; at most capacity-1 bytes.
 * Capacity must be >=2. Returns length, 0 at EOF, -1 on error (partial text
 * remains terminated). No read-ahead: excess input remains available. */
int cp32_readline(char *buffer,unsigned capacity);
#endif
