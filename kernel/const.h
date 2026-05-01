/* General constants used by the kernel. */

#ifndef CONST_H
#define CONST_H

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

#endif