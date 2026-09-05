/**
 * This file prepare the enviroment before the main()
 * function starts
 * 
 * Author: Ubirajara Cortes
 * Date: 22.05.2026
 * 
 */
#include "kernel.h"
#include <stdlib.h>
#include <minix/boot.h>

PRIVATE char k_environ[256];	/* environment strings passed by loader */
//struct bparam_s boot_parameters;
unsigned int processor;

/* Startup validation sentinels. The image loader must initialize .data at
 * its DRAM VMA, while startup must clear .bss before entering C. */
volatile uint32_t cp32_data_sentinel = 0xC032DA7Au;
volatile uint32_t cp32_bss_sentinel;

/* Set to 1 only for a hardware exception-handler test. */
#ifndef CP32_TEST_EXCEPTION
#define CP32_TEST_EXCEPTION 0
#endif

extern char _vectors_start[];
extern char _vectors_end[];
extern char _stack_bottom[];
extern char _stack_top[];
extern char _heap_start[];

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

    status_line("checking initialized memory", 0);
    usbj_print(".data sentinel: ");
    usbj_print_hex32(cp32_data_sentinel);
    usbj_print(" (expected 0xC032DA7A)\r\n");
    usbj_print(".bss sentinel:  ");
    usbj_print_hex32(cp32_bss_sentinel);
    usbj_print(" (expected 0x00000000)\r\n");

    status_line("checking exception vectors", 0);
    usbj_print("vector start:  ");
    usbj_print_hex32((uint32_t)_vectors_start);
    usbj_print("\r\n");
    usbj_print("vector end:    ");
    usbj_print_hex32((uint32_t)_vectors_end);
    usbj_print("\r\n");
    usbj_print("vector word 0: ");
    usbj_print_hex32(*(volatile uint32_t *)_vectors_start);
    usbj_print("\r\n");
    usbj_print("vector word 6: ");
    usbj_print_hex32(*(volatile uint32_t *)(_vectors_start + 0x180));
    usbj_print("\r\n");

    uint32_t current_stack;
    __asm__ volatile ("mov %0, a1" : "=a" (current_stack));
    status_line("checking call0 stack", 0);
    usbj_print("stack bottom:  ");
    usbj_print_hex32((uint32_t)_stack_bottom);
    usbj_print("\r\n");
    usbj_print("stack pointer: ");
    usbj_print_hex32(current_stack);
    usbj_print("\r\n");
    usbj_print("stack top:     ");
    usbj_print_hex32((uint32_t)_stack_top);
    usbj_print("\r\n");
    usbj_print("stack align:   ");
    usbj_print_u32(current_stack & 0x0Fu);
    usbj_print(" (expected 0)\r\n");

    if (current_stack < (uint32_t)_stack_bottom ||
        current_stack > (uint32_t)_stack_top ||
        (current_stack & 0x0Fu) != 0) {
        usbj_print("FATAL: invalid call0 stack\r\n");
        for (;;) { }
    }

#if CP32_TEST_EXCEPTION
    status_line("triggering test exception", 0);
    *(volatile uint32_t *)0 = 0xC032FA17u;
#endif

    /* Step 5 — one more disable pass right before the main loop so any
     * ROM activity that happened during the diagnostic prints is cleared */
    wdt_disable_all();
    wdt_feed_all();

    /* Feed/disable all watchdogs at the top of every iteration */
    wdt_feed_all();
    swd_disable(); /* extra SWD latch — cheap insurance */

    status_line("\r\nCP32 OS kernel booting", 2);

    status_line("setup boot parameters to kernel memory", 0);
    boot_parameters.bp_rootdev = 0;
    boot_parameters.bp_ramimagedev = 0;
    /* Report the linker-defined free DRAM span, not the whole SRAM bank.
     * The linker has already reserved .bss, heap, and stack regions. */
    boot_parameters.bp_ramsize = (unsigned int)(_stack_bottom - _heap_start);
    boot_parameters.bp_processor = ESP32S3_PROCESSOR_ID;

    processor = boot_parameters.bp_processor;

    status_line("exiting start()", 0);
}

/*==========================================================================*
 *				k_atoi					    *
 *==========================================================================*/
PRIVATE int k_atoi(s)
register char *s;
{
/* Convert string to integer. */

  return strtol(s, (char **) NULL, 10);
}


/*==========================================================================*
 *				k_getenv				    *
 *==========================================================================*/
PUBLIC char *k_getenv(name)
char *name;
{
/* Get environment value - kernel version of getenv to avoid setting up the
 * usual environment array.
 */

  register char *namep;
  register char *envp;

  for (envp = k_environ; *envp != 0;) {
	for (namep = name; *namep != 0 && *namep == *envp; namep++, envp++)
		;
	if (*namep == '\0' && *envp == '=') return(envp + 1);
	while (*envp++ != 0)
		;
  }
  return(NIL_PTR);
}
