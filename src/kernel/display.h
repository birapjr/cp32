#ifndef CP32_DISPLAY_H
#define CP32_DISPLAY_H
void cardputer_display_init(void);
void cardputer_display_clear(void);
void cardputer_display_begin_batch(void);
void cardputer_display_end_batch(void);
void cardputer_display_putc(char c);
/* Echo-only: retain newline/scroll layout until the next visible output. */
void cardputer_display_defer_newline(void);
void cardputer_display_write(const char *s);
unsigned cardputer_display_faulted(void);
#endif
