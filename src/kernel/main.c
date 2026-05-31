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
#include "proc.h"

/* ── main ─────────────────────────────────────────────────────────────────────
 * Kernel entry point — called by the STEP 5 - call0   main - in mpx32.S. */
void main(void) {
  status_line("main() starting", 0);

  status_line("setup initial kernel variables", 0);
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
  status_line("initialize memory", 0);
  mem_init();


  /* Clear the process table.
   * Set up mappings for proc_addr() and proc_number() macros.
   */
  status_line("cleaning proccess table", 0);
  for (rp = BEG_PROC_ADDR, t = -NR_TASKS; rp < END_PROC_ADDR; ++rp, ++t) {
	rp->p_flags = P_SLOT_FREE;
	rp->p_nr = t;		/* proc number from ptr */
        (pproc_addr + NR_TASKS)[t] = rp;        /* proc ptr from number */
  }

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
