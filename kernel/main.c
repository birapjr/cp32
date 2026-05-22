/*
 * CP32 OS Kernel entry point
 *
 *  The startup code from the mpx32.S file
 *  disable all interrupts, set the vectors table
 *  for interrupts and jump to
 *  the main() where the kernel start rolling 
 * 
 *  Author: Ubirajara Cortes
 *  Date: 16.05.2026
 */

#include "kernel.h"

/* ── Persistent kernel state ──────────────────────────────────────────────────
 * Placing at least one symbol in .bss and one in .data ensures the linker
 * script pins both sections into DRAM and that the startup code in mpx32.S
 * actually zeroes / copies them. */
static volatile uint32_t g_boot_count;           /* .bss — zeroed by startup */
static volatile uint32_t g_magic = 0xC0320000u;  /* .data — copied by startup */


/* ── main ─────────────────────────────────────────────────────────────────────
 * Kernel entry point — called by the STEP 5 - call0   main - in mpx32.S. */
void main(void) {
    usbj_print("\r\nCP32 OS kernel booting...\r\n");
    while(1){}
    // TODO: Continue kernel from here
}
