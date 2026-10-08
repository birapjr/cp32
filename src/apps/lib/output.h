#ifndef CP32_APP_OUTPUT_H
#define CP32_APP_OUTPUT_H
#include "io.h"
/* Caller-owned stdout/stderr buffer. Initialize after cp32_io_init and flush
 * explicitly before exit or a blocking input operation. No automatic cleanup.
 * Keep one owner per stream; mixing buffered/raw writes can reorder output. */
struct cp32_output {
  unsigned char buffer[64];
  unsigned used;
  int fd, error;
};
int cp32_output_init(struct cp32_output *stream,int fd);
int cp32_putc(int character,struct cp32_output *stream);
int cp32_flush(struct cp32_output *stream);
int cp32_output_error(const struct cp32_output *stream);
/* Preserve unsent bytes; retry flush after resolving the underlying failure. */
void cp32_output_clearerr(struct cp32_output *stream);
#endif
