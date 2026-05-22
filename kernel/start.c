/**
 * This file prepare the enviroment before the main()
 * function starts
 * 
 * Author: Ubirajara Cortes
 * Date: 22.05.2026
 * 
 */
#include "kernel.h"

void start() {
      /* Step 1 — strobe the Super WDT before anything else. */
    wdt_feed_super_wdt();

    /* Step 2 — disable all watchdogs */
    wdt_disable_all();
    wdt_feed_all();

    /* Step 3 — wait for USB to enumerate on the host side. */
    startup_usb_conn();

    /* Step 4 — final disable pass then print diagnostics */
    wdt_disable_all();
    wdt_feed_all();
    print_diagnostics();

    /* Step 5 — one more disable pass right before the main loop so any
     * ROM activity that happened during the diagnostic prints is cleared */
    wdt_disable_all();
    wdt_feed_all();

    /* Feed/disable all watchdogs at the top of every iteration */
    wdt_feed_all();
    swd_disable(); /* extra SWD latch — cheap insurance */

    printk("\r\nCP32 OS kernel booting...\r\n");

    printk("exiting start()\r\n");
}
