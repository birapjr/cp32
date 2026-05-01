#include <stdint.h>
#include "const.h"
#include "serial.h"

/* Non-empty .bss and .data so linker symbols are pinned in DRAM */
static volatile uint32_t g_boot_count;            /* .bss  */
static volatile uint32_t g_magic = 0xC0320000u;   /* .data */

static void delay(volatile uint32_t n) { while (n--); }

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