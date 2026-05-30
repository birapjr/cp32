// kernel/serial.h
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void usbj_print(const char *str);
void usbj_print_u32(uint32_t val);
void usbj_print_hex32(uint32_t val);
void print_diagnostics(void);
void startup_usb_conn(void);

#endif
