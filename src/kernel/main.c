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
#include <minix/com.h>
#include <string.h>
extern void schedule(void);

extern char _stack_bottom[];
extern volatile uint32_t cp32_timer_irq_ticks;
extern volatile uint32_t cp32_clock_irq_bridge_calls;
extern volatile uint32_t cp32_clock_irq_frame_aligned_calls;
extern volatile uint32_t cp32_clock_irq_frame_stack_calls;
extern volatile int cp32_clock_irq_bridge_enabled;
extern volatile int k_reenter;

/* Simple test for IPC and MM */
void test_ipc_mm(void) {
    usbj_print("\r\n[TEST] Starting IPC and MM validation...\r\n");

    struct proc *p1 = &proc[1];
    struct proc *p2 = &proc[2];

    // Setup dummy memory maps for testing
    p1->p_map[D].mem_vir = 0x1000;
    p1->p_map[D].mem_phys = 0x3FCB0000 >> CLICK_SHIFT;
    p1->p_map[D].mem_len = 0x100;
    
    p2->p_map[D].mem_vir = 0x2000;
    p2->p_map[D].mem_phys = 0x3FCC0000 >> CLICK_SHIFT;
    p2->p_map[D].mem_len = 0x100;

    message m1, m2;
    memset(&m1, 0, sizeof(message));
    strcpy((char*)&m1, "Hello IPC!");
    
    usbj_print("[TEST] IPC send/receive: ");
    
    extern int mini_send(struct proc *caller, int dest, message *m);
    extern int mini_rec(struct proc *caller, int src, message *m);

    int res = mini_send(p1, p2->p_nr, &m1);
    usbj_print_u32((uint32_t)res);
    res = mini_rec(p2, p1->p_nr, &m2);
    usbj_print("/");
    usbj_print_u32((uint32_t)res);
    usbj_print(" (send/receive, flags=");
    usbj_print_u32((uint32_t)(p1->p_flags | p2->p_flags));
    usbj_print(")\r\n\r\n");
}

/* ── main ─────────────────────────────────────────────────────────────────────
 * Kernel entry point — called by the STEP 6 - call0   main - in mpx32.S. */
void kernel_idle_loop(void) {
    usbj_print("[IDLE] entered idle loop\r\n");
    for (;;) {
        wdt_feed_all();
        delay(1000000);
    }
}

void main(void) {
  status_line("main() starting", 0);

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

  /* Clear the process table and set up mappings. */
  status_line("cleaning proccess table", 0);
  for (rp = BEG_PROC_ADDR, t = -NR_TASKS; rp < END_PROC_ADDR; ++rp, ++t) {
    rp->p_flags = P_SLOT_FREE;
    rp->p_nr = t;
    (pproc_addr + NR_TASKS)[t] = rp;
  }

  status_line("init ready queues", 0);
  for (t = 0; t < NQ; t++) {
    rdy_head[t] = NIL_PROC;
    rdy_tail[t] = NIL_PROC;
  }

  /* Set up proc table entries for tasks and servers. */
  status_line("initializing proc table", 0);
  
  // Use the existing ktsb declaration from line 39
  ktsb = (reg_t)_stack_bottom + 0x4000; // Offset from bottom to avoid overlap

  for (t = -NR_TASKS; t <= LOW_USER; ++t) {
    rp = proc_addr(t);
    
    // 1. Assign Name (simplified since tasktab might not be fully ported)
    
    // 2. Initialize Registers
    rp->p_reg.pc = (reg_t)kernel_idle_loop; // Point to a valid execution loop
    rp->p_reg.psw = istaskp(rp) ? 0x100 : 0x0; // Simplified PSW
    
    /* Fix: Initialize all registers to 0 to avoid junk in p_reg */
    memset(rp->p_reg.a, 0, sizeof(rp->p_reg.a));
    
    /* Initialize a15 to a safe, non-null value for all processes.
     * Many kernel functions (like printk) use a15 as a base pointer for frames.
     * We point it to a safe region in DRAM to prevent null pointer exceptions. */
    rp->p_reg.a[15] = 0x3FC00000; 
    
    /* Ensure the IDLE process (proc[0]) is explicitly handled if needed */
    if (t == 0) {
        rp->p_reg.pc = (reg_t)kernel_idle_loop;
    }
    
    if (t < 0) {
      // Kernel Task: Assign stack from internal DRAM
      rp->p_reg.sp = (ktsb + 4096) & ~0xF; // 16-byte align
      ktsb += 4096;
    } else {
      // Server: Assign stack using a fixed offset from bottom
      rp->p_reg.sp = ((reg_t)_stack_bottom + 0x10000 + (t * 4096) + 4096) & ~0xF; // 16-byte align
    }

    // 3. Initialize Memory Maps (Simplified for ESP32-S3 flat memory)
    rp->p_map[T].mem_phys = 0; 
    rp->p_map[T].mem_len = 0;
    rp->p_map[D].mem_phys = 0;
    rp->p_map[D].mem_len = 0;
    rp->p_map[S].mem_phys = (rp->p_reg.sp >> CLICK_SHIFT);
    rp->p_map[S].mem_len = 4; // 4 clicks = 16KB

    // 4. Set Status
    if (!isidlehardware(t)) {
      lock_ready(rp);
    }
    rp->p_flags = 0; // Runnable
  }
  
  bill_ptr = proc_addr(IDLE);
  lock_pick_proc();


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
    proc_ptr = &proc[0]; /* Ensure proc_ptr is valid before enabling IRQs */
    systimer_irq_start();
    usbj_print("TARGET0 periodic IRQ enabled (CPU interrupt 2, level 1)\r\n");

   
   test_ipc_mm();

   /* Temporary pre-scheduler idle loop. Keep the watchdogs serviced and emit

   * a low-rate heartbeat so a silent hang can be distinguished from an
   * intentional idle state while task dispatch is still being ported. */
  status_line("entering kernel idle", 0);
  usbj_print("timer probe build: CP32-IRQ-FRAME-64-SCHED-2\r\n");
  schedule();
  usbj_print("timer reentry baseline: ");
  usbj_print_u32((uint32_t) k_reenter);
  usbj_print(" (expected 0)\r\n");
  usbj_print("Periodic interrupt and reentry validation starting...\r\n");
  for (;;) {
    wdt_feed_all();
    swd_disable();
    delay(1000000);
    if (k_reenter != 0) {
      usbj_print("\r\nFATAL: unbalanced timer reentry: ");
      usbj_print_u32((uint32_t) k_reenter);
      usbj_print("\r\n");
      for (;;) { }
    }
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
    usbj_print(" f=");
    usbj_print_u32(cp32_clock_irq_frame_aligned_calls);
    usbj_print(" s=");
    usbj_print_u32(cp32_clock_irq_frame_stack_calls);
    usbj_print(" e=");
    usbj_print_u32((uint32_t) cp32_clock_irq_bridge_enabled);
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
