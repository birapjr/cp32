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

/* ── main ─────────────────────────────────────────────────────────────────────
 * Kernel entry point — called by the STEP 5 - call0   main - in mpx32.S. */
void main(void) {
    printk("main() starting...\r\n");

    while(1){}
    // TODO: Continue kernel from here
}
