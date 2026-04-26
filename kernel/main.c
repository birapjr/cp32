#include <stdint.h>

/* ── USB Serial/JTAG ──────────────────────────────────────────────── */
#define USB_SERIAL_JTAG_BASE     0x60038000
#define USBJ_EP1      (*(volatile uint32_t*)(USB_SERIAL_JTAG_BASE + 0x00))
#define USBJ_EP1_CONF (*(volatile uint32_t*)(USB_SERIAL_JTAG_BASE + 0x04))
#define USBJ_WR_DONE          (1u << 0)
#define USBJ_IN_EP_DATA_FREE  (1u << 1)

/* ── System clock ─────────────────────────────────────────────────── */
#define SYSTEM_BASE      0x600C0000
#define SYS_CPU_PER_CONF (*(volatile uint32_t*)(SYSTEM_BASE + 0x08))
#define SYS_SYSCLK_CONF  (*(volatile uint32_t*)(SYSTEM_BASE + 0x58))

/* Non-empty .bss and .data so linker symbols are pinned in DRAM */
static volatile uint32_t g_boot_count;            /* .bss  */
static volatile uint32_t g_magic = 0xC0320000u;   /* .data */


/* ── Timer Group 0 WDT (TG0) ─────────────────────────────────────── */
#define TIMG0_BASE          0x6001F000
#define TIMG0_WDTCONFIG0    (*(volatile uint32_t*)(TIMG0_BASE + 0x0048))
#define TIMG0_WDTFEED       (*(volatile uint32_t*)(TIMG0_BASE + 0x0060))
#define TIMG0_WDTWPROTECT   (*(volatile uint32_t*)(TIMG0_BASE + 0x0064))

/* ── Timer Group 1 WDT (TG1) ─────────────────────────────────────── */
#define TIMG1_BASE          0x60020000
#define TIMG1_WDTCONFIG0    (*(volatile uint32_t*)(TIMG1_BASE + 0x0048))
#define TIMG1_WDTFEED       (*(volatile uint32_t*)(TIMG1_BASE + 0x0060))
#define TIMG1_WDTWPROTECT   (*(volatile uint32_t*)(TIMG1_BASE + 0x0064))

/* ── RTC WDT ──────────────────────────────────────────────────────── */
#define RTC_CNTL_BASE       0x60008000
#define RTC_WDTCONFIG0      (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x0098))
#define RTC_WDTFEED         (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00A4))
#define RTC_WDTWPROTECT     (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00A8))

#define WDT_UNLOCK_KEY  0x50D83AA1u
#define WDT_LOCK_KEY    0x00000000u

/* ── Super WDT (SWD) ───────────────────────────────────── */
#define RTC_SWD_CONF        (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00AC))
#define RTC_SWD_WPROTECT    (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00B0))
#define SWD_UNLOCK_KEY      0x8F1D312Au
#define SWD_DISABLE_BIT     (1u << 31)

static void delay(volatile uint32_t n) { while (n--); }

static void usbj_putc(char c) {
    volatile uint32_t t = 200000;
    while (!(USBJ_EP1_CONF & USBJ_IN_EP_DATA_FREE))
        if (!--t) return;
    USBJ_EP1      = (uint8_t)c;
    USBJ_EP1_CONF |= USBJ_WR_DONE;
}

static void usbj_print(const char *s) {
    while (*s) usbj_putc(*s++);
}

static void usbj_print_u32(uint32_t v) {
    char buf[11];          /* max "4294967295\0" */
    char *p = buf + 10;
    *p = '\0';
    do {
        *--p = '0' + (v % 10);
        v /= 10;
    } while (v);
    usbj_print(p);
}

static void usbj_print_hex32(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++)
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    buf[10] = '\0';
    usbj_print(buf);
}

static void wdt_disable_all(void) {
    /* TG0 */
    TIMG0_WDTWPROTECT = WDT_UNLOCK_KEY;
    TIMG0_WDTCONFIG0 = 0;
    TIMG0_WDTWPROTECT = 0;

    /* TG1 */
    TIMG1_WDTWPROTECT = WDT_UNLOCK_KEY;
    TIMG1_WDTCONFIG0 = 0;
    TIMG1_WDTWPROTECT = 0;

    /* RTC */
    RTC_WDTWPROTECT = WDT_UNLOCK_KEY;
    RTC_WDTCONFIG0 = 0;
    RTC_WDTWPROTECT = 0;

    /* Super WDT */
    RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
    RTC_SWD_CONF |= SWD_DISABLE_BIT;
    RTC_SWD_WPROTECT = 0;

    /* Disable timer group completely */
    *(volatile uint32_t*)(TIMG0_BASE + 0x0048) = 0;
    *(volatile uint32_t*)(TIMG0_BASE + 0x0090) = 0; // possible clk gate / reset
}

void main(void) {
    wdt_disable_all();

    g_boot_count++;

    while (g_boot_count < 10) {
        if (g_boot_count == 4) {
            usbj_print("CP32 OS kernel booting...\r\n");
        }
        else if (g_boot_count > 4) {
            usbj_print("next...");
            usbj_print_u32(g_boot_count);
            usbj_print("\r\n");
        }
                
        g_boot_count++;

        delay(200000);
    }
}