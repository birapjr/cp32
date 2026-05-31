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

    status_line("\r\nCP32 OS kernel booting", 2);

    status_line("setup boot parameters to kernel memory", 0);
    boot_parameters.bp_rootdev = 0;
    boot_parameters.bp_ramimagedev = 0;
    boot_parameters.bp_ramsize = 480 * 1024;
    boot_parameters.bp_processor = 32; /* magic number for esp32s3 */

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
