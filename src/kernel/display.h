#ifndef CP32_DISPLAY_H
#define CP32_DISPLAY_H
void cardputer_display_init(void);
void cardputer_display_putc(char c);
void cardputer_display_write(const char *s);
unsigned cardputer_display_faulted(void);
void cardputer_display_boot_test(void);
#endif
