/* function to log/write to serial */
#include <stdint.h>
#include "const.h"

static void usbj_putc(char c) {
    volatile uint32_t t = 200000;
    while (!(USBJ_EP1_CONF & USBJ_IN_EP_DATA_FREE))
        if (!--t) return;
    USBJ_EP1      = (uint8_t)c;
    USBJ_EP1_CONF |= USBJ_WR_DONE;
}

void usbj_print(const char *s) {
    while (*s) usbj_putc(*s++);
}

void usbj_print_u32(uint32_t v) {
    char buf[11];          /* max "4294967295\0" */
    char *p = buf + 10;
    *p = '\0';
    do {
        *--p = '0' + (v % 10);
        v /= 10;
    } while (v);
    usbj_print(p);
}

void usbj_print_hex32(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++)
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    buf[10] = '\0';
    usbj_print(buf);
}