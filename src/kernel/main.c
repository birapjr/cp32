#include "kernel.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "tty.h"
#include "proc.h"
#include "irq_frame.h"
#include "cardputer.h"
#include "ramdisk.h"
#include "display.h"
#include "cp32-shell.h"
#include <string.h>
#include <minix/com.h>

extern char _stack_bottom[];
extern void systimer_irq_start(void);
extern void clock_task(void);
extern void sys_task(void);
extern void tty_task(void);
extern void mm_task(void);
extern struct proc *current_proc;
extern struct proc *proc_ptr;

extern volatile int cp32_clock_irq_bridge_enabled;
extern volatile int cp32_context_restore_gate;
extern volatile int cp32_context_handoff_gate;
extern volatile int cp32_blocked_handoff_gate;
extern volatile int cp32_user_handoff_gate;
extern volatile uint32_t cp32_timer_irq_ticks;
extern int _sendrec(int dest, message *m);
extern unsigned cardputer_keyboard_stale_events;
extern tty_t tty_table[];

/* Only implemented services are runnable at boot. Keep reserved descriptors
 * and their frames available, but do not schedule copies of the idle loop.
 * MINIX runs IDLE only when the ready queues have no work. */
CP32_IRAM_EXT static int cp32_boot_process_flags(int endpoint)
{
  if (endpoint == TTY_PROC_NR || endpoint == CLOCK || endpoint == SYSTASK ||
      endpoint == MM_PROC_NR || endpoint == FS_PROC_NR || endpoint == MEM ||
      endpoint == IDLE || endpoint == HARDWARE) return 0;
  return P_STOP;
}

/* Validate the first task-owned descriptor before the IRQ return path is
 * enabled.  CLOCK is the first production task and its private stack window
 * is the smallest useful proof that the saved call0 frame is self-owned. */
static int cp32_boot_clock_descriptor_check(void)
{
  struct proc *rp = proc_addr(CLOCK);
  reg_t stack_lo = (reg_t)_stack_bottom;
  reg_t stack_hi = stack_lo + 0x10000;

  if (rp == NIL_PROC || rp->p_flags == P_SLOT_FREE ||
      rp->p_reg.pc != (reg_t)clock_task || rp->p_reg.sp == 0 ||
      rp->p_reg.a[1] != rp->p_reg.sp || rp->p_reg.a[15] == 0 ||
      rp->p_reg.psw == 0 || (rp->p_reg.sp & 0x0F) != 0)
    return FALSE;

  /* The task stack must be inside the reserved DRAM stack area and leave a
   * complete 4 KiB window below its top for call0 frames. */
  if (rp->p_reg.sp <= stack_lo || rp->p_reg.sp > stack_hi ||
      rp->p_reg.sp - stack_lo < 4096)
    return FALSE;
  return TRUE;
}

