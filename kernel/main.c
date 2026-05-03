/*
 * Execution arrives here from the reset vector in mpx32.S after the stack
 * pointer is set up and .bss/.data are initialised. There is no C runtime,
 * no FreeRTOS, no IDF — every register is touched directly.
 *
 * Boot flow:
 *   1. Immediately strobe the Super WDT so it does not fire during startup.
 *   2. Disable all four watchdogs (TG0, TG1, RTC WDT, Super WDT).
 *   3. Keep feeding them in a chunked delay loop while USB enumerates.
 *   4. Print a register-dump so we can verify all WDTs are truly off.
 *   5. Enter the main kernel loop, feeding watchdogs every iteration.
 */

#include <stdint.h>
#include "const.h"
#include "serial.h"

/* ── Persistent kernel state ──────────────────────────────────────────────────
 * Placing at least one symbol in .bss and one in .data ensures the linker
 * script pins both sections into DRAM and that the startup code in mpx32.S
 * actually zeroes / copies them. */
static volatile uint32_t g_boot_count;           /* .bss — zeroed by startup */
static volatile uint32_t g_magic = 0xC0320000u;  /* .data — copied by startup */

/* ── delay ────────────────────────────────────────────────────────────────────
 * Busy-wait loop calibrated for 240 MHz with -O0.
 * Each iteration is ~7 CPU cycles, so delay(200000) ≈ 5.8 ms. */
static void delay(volatile uint32_t n) { while (n--); }

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
static void swd_disable(void) {
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
static void rtc_wdt_disable(void) {
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
static void wdt_disable_all(void) {
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
static void wdt_feed_all(void) {
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
static void print_diagnostics(void) {
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

/* ── main ─────────────────────────────────────────────────────────────────────
 * Kernel entry point — called by the reset vector in mpx32.S. */
void main(void) {
    /* Step 1 — strobe the Super WDT before anything else.
     * After a hard reset the ROM sets a short SWD timeout (~200 ms).
     * We must feed it immediately or we will reset before setup finishes. */
    RTC_SWD_WPROTECT = SWD_UNLOCK_KEY;
    RTC_SWD_CONF     = SWD_DISABLE_BIT | SWD_FEED_BIT;
    RTC_SWD_WPROTECT = 0;

    /* Step 2 — disable all watchdogs */
    wdt_disable_all();
    wdt_feed_all();

    /* Step 3 — wait for USB to enumerate on the host side.
     * The USB Serial/JTAG peripheral needs ~2 seconds after reset before
     * the host CDC driver is ready to receive bytes. We split the wait into
     * 20 chunks of ~5.8 ms and feed every watchdog in each chunk so no WDT
     * fires during the wait — the ROM can re-arm the RTC WDT asynchronously
     * so we must keep actively disabling it here too. */
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

    /* Step 4 — final disable pass then print diagnostics */
    wdt_disable_all();
    wdt_feed_all();
    print_diagnostics();

    /* Step 5 — one more disable pass right before the main loop so any
     * ROM activity that happened during the diagnostic prints is cleared */
    wdt_disable_all();
    wdt_feed_all();

    /* ── Main kernel loop ─────────────────────────────────────────────────── */
    g_boot_count++;
    while (1) {
        /* Feed/disable all watchdogs at the top of every iteration.
         * delay(200000) ≈ 5.8 ms — well within any WDT timeout. */
        wdt_feed_all();
        swd_disable(); /* extra SWD latch — cheap insurance */

        if (g_boot_count == 4) {
            usbj_print("CP32 OS kernel booting...\r\n");
        } else if (g_boot_count > 4) {
            usbj_print("next...");
            usbj_print_u32(g_boot_count);
            usbj_print("\r\n");
        }

        g_boot_count++;
        delay(200000);
    }
}