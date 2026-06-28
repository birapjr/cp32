/* This file contains the code and data for the clock task.  The clock task
 * accepts six message types:
 *
 *   HARD_INT:    a clock interrupt has occurred
 *   GET_UPTIME:  get the time since boot in ticks
 *   GET_TIME:    a process wants the real time in seconds
 *   SET_TIME:    a process wants to set the real time in seconds
 *   SET_ALARM:   a process wants to be alerted after a specified interval
 *   SET_SYN_AL:  set the sync alarm
 *
 * The input message is format m6.  The parameters are as follows:
 *
 *     m_type    CLOCK_PROC   FUNC    NEW_TIME
 * ---------------------------------------------
 * | HARD_INT   |          |         |         |
 * |------------+----------+---------+---------|
 * | GET_UPTIME |          |         |         |
 * |------------+----------+---------+---------|
 * | GET_TIME   |          |         |         |
 * |------------+----------+---------+---------|
 * | SET_TIME   |          |         | newtime |
 * |------------+----------+---------+---------|
 * | SET_ALARM  | proc_nr  |f to call|  delta  |
 * |------------+----------+---------+---------|
 * | SET_SYN_AL | proc_nr  |         |  delta  |
 * ---------------------------------------------
 * NEW_TIME, DELTA_CLICKS, and SECONDS_LEFT all refer to the same field in
 * the message, depending upon the message type.
 *
 * Reply messages are of type OK, except in the case of a HARD_INT, to
 * which no reply is generated. For the GET_* messages the time is returned
 * in the NEW_TIME field, and for SET_ALARM and SET_SYN_AL the time in
 * seconds remaining until the alarm fires is returned in the same field.
 *
 * When an alarm goes off, if the caller is a user process, a SIGALRM signal
 * is sent to it.  If it is a task, a function specified by the caller will
 * be invoked.  This function may, for example, send a message, but only if
 * it is certain that the task will be blocked when the timer goes off.  A
 * synchronous alarm sends a message to the synchronous alarm task, which
 * in turn can dispatch a message to another server.  This is the only way
 * to send an alarm to a server, since servers cannot use the function-call
 * mechanism available to tasks and servers cannot receive signals.
 *
 * ESP32-S3 notes:
 *   - Clock source: SYSTIMER peripheral, UNIT0 counter (16 MHz fixed clock).
 *   - Clock tick: SYSTIMER TARGET0 alarm, one-shot mode, reloaded each ISR.
 *   - No PIC, no 8253 PIT, no Port B acknowledge needed.
 *   - Interrupt acknowledgement: clear TARGET0 flag in SYSTIMER_INT_CLR_REG.
 *   - milli_delay() uses SYSTIMER UNIT0 as a 64-bit free-running counter;
 *     no accumulation loop needed (unlike the 8253 latch approach).
 */

#include "kernel.h"
#include <signal.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "proc.h"
#include "esp32s3/systimer.h"

/* Constant definitions. */
#define MILLISEC      100                       /* how often to call the scheduler (msec) */
#define SCHED_RATE    (MILLISEC*HZ/1000)        /* number of ticks per schedule */

/* ESP32-S3 SYSTIMER clock parameters.
 * SYSTIMER UNIT0 runs at a fixed 16 MHz regardless of CPU frequency.
 * TIMER_COUNT is the number of SYSTIMER ticks between each Minix HZ tick.
 */
#define TIMER_FREQ    16000000L                 /* SYSTIMER UNIT0 frequency: 16 MHz */
#define TIMER_COUNT   (TIMER_FREQ / HZ)         /* SYSTIMER ticks per Minix tick    */

/* Clock task variables. */
PRIVATE clock_t realtime;                       /* real time clock in ticks         */
PRIVATE time_t  boot_time;                      /* time in seconds of system boot   */
PRIVATE clock_t next_alarm;                     /* probable time of next alarm      */
PRIVATE message mc;                             /* message buffer for input/output  */
PRIVATE int     watchdog_proc;                  /* proc_nr at call of *watch_dog[]  */
PRIVATE watchdog_t watch_dog[NR_TASKS+NR_PROCS];

/* Variables used by both clock task and synchronous alarm task. */
PRIVATE int syn_al_alive = TRUE;                /* don't wake syn_alrm_task before inited */
PRIVATE int syn_table[NR_TASKS+NR_PROCS];      /* which tasks get CLOCK_INT        */

