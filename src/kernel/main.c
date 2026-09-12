#include "kernel.h"
#include "proc.h"
#include "irq_frame.h"
#include <string.h>
#include <minix/com.h>

extern char _stack_bottom[];
extern void systimer_irq_start(void);
extern void clock_task(void);
extern void sys_task(void);
extern void tty_task(void);
extern void mm_task(void);
extern struct proc *current_proc;

extern volatile int cp32_clock_irq_bridge_enabled;
extern volatile int cp32_context_restore_gate;
extern volatile int cp32_context_handoff_gate;
extern volatile int cp32_blocked_handoff_gate;
extern volatile int cp32_user_handoff_gate;
extern volatile uint32_t cp32_timer_irq_ticks;

void kernel_idle_loop(void)
{
  static uint32_t heartbeat;
  static uint32_t executions;
  static int announced;

  if (!announced) {
    announced = 1;
    status_line("\r\nkernel_idle_loop()", 2);
  }

  for (;;) {
    wdt_feed_all();
    delay(100000);
    /* Keep a low-rate boot heartbeat while the scheduler is restored. */
    if (++executions % 20 == 0) {
      usbj_print("[CTX kernel-running ticks=");
      usbj_print_u32(cp32_timer_irq_ticks);
      usbj_print(" heartbeat=");
      usbj_print_u32(++heartbeat);
      usbj_print("]\r\n");
    }
  }
}

void main(void)
{
  status_line("\r\nmain() started", 2);
  struct proc *rp;
  int t;
  reg_t kernel_stack = (reg_t)_stack_bottom + 0x4000;

  status_line("init process table", 0);
  for (rp = BEG_PROC_ADDR, t = -NR_TASKS;
       rp < END_PROC_ADDR; ++rp, ++t) {
    rp->p_flags = P_SLOT_FREE;
    rp->p_nr = t;
    (pproc_addr + NR_TASKS)[t] = rp;
  }

  status_line("init stack", 0);
  for (t = 0; t < NQ; ++t) {
    rdy_head[t] = NIL_PROC;
    rdy_tail[t] = NIL_PROC;
  }

  status_line("init TASKS", 0);
  for (t = -NR_TASKS; t <= LOW_USER; ++t) {
    rp = proc_addr(t);
    /* Start the first production descriptor. CLOCK owns the normal receive
     * loop and is the first real consumer of task-owned IPC suspension. */
    rp->p_reg.pc = (t == CLOCK) ? (reg_t)clock_task :
        (t == SYSTASK) ? (reg_t)sys_task :
        (t == TTY) ? (reg_t)tty_task :
        (t == MM_PROC_NR) ? (reg_t)mm_task :
        (reg_t)kernel_idle_loop;
    rp->p_reg.psw = istaskp(rp) ? 0x100 : 0;
    memset(rp->p_reg.a, 0, sizeof(rp->p_reg.a));
    rp->p_reg.a[15] = 0x3FC00000;

    if (t < 0) {
      rp->p_reg.sp = (kernel_stack + 4096) & ~0x0F;
      kernel_stack += 4096;
    } else {
      rp->p_reg.sp = ((reg_t)_stack_bottom + 0x10000 +
                      (t * 4096) + 4096) & ~0x0F;
    }
    rp->p_reg.a[1] = rp->p_reg.sp;
    rp->p_map[T].mem_phys = 0;
    rp->p_map[T].mem_len = 0;
    rp->p_map[D].mem_phys = 0;
    rp->p_map[D].mem_len = 0;
    rp->p_map[S].mem_phys = rp->p_reg.sp >> CLICK_SHIFT;
    rp->p_map[S].mem_len = 4;
    rp->p_flags = 0;

    if (!isidlehardware(t)) lock_ready(rp);
  }

  proc_ptr = proc_addr(IDLE);
  current_proc = proc_ptr;
  bill_ptr = proc_ptr;
  cp32_clock_irq_bridge_enabled = 1;
  cp32_context_restore_gate = 1;
  cp32_context_handoff_gate = 1;
  cp32_blocked_handoff_gate = 1;
  cp32_user_handoff_gate = 1;

  status_line("systemer irq start", 0);
  systimer_irq_start();

  /* Image/test identity: this is the IRQ handler-registration dispatcher
   * build, immediately before control enters the diagnostic workload. */
  usbj_print("[TEST MM-DESC]\r\n");
  kernel_idle_loop();
}

PUBLIC void panic(s, n)
_CONST char *s;
int n;
{
  (void)s;
  (void)n;
  for (;;) __asm__ volatile("nop");
}
