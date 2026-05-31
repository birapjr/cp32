/* function to log/write to serial */
#include "kernel.h"

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

/* ── print_diagnostics ────────────────────────────────────────────────────────
 * Prints the live values of every watchdog register plus the reset-reason
 * field so we can confirm all WDTs are off and identify what caused the
 * previous reset.
 *
 * Expected good values after a clean disable:
 *   TG0/TG1 WDTCONFIG0  : 0x00000000  (WDT disabled)
 *   RTC_WDTCONFIG0      : 0x00000000  (WDT disabled)
 *   RTC_WDTWPROTECT     : 0x00000000  (locked — normal after WDT_LOCK_KEY)
 *   RTC_SWD_CONF        : 0xC0000000  (bit31=disable, bit30 already cleared
 *                                      by hardware after feed strobe)
 *   RTC_SWD_WPROTECT    : 0x00000000  (locked)
 *
 * The extended dump of raw offsets lets us cross-check that the macro
 * definitions in const.h are pointing at the right physical registers. */
void print_diagnostics(void) {
    usbj_print("-- WDT register dump --\r\n");
    usbj_print("TG0_WDTCONFIG0:  "); usbj_print_hex32(TIMG0_WDTCONFIG0);  usbj_print("\r\n");
    usbj_print("TG1_WDTCONFIG0:  "); usbj_print_hex32(TIMG1_WDTCONFIG0);  usbj_print("\r\n");
    usbj_print("RTC_WDTCONFIG0:  "); usbj_print_hex32(RTC_WDTCONFIG0);    usbj_print("\r\n");
    usbj_print("RTC_WDTWPROTECT: "); usbj_print_hex32(RTC_WDTWPROTECT);   usbj_print("\r\n");
    usbj_print("RTC_SWD_CONF:    "); usbj_print_hex32(RTC_SWD_CONF);      usbj_print("\r\n");
    usbj_print("RTC_SWD_WPROTECT:"); usbj_print_hex32(RTC_SWD_WPROTECT);  usbj_print("\r\n");

    /* Reset reason is stored in bits [5:0] of RTC_CNTL_RESET_STATE_REG.
     * Common values:
     *   0x01 = power-on reset
     *   0x10 = RTC WDT reset
     *   0x12 = Super WDT reset */
    usbj_print("reset reason:    ");
    usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0x0038));
    usbj_print("\r\n");

    /* Raw offset dump — cross-check that our macros hit the right addresses.
     * With correct offsets you should see:
     *   +0xAC = 0x00000000  (WDTFEED — write-only, reads 0)
     *   +0xB0 = 0x00000000  (WDTWPROTECT — locked = 0)
     *   +0xB4 = 0xC0000000  (SWD_CONF — disable+feed latched)
     *   +0xB8 = 0x00000000  (SWD_WPROTECT — locked = 0) */
    usbj_print("-- raw RTC offsets --\r\n");
    usbj_print("RTC+0xAC: "); usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0xAC)); usbj_print("\r\n");
    usbj_print("RTC+0xB0: "); usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0xB0)); usbj_print("\r\n");
    usbj_print("RTC+0xB4: "); usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0xB4)); usbj_print("\r\n");
    usbj_print("RTC+0xB8: "); usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0xB8)); usbj_print("\r\n");
    usbj_print("RTC+0xBC: "); usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0xBC)); usbj_print("\r\n");
    usbj_print("RTC+0xC0: "); usbj_print_hex32(*(volatile uint32_t*)(RTC_CNTL_BASE + 0xC0)); usbj_print("\r\n");
    usbj_print("-- end dump --\r\n");
}

/* wait for USB to enumerate on the host side.
 * The USB Serial/JTAG peripheral needs ~2 seconds after reset before
 * the host CDC driver is ready to receive bytes. We split the wait into
 * 20 chunks of ~5.8 ms and feed every watchdog in each chunk so no WDT
 * fires during the wait — the ROM can re-arm the RTC WDT asynchronously
 * so we must keep actively disabling it here too. */
void startup_usb_conn(void) {
    for (volatile int i = 0; i < 20; i++) {
        /* Super WDT — re-latch disable every chunk */
        RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
        RTC_SWD_CONF     = SWD_DISABLE_BIT | SWD_FEED_BIT;
        RTC_SWD_WPROTECT = 0;

        /* RTC WDT — re-disable in case ROM re-armed it */
        RTC_WDTWPROTECT = WDT_UNLOCK_KEY;
        RTC_WDTCONFIG0  = 0;
        RTC_WDTFEED     = 1;
        RTC_WDTWPROTECT = WDT_LOCK_KEY;

        /* TG0 / TG1 — feed to reset their counters */
        TIMG0_WDTWPROTECT = WDT_UNLOCK_KEY;
        TIMG0_WDTFEED     = 1;
        TIMG0_WDTWPROTECT = WDT_LOCK_KEY;

        TIMG1_WDTWPROTECT = WDT_UNLOCK_KEY;
        TIMG1_WDTFEED     = 1;
        TIMG1_WDTWPROTECT = WDT_LOCK_KEY;

        delay(100000); /* ~5.8 ms per chunk, 20 chunks = ~116 ms total */
    }
}

/**
 * Print kernel startup stage to serial connection
 */
void status_line(const char *msg, int width) {
    int widthF = width + BASE_PRINT_WIDTH;
    printk("%-*s...\r\n", widthF, msg);
}