/* Variables changed by interrupt handler. */
PRIVATE clock_t pending_ticks;                  /* ticks seen by low level only     */
PRIVATE int sched_ticks = SCHED_RATE;           /* counter: when 0, call scheduler  */
PRIVATE struct proc *prev_ptr;                  /* last user process run by clock task */

FORWARD _PROTOTYPE( void common_setalarm, (int proc_nr,
        long delta_ticks, watchdog_t function) );
FORWARD _PROTOTYPE( void do_clocktick,    (void) );
FORWARD _PROTOTYPE( void do_get_time,     (void) );
FORWARD _PROTOTYPE( void do_getuptime,    (void) );
FORWARD _PROTOTYPE( void do_set_time,     (message *m_ptr) );
FORWARD _PROTOTYPE( void do_setalarm,     (message *m_ptr) );
FORWARD _PROTOTYPE( void init_clock,      (void) );
FORWARD _PROTOTYPE( void cause_alarm,     (void) );
FORWARD _PROTOTYPE( void do_setsyn_alrm,  (message *m_ptr) );
FORWARD _PROTOTYPE( int  clock_handler,   (int irq) );

/*===========================================================================*
 *                              clock_task                                    *
 *===========================================================================*/
PUBLIC void clock_task()
{
/* Main program of clock task.  It corrects realtime by adding pending
 * ticks seen only by the interrupt handler, then dispatches based on
 * the message type.
 */
  int opcode;

  init_clock();           /* initialize SYSTIMER and register IRQ handler */

  while (TRUE) {
    receive(ANY, &mc);
    opcode = mc.m_type;

    /* Transfer ticks accumulated by the ISR into realtime atomically. */
    lock();
    realtime      += pending_ticks;
    pending_ticks  = 0;
    unlock();

    switch (opcode) {
      case HARD_INT:    do_clocktick();       break;
      case GET_UPTIME:  do_getuptime();       break;
      case GET_TIME:    do_get_time();        break;
      case SET_TIME:    do_set_time(&mc);     break;
      case SET_ALARM:   do_setalarm(&mc);     break;
      case SET_SYNC_AL: do_setsyn_alrm(&mc);  break;
      default: panic("clock task got bad message", mc.m_type);
    }

    /* Send reply, except for clock tick. */
    mc.m_type = OK;
    if (opcode != HARD_INT) send(mc.m_source, &mc);
  }
}


/*===========================================================================*
 *                              do_clocktick                                  *
 *===========================================================================*/
PRIVATE void do_clocktick()
{
/* Called on clock ticks when significant work is needed (alarm expired,
 * or quantum used up).  Not called on every tick.
 */
  register struct proc *rp;
  register int proc_nr;

  if (next_alarm <= realtime) {
    /* An alarm may have gone off; the process may have exited, so check. */
    next_alarm = LONG_MAX;
    for (rp = BEG_PROC_ADDR; rp < END_PROC_ADDR; rp++) {
      if (rp->p_alarm != 0) {
        if (rp->p_alarm <= realtime) {
          /* Timer expired: signal user process or call task watchdog. */
          proc_nr = proc_number(rp);
          if (watch_dog[proc_nr + NR_TASKS]) {
            watchdog_proc = proc_nr;
            (*watch_dog[proc_nr + NR_TASKS])();
          } else {
            cause_sig(proc_nr, SIGALRM);
          }
          rp->p_alarm = 0;
        }
        /* Track the nearest future alarm. */
        if (rp->p_alarm != 0 && rp->p_alarm < next_alarm)
          next_alarm = rp->p_alarm;
      }
    }
  }

  /* If a user process has been running too long, pick another one. */
  if (--sched_ticks == 0) {
    if (bill_ptr == prev_ptr) lock_sched();   /* process has run too long */
    sched_ticks = SCHED_RATE;                 /* reset quantum            */
    prev_ptr    = bill_ptr;                   /* new previous process     */
  }

#if (SHADOWING == 1)
  if (rdy_head[SHADOW_Q]) unshadow(rdy_head[SHADOW_Q]);
#endif
}


/*===========================================================================*
 *                              do_getuptime                                  *
 *===========================================================================*/
PRIVATE void do_getuptime()
{
/* Return the current clock uptime in ticks. */
  mc.NEW_TIME = realtime;
}


/*===========================================================================*
 *                              get_uptime                                    *
 *===========================================================================*/
