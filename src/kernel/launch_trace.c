/* Bounded USB breadcrumbs from the production FS/MM/exception-return path.
 * Trace the first two launches (the suite request and its first test on a
 * fresh boot). An IRQ may observe a return before FS resumes from SENDREC. */
#include "kernel.h"
#include <esp32s3/systimer.h>
#include <minix/com.h>
#include "proc.h"
#include "launch_trace.h"

extern volatile struct proc *cp32_irq_saved_owner;

static const char *const names[] = {
  "load", "accept", "alloc", "slot", "files", "publish", "reply",
  "fs-resume", "rfe", "fork-slot", "swap-counts", "signal", "sigreturn",
  "exit", "fs-drop", "done"
};
static unsigned runs;
static volatile unsigned seen, ticks_left;
static enum cp32_launch_step last_step;
static int sampled;
static unsigned root_slot, child_slot;

void cp32_trace_launch(enum cp32_launch_step step, unsigned value)
{
  unsigned bit = 1U << step;
  int saved;

  if (step != LAUNCH_LOAD && (seen & bit)) return;
  saved = lock_save();
  if (step == LAUNCH_LOAD && runs < 3) {
    ++runs;
    seen = runs <= 2 ? 0 : ~0U;
    root_slot = child_slot = 0;
    sampled = 0;
    ticks_left = 0;
  }
  if (runs && !(seen & bit)) {
    seen |= bit;
    if (step == LAUNCH_SLOT) root_slot = value;
    if (step == LAUNCH_FORK) child_slot = value;
    last_step = step;
    if (step == LAUNCH_DONE ||
        ((step == LAUNCH_REPLY || step == LAUNCH_FS_RESUME) && (int)value < 0))
      ticks_left = 0;
    else if (!sampled)
      ticks_left = 10 * HZ;
    usbj_print("[CTX V207 launch=");
    usbj_print_u32(runs);
    usbj_print(" ");
    usbj_print(names[step]);
    usbj_print("=");
    usbj_print_hex32(value);
    usbj_print("]\r\n");
  }
  restore_lock(saved);
}

/* Called with scheduling excluded. Only the first complete save + restore
 * is timed. Unsigned subtraction handles wrapping of the counter's low word. */
static unsigned swap_start;
static int swap_started, swap_measured;

void cp32_trace_swap_begin(void)
{
  if (!swap_measured && runs > 0 && runs <= 2) {
    swap_start = (unsigned)systimer_unit0_read();
    swap_started = 1;
  }
}

void cp32_trace_swap_end(void)
{
  if (swap_started) {
    unsigned elapsed = (unsigned)systimer_unit0_read() - swap_start;
    swap_started = 0;
    swap_measured = 1;
    cp32_trace_launch(LAUNCH_SWAP_TICKS, elapsed);
  }
}

static void print_slot(unsigned nr)
{
  const struct proc *p;
  if (nr <= LOW_USER || nr >= NR_PROCS) return;
  p = proc_addr(nr);
  usbj_print("[CTX V207 slot="); usbj_print_u32(nr);
  usbj_print(" pid="); usbj_print_u32(p->p_pid);
  usbj_print(" flags="); usbj_print_hex32(p->p_flags);
  usbj_print(" pc="); usbj_print_hex32(p->p_reg.pc);
  usbj_print(" a0="); usbj_print_hex32(p->p_reg.a[0]);
  usbj_print(" pending="); usbj_print_hex32(p->p_app_pending);
  usbj_print("]\r\n");
}

/* IRQ entry saved EPC1 in the interrupted owner's frame. Sample it before
 * clock_handler can select another process. No IPC or scheduler calls here. */
void cp32_trace_launch_tick(void)
{
  const volatile struct proc *owner;
  if (!ticks_left || --ticks_left) return;
  sampled = 1;
  owner = cp32_irq_saved_owner;
  usbj_print("[CTX V207 stall=");
  usbj_print(names[last_step]);
  usbj_print(" owner=");
  usbj_print_hex32(owner ? (unsigned)owner->p_nr : ~0U);
  usbj_print(" pc=");
  usbj_print_hex32(owner ? owner->p_reg.pc : 0);
  usbj_print(" mm=");
  usbj_print_hex32(proc_addr(MM_PROC_NR)->p_flags);
  usbj_print(" fs=");
  usbj_print_hex32(proc_addr(FS_PROC_NR)->p_flags);
  usbj_print("]\r\n");
  print_slot(root_slot);
  print_slot(child_slot);
}
