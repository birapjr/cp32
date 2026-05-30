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

  printf("setup initial kernel variables...\r\n");
  register struct proc *rp;
  register int t;
  int sizeindex;
  phys_clicks text_base;
  vir_clicks text_clicks;
  vir_clicks data_clicks;
  phys_bytes phys_b;
  reg_t ktsb;			/* kernel task stack base */
  struct memory *memp;
  struct tasktab *ttp;

  /* Interpret memory sizes. */
  printf("initialize memory...\r\n");
  mem_init();

  while(1){}
  // TODO: Continue kernel from here
}

/*===========================================================================*
 *                                   panic                                   *
 *===========================================================================*/
PUBLIC void panic(s,n)
_CONST char *s;
int n;
{
/* The system has run aground of a fatal error.  Terminate execution.
 * If the panic originated in MM or FS, the string will be empty and the
 * file system already syncked.  If the panic originates in the kernel, we are
 * kind of stuck.
 */

  if (*s != 0) {
	printf("\nKernel panic: %s",s);
	if (n != NO_NUM) printf(" %d", n);
	printf("\n");
  }
}
