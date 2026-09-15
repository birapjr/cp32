#include "kernel.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "tty.h"
#include "proc.h"
#include "irq_frame.h"
#include "cardputer.h"
#include "ramdisk.h"
#include "display.h"
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
extern void cp32_enter_initial_user(struct proc *rp);
extern volatile int cp32_context_handoff_gate;
extern volatile int cp32_blocked_handoff_gate;
extern volatile int cp32_user_handoff_gate;
extern volatile uint32_t cp32_timer_irq_ticks;
extern int _sendrec(int dest, message *m);
extern unsigned cardputer_keyboard_stale_events;
extern tty_t tty_table[];
extern void cp32_tty_poll_keyboard(void);
extern int cp32_tty_read_char(char *out);
volatile char cp32_tty_user_byte;
static char cp32_tty_line[64];
static unsigned cp32_tty_line_len;

/* TTY shell output is the user-visible console: retain the USB UART stream
 * while also rendering the same text on the Cardputer display. Kernel and
 * bring-up diagnostics continue to use usbj_print() directly. */
CP32_IRAM_EXT static void cp32_shell_print(const char *s)
{
  lock();
  usbj_print(s);
  unlock();
  cardputer_display_write(s);
}
CP32_IRAM_EXT static void cp32_shell_print_u32(uint32_t value)
{
  char buf[11]; char *p = buf + sizeof(buf) - 1;
  *p = '\0';
  do { *--p = (char)('0' + value % 10); value /= 10; } while (value);
  cp32_shell_print(p);
}

CP32_IRAM_EXT static void cp32_shell_command(const char *line)
{
  if (strcmp(line, "ls") == 0) {
    cp32_shell_print("ramdisk\r\n");
    cp32_shell_print("boot\r\n");
    cp32_shell_print("README\r\n");
    cp32_shell_print("[CMD ls capacity=");
    cp32_shell_print_u32(cp32_ramdisk_capacity());
    cp32_shell_print(" formatted=");
    cp32_shell_print(cp32_ramdisk_is_formatted() ? "1" : "0");
    cp32_shell_print("]\r\n");
  } else if (strcmp(line, "ramdisk") == 0) {
    cp32_shell_print("[CMD ramdisk capacity=");
    cp32_shell_print_u32(cp32_ramdisk_capacity());
    cp32_shell_print("]\r\n");
  } else if (line[0] != '\0') {
    cp32_shell_print("[CMD unknown=");
    cp32_shell_print(line);
    cp32_shell_print("]\r\n");
  }
}

CP32_IRAM_EXT static void cp32_tty_read_client(void)
{
  usbj_print("[TTY user-entry]\r\n");
  for (;;) {
    cp32_tty_poll_keyboard();
    if (cp32_tty_read_char((char *)&cp32_tty_user_byte) == 1) {
      usbj_print("[TTY user-char=");
      usbj_print_hex32((uint32_t)(unsigned char)cp32_tty_user_byte);
      usbj_print("]\r\n");
      if (cp32_tty_user_byte == '\b') {
        if (cp32_tty_line_len != 0) cp32_tty_line_len--;
      } else if (cp32_tty_user_byte == '\r' || cp32_tty_user_byte == '\n') {
        cp32_tty_line[cp32_tty_line_len] = '\0';
        usbj_print("[TTY line=");
        usbj_print(cp32_tty_line);
        usbj_print("]\r\n");
        cp32_shell_command(cp32_tty_line);
        cp32_tty_line_len = 0;
      } else if (cp32_tty_user_byte >= 0x20 &&
                 cp32_tty_user_byte <= 0x7E && cp32_tty_line_len < 63) {
        cp32_tty_line[cp32_tty_line_len++] = cp32_tty_user_byte;
      }
    }
  }
}

CP32_IRAM_EXT void kernel_idle_loop(void)
{
  static uint32_t heartbeat;
  static uint32_t executions;
  static int announced;
  extern volatile struct proc *cp32_last_selected_fs;

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
    /* The clock task can select FS outside an IRQ (nest=0). Enter its saved
     * call0 frame directly because no rfe path exists in that case. */
    if (cp32_last_selected_fs != NIL_PROC &&
        cp32_last_selected_fs->p_nr == FS_PROC_NR &&
        cp32_last_selected_fs->p_flags == 0 &&
        cp32_last_selected_fs->p_reg.pc != 0 &&
        cp32_last_selected_fs->p_reg.sp != 0) {
      cp32_enter_initial_user((struct proc *)cp32_last_selected_fs);
    }
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
  cardputer_display_init();
  cardputer_display_write("CP32\r\n");
  usbj_print("[LCD spi-fault=");
  usbj_print_u32(cardputer_display_faulted());
  usbj_print("]\r\n");
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
      /* The bring-up FS client uses a kernel SRAM stack as its user buffer.
       * Map the complete reserved stack window as flat D memory so TTY's
       * numap() accepts read/write buffers without a synthetic segment fault. */
      rp->p_map[D].mem_vir = 0x3FC00000;
      rp->p_map[D].mem_phys = 0x3FC00000 >> CLICK_SHIFT;
      rp->p_map[D].mem_len = 0x1000;
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
  usbj_print("[RAMDISK capacity=");
  usbj_print_u32((uint32_t)cp32_ramdisk_capacity());
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
    usbj_print("[TEST CARDPUTER-KBD 245]\r\n");
    usbj_print("[TEST CARDPUTER-CORE 49]\r\n");
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
