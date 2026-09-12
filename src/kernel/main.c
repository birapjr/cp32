#include "kernel.h"
#include "proc.h"
#include "irq_frame.h"
#include "cardputer.h"
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
extern int _sendrec(int dest, message *m);

static void cp32_tty_read_client(void)
{
  message m;
  char byte;
  unsigned attempts = 0;
  usbj_print("[TTY user-entry]\r\n");
  for (;;) {
    memset(&m, 0, sizeof(m));
    m.m_type = DEV_READ;
    m.TTY_LINE = 0;
    m.PROC_NR = FS_PROC_NR;
    m.COUNT = 1;
    m.ADDRESS = &byte;
    if (++attempts == 1) usbj_print("[TTY user-read-start]\r\n");
    if (_sendrec(TTY, &m) != OK) {
      if ((attempts % 50) == 0) usbj_print("[TTY user-read-error]\r\n");
      continue;
    }
    usbj_print("[TTY user-char=");
    if (byte >= 0x20 && byte <= 0x7E) {
      char shown[2]; shown[0] = byte; shown[1] = '\0'; usbj_print(shown);
    } else {
      usbj_print_hex32((uint32_t)(unsigned char)byte);
    }
    usbj_print("]\r\n");
  }
}

void kernel_idle_loop(void)
{
  static uint32_t heartbeat;
  static uint32_t executions;
  static int announced;

  /* main() enters here with the boot lock held so its test marker cannot be
   * interrupted; open the CPU gate at the first instruction of the loop. */
  unlock();
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
        (t == FS_PROC_NR) ? (reg_t)cp32_tty_read_client :
        (reg_t)kernel_idle_loop;
    /* rfi restores EPS1, not the live PS.  A zero saved PSW leaves the first
     * process return in an exception-level state on Xtensa; use the same
     * call0-compatible baseline for task and server descriptors. */
    rp->p_reg.psw = 0x100;
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
    rp->p_map[T].mem_vir = 0x3FC00000;
    rp->p_map[D].mem_phys = 0;
    rp->p_map[D].mem_len = 0;
    rp->p_map[D].mem_vir = 0x3FC00000;
    if (t < 0) {
      /* Kernel tasks exchange messages from their private stack windows.
       * Keep an explicit flat map for descriptor validation; numap() still
       * applies the bounded task-stack fast path on ESP32-S3. */
      rp->p_map[T].mem_phys = 0x3FC00000 >> CLICK_SHIFT;
      rp->p_map[T].mem_len = 0x100;
      rp->p_map[D].mem_phys = 0x3FC00000 >> CLICK_SHIFT;
      rp->p_map[D].mem_len = 0x100;
    }
    if (t == FS_PROC_NR) {
      rp->p_map[D].mem_vir = 0x3FC00000;
      rp->p_map[D].mem_phys = 0x3FC00000 >> CLICK_SHIFT;
      rp->p_map[D].mem_len = 0x100;
    }
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

  usbj_print("[KBD probe=");
  usbj_print_u32((uint32_t)cardputer_keyboard_probe());
  usbj_print("]\r\n");
  {
    phys_clicks probe = cp32_mem_alloc(1, FS_PROC_NR);
    int denied = probe != 0 && cp32_mem_free(probe, MM_PROC_NR) == EACCES;
    int released = probe != 0 && cp32_mem_free(probe, FS_PROC_NR) == OK;
    int zero_rejected = cp32_mem_alloc(0, FS_PROC_NR) == 0;
    int huge_rejected = cp32_mem_alloc((phys_clicks)0xFFFFFFFFu, FS_PROC_NR) == 0;
    phys_clicks block_a = cp32_mem_alloc(2, FS_PROC_NR);
    phys_clicks block_b = cp32_mem_alloc(3, FS_PROC_NR);
    int fragmented = block_a != 0 && block_b == block_a + 2;
    int owned = block_a != 0 && cp32_mem_owned(block_a, 2, FS_PROC_NR);
    int merged = block_b != 0 && cp32_mem_free(block_b, FS_PROC_NR) == OK &&
                 block_a != 0 && cp32_mem_free(block_a, FS_PROC_NR) == OK;
    int released_not_owned = block_a != 0 &&
                             !cp32_mem_owned(block_a, 2, FS_PROC_NR);
    usbj_print("[MM probe alloc=");
    usbj_print_u32((uint32_t)(probe != 0));
    usbj_print(" release=");
    usbj_print_u32((uint32_t)released);
    usbj_print(" deny=");
    usbj_print_u32((uint32_t)denied);
    usbj_print(" zero=");
    usbj_print_u32((uint32_t)zero_rejected);
    usbj_print(" huge=");
    usbj_print_u32((uint32_t)huge_rejected);
    usbj_print(" adjacent=");
    usbj_print_u32((uint32_t)fragmented);
    usbj_print(" merge=");
    usbj_print_u32((uint32_t)merged);
    usbj_print(" owned=");
    usbj_print_u32((uint32_t)owned);
    usbj_print(" clear=");
    usbj_print_u32((uint32_t)released_not_owned);
    usbj_print("]\r\n");
  }
  usbj_print("[KBD init=");
  usbj_print_u32((uint32_t)cardputer_keyboard_init());
  usbj_print("]\r\n");
  usbj_print("[TTY user-ready nr=");
  usbj_print_u32((uint32_t)FS_PROC_NR);
  usbj_print(" pc=");
  usbj_print_hex32((uint32_t)proc_addr(FS_PROC_NR)->p_reg.pc);
  usbj_print(" sp=");
  usbj_print_hex32((uint32_t)proc_addr(FS_PROC_NR)->p_reg.sp);
  usbj_print(" flags=");
  usbj_print_u32((uint32_t)proc_addr(FS_PROC_NR)->p_flags);
  usbj_print("]\r\n");
  /* Do not enable preemption until all boot-time keyboard diagnostics finish. */
  lock();
  systimer_irq_start();
  usbj_print("[TEST CARDPUTER-KBD 82]\r\n");
  /* Image/test identity: this is the IRQ handler-registration dispatcher
   * build, immediately before control enters the diagnostic workload. */
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