CP32_IRAM_EXT void kernel_idle_loop(void)
{
  static uint32_t heartbeat;
  static uint32_t executions;
  static int announced;

  /* main() enters here with the boot lock held so its test marker cannot be
   * interrupted; open the CPU gate at the first instruction of the loop. */
  unlock();
  if (!announced) {
    announced = 1;
    status_line("\r\nSTABLE idle-entry", 2);
  }

  for (;;) {
    wdt_feed_all();
    delay(100000);
    /* Only the IRQ/syscall return boundary may restore another task. This
     * loop also runs under LOW_USER: replaying a previous FS selection here
     * would execute its stack while proc_ptr still names LOW_USER, causing
     * the next IRQ to save FS registers into the wrong descriptor. */
    /* Aggregate across idle descriptors: report every 400 iterations,
     * 20 times less often than image 36 (roughly 30 seconds in that run). */
    if (++executions % 400 == 0) {
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
  cardputer_display_init();
  cardputer_display_write("CP32\r\n");
  unsigned char kbd_status = 0, kbd_count = 0;
  int kbd_init_result;
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
        (t == MEM) ? (reg_t)mem_task :
        (t == SYSTASK) ? (reg_t)sys_task :
        (t == TTY_PROC_NR) ? (reg_t)tty_task :
        (t == MM_PROC_NR) ? (reg_t)mm_task :
        (t == FS_PROC_NR) ? (reg_t)cp32_tty_read_client :
        (reg_t)kernel_idle_loop;
    /* The level-1 epilogue writes PS and uses RFE to clear EXCM.  Keep
     * INTLEVEL zero in the initial call0 task/server status. */
    rp->p_reg.psw = 0x100;
    memset(rp->p_reg.a, 0, sizeof(rp->p_reg.a));
    rp->p_reg.sar = rp->p_reg.lbeg = rp->p_reg.lend = rp->p_reg.lcount = 0;

    if (t < 0) {
      rp->p_reg.sp = (kernel_stack + 4096) & ~0x0F;
      kernel_stack += 4096;
    } else {
      rp->p_reg.sp = ((reg_t)_stack_bottom + 0x10000 +
                      (t * 4096) + 4096) & ~0x0F;
    }
    rp->p_reg.a[1] = rp->p_reg.sp;
    /* call0 uses a15 as the frame pointer. Keep every fresh descriptor's
     * frame in its own stack window; a shared fixed base faults on entry. */
    rp->p_reg.a[15] = rp->p_reg.sp;
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
    if (t == FS_PROC_NR || t == MM_PROC_NR) {
      /* Both bring-up servers use physical SRAM stack buffers. MM is a
       * server (nonnegative endpoint), so numap's kernel-task fast path does
       * not apply. Give it the same flat D map as the FS IPC client. */
      rp->p_map[D].mem_vir = 0x3FC00000;
      rp->p_map[D].mem_phys = 0x3FC00000 >> CLICK_SHIFT;
      rp->p_map[D].mem_len = 0x1000;
    }
    rp->p_map[S].mem_phys = rp->p_reg.sp >> CLICK_SHIFT;
    rp->p_map[S].mem_len = 4;
    rp->p_flags = cp32_boot_process_flags(t);

    /* Implemented services, including MEM, suspend on their receive frames. */
    if (!isidlehardware(t) && rp->p_flags == 0) lock_ready(rp);
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
  usbj_print("[FEATURE MM-LAYOUT 58]");
  usbj_print_u32((uint32_t)cp32_boot_clock_descriptor_check());
  usbj_print("\r\n");

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
  kbd_init_result = cardputer_keyboard_init();
  usbj_print("[KBD init=");
  usbj_print_u32((uint32_t)kbd_init_result);
  usbj_print(" stale=");
  usbj_print_u32((uint32_t)cardputer_keyboard_stale_events);
  usbj_print(" int=");
  usbj_print_u32((uint32_t)(cardputer_keyboard_interrupt_asserted() != 0));
  usbj_print(" status=");
  usbj_print_u32((uint32_t)(cardputer_keyboard_read_status(&kbd_status, &kbd_count) == 0));
  usbj_print(" fifo=");
  usbj_print_u32((uint32_t)kbd_count);
  usbj_print("]\r\n");
  if (cp32_minix_demo_init() != 0) panic("RAM demo init failed", NO_NUM);
  usbj_print("[RAMDISK capacity=");
  usbj_print_u32((uint32_t)cp32_ramdisk_capacity());
  usbj_print("]\r\n");
  cardputer_display_clear();
  usbj_print("[LCD clear-done]\r\n");
  cardputer_display_write("CP32 OS\r\n$ ");
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
    usbj_print("[TEST SCHEDULER 2]\r\n");

    usbj_print("[CORE readyq=");
    usbj_print_u32((uint32_t)cp32_ready_queue_check());
    usbj_print("]\r\n");
    usbj_print("[CORE heldq=");
    usbj_print_u32((uint32_t)cp32_held_queue_check());
    usbj_print("]\r\n");
    usbj_print("[CORE blockedowner=");
    usbj_print_u32((uint32_t)cp32_blocked_owner_check());
    usbj_print("]\r\n");
    usbj_print("[CORE blockedframe=");
    usbj_print_u32((uint32_t)cp32_blocked_frame_check());
    usbj_print("]\r\n");
    usbj_print("[CORE runtime=");
    usbj_print_u32((uint32_t)cp32_runtime_owner_check());
    usbj_print("]\r\n");
    usbj_print("[CORE maps=");
    usbj_print_u32((uint32_t)cp32_map_state_check());
    usbj_print("]\r\n");
    usbj_print("[CORE flags=");
    usbj_print_u32((uint32_t)cp32_process_flags_check());
    usbj_print("]\r\n");
    usbj_print("[CORE contexts=");
    usbj_print_u32((uint32_t)cp32_saved_context_check());
    usbj_print("]\r\n");
    usbj_print("[CORE owners=");
    usbj_print_u32((uint32_t)cp32_scheduler_owner_check());
    usbj_print("]\r\n");
    usbj_print("[CORE ipcstate=");
    usbj_print_u32((uint32_t)cp32_ipc_state_check());
    usbj_print("]\r\n");
    usbj_print("[CORE ipcqueue=");
    usbj_print_u32((uint32_t)cp32_ipc_queue_check());
    usbj_print("]\r\n");
    usbj_print("[CORE ipclinks=");
    usbj_print_u32((uint32_t)cp32_ipc_link_check());
    usbj_print("]\r\n");
    usbj_print("[CORE proctab=");
    usbj_print_u32((uint32_t)cp32_process_table_check());
    usbj_print("]\r\n");
    usbj_print("[CORE tasks=");
    usbj_print_u32((uint32_t)cp32_task_table_check());
    usbj_print("]\r\n");
    usbj_print("[CORE systask=");
    usbj_print_u32((uint32_t)cp32_system_task_check());
    usbj_print("]\r\n");
  /* Keep image identity adjacent to the handoff into the idle workload. */
  usbj_print("[TEST MM-LAYOUT 58]\r\n");
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
