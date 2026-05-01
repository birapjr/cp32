// kernel/serial.h
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void usbj_print(const char *str);
void usbj_print_u32(uint32_t val);

#endif