PUBLIC clock_t get_uptime()
{
/* Return uptime in ticks for callers outside the clock task.
 * Guards pending_ticks with lock/unlock to get a consistent snapshot.
 */
  clock_t uptime;

  lock();
  uptime = realtime + pending_ticks;
  unlock();
  return uptime;
}


/*===========================================================================*
 *                              do_get_time                                   *
 *===========================================================================*/
PRIVATE void do_get_time()
{
/* Return the current wall-clock time in seconds. */
  mc.NEW_TIME = boot_time + realtime / HZ;
}


/*===========================================================================*
 *                              do_set_time                                   *
 *===========================================================================*/
PRIVATE void do_set_time(m_ptr)
message *m_ptr;
{
/* Set the real time clock.  Only the superuser can use this call. */
  boot_time = m_ptr->NEW_TIME - realtime / HZ;
}


/*===========================================================================*
 *                              do_setalarm                                   *
 *===========================================================================*/
PRIVATE void do_setalarm(m_ptr)
message *m_ptr;
{
/* A process wants an alarm signal or a task wants a watchdog function
 * called after a specified interval.
 */
  register struct proc *rp;
  int        proc_nr;       /* which process wants the alarm    */
  long       delta_ticks;   /* how many ticks until alarm fires */
  watchdog_t function;      /* function to call (tasks only)    */

  proc_nr     = m_ptr->CLOCK_PROC_NR;
  delta_ticks = m_ptr->DELTA_TICKS;
  function    = (watchdog_t) m_ptr->FUNC_TO_CALL;

  rp = proc_addr(proc_nr);
  mc.SECONDS_LEFT = (rp->p_alarm == 0 ? 0 : (rp->p_alarm - realtime) / HZ);
  if (!istaskp(rp)) function = 0;    /* user processes get signaled, not called */
  common_setalarm(proc_nr, delta_ticks, function);
}


/*===========================================================================*
 *                              do_setsyn_alrm                                *
 *===========================================================================*/
PRIVATE void do_setsyn_alrm(m_ptr)
message *m_ptr;
{
/* A process wants a synchronous alarm. */
  register struct proc *rp;
  int  proc_nr;
  long delta_ticks;

  proc_nr     = m_ptr->CLOCK_PROC_NR;
  delta_ticks = m_ptr->DELTA_TICKS;

  rp = proc_addr(proc_nr);
  mc.SECONDS_LEFT = (rp->p_alarm == 0 ? 0 : (rp->p_alarm - realtime) / HZ);
  common_setalarm(proc_nr, delta_ticks, cause_alarm);
}


/*===========================================================================*
 *                              common_setalarm                               *
 *===========================================================================*/
PRIVATE void common_setalarm(proc_nr, delta_ticks, function)
int        proc_nr;
long       delta_ticks;
watchdog_t function;
{
/* Record an alarm request and recompute next_alarm. */
  register struct proc *rp;

  rp = proc_addr(proc_nr);
  rp->p_alarm = (delta_ticks == 0 ? 0 : realtime + delta_ticks);
  watch_dog[proc_nr + NR_TASKS] = function;

  /* Recompute which alarm fires next. */
  next_alarm = LONG_MAX;
  for (rp = BEG_PROC_ADDR; rp < END_PROC_ADDR; rp++)
    if (rp->p_alarm != 0 && rp->p_alarm < next_alarm)
      next_alarm = rp->p_alarm;
}


/*===========================================================================*
 *                              cause_alarm                                   *
 *===========================================================================*/
PRIVATE void cause_alarm()
{
/* Called when a synchronous alarm fires.  Notify the syn_alrm_task.
 * watchdog_proc holds the proc_nr of the target (set just before call).
 */
  message mess;

  syn_table[watchdog_proc + NR_TASKS] = TRUE;
  if (!syn_al_alive) send(SYN_ALRM_TASK, &mess);
}


/*===========================================================================*
 *                              syn_alrm_task                                 *
 *===========================================================================*/
PUBLIC void syn_alrm_task()
{
/* Main program of the synchronous alarm task.
 * Receives wake-ups from cause_alarm() (clock task ISR context) and
 * forwards a CLOCK_INT message to each process that registered a
 * synchronous alarm.  Synchronous alarms are received by a process
 * at a known point in its code (when it is blocked in receive()), unlike
 * signals or watchdog callbacks which can arrive asynchronously.
 */
  message mess;
  int  work_done;
  int *al_ptr;
  int  i;

  syn_al_alive = TRUE;
  for (i = 0, al_ptr = syn_table; i < NR_TASKS + NR_PROCS; i++, al_ptr++)
    *al_ptr = FALSE;

  while (TRUE) {
    work_done = TRUE;
    for (i = 0, al_ptr = syn_table; i < NR_TASKS + NR_PROCS; i++, al_ptr++) {
      if (*al_ptr) {
        *al_ptr       = FALSE;
        mess.m_type   = CLOCK_INT;
        send(i - NR_TASKS, &mess);
        work_done = FALSE;
      }
    }
    if (work_done) {
      syn_al_alive = FALSE;
      receive(CLOCK, &mess);
      syn_al_alive = TRUE;
    }
  }
}


/*===========================================================================*
 *                              clock_handler                                 *
 *===========================================================================*/
PRIVATE int clock_handler(irq)
int irq;
{
/* ISR: called on every SYSTIMER TARGET0 alarm (i.e., every Minix clock tick).
 * Kept deliberately short — heavy work is deferred to do_clocktick() which
 * runs in the clock task context via interrupt(CLOCK).
 *
 * Variable safety:
 *   k_reenter      — read-only here; safe without lock.
 *   proc_ptr, bill_ptr — pointers always valid; worst case wrong process
 *                        gets billed for one tick.
 *   pending_ticks  — protected by lock/unlock in clock_task().
 *   lost_ticks     — protected by lock/unlock in system.c.
 *   sched_ticks, prev_ptr — benign races; do_clocktick() corrects them.
 *   next_alarm, rdy_head  — tested, not modified; rare race only delays
 *                            do_clocktick() by one tick.
 */
  register struct proc *rp;
  register unsigned ticks;
  clock_t now;

  /* Step 1: Acknowledge the SYSTIMER TARGET0 interrupt.
   * Clear the peripheral flag BEFORE re-enabling the CPU interrupt line,
   * otherwise the ISR re-fires immediately.                               */
  REG_WRITE(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_BIT);

  /* Step 2: Reload next alarm (TARGET0 is one-shot, must reschedule).
   * Use current counter value as base to avoid drift on missed ticks.    */
  {
    uint64_t next = systimer_unit0_read() + SYSTIMER_TICKS_PER_CLOCK;
    systimer_set_target0(next);
  }

  /* Step 3: Charge CPU time to the running process.
   * If interrupted inside a kernel handler (k_reenter != 0), charge HARDWARE.
   * Otherwise charge proc_ptr (current user/task process).
   * lost_ticks covers any ticks missed while the clock task was busy.    */
  if (k_reenter != 0)
    rp = proc_addr(HARDWARE);
  else
    rp = proc_ptr;

  ticks      = lost_ticks + 1;
  lost_ticks = 0;
  rp->user_time += ticks;
  if (rp != bill_ptr && rp != proc_addr(IDLE))
    bill_ptr->sys_time += ticks;   /* unbillable task time → billed as sys */

  /* Step 4: Accumulate ticks; compute virtual 'now' for alarm/TTY checks. */
  pending_ticks += ticks;
  now = realtime + pending_ticks;

  /* Step 5: Wake TTY if its timeout has expired. */
  if (tty_timeout <= now) tty_wakeup(now);

  /* Step 6: Give printer a chance to restart if stalled. */
  pr_restart();

  /* Step 7: Switch to do_clocktick() if:
   *   (a) an alarm has expired, OR
   *   (b) the scheduling quantum is up AND the current bill_ptr has not
   *       changed since last tick AND a user process is waiting to run.
   * Occasional false positives are harmless (do_clocktick is idempotent).
   */
  if (next_alarm <= now ||
      sched_ticks == 1 &&
      bill_ptr == prev_ptr &&
#if (SHADOWING == 0)
      rdy_head[USER_Q] != NIL_PROC) {
#else
      (rdy_head[USER_Q] != NIL_PROC || rdy_head[SHADOW_Q] != NIL_PROC)) {
#endif
    interrupt(CLOCK);
    return 1;       /* re-enable interrupts */
  }

  /* Step 8: Count down the scheduling quantum. */
  if (--sched_ticks == 0) {
    sched_ticks = SCHED_RATE;   /* reset quantum        */
    prev_ptr    = bill_ptr;     /* new previous process */
  }

  return 1;   /* re-enable clock interrupt */
}


/*===========================================================================*
 *                              init_clock                                    *
 *===========================================================================*/
