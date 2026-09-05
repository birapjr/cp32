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
#include "esp32s3/systimer.h"

extern char _stack_bottom[];
extern volatile uint32_t cp32_timer_irq_ticks;
extern volatile uint32_t cp32_clock_irq_bridge_calls;
extern volatile int k_reenter;

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

  status_line("checking process table", 0);
  for (rp = BEG_PROC_ADDR, t = -NR_TASKS; rp < END_PROC_ADDR; ++rp, ++t) {
    if (rp->p_nr != t || (pproc_addr + NR_TASKS)[t] != rp) {
      usbj_print("FATAL: process table mapping at ");
      usbj_print_u32((uint32_t)(t + NR_TASKS));
      usbj_print("\r\n");
      for (;;) { }
    }
  }
  usbj_print("process slots: ");
  usbj_print_u32((uint32_t)(NR_TASKS + NR_PROCS));
  usbj_print(" (mapping valid)\r\n");

  status_line("checking click memory accounting", 0);
  usbj_print("memory base clicks: ");
  usbj_print_u32((uint32_t)mem[1].base);
  usbj_print("\r\n");
  usbj_print("memory size clicks: ");
  usbj_print_u32((uint32_t)mem[1].size);
  usbj_print("\r\n");
  usbj_print("memory total clicks:");
  usbj_print_u32((uint32_t)tot_mem_size);
  usbj_print("\r\n");

  /* Reserve the first 16 bytes below the downward-growing stack as a guard.
   * It is checked in the temporary idle loop until task stacks exist. */
  volatile uint32_t *stack_guard = (volatile uint32_t *)_stack_bottom;
  stack_guard[0] = CP32_STACK_GUARD_WORD;
  stack_guard[1] = CP32_STACK_GUARD_WORD;
  stack_guard[2] = CP32_STACK_GUARD_WORD;
  stack_guard[3] = CP32_STACK_GUARD_WORD;
  status_line("installing stack guard", 0);

  status_line("checking systimer", 0);
  if (systimer_probe() != OK) {
    usbj_print("FATAL: systimer counter is not advancing\r\n");
    for (;;) { }
  }
  usbj_print("systimer UNIT0 advancing (TARGET0 IRQ disabled)\r\n");
  status_line("checking systimer interrupt route", 0);
  if (systimer_route_probe() != OK) {
    usbj_print("FATAL: systimer interrupt route rejected\r\n");
    for (;;) { }
  }
  usbj_print("TARGET0 mapped to CPU interrupt 2 (IRQ disabled)\r\n");
  status_line("starting systimer interrupt probe", 0);
  systimer_irq_start();
  usbj_print("TARGET0 periodic IRQ enabled (CPU interrupt 2, level 1)\r\n");

  /* Temporary pre-scheduler idle loop. Keep the watchdogs serviced and emit
   * a low-rate heartbeat so a silent hang can be distinguished from an
   * intentional idle state while task dispatch is still being ported. */
  status_line("entering kernel idle", 0);
  usbj_print("timer probe build: CP32-IRQ-FRAME-64-REENTER-5\r\n");
  usbj_print("timer reentry baseline: ");
  usbj_print_u32((uint32_t) k_reenter);
  usbj_print(" (expected 0)\r\n");
  for (;;) {
    wdt_feed_all();
    swd_disable();
    delay(1000000);
    if (stack_guard[0] != CP32_STACK_GUARD_WORD ||
        stack_guard[1] != CP32_STACK_GUARD_WORD ||
        stack_guard[2] != CP32_STACK_GUARD_WORD ||
        stack_guard[3] != CP32_STACK_GUARD_WORD) {
      usbj_print("\r\nFATAL: stack guard corrupted\r\n");
      for (;;) { }
    }
    usbj_print(".");
    usbj_print_u32(cp32_timer_irq_ticks);
    usbj_print("[r=");
    usbj_print_u32((uint32_t) k_reenter);
    usbj_print(" c=");
    usbj_print_u32(cp32_clock_irq_bridge_calls);
    usbj_print("]");
  if ((cp32_timer_irq_ticks & 0x3Fu) == 0) {
      uint32_t cpu_interrupt;
      usbj_print(" [target_hi=");
      usbj_print_hex32(REG_READ(SYSTIMER_TARGET0_HI_REG));
      usbj_print(" lo=");
      usbj_print_hex32(REG_READ(SYSTIMER_TARGET0_LO_REG));
      usbj_print(" conf=");
      usbj_print_hex32(REG_READ(SYSTIMER_TARGET0_CONF_REG));
      {
        uint64_t counter = systimer_unit0_read();
        usbj_print(" now_hi=");
        usbj_print_hex32((uint32_t)(counter >> 32));
      usbj_print(" now_lo=");
        usbj_print_hex32((uint32_t)counter);
      }
      usbj_print(" real_hi=");
      usbj_print_hex32(REG_READ(SYSTIMER_REAL_TARGET0_HI_REG));
      usbj_print(" real_lo=");
      usbj_print_hex32(REG_READ(SYSTIMER_REAL_TARGET0_LO_REG));
      usbj_print(" raw=");
      usbj_print_hex32(REG_READ(SYSTIMER_INT_RAW_REG));
      usbj_print(" st=");
      usbj_print_hex32(REG_READ(SYSTIMER_INT_ST_REG));
      usbj_print(" cpu=");
      __asm__ volatile ("rsr %0, interrupt" : "=a" (cpu_interrupt));
      usbj_print_hex32(cpu_interrupt);
      usbj_print(" reentry=");
      usbj_print_u32((uint32_t) k_reenter);
      usbj_print("]");
    }
  }
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
