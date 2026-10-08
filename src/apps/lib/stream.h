#ifndef CP32_APP_STREAM_H
#define CP32_APP_STREAM_H
#include "io.h"
#define CP32_EOF (-1)
/* One owner of stdin: do not mix buffered reads with cp32_read/readline.
 * Initialize once before reading. No seeking, output buffering or FILE ABI. */
struct cp32_input {
  unsigned char buffer[64];
  unsigned next, used;
  int eof, error, fd;
};
void cp32_input_init(struct cp32_input *stream);
void cp32_input_fd(struct cp32_input *stream,int fd);
int cp32_getc(struct cp32_input *stream);
char *cp32_fgets(char *text, unsigned capacity, struct cp32_input *stream);
int cp32_feof(const struct cp32_input *stream);
int cp32_ferror(const struct cp32_input *stream);
void cp32_clearerr(struct cp32_input *stream);
#endif
