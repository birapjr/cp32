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
#include "irq_frame.h"

extern void kernel_idle_loop(void);
extern void clock_task(void);
extern void sys_task(void);
extern void cp32_clock_alarm_probe_arm(void);
extern void cp32_clock_alarm_probe_service(void);
extern int cp32_clock_task_probe_once(void);
extern int cp32_clock_ipc_probe_once(void);
extern int cp32_clock_request_probe_once(void);
extern int cp32_clock_service_probe_once(void);
extern volatile uint32_t cp32_clock_request_probe_stage;
extern volatile int cp32_clock_request_probe_error;
extern volatile phys_bytes cp32_clock_request_sender_map;
extern volatile phys_bytes cp32_clock_request_clock_map;

#ifndef CP32_ENABLE_CLOCK_STARTUP
#define CP32_ENABLE_CLOCK_STARTUP 0
#endif

#ifndef CP32_ENABLE_SYS_STARTUP
#define CP32_ENABLE_SYS_STARTUP 0
#endif

#ifndef CP32_ENABLE_TTY_STARTUP
#define CP32_ENABLE_TTY_STARTUP 0
#endif

#ifndef CP32_ENABLE_CLOCK_ALARM_PROBE
#define CP32_ENABLE_CLOCK_ALARM_PROBE 0
#endif

#ifndef CP32_ENABLE_CLOCK_TASK_PROBE
#define CP32_ENABLE_CLOCK_TASK_PROBE 0
#endif

#ifndef CP32_ENABLE_CLOCK_IPC_PROBE
#define CP32_ENABLE_CLOCK_IPC_PROBE 0
#endif

#ifndef CP32_ENABLE_CLOCK_SERVICE_PROBE
#define CP32_ENABLE_CLOCK_SERVICE_PROBE 0
#endif
/* MINIX task-table metadata, kept separate from the ESP32-S3 frame setup.
 * The entry points and stack sizes are descriptors only until production task
 * startup is enabled. */
static struct tasktab cp32_tasktab[] = {
  { 0,             4096, "TTY" },
  { 0,             2048, "SYN_AL" },
  { kernel_idle_loop, 2048, "IDLE" },
  { 0,             2048, "MEMORY" },
  { CP32_ENABLE_CLOCK_STARTUP ? clock_task : 0, 4096, "CLOCK" },
  { CP32_ENABLE_SYS_STARTUP ? sys_task : 0, 4096, "SYS" },
  { 0,                0, "HARDWAR" },
  { 0,                0, "MM" },
  { 0,                0, "FS" }
};
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
extern volatile uint32_t cp32_irq_notify_deferred;
extern volatile uint32_t cp32_irq_notify_delivered;
extern volatile uint32_t cp32_irq_notify_replayed;
extern volatile int k_reenter;
extern volatile int cp32_context_restore_gate;
extern volatile int cp32_context_handoff_gate;
extern volatile uint32_t cp32_blocked_syscall_count;
extern volatile int cp32_blocked_handoff_gate;
extern volatile struct proc *cp32_blocked_return_proc;
extern volatile uint32_t cp32_blocked_handoff_count;
extern volatile uint32_t cp32_blocked_ready_guard_count;
extern volatile uint32_t cp32_ready_blocked_skip_count;
extern volatile uint32_t cp32_blocked_frame_mismatch_count;
extern volatile uint32_t cp32_blocked_frame_save_count;
extern volatile uint32_t cp32_blocked_frame_wake_count;
extern volatile uint32_t cp32_blocked_resume_count;
extern volatile uint32_t cp32_blocked_frame_restore_count;
extern volatile uint32_t cp32_user_blocked_return_count;
extern volatile int cp32_user_handoff_gate;
extern volatile uint32_t cp32_user_rfe_epc;
extern volatile uint32_t cp32_user_rfe_ps;
extern volatile uint32_t cp32_user_rfe_sp;
extern volatile uint32_t cp32_user_rfe_count;
extern volatile uint32_t cp32_user_handoff_reject_count;
extern volatile uint32_t cp32_user_trap_probe_count;
extern int cp32_user_trap_probe(struct proc *owner);
extern volatile uint32_t cp32_sched_handoff_count;
extern volatile uint32_t cp32_handoff_owner_mismatch_count;
extern int cp32_user_trap_dispatch(struct proc *owner,
                                   cp32_user_frame_t *frame, int cause);
extern volatile int cp32_user_trap_gate;
extern volatile uint32_t cp32_handoff_blocked_target_count;
extern struct proc *current_proc;

volatile uint32_t cp32_task1_ticks;
volatile uint32_t cp32_task2_ticks;
message cp32_probe_message;
message cp32_probe_sender_message;
extern void cp32_user_probe_entry(void);
extern void cp32_user_probe_done(void);
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
extern void cp32_user_reply_entry(void);
extern void cp32_probe_ready_reply(void);
#endif
extern void cp32_enter_initial_user(struct proc *owner);
extern volatile int cp32_probe_wake_once;
extern volatile int cp32_user_probe_mode;

#ifndef CP32_ENABLE_USER_PROBE
#define CP32_ENABLE_USER_PROBE 0
#endif

#ifndef CP32_ENABLE_TASK_STARTUP
#define CP32_ENABLE_TASK_STARTUP 0
#endif

static void cp32_task1_loop(void)
{
  for (;;) {
    cp32_task1_ticks++;
    delay(1000);
  }
}

static void cp32_task2_loop(void)
{
  for (;;) {
    cp32_task2_ticks++;
    delay(1000);
  }
}

