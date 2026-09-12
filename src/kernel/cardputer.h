#ifndef CP32_CARDPUTER_H
#define CP32_CARDPUTER_H

int cardputer_keyboard_probe(void);
int cardputer_keyboard_init(void);
int cardputer_keyboard_read_status(unsigned char *status, unsigned char *count);
int cardputer_keyboard_read_config(unsigned char *rows, unsigned char *cols0,
                                   unsigned char *cols1, unsigned char *cfg);
int cardputer_keyboard_read_event(unsigned char *event);
int cardputer_keyboard_interrupt_asserted(void);
unsigned cardputer_keyboard_bus_idle(void);
unsigned cardputer_keyboard_trace_stage(void);

#endif
#ifndef CP32_IRAM_EXT
#if defined(__XTENSA__)
#define CP32_IRAM_EXT __attribute__((section(".iram_ext.text")))
#else
#define CP32_IRAM_EXT
#endif
#endif
