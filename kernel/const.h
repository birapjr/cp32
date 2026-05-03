/* General constants used by the kernel.
 *
 * All peripheral registers are memory-mapped on the ESP32-S3.
 * Offsets were verified by live register dumps on real hardware —
 * several differ from what early versions of the TRM document. */

#ifndef CONST_H
#define CONST_H

/* ── USB Serial/JTAG ──────────────────────────────────────────────────────────
 * The ESP32-S3 has a built-in USB Serial/JTAG controller that exposes a CDC
 * serial port over USB without any external chip. We use it as the kernel
 * console. EP1 is the IN endpoint (device → host).
 *
 * USBJ_EP1      : write one byte here to queue it for transmission
 * USBJ_EP1_CONF : control/status register for EP1
 *   bit 0 (WR_DONE)         : strobe — write 1 to commit the queued byte
 *   bit 1 (IN_EP_DATA_FREE) : 1 when the FIFO has room for another byte */
#define USB_SERIAL_JTAG_BASE    0x60038000
#define USBJ_EP1      (*(volatile uint32_t*)(USB_SERIAL_JTAG_BASE + 0x00))
#define USBJ_EP1_CONF (*(volatile uint32_t*)(USB_SERIAL_JTAG_BASE + 0x04))
#define USBJ_WR_DONE          (1u << 0)
#define USBJ_IN_EP_DATA_FREE  (1u << 1)

/* ── System clock ─────────────────────────────────────────────────────────────
 * These two registers select the CPU and system bus clock sources/dividers.
 * We do not currently change the clock — the ROM bootloader leaves the CPU
 * running at 240 MHz which is what all our delay calibrations assume. */
#define SYSTEM_BASE      0x600C0000
#define SYS_CPU_PER_CONF (*(volatile uint32_t*)(SYSTEM_BASE + 0x08))
#define SYS_SYSCLK_CONF  (*(volatile uint32_t*)(SYSTEM_BASE + 0x58))

/* ── Timer Group 0 WDT (TG0) ─────────────────────────────────────────────────
 * The ESP32-S3 has two independent Timer Groups (TG0, TG1), each containing
 * a watchdog timer. The ROM bootloader arms both before handing control to
 * user code. If not fed or disabled they reset the chip.
 *
 * WDTCONFIG0  : main config register — writing 0 disables the WDT entirely
 * WDTFEED     : write any value to reset the WDT counter (feed it)
 * WDTWPROTECT : write-protect register — must write WDT_UNLOCK_KEY before
 *               any other WDT register can be modified, then lock again with
 *               WDT_LOCK_KEY (0) when done */
#define TIMG0_BASE        0x6001F000
#define TIMG0_WDTCONFIG0  (*(volatile uint32_t*)(TIMG0_BASE + 0x0048))
#define TIMG0_WDTFEED     (*(volatile uint32_t*)(TIMG0_BASE + 0x0060))
#define TIMG0_WDTWPROTECT (*(volatile uint32_t*)(TIMG0_BASE + 0x0064))

/* ── Timer Group 1 WDT (TG1) ─────────────────────────────────────────────────
 * Identical layout to TG0, different base address. */
#define TIMG1_BASE        0x60020000
#define TIMG1_WDTCONFIG0  (*(volatile uint32_t*)(TIMG1_BASE + 0x0048))
#define TIMG1_WDTFEED     (*(volatile uint32_t*)(TIMG1_BASE + 0x0060))
#define TIMG1_WDTWPROTECT (*(volatile uint32_t*)(TIMG1_BASE + 0x0064))

/* ── RTC WDT + Super WDT (SWD) ───────────────────────────────────────────────
 * The RTC controller (RTC_CNTL) houses two more watchdogs:
 *
 *  RTC WDT  — a conventional staged watchdog clocked from the RTC domain.
 *             The ROM arms it during boot. It fires reset cause 0x10.
 *
 *  Super WDT (SWD) — a background watchdog that cannot be fully disabled by
 *             normal means; it can only be neutered by setting its AUTO_FEED
 *             (disable) bit and strobing the feed bit simultaneously, and
 *             the write must be repeated because the RTC slow clock (~150 kHz)
 *             needs several cycles to latch the value. It fires reset cause
 *             0x12.
 *
 * IMPORTANT — offset correction:
 *   Early drafts of the ESP32-S3 TRM list WDTFEED at +0xA4 and WDTWPROTECT
 *   at +0xA8. Live register dumps on real hardware show those addresses hold
 *   WDTCONFIG2/3 (timer reload values). The real layout is:
 *
 *     +0x0098  WDTCONFIG0   (enable + stage actions)
 *     +0x009C  WDTCONFIG1   (stage 0 timeout)
 *     +0x00A0  WDTCONFIG2   (stage 1 timeout)
 *     +0x00A4  WDTCONFIG3   (stage 2 timeout)
 *     +0x00A8  WDTCONFIG4   (stage 3 timeout)
 *     +0x00AC  WDTFEED      ← was wrongly documented as +0xA4
 *     +0x00B0  WDTWPROTECT  ← was wrongly documented as +0xA8
 *     +0x00B4  SWD_CONF     ← was wrongly documented as +0xAC
 *     +0x00B8  SWD_WPROTECT ← was wrongly documented as +0xB0
 */
#define RTC_CNTL_BASE   0x60008000

/* RTC general options — not currently used but defined for completeness */
#define RTC_OPTIONS0    (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x0000))

/* RTC WDT registers */
#define RTC_WDTCONFIG0  (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x0098))
#define RTC_WDTCONFIG1  (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x009C))
#define RTC_WDTCONFIG2  (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00A0))
#define RTC_WDTCONFIG3  (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00A4))
#define RTC_WDTCONFIG4  (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00A8))
#define RTC_WDTFEED     (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00AC)) /* corrected from +0xA4 */
#define RTC_WDTWPROTECT (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00B0)) /* corrected from +0xA8 */

/* Super WDT registers */
#define RTC_SWD_CONF    (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00B4)) /* corrected from +0xAC */
#define RTC_SWD_WPROTECT (*(volatile uint32_t*)(RTC_CNTL_BASE + 0x00B8)) /* corrected from +0xB0 */

/* ── WDT keys ─────────────────────────────────────────────────────────────────
 * Writing WDT_UNLOCK_KEY to a WDTWPROTECT register allows writes to the
 * associated WDT config registers. Writing WDT_LOCK_KEY (0) re-locks them.
 * The TG0/TG1 and RTC WDT share the same key value. */
#define WDT_UNLOCK_KEY  0x50D83AA1u
#define WDT_LOCK_KEY    0x00000000u

/* RTC WDT enable bit — bit 31 of WDTCONFIG0.
 * Writing 0 to the whole register clears this and disables the WDT. */
#define RTC_WDT_EN      (1u << 31)

/* ── Super WDT bits ───────────────────────────────────────────────────────────
 * SWD_UNLOCK_KEY  : written to SWD_WPROTECT to allow writes to SWD_CONF
 * SWD_DISABLE_BIT : bit 31 of SWD_CONF — sets AUTO_FEED mode (disables SWD)
 * SWD_FEED_BIT    : bit 30 of SWD_CONF — strobes the feed; hardware clears
 *                   it automatically after latching. Must be written together
 *                   with SWD_DISABLE_BIT in a single write to latch the
 *                   disable — two separate writes do not work reliably. */
#define SWD_UNLOCK_KEY  0x8F1D312Au
#define SWD_DISABLE_BIT (1u << 31)
#define SWD_FEED_BIT    (1u << 30)

#endif /* CONST_H */