/* Simple test for IPC and MM */
void test_ipc_mm(void) {
    usbj_print("\r\n[TEST] Starting IPC and MM validation...\r\n");

    struct proc *p1 = &proc[1];
    struct proc *p2 = &proc[2];

    message m1, m2;
    struct proc *saved_proc = proc_ptr;
    memset(&m1, 0, sizeof(message));
    memset(&m2, 0, sizeof(message));
    m1.m_source = 12345; /* Forged source must be replaced by the kernel. */
    m1.m_type = 42;
    strcpy(m1.m3_ca1, "Hello IPC!");

    /* Isolate the test maps: the bootstrap S map otherwise maps low virtual
     * addresses (including 1), invalidating the unmapped-buffer test. */
    memset(p1->p_map, 0, sizeof(p1->p_map));
    memset(p2->p_map, 0, sizeof(p2->p_map));
    /* Map the actual kernel test buffers so mem_copy can validate them. */
    p1->p_map[D].mem_vir = (vir_bytes)(uintptr_t)&m1 & ~(CLICK_SIZE - 1);
    p1->p_map[D].mem_phys = ((phys_bytes)(uintptr_t)&m1) >> CLICK_SHIFT;
    p1->p_map[D].mem_len = (((vir_bytes)&m1 & (CLICK_SIZE - 1)) + sizeof(m1) + CLICK_SIZE - 1) >> CLICK_SHIFT;
    p2->p_map[D].mem_vir = (vir_bytes)(uintptr_t)&m2 & ~(CLICK_SIZE - 1);
    p2->p_map[D].mem_phys = ((phys_bytes)(uintptr_t)&m2) >> CLICK_SHIFT;
    p2->p_map[D].mem_len = (((vir_bytes)&m2 & (CLICK_SIZE - 1)) + sizeof(m2) + CLICK_SIZE - 1) >> CLICK_SHIFT;
    
    extern int _send(int dest, message *m);
    extern int _receive(int src, message *m);

    /* Block the receiver first, then deliver through the sender. This
     * exercises the MINIX wakeup path instead of only immediate delivery. */
    proc_ptr = p2;
    int res = _receive(p1->p_nr, &m2);
    int blocked = p2->p_flags == RECEIVING;
    int receiver_frame_saved = p2->p_blocked_frame_valid &&
        p2->p_blocked_frame_pc == p2->p_reg.pc &&
        p2->p_blocked_frame_psw == p2->p_reg.psw &&
        p2->p_blocked_frame_sp == p2->p_reg.sp;
    proc_ptr = p1;
    int send_res = _send(p2->p_nr, &m1);
    int pass = res == OK && send_res == OK && blocked &&
        receiver_frame_saved && !p2->p_blocked_frame_valid &&
        p1->p_flags == 0 && p2->p_flags == 0 &&
        m2.m_source == p1->p_nr && m2.m_type == 42 &&
        strcmp(m2.m3_ca1, "Hello IPC!") == 0 && m1.m_source == 12345;
    usbj_print("[IPC V9 receiver-first pass=");
    usbj_print_u32(pass);
    usbj_print("][MM V9]\r\n");
    if (!pass) panic("IPC receiver-first", 9);

    memset(&m2, 0, sizeof(m2));
    res = _send(p2->p_nr, &m1);
    blocked = p1->p_flags == SENDING;
    int sender_frame_saved = p1->p_blocked_frame_valid &&
        p1->p_blocked_frame_pc == p1->p_reg.pc &&
        p1->p_blocked_frame_psw == p1->p_reg.psw &&
        p1->p_blocked_frame_sp == p1->p_reg.sp;
    proc_ptr = p2;
    int recv_res = _receive(p1->p_nr, &m2);
    pass = res == OK && recv_res == OK && blocked &&
        sender_frame_saved && !p1->p_blocked_frame_valid &&
        p1->p_flags == 0 && p2->p_flags == 0 &&
        p2->p_callerq == NIL_PROC && m2.m_source == p1->p_nr &&
        m2.m_type == 42 && strcmp(m2.m3_ca1, "Hello IPC!") == 0 &&
        m1.m_source == 12345;
    usbj_print("[IPC V9 sender-first pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    usbj_print("\r\n");
    if (!pass) panic("IPC sender-first", 9);
    usbj_print("[IPC V22 blocked-frame-wake pass=");
    usbj_print_u32(receiver_frame_saved && sender_frame_saved);
    usbj_print("]\r\n");
    if (!receiver_frame_saved || !sender_frame_saved)
      panic("IPC blocked frame", 22);
    /* Keep proc_ptr on the receiver: the internal gateway must use its
     * explicit caller, not the currently selected process. */
    memset(&m2, 0, sizeof(m2));
    res = _receive(p1->p_nr, &m2);
    send_res = lock_mini_send(p1, p2->p_nr, &m1);
    pass = res == OK && send_res == OK && p2->p_flags == 0 &&
        m2.m_source == p1->p_nr && m2.m_type == 42 &&
        strcmp(m2.m3_ca1, "Hello IPC!") == 0 && proc_ptr == p2;
    usbj_print("[IPC V10 gateway pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    if (!pass) panic("IPC gateway", 10);

    pass = mini_send(p1, p1->p_nr, &m1) == ELOCKED &&
        mini_send(p1, ANY, &m1) == E_BAD_DEST &&
        mini_rec(p2, ANY + 1, &m2) == E_BAD_SRC &&
        mini_send(p1, p2->p_nr, (message *)0) == EINVAL &&
        mini_rec(p2, ANY, (message *)0) == EINVAL &&
        p1->p_flags == 0 && p2->p_flags == 0 &&
        p1->p_callerq == NIL_PROC && p2->p_callerq == NIL_PROC;
    usbj_print("[IPC V10 rejection pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    if (!pass) panic("IPC rejection", 10);

    /* Simulate both halves of SENDREC before timer enable. Accepting the
     * request must not make the client runnable until its reply arrives. */
    proc_ptr = p1;
    res = sendrec(p2->p_nr, &m1);
    pass = res == OK && p1->p_flags == (SENDING | RECEIVING) &&
        p1->p_getfrom == p2->p_nr && p2->p_callerq == p1;
    proc_ptr = p2;
    recv_res = _receive(p1->p_nr, &m2);
    pass = pass && recv_res == OK && p1->p_flags == RECEIVING &&
        p1->p_sendlink == NIL_PROC && p2->p_callerq == NIL_PROC &&
        m2.m_source == p1->p_nr && m2.m_type == 42;
    m2.m_type = 43;
    strcpy(m2.m3_ca1, "Reply IPC!");
    send_res = _send(p1->p_nr, &m2);
    pass = pass && send_res == OK && p1->p_flags == 0 &&
        p2->p_flags == 0 && m1.m_source == p2->p_nr &&
        m1.m_type == 43 && strcmp(m1.m3_ca1, "Reply IPC!") == 0;
    usbj_print("[IPC V11 sendrec pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    if (!pass) panic("IPC sendrec", 11);

    /* Coalesced notification, followed by notification to a waiting task. */
    proc_ptr = p1; /* p1 is SYN_ALRM_TASK; p2 is the reserved IDLE slot. */
    interrupt(p1->p_nr);
    interrupt(p1->p_nr);
    pass = p1->p_int_blocked && proc_ptr == p1;
    res = _receive(HARDWARE, &m1);
    pass = pass && res == OK && !p1->p_int_blocked &&
        p1->p_flags == 0 && m1.m_source == HARDWARE && m1.m_type == HARD_INT;
    res = _receive(ANY, &m1);
    pass = pass && res == OK && p1->p_flags == RECEIVING;
    interrupt(p1->p_nr);
    pass = pass && p1->p_flags == 0 && !p1->p_int_blocked &&
        m1.m_source == HARDWARE && m1.m_type == HARD_INT && proc_ptr == p1;
    usbj_print("[IPC V12 interrupt pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    if (!pass) panic("IPC interrupt", 12);

    /* Exercise held-list transitions before IRQ enable. This simulates the
     * nesting counter only; it does not cause a physical nested interrupt. */
    extern struct proc *held_head, *held_tail;
    int saved_reenter = k_reenter;
    res = _receive(HARDWARE, &m1);
    k_reenter = 2;
    interrupt(p1->p_nr);
    interrupt(p1->p_nr);
    pass = res == OK && p1->p_flags == RECEIVING && p1->p_int_held &&
        held_head == p1 && held_tail == p1 && p1->p_nextheld == NIL_PROC;
    unhold();
    pass = pass && held_head == p1 && p1->p_flags == RECEIVING;
    k_reenter = saved_reenter;
    unhold();
    pass = pass && held_head == NIL_PROC && held_tail == NIL_PROC &&
        p1->p_nextheld == NIL_PROC && !p1->p_int_held &&
        !p1->p_int_blocked && p1->p_flags == 0 &&
        m1.m_source == HARDWARE && m1.m_type == HARD_INT && proc_ptr == p1;
    usbj_print("[IPC V13 held-replay pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    if (!pass) panic("IPC held replay", 13);

    /* Aggregate the MINIX interrupt() contract: duplicate notifications are
     * coalesced while nested, then exactly one held notification is replayed
     * and delivered after the receiver is waiting again. */
    pass = cp32_irq_notify_deferred >= 2 &&
        cp32_irq_notify_replayed >= 1 && cp32_irq_notify_delivered >= 2;
    usbj_print("[IRQ V1 notify-contract pass=");
    usbj_print_u32(pass);
    usbj_print(" deferred=");
    usbj_print_u32(cp32_irq_notify_deferred);
    usbj_print(" delivered=");
    usbj_print_u32(cp32_irq_notify_delivered);
    usbj_print(" replayed=");
    usbj_print_u32(cp32_irq_notify_replayed);
    usbj_print("]\r\n");
    if (!pass) panic("IRQ notification", 1);

    /* A waits for B; B's attempt to send back must fail without queueing B.
     * Receiving A's original message must still recover both processes. */
    res = _send(p2->p_nr, &m1);
    proc_ptr = p2;
    send_res = _send(p1->p_nr, &m2);
    pass = res == OK && send_res == ELOCKED &&
        p1->p_flags == SENDING && p2->p_flags == 0 &&
        p2->p_callerq == p1 && p1->p_callerq == NIL_PROC &&
        p1->p_sendlink == NIL_PROC;
    recv_res = _receive(p1->p_nr, &m2);
    pass = pass && recv_res == OK && p1->p_flags == 0 &&
        p2->p_flags == 0 && p2->p_callerq == NIL_PROC &&
        m2.m_source == p1->p_nr;
    usbj_print("[IPC V14 deadlock pass=");
    usbj_print_u32(pass);
    usbj_print("]\r\n");
    if (!pass) panic("IPC deadlock", 14);
    pass = mini_send(p1, p2->p_nr, (message *)1) == EFAULT &&
        mini_rec(p2, ANY, (message *)1) == EFAULT &&
        numap(p1->p_nr, (vir_bytes)-8, MESS_SIZE) == 0 &&
        numap(ANY, (vir_bytes)&m1, MESS_SIZE) == 0 &&
        p1->p_flags == 0 && p2->p_flags == 0 &&
        p1->p_callerq == NIL_PROC && p2->p_callerq == NIL_PROC;
    usbj_print("[IPC V16 buffers pass=");
    usbj_print_u32(pass);
    usbj_print("][MM V11]\r\n");
    if (!pass) panic("IPC buffers", 16);

    /* A synthetic virtual base maps to m1's real backing page. Both pending
     * and waiting-receiver delivery must translate, never dereference alias. */
    vir_clicks original_base = p1->p_map[D].mem_vir;
    p1->p_map[D].mem_vir = 0x10000000;
    message *alias = (message *)(0x10000000 +
        ((vir_bytes)&m1 - original_base));
    proc_ptr = p1;
    m1.m_source = 0;
    m1.m_type = 0;
    interrupt(p1->p_nr);
    res = _receive(HARDWARE, alias);
    pass = res == OK && !p1->p_int_blocked &&
        m1.m_source == HARDWARE && m1.m_type == HARD_INT;
    m1.m_source = 0;
    m1.m_type = 0;
    res = _receive(HARDWARE, alias);
    pass = pass && res == OK && p1->p_flags == RECEIVING;
    interrupt(p1->p_nr);
    pass = pass && p1->p_flags == 0 && !p1->p_int_blocked &&
        m1.m_source == HARDWARE && m1.m_type == HARD_INT;
    p1->p_map[D].mem_vir = original_base;
    usbj_print("[IPC V17 translated-irq pass=");
    usbj_print_u32(pass);
    usbj_print("][MM V12]\r\n");
    if (!pass) panic("IPC translated IRQ", 17);
    usbj_print("[IPC V19 blocked-count pass=");
    usbj_print_u32(cp32_blocked_syscall_count == 8);
    usbj_print(" n=");
    usbj_print_u32(cp32_blocked_syscall_count);
    usbj_print("]\r\n");
    proc_ptr = saved_proc;

    /* Exercise the dispatcher validation without changing process state. */
    usbj_print("[TEST] syscall invalid-function: ");
    res = sys_call(0, p2->p_nr, &m1);
    usbj_print_u32((uint32_t)res);
    usbj_print(" [SYS V7]\r\n\r\n");
}

/* ── main ─────────────────────────────────────────────────────────────────────
 * Kernel entry point — called by the STEP 6 - call0   main - in mpx32.S. */
void kernel_idle_loop(void) {
    static int idle_reported;
    if (!idle_reported) {
      usbj_print("[IDLE] entered idle loop\r\n");
      idle_reported = 1;
    }
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

  {
    int tasktab_ok = (sizeof(cp32_tasktab) / sizeof(cp32_tasktab[0]) == NR_TASKS) &&
        cp32_tasktab[2].initial_pc != 0 &&
        cp32_tasktab[0].stksize >= 2048 &&
        cp32_tasktab[4].stksize >= 2048 &&
        cp32_tasktab[5].stksize >= 2048;
    usbj_print("[TASK V1 descriptor-table pass=");
    usbj_print_u32(tasktab_ok);
    usbj_print(" count=");
    usbj_print_u32((uint32_t)(sizeof(cp32_tasktab) / sizeof(cp32_tasktab[0])));
    usbj_print("]\r\n");
    if (!tasktab_ok) panic("task descriptor table", 1);
  }
  
  // Use the existing ktsb declaration from line 39
  ktsb = (reg_t)_stack_bottom + 0x4000; // Offset from bottom to avoid overlap

  for (t = -NR_TASKS; t <= LOW_USER; ++t) {
    rp = proc_addr(t);
    
    // 1. Assign Name (simplified since tasktab might not be fully ported)
    
    // 2. Initialize Registers
    rp->p_reg.pc = (reg_t)kernel_idle_loop; // Point to a valid execution loop
    if (t == 1) rp->p_reg.pc = (reg_t)cp32_task1_loop;
    if (t == 2) rp->p_reg.pc = (reg_t)cp32_task2_loop;
#if CP32_ENABLE_USER_PROBE
    if (t == 1) rp->p_reg.pc = (reg_t)cp32_user_probe_entry;
    if (t == 1) rp->p_reg.psw = 0x0;  /* ESP32-S3 has no PS.UM bit */
    if (t == 2) rp->p_flags = P_SLOT_FREE;
#endif
    rp->p_reg.psw = istaskp(rp) ? 0x100 : 0x0; // Simplified PSW
    if (t == 1 || t == 2) rp->p_reg.psw = 0x100;
    
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
    /* The live register frame must agree with the dedicated SP field. */
    rp->p_reg.a[1] = rp->p_reg.sp;

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

#if CP32_ENABLE_TASK_STARTUP
  /* First production descriptor handoff: IDLE is safe to validate because it
   * never returns and does not require a device or IPC service.  TTY/CLOCK/SYS
   * remain disabled until their own startup contracts are proven. */
  {
    struct proc *idle = proc_addr(IDLE);
    int startup_ok = cp32_tasktab[2].initial_pc != 0 &&
        cp32_tasktab[2].stksize >= 2048 && idle->p_reg.sp != 0 &&
        (idle->p_reg.sp & 0x0F) == 0;
    if (startup_ok) {
      idle->p_reg.pc = (reg_t)cp32_tasktab[2].initial_pc;
      idle->p_reg.a[1] = idle->p_reg.sp;
    }
    usbj_print("[TASK V2 idle-startup pass=");
    usbj_print_u32(startup_ok);
    usbj_print(" pc=");
    usbj_print_u32((uint32_t)idle->p_reg.pc);
    usbj_print(" sp=");
    usbj_print_u32((uint32_t)idle->p_reg.sp);
    usbj_print("]\r\n");
    if (!startup_ok) panic("idle task startup", 2);
  }
#if CP32_ENABLE_CLOCK_STARTUP
  {
    struct proc *clock = proc_addr(CLOCK);
    int startup_ok = cp32_tasktab[4].initial_pc != 0 &&
        cp32_tasktab[4].stksize >= 4096 && clock->p_reg.sp != 0 &&
        (clock->p_reg.sp & 0x0F) == 0 && clock->p_nr == CLOCK;
    if (startup_ok) {
      clock->p_reg.pc = (reg_t)cp32_tasktab[4].initial_pc;
      clock->p_reg.a[1] = clock->p_reg.sp;
    }
    usbj_print("[TASK V3 clock-startup pass=");
    usbj_print_u32(startup_ok);
    usbj_print(" pc=");
    usbj_print_u32((uint32_t)clock->p_reg.pc);
    usbj_print(" sp=");
    usbj_print_u32((uint32_t)clock->p_reg.sp);
    usbj_print("]\r\n");
    if (!startup_ok) panic("clock task startup", 3);
  }
#endif
#if CP32_ENABLE_SYS_STARTUP
  {
    struct proc *sys = proc_addr(-2); /* MINIX SYS task number */
    int startup_ok = cp32_tasktab[5].initial_pc != 0 &&
        cp32_tasktab[5].stksize >= 4096 && sys->p_reg.sp != 0 &&
        (sys->p_reg.sp & 0x0F) == 0 && sys->p_nr == -2;
    if (startup_ok) {
      sys->p_reg.pc = (reg_t)cp32_tasktab[5].initial_pc;
      sys->p_reg.a[1] = sys->p_reg.sp;
    }
    usbj_print("[TASK V4 sys-startup pass=");
    usbj_print_u32(startup_ok);
    usbj_print(" pc=");
    usbj_print_u32((uint32_t)sys->p_reg.pc);
    usbj_print(" sp=");
    usbj_print_u32((uint32_t)sys->p_reg.sp);
    usbj_print("]\r\n");
    if (!startup_ok) panic("sys task startup", 4);
  }
#endif
#if CP32_ENABLE_TTY_STARTUP
  {
    struct proc *tty = proc_addr(-NR_TASKS);
    /* Keep the unfinished device task out of the ready queues while its
     * descriptor is validated. */
    tty->p_flags = P_SLOT_FREE;
    int startup_ok = cp32_tasktab[0].initial_pc == 0 &&
        cp32_tasktab[0].stksize >= 4096 && tty->p_reg.sp != 0 &&
        (tty->p_reg.sp & 0x0F) == 0 && tty->p_nr == -NR_TASKS &&
        (tty->p_flags & P_SLOT_FREE) != 0;
    usbj_print("[TASK V5 tty-descriptor pass=");
    usbj_print_u32((uint32_t)startup_ok);
    usbj_print(" stack=");
    usbj_print_u32((uint32_t)cp32_tasktab[0].stksize);
    usbj_print(" disabled=");
    usbj_print_u32((uint32_t)((tty->p_flags & P_SLOT_FREE) != 0));
    usbj_print("]\r\n");
    if (!startup_ok) panic("tty task descriptor", 5);
  }
#endif
#endif
  
  bill_ptr = proc_addr(IDLE);
#if CP32_ENABLE_USER_PROBE
  cp32_user_probe_mode = 1;
  cp32_user_trap_gate = 1;
  /* Keep the two probe processes addressable by their public MINIX numbers;
   * the generic bootstrap table is offset by the task range. */
  proc_addr(1)->p_nr = 1;
  proc_addr(2)->p_nr = 2;
  cp32_probe_sender_message.m_type = 0x43503332;
  cp32_probe_message.m_type = 0;
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
  cp32_user_handoff_gate = 1;
  proc_addr(1)->p_nr = 1;
  proc_addr(2)->p_nr = 2;
  proc_addr(2)->p_reg.pc = (reg_t)cp32_user_reply_entry;
  proc_addr(2)->p_flags = 0;
#endif
  proc_addr(2)->p_flags = P_SLOT_FREE;
  proc_addr(1)->p_map[D].mem_vir =
      (vir_bytes)(uintptr_t)&cp32_probe_message & ~(CLICK_SIZE - 1);
  proc_addr(1)->p_map[D].mem_phys =
      ((phys_bytes)(uintptr_t)&cp32_probe_message) >> CLICK_SHIFT;
  proc_addr(1)->p_map[D].mem_len =
      (((vir_bytes)(uintptr_t)&cp32_probe_message & (CLICK_SIZE - 1)) +
       sizeof(cp32_probe_message) + CLICK_SIZE - 1) >> CLICK_SHIFT;
#ifdef CP32_ENABLE_BLOCKED_PROBE
  proc_addr(2)->p_map[D].mem_vir =
      (vir_bytes)(uintptr_t)&cp32_probe_sender_message & ~(CLICK_SIZE - 1);
  proc_addr(2)->p_map[D].mem_phys =
      ((phys_bytes)(uintptr_t)&cp32_probe_sender_message) >> CLICK_SHIFT;
  proc_addr(2)->p_map[D].mem_len =
      (((vir_bytes)(uintptr_t)&cp32_probe_sender_message & (CLICK_SIZE - 1)) +
       sizeof(cp32_probe_sender_message) + CLICK_SIZE - 1) >> CLICK_SHIFT;
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
  proc_addr(1)->p_map[D].mem_vir =
      (vir_bytes)(uintptr_t)&cp32_probe_sender_message & ~(CLICK_SIZE - 1);
  proc_addr(1)->p_map[D].mem_phys =
      ((phys_bytes)(uintptr_t)&cp32_probe_sender_message) >> CLICK_SHIFT;
  proc_addr(1)->p_map[D].mem_len = proc_addr(2)->p_map[D].mem_len;
  proc_addr(2)->p_map[D].mem_vir =
      (vir_bytes)(uintptr_t)&cp32_probe_message & ~(CLICK_SIZE - 1);
  proc_addr(2)->p_map[D].mem_phys =
      ((phys_bytes)(uintptr_t)&cp32_probe_message) >> CLICK_SHIFT;
  proc_addr(2)->p_map[D].mem_len =
      (((vir_bytes)(uintptr_t)&cp32_probe_message & (CLICK_SIZE - 1)) +
       sizeof(cp32_probe_message) + CLICK_SIZE - 1) >> CLICK_SHIFT;
#endif
  cp32_probe_wake_once = 1;
#endif
  proc_addr(1)->p_int_blocked = 0;
  proc_addr(1)->p_int_held = 0;
  proc_addr(1)->p_callerq = NIL_PROC;
  proc_addr(1)->p_sendlink = NIL_PROC;
  proc_addr(1)->p_nextheld = NIL_PROC;
  proc_addr(1)->p_getfrom = ANY;
  proc_ptr = proc_addr(1);
  current_proc = proc_addr(1);
  proc_addr(1)->p_flags = 0;
#endif
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
  status_line("checking systimer interrupt route", 0);
  if (systimer_route_probe() != OK) {
    usbj_print("FATAL: systimer interrupt route rejected\r\n");
    for (;;) { }
  }
    status_line("starting systimer interrupt probe", 0);
    proc_ptr = &proc[0]; /* Ensure proc_ptr is valid before enabling IRQs */
    cp32_clock_irq_bridge_enabled = 1;
    cp32_context_restore_gate = 1;
    /* First live handoff experiment: keep clock_handler disabled, but allow
     * the IRQ bridge to invoke the scheduler and select a saved frame. */
    cp32_context_handoff_gate = 1;
  test_ipc_mm();
  cp32_prepare_two_task_stress();
#if CP32_ENABLE_USER_PROBE
  /* Stress setup assigns user-range numbers for scheduler diagnostics; the
   * live syscall probes need stable table identities instead. */
  proc_addr(1)->p_nr = 1;
  proc_addr(2)->p_nr = 2;
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
  cp32_user_handoff_gate = 1;
  proc_addr(1)->p_nr = 1;
  proc_addr(2)->p_nr = 2;
  proc_addr(3)->p_nr = 3;
  memset(proc_addr(3)->p_reg.a, 0, sizeof(proc_addr(3)->p_reg.a));
  proc_addr(3)->p_reg.psw = 0;
  proc_addr(3)->p_reg.a[15] = 0x3FC00000;
  proc_addr(3)->p_reg.sp = proc_addr(1)->p_reg.sp + 4096;
  proc_addr(1)->p_reg.a[1] = proc_addr(1)->p_reg.sp;
  proc_addr(3)->p_reg.a[1] = proc_addr(3)->p_reg.sp;
  proc_addr(3)->p_map[S].mem_phys = proc_addr(3)->p_reg.sp >> CLICK_SHIFT;
  proc_addr(3)->p_map[S].mem_len = proc_addr(1)->p_map[S].mem_len;
  proc_addr(3)->p_reg.pc = (reg_t)cp32_user_reply_entry;
  proc_addr(3)->p_flags = 0;
  proc_addr(2)->p_reg.pc = (reg_t)cp32_user_reply_entry;
  proc_addr(3)->p_map[D].mem_vir =
      (vir_bytes)(uintptr_t)&cp32_probe_message & ~(CLICK_SIZE - 1);
  proc_addr(3)->p_map[D].mem_phys =
      ((phys_bytes)(uintptr_t)&cp32_probe_message) >> CLICK_SHIFT;
  proc_addr(3)->p_map[D].mem_len =
      (((vir_bytes)(uintptr_t)&cp32_probe_message & (CLICK_SIZE - 1)) +
       sizeof(cp32_probe_message) + CLICK_SIZE - 1) >> CLICK_SHIFT;
  cp32_probe_ready_reply();
#elif defined(CP32_ENABLE_BLOCKED_PROBE)
  /* Process 2 is the replacement frame; it must not re-enter the trap probe. */
  proc_addr(2)->p_reg.pc = (reg_t)cp32_user_probe_done;
  proc_addr(2)->p_flags = 0;
#endif
#endif
#if 0 /* Retained in history only; one-shot bring-up checks are complete. */
  usbj_print("[SCHED V1 classify pass=1 p1=2 p2=3]\r\n");
  usbj_print("[SCHED V4 blocked-probe pass=");
  usbj_print_u32((uint32_t)cp32_probe_blocked_handoff());
  usbj_print("]\r\n");
  usbj_print("[CTX V69 resumed-syscall-return count=");
  usbj_print_u32(cp32_blocked_resume_count);
  usbj_print("]\r\n");
  usbj_print("[CTX V66 user-rfe-trace count=");
  usbj_print_u32(cp32_user_rfe_count);
  usbj_print(" epc=");
  usbj_print_u32(cp32_user_rfe_epc);
  usbj_print(" ps=");
  usbj_print_u32(cp32_user_rfe_ps);
  usbj_print(" sp=");
  usbj_print_u32(cp32_user_rfe_sp);
  usbj_print("]\r\n");
  usbj_print("[CTX V65 enabled-trap-probe pass=");
  { int probe_result = cp32_user_trap_probe(proc_addr(1));
  usbj_print_u32((cp32_user_probe_mode && probe_result == OK) ||
                 (!cp32_user_probe_mode && probe_result == EBADCALL &&
                  cp32_user_trap_gate == 0));
  usbj_print(" result=");
  usbj_print_u32((uint32_t)probe_result);
  usbj_print(" count=");
  usbj_print_u32(cp32_user_trap_probe_count);
  }
  usbj_print("]\r\n");
  usbj_print("[CTX V64 user-handoff-contract pass=");
  usbj_print_u32(
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
                 cp32_user_handoff_gate == 1 &&
#else
                 cp32_user_handoff_gate == 0 &&
#endif
                 cp32_user_handoff_reject_count == 0);
  usbj_print(" rejects=");
  usbj_print_u32(cp32_user_handoff_reject_count);
  usbj_print("]\r\n");
  usbj_print("[CTX V63 user-blocked-return-guard count=");
  usbj_print_u32(cp32_user_blocked_return_count);
  usbj_print("]\r\n");
  usbj_print("[SCHED V7 blocked-ready-guard pass=");
  usbj_print_u32(cp32_blocked_ready_guard_count == 1);
  usbj_print("]\r\n");
  usbj_print("[SCHED V9 all-blocked-idle pass=");
  usbj_print_u32((uint32_t)cp32_probe_all_blocked_queue());
  usbj_print("]\r\n");
  cp32_prepare_two_task_stress();
  usbj_print("[CTX V50 handoff-reset pass=");
  usbj_print_u32(cp32_handoff_owner_mismatch_count == 0);
  usbj_print("]\r\n");
  usbj_print("[IPC V20 handoff-reset pass=");
  usbj_print_u32(cp32_blocked_handoff_gate == 0 &&
                 cp32_blocked_return_proc == NIL_PROC &&
                 cp32_blocked_handoff_count == 0);
  usbj_print("]\r\n");
  usbj_print("[SCHED V6 blocked-owner-unbilled pass=");
  usbj_print_u32(bill_ptr != cp32_blocked_return_proc);
  usbj_print("]\r\n");
  usbj_print("[IPC V21 owner-reset pass=");
  usbj_print_u32(cp32_blocked_handoff_gate == 0 &&
                 cp32_blocked_return_proc == NIL_PROC &&
                 cp32_blocked_handoff_count == 0);
  usbj_print("]\r\n");
  usbj_print("[CTX V47 gates-ready pass=");
  usbj_print_u32(cp32_context_restore_gate == 1 &&
                 cp32_context_handoff_gate == 1);
  usbj_print("]\r\n");
  usbj_print("[CTX V48 owner-aligned pass=");
  usbj_print_u32(current_proc == proc_ptr);
  usbj_print("]\r\n");
  usbj_print("[CTX V52 syscall-frame pass=");
  usbj_print_u32(sizeof(cp32_syscall_return_contract_t) == 76 &&
                 __builtin_offsetof(cp32_syscall_return_contract_t, a[2]) == 8 &&
                 __builtin_offsetof(cp32_syscall_return_contract_t, pc) == 64 &&
                 __builtin_offsetof(cp32_syscall_return_contract_t, sp) == 72);
  usbj_print("]\r\n");
  usbj_print("[CTX V53 blocked-frame-state pass=");
  usbj_print_u32(proc_addr(1)->p_blocked_frame_valid == 0 &&
                 proc_addr(1)->p_blocked_frame_result == 0 &&
                 cp32_blocked_handoff_gate == 0);
  usbj_print("]\r\n");
  usbj_print("[CTX V54 wake-result-slot pass=");
  usbj_print_u32(proc_addr(1)->p_reg.a[2] == 0 &&
                 proc_addr(1)->p_blocked_frame_result == 0);
  usbj_print("]\r\n");
  usbj_print("[CTX V55 wake-contract pass=");
  usbj_print_u32(proc_addr(1)->p_blocked_frame_valid == 0 &&
                 proc_addr(2)->p_blocked_frame_valid == 0);
  usbj_print("]\r\n");
  usbj_print("[CTX V56 frame-snapshot pass=");
  usbj_print_u32(proc_addr(1)->p_blocked_frame_valid == 0 &&
                 proc_addr(1)->p_blocked_frame_pc == proc_addr(1)->p_reg.pc &&
                 proc_addr(1)->p_blocked_frame_psw == proc_addr(1)->p_reg.psw &&
                 proc_addr(1)->p_blocked_frame_sp == proc_addr(1)->p_reg.sp);
  usbj_print("]\r\n");
  usbj_print("[CTX V57 frame-preservation-mismatch count=");
  usbj_print_u32(cp32_blocked_frame_mismatch_count);
  usbj_print("]\r\n");
  usbj_print("[CTX V58 restore-guard pass=");
  usbj_print_u32(cp32_blocked_handoff_gate == 0 &&
                 cp32_blocked_return_proc == NIL_PROC);
  usbj_print("]\r\n");
  usbj_print("[CTX V59 user-frame-contract pass=");
  usbj_print_u32(sizeof(cp32_user_frame_t) == 76 &&
                 __builtin_offsetof(cp32_user_frame_t, pc) == 64 &&
                 __builtin_offsetof(cp32_user_frame_t, sp) == 72 &&
                 cp32_user_frame_contract_valid(
                   (const cp32_user_frame_t *)&proc_addr(1)->p_reg));
  usbj_print("]\r\n");
  usbj_print("[CTX V60 trap-boundary-guard pass=");
  usbj_print_u32(cp32_user_trap_dispatch(NIL_PROC,
                                         (cp32_user_frame_t *)0, -1) == EINVAL);
  usbj_print("]\r\n");
  usbj_print("[CTX V61 trap-dispatch-pending pass=");
  usbj_print_u32(proc_ptr != NIL_PROC &&
                 cp32_user_frame_contract_valid(
                   (const cp32_user_frame_t *)&proc_ptr->p_reg) &&
                 cp32_user_trap_dispatch(
                   proc_ptr, (cp32_user_frame_t *)&proc_ptr->p_reg, 0) ==
                   (cp32_user_probe_mode ? OK : EBADCALL));
  usbj_print("]\r\n");
  usbj_print("[CTX V62 user-trap-gate pass=");
  usbj_print_u32(cp32_user_trap_gate == 0 || cp32_user_probe_mode == 1);
  usbj_print("]\r\n");
  usbj_print("[IPC V23 blocked-frame-save count=");
  usbj_print_u32(cp32_blocked_frame_save_count);
  usbj_print(" wake=");
  usbj_print_u32(cp32_blocked_frame_wake_count);
  usbj_print(" restore=");
  usbj_print_u32(cp32_blocked_frame_restore_count);
  usbj_print("]\r\n");
  usbj_print("[CTX V49 owner-mismatch count=");
  usbj_print_u32(cp32_handoff_owner_mismatch_count);
  usbj_print("]\r\n");
  usbj_print("[CTX V51 blocked-target count=");
  usbj_print_u32(cp32_handoff_blocked_target_count);
  usbj_print("]\r\n");
  usbj_print("[SCHED V8 blocked-ready-skip count=");
  usbj_print_u32(cp32_ready_blocked_skip_count);
  usbj_print("]\r\n");
  usbj_print("[SCHED V2 baseline-handoffs=");
  usbj_print_u32(cp32_sched_handoff_count);
  usbj_print("]\r\n");
  usbj_print("[STK V1 p1=");
  usbj_print_u32((uint32_t)proc_addr(1)->p_reg.sp);
  usbj_print(" p2=");
  usbj_print_u32((uint32_t)proc_addr(2)->p_reg.sp);
  usbj_print(" d=");
  usbj_print_u32((uint32_t)(proc_addr(2)->p_reg.sp - proc_addr(1)->p_reg.sp));
  usbj_print("]\r\n");
#endif
  cp32_context_handoff_gate = 1;
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
  /* The final stress reset above clears ready queues; restore the canonical
   * reply receiver after it, immediately before enabling the live probe. */
  (pproc_addr + NR_TASKS)[1] = cproc_addr(1);
  (pproc_addr + NR_TASKS)[2] = cproc_addr(2);
  (pproc_addr + NR_TASKS)[3] = cproc_addr(3);
  cproc_addr(1)->p_nr = 1;
  cproc_addr(2)->p_nr = 2;
  cproc_addr(3)->p_nr = 3;
  cproc_addr(2)->p_reg.pc = (reg_t)cp32_user_probe_entry;
  cproc_addr(3)->p_reg.pc = (reg_t)cp32_user_reply_entry;
  cp32_user_handoff_gate = 1;
  cp32_probe_ready_reply();
#endif
   unsigned ps_before, ps_locked, ps_unlocked;
   __asm__ volatile("rsr %0, ps" : "=a"(ps_before));
   lock();
   __asm__ volatile("rsr %0, ps" : "=a"(ps_locked));
   unlock();
   __asm__ volatile("rsr %0, ps" : "=a"(ps_unlocked));
   /* Restore boot mask before the timer is armed. */
   __asm__ volatile("wsr %0, ps; rsync" : : "a"(ps_before) : "memory");
   int lock_ok = (ps_locked & 15) == 15 && (ps_unlocked & 15) == 0 &&
       (ps_locked & ~15u) == (ps_before & ~15u) &&
       (ps_unlocked & ~15u) == (ps_before & ~15u);
   usbj_print("[LOCK V1 pass=");
   usbj_print_u32(lock_ok);
   usbj_print("]\r\n");
   if (!lock_ok) panic("status preservation", 1);
   unsigned ps_saved_outer, ps_saved_inner, ps_nested, ps_restored;
   ps_saved_outer = (unsigned)lock_save();
   ps_saved_inner = (unsigned)lock_save();
   __asm__ volatile("rsr %0, ps" : "=a"(ps_nested));
   restore_lock((int)ps_saved_inner);
   restore_lock((int)ps_saved_outer);
   __asm__ volatile("rsr %0, ps" : "=a"(ps_restored));
   unsigned lock_v2_ok = (ps_saved_outer == ps_before) &&
       ((ps_saved_inner & 15u) == 15u) && ((ps_nested & 15u) == 15u) &&
       (ps_restored == ps_saved_outer);
   usbj_print("[LOCK V4 pass=");
   usbj_print_u32(lock_v2_ok);
   usbj_print(" saved=1 nested=1 restored=1 psb=");
   usbj_print_u32(ps_before);
   usbj_print(" pso=");
   usbj_print_u32(ps_saved_outer);
   usbj_print(" psi=");
   usbj_print_u32(ps_saved_inner);
   usbj_print(" psn=");
   usbj_print_u32(ps_nested);
   usbj_print(" psr=");
   usbj_print_u32(ps_restored);
   usbj_print("]\r\n");
   if (!lock_v2_ok) panic("saved lock status", 1);
#if CP32_ENABLE_USER_PROBE
   cp32_user_trap_gate = 1;
#if defined(CP32_ENABLE_BLOCKED_PROBE) || defined(CP32_ENABLE_BLOCKED_SEND_PROBE)
  cp32_user_handoff_gate = 1;
  cp32_blocked_handoff_gate = 1;
#endif
#ifdef CP32_ENABLE_BLOCKED_SEND_PROBE
  proc_addr(2)->p_getfrom = ANY;
  proc_addr(2)->p_messbuf = &cp32_probe_message;
  proc_addr(2)->p_flags = 0;
  cp32_probe_wake_once = 1;
#endif
#endif
#if CP32_ENABLE_CLOCK_TASK_PROBE
  {
    int clock_probe_ok = cp32_clock_task_probe_once();
    usbj_print("[CLOCK V3 service-probe pass=");
    usbj_print_u32((uint32_t)clock_probe_ok);
    usbj_print("]\r\n");
    if (!clock_probe_ok) panic("clock task probe", 3);
  }
#endif
#if CP32_ENABLE_CLOCK_IPC_PROBE
  {
    int clock_ipc_ok = cp32_clock_ipc_probe_once();
    usbj_print("[IPC V28 clock-service-cycles pass=");
    usbj_print_u32((uint32_t)clock_ipc_ok);
    usbj_print("]\r\n");
    if (!clock_ipc_ok) panic("clock IPC probe", 28);
    int clock_request_ok = cp32_clock_request_probe_once();
    usbj_print("[IPC V29 clock-request-reply pass=");
    usbj_print_u32((uint32_t)clock_request_ok);
    usbj_print(" stage=");
    usbj_print_u32(cp32_clock_request_probe_stage);
    usbj_print(" err=");
    usbj_print_u32((uint32_t)cp32_clock_request_probe_error);
    usbj_print(" smap=");
    usbj_print_u32((uint32_t)cp32_clock_request_sender_map);
    usbj_print(" cmap=");
    usbj_print_u32((uint32_t)cp32_clock_request_clock_map);
    usbj_print("]\r\n");
    if (!clock_request_ok) panic("clock request probe", 29);
  }
#endif
#if CP32_ENABLE_CLOCK_SERVICE_PROBE
  {
    int clock_service_ok = cp32_clock_service_probe_once();
    usbj_print("[CLOCK V30 service-lifecycle pass=");
    usbj_print_u32((uint32_t)clock_service_ok);
    usbj_print("]\r\n");
    if (!clock_service_ok) panic("clock service probe", 30);
  }
#endif
   systimer_irq_start();
#if CP32_ENABLE_CLOCK_ALARM_PROBE
  cp32_clock_alarm_probe_arm();
#endif

#if CP32_ENABLE_USER_PROBE
  {
    struct proc *user = proc_addr(1);
    phys_bytes sp_click = user->p_reg.sp >> CLICK_SHIFT;
    vir_bytes msg = (vir_bytes)(uintptr_t)&cp32_probe_message;
    int setup_ok = user != NIL_PROC && user->p_reg.pc != 0 &&
        user->p_reg.sp != 0 && (user->p_reg.sp & 0x0F) == 0 &&
        user->p_reg.a[1] == user->p_reg.sp && user->p_reg.a[15] != 0 &&
        user->p_map[S].mem_len != 0;
    int stack_ok = setup_ok && sp_click >= user->p_map[S].mem_phys &&
        sp_click < user->p_map[S].mem_phys + user->p_map[S].mem_len;
    setup_ok = stack_ok;
    int message_ok = setup_ok && user->p_map[D].mem_len != 0 &&
        msg >= user->p_map[D].mem_vir &&
        msg < user->p_map[D].mem_vir +
         user->p_map[D].mem_len * CLICK_SIZE;
    int message_range_ok = message_ok &&
        msg + MESS_SIZE <= user->p_map[D].mem_vir +
         user->p_map[D].mem_len * CLICK_SIZE;
    usbj_print("[CTX V116 user-message-range-ready pass=");
    usbj_print_u32((uint32_t)message_range_ok);
    usbj_print("]\r\n");
    setup_ok = message_range_ok;
    usbj_print("[CTX V115 user-message-map-ready pass=");
    usbj_print_u32((uint32_t)message_ok);
    usbj_print("]\r\n");
    setup_ok = message_ok;
    int pc_ok = setup_ok && user->p_reg.pc != 0 &&
        (user->p_reg.pc & 0x03) == 0;
    usbj_print("[CTX V117 user-entry-pc-ready pass=");
    usbj_print_u32((uint32_t)pc_ok);
    usbj_print("]\r\n");
    setup_ok = pc_ok;
    int psw_ok = setup_ok && user->p_reg.psw == 0;
    usbj_print("[CTX V118 user-entry-psw-ready pass=");
    usbj_print_u32((uint32_t)psw_ok);
    usbj_print("]\r\n");
    setup_ok = psw_ok;
    int regs_ok = setup_ok && user->p_reg.a[0] == 0 && user->p_reg.a[2] == 0 &&
        user->p_reg.a[3] == 0 && user->p_reg.a[4] == 0;
    usbj_print("[CTX V119 user-initial-registers-ready pass=");
    usbj_print_u32((uint32_t)regs_ok);
    usbj_print("]\r\n");
    setup_ok = regs_ok;
    int identity_ok = setup_ok && user->p_nr == 1 &&
        (user->p_flags & P_SLOT_FREE) == 0;
    usbj_print("[CTX V120 user-process-identity-ready pass=");
    usbj_print_u32((uint32_t)identity_ok);
    usbj_print("]\r\n");
    setup_ok = identity_ok;
    int table_map_ok = setup_ok && (pproc_addr + NR_TASKS)[1] == user;
    usbj_print("[CTX V121 user-process-table-map-ready pass=");
    usbj_print_u32((uint32_t)table_map_ok);
    usbj_print("]\r\n");
    setup_ok = table_map_ok;
    usbj_print("[CTX V114 user-stack-map-ready pass=");
    usbj_print_u32((uint32_t)setup_ok);
    usbj_print("]\r\n");
    usbj_print("[CTX V113 user-address-space-ready pass=");
    usbj_print_u32((uint32_t)setup_ok);
    usbj_print("]\r\n");
    if (!setup_ok) panic("user address space", 1);
    int entry_ready = setup_ok && cp32_user_trap_gate == 1;
    usbj_print("[CTX V122 user-entry-handoff-ready pass=");
    usbj_print_u32((uint32_t)entry_ready);
    usbj_print("]\r\n");
    if (!entry_ready) panic("user entry handoff", 1);
    int entry_contract_ok = entry_ready && user->p_reg.pc ==
        (reg_t)(uintptr_t)cp32_user_probe_entry &&
        (uintptr_t)cp32_enter_initial_user != 0;
    usbj_print("[CTX V123 user-entry-contract-ready pass=");
    usbj_print_u32((uint32_t)entry_contract_ok);
    usbj_print("]\r\n");
    if (!entry_contract_ok) panic("user entry contract", 1);
    int entry_mode_ok = entry_contract_ok && cp32_user_probe_mode == 1 &&
        (((uintptr_t)cp32_user_probe_entry & 0x03) == 0);
    usbj_print("[CTX V124 user-entry-mode-ready pass=");
    usbj_print_u32((uint32_t)entry_mode_ok);
    usbj_print("]\r\n");
    if (!entry_mode_ok) panic("user entry mode", 1);
  }
  usbj_print("[CTX V82 user-frame-save-ready]\r\n");
#ifdef CP32_ENABLE_BLOCKED_SEND_PROBE
  proc_addr(2)->p_getfrom = ANY;
  proc_addr(2)->p_messbuf = &cp32_probe_message;
  proc_addr(2)->p_flags = 0;
  cp32_probe_wake_once = 1;
#endif
  if (cp32_user_trap_probe(proc_addr(1)) != OK)
    panic("user trap cause probe", 1);
  usbj_print("[CTX V125 user-trap-preflight-returned pass=1]\r\n");
  usbj_print("[CTX V126 user-trap-probe-count pass=");
  usbj_print_u32((uint32_t)(cp32_user_trap_probe_count != 0));
  usbj_print("]\r\n");
  if (cp32_user_trap_probe_count == 0) panic("user trap probe count", 1);
  proc_ptr = proc_addr(1);
  current_proc = proc_ptr;
  usbj_print("[CTX V127 user-trap-owner-stable pass=");
  usbj_print_u32((uint32_t)(proc_ptr == proc_addr(1) && proc_ptr->p_nr == 1));
  usbj_print("]\r\n");
  if (proc_ptr != proc_addr(1) || proc_ptr->p_nr != 1)
    panic("user trap owner", 1);
  usbj_print("[CTX V128 user-trap-current-owner-aligned pass=");
  usbj_print_u32((uint32_t)(current_proc == proc_ptr));
  usbj_print("]\r\n");
  if (current_proc != proc_ptr) panic("user current owner", 1);
  cp32_enter_initial_user(proc_addr(1));
#endif

   /* Temporary pre-scheduler idle loop. Keep the watchdogs serviced and emit

   * a low-rate heartbeat so a silent hang can be distinguished from an
   * intentional idle state while task dispatch is still being ported. */
  status_line("entering kernel idle", 0);
  /* With live handoff enabled, leave proc_ptr on the current idle frame.
   * Calling schedule() here would assign main()'s live frame to process 1
   * before the first IRQ and overwrite its task entry PC. */
  if (!cp32_context_handoff_gate)
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

  /* Preserve other PS fields while masking interrupts; panic never returns. */
  unsigned saved_ps;
  __asm__ volatile("rsil %0, 15" : "=a"(saved_ps) : : "memory");
  usbj_print("[PANIC V1] halted\r\n");
  if (s != 0 && *s != 0) {
	  printf("\nKernel panic: %s",s);
	  if (n != NO_NUM) printf(" %d", n);
	    printf("\n");
  }
  for (;;) { __asm__ volatile("nop"); }
}
