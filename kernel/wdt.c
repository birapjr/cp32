#include "const.h"
#include <stdint.h>
#include "klib.h"

/* ── swd_disable ──────────────────────────────────────────────────────────────
 * Disables the Super WDT (SWD) by setting its AUTO_FEED bit (bit 31) and
 * strobing the FEED bit (bit 30) in a single write. Two separate |= writes
 * do not work — the hardware only latches the disable when both bits arrive
 * together.
 *
 * The SWD is clocked by the RTC slow clock (~150 kHz). At 240 MHz the CPU
 * can issue the write and move on before the RTC domain has seen even one
 * clock edge. We therefore repeat the write after a short delay to guarantee
 * the slow-clock domain has latched it. */
void swd_disable(void) {
    /* First write — unlatch write-protect, disable+feed, re-lock */
    RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
    RTC_SWD_CONF     = SWD_DISABLE_BIT | SWD_FEED_BIT;
    RTC_SWD_WPROTECT = 0;

    delay(100); /* ~700 ns at 240 MHz — enough for several 150 kHz cycles */

    /* Second write — ensures the RTC domain has definitely latched it */
    RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
    RTC_SWD_CONF     = SWD_DISABLE_BIT | SWD_FEED_BIT;
    RTC_SWD_WPROTECT = 0;
}

/* ── rtc_wdt_disable ──────────────────────────────────────────────────────────
 * Disables the RTC watchdog by zeroing WDTCONFIG0 (clears the WDT_EN bit 31
 * and all stage-action fields). We loop up to 10 times because the ROM
 * bootloader can re-arm the RTC WDT asynchronously; we keep trying until the
 * readback confirms the enable bit is clear. */
void rtc_wdt_disable(void) {
    for (int i = 0; i < 10; i++) {
        RTC_WDTWPROTECT = WDT_UNLOCK_KEY; /* unlock writes to RTC WDT regs  */
        RTC_WDTCONFIG0  = 0;              /* clear enable bit + all stages   */
        RTC_WDTFEED     = 1;              /* feed so the counter resets too   */
        RTC_WDTWPROTECT = WDT_LOCK_KEY;  /* re-lock immediately after write  */

        /* Read back to confirm the write stuck before exiting the loop */
        if ((RTC_WDTCONFIG0 & RTC_WDT_EN) == 0) break;

        delay(10); /* short pause — give RTC domain time before retry */
    }
}

/* ── wdt_disable_all ──────────────────────────────────────────────────────────
 * Disables all four watchdogs the ROM bootloader may have armed:
 *   TG0 WDT, TG1 WDT, RTC WDT, Super WDT. */
void wdt_disable_all(void) {
    /* TG0 WDT — unlock, zero config (disables), re-lock */
    TIMG0_WDTWPROTECT = WDT_UNLOCK_KEY;
    TIMG0_WDTCONFIG0  = 0;
    TIMG0_WDTWPROTECT = WDT_LOCK_KEY;

    /* TG1 WDT — same procedure */
    TIMG1_WDTWPROTECT = WDT_UNLOCK_KEY;
    TIMG1_WDTCONFIG0  = 0;
    TIMG1_WDTWPROTECT = WDT_LOCK_KEY;

    /* RTC WDT — uses retry loop because ROM may re-arm it */
    rtc_wdt_disable();

    /* Super WDT — must write disable+feed bits together, done twice */
    swd_disable();
}

/* ── wdt_feed_all ─────────────────────────────────────────────────────────────
 * Called every main-loop iteration to keep all watchdogs quiet.
 *
 * For TG0/TG1 we just feed (the ROM left them enabled; feeding resets their
 * counters). For the RTC WDT we also zero WDTCONFIG0 on every call because
 * the ROM can re-arm it between iterations. For the SWD we re-issue the
 * disable+feed to keep it latched. */
void wdt_feed_all(void) {
    /* TG0 — feed only (counter reset is enough while WDT stays disabled) */
    TIMG0_WDTWPROTECT = WDT_UNLOCK_KEY;
    TIMG0_WDTFEED     = 1;
    TIMG0_WDTWPROTECT = WDT_LOCK_KEY;

    /* TG1 — same */
    TIMG1_WDTWPROTECT = WDT_UNLOCK_KEY;
    TIMG1_WDTFEED     = 1;
    TIMG1_WDTWPROTECT = WDT_LOCK_KEY;

    /* RTC WDT — disable + feed every call in case ROM re-armed it */
    RTC_WDTWPROTECT = WDT_UNLOCK_KEY;
    RTC_WDTCONFIG0  = 0;   /* re-disable in case ROM wrote it back */
    RTC_WDTFEED     = 1;
    RTC_WDTWPROTECT = WDT_LOCK_KEY;

    /* Super WDT — re-issue disable+feed to keep the latch held */
    RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
    RTC_SWD_CONF     = SWD_DISABLE_BIT | SWD_FEED_BIT;
    RTC_SWD_WPROTECT = 0;
}

/* ── wdt_feed_super_wdt()────────────────────────────────────────────────────
 * strobe the Super WDT before anything else.
 * After a hard reset the ROM sets a short SWD timeout (~200 ms).
 * We must feed it immediately or we will reset before setup finishes. */
void wdt_feed_super_wdt() {
    RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
    RTC_SWD_CONF     = SWD_DISABLE_BIT | SWD_FEED_BIT;
    RTC_SWD_WPROTECT = 0;
}