PRIVATE void init_clock()
{
/* Initialize the ESP32-S3 SYSTIMER to generate Minix clock ticks at HZ.
 *
 * SYSTIMER UNIT0 is a 52-bit counter clocked at a fixed 16 MHz.
 * TARGET0 is configured in one-shot (alarm) mode and reloaded each ISR.
 * This gives the same periodic behaviour as the 8253 square-wave mode,
 * but requires an explicit reload in clock_handler() after each tick.
 *
 * Interrupt routing: TARGET0 → CPU interrupt line CLOCK_IRQ.
 * The interrupt matrix mapping must be set up in arch/esp32s3/interrupt.c
 * before init_clock() is called.
 */

  /* Enable SYSTIMER peripheral clock gate. */
  REG_SET_BIT(SYSTIMER_CONF_REG, SYSTIMER_CLK_EN);

  /* Start UNIT0 free-running counter. */
  REG_SET_BIT(SYSTIMER_CONF_REG, SYSTIMER_TIMER_UNIT0_WORK_EN);

  /* Configure TARGET0 in one-shot alarm mode, tied to UNIT0. */
  systimer_enable_target0_alarm();

  /* Load first alarm: current counter + one tick period. */
  {
    uint64_t first = systimer_unit0_read() + SYSTIMER_TICKS_PER_CLOCK;
    systimer_set_target0(first);
  }

  /* Unmask TARGET0 interrupt inside the SYSTIMER peripheral. */
  REG_SET_BIT(SYSTIMER_INT_ENA_REG, SYSTIMER_TARGET0_INT_BIT);

  /* Register Minix IRQ handler and enable the CPU-side interrupt line. */
  put_irq_handler(CLOCK_IRQ, clock_handler);
  enable_irq(CLOCK_IRQ);
}


/*===========================================================================*
 *                              clock_stop                                    *
 *===========================================================================*/
PUBLIC void clock_stop()
{
/* Shut down the clock interrupt.  Called during system reboot to prevent
 * spurious ticks while the kernel is tearing down.
 */

  /* Mask TARGET0 inside SYSTIMER so no new alarms fire. */
  REG_CLR_BIT(SYSTIMER_INT_ENA_REG, SYSTIMER_TARGET0_INT_BIT);

  /* Clear any already-pending TARGET0 flag. */
  REG_WRITE(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_BIT);

  /* Stop the UNIT0 counter. */
  REG_CLR_BIT(SYSTIMER_CONF_REG, SYSTIMER_TIMER_UNIT0_WORK_EN);

  /* Disable the CPU-side interrupt line. */
  disable_irq(CLOCK_IRQ);
}


/*===========================================================================*
 *                              milli_delay                                   *
 *===========================================================================*/
PUBLIC void milli_delay(millisec)
unsigned millisec;
{
/* Busy-wait for at least 'millisec' milliseconds.
 * Used during early boot and driver init before the clock task is running.
 * Safe to call from any context; does not rely on interrupts or the
 * Minix scheduler.
 */
  struct milli_state ms;
  milli_start(&ms);
  while (milli_elapsed(&ms) < millisec) {}
}


/*===========================================================================*
 *                              milli_start                                   *
 *===========================================================================*/
PUBLIC void milli_start(msp)
struct milli_state *msp;
{
/* Snapshot the SYSTIMER UNIT0 counter as the start reference for
 * subsequent calls to milli_elapsed().
 */
  msp->start_count = systimer_unit0_read();
}


/*===========================================================================*
 *                              milli_elapsed                                 *
 *===========================================================================*/
PUBLIC unsigned milli_elapsed(msp)
struct milli_state *msp;
{
/* Return milliseconds elapsed since milli_start().
 *
 * SYSTIMER UNIT0 is a 52-bit counter at 16 MHz (SYSTIMER_CLK_HZ = 16000000).
 *   elapsed_ms = delta_ticks / (SYSTIMER_CLK_HZ / 1000)
 *              = delta_ticks / 16000
 *
 * The subtraction wraps correctly even if UNIT0 overflows its 52-bit range
 * (overflow period ≈ 281474976710656 / 16000000 ≈ 17592186 seconds ≈ 203 days).
 * No accumulation loop is needed unlike the 8253 latch approach, because
 * SYSTIMER is a true free-running 64-bit (hardware-extended) counter.
 */
  uint64_t now   = systimer_unit0_read();
  uint64_t delta = now - msp->start_count;
  return (unsigned)(delta / (SYSTIMER_CLK_HZ / 1000));
}