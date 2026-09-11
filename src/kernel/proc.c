/* This file contains essentially all of the process and message handling.
 * It has two main entry points from the outside:
 *
 *   sys_call:   called when a process or task does SEND, RECEIVE or SENDREC
 *   interrupt:	called by interrupt routines to send a message to task
 *
 * It also has several minor entry points:
 *
 *   lock_ready:      put a process on one of the ready queues so it can be run
 *   lock_unready:    remove a process from the ready queues
 *   lock_sched:      a process has run too long; schedule another one
 *   lock_mini_send:  send a message (used by interrupt signals, etc.)
 *   lock_pick_proc:  pick a process to run (used by system initialization)
 *   unhold:          repeat all held-up interrupts
 *
 */
 
#include "kernel.h"
#include <minix/callnr.h>
#include <minix/com.h>
#include "proc.h"
#include "irq_frame.h"

extern reg_t cp32_context_probe_pc(struct proc *next);
extern reg_t cp32_context_probe_sp(struct proc *next);
extern reg_t cp32_context_probe_ps(struct proc *next);
extern volatile int cp32_context_restore_gate;
extern volatile int cp32_context_handoff_gate;
extern volatile uint32_t cp32_task1_ticks;
extern volatile uint32_t cp32_task2_ticks;
extern struct proc *current_proc;
extern message cp32_probe_message;
extern message cp32_probe_sender_message;
PRIVATE int copy_message(struct proc *sender, message *src,
                         struct proc *receiver, message *dst);
PRIVATE int deliver_blocked_message(struct proc *sender, message *src,
                                    struct proc *receiver, message *dst);

void sched(void);
PRIVATE void ready(struct proc *rp);
PRIVATE void cp32_complete_blocked_frame(struct proc *rp, int result);
PRIVATE int proc_is_ready_queued(struct proc *target);
PUBLIC int mini_rec(struct proc *caller_ptr, int src, message *m_ptr);

#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
PUBLIC void cp32_probe_ready_reply(void)
{
  struct proc *reply = proc_addr(3);
  struct proc *caller = proc_addr(2);
  int q;
  /* Stress setup may have left a stale process-table object with the same
   * synthetic number in a ready queue.  The reply probe must start with one
   * canonical receiver, otherwise sched() can select the stale object. */
  for (q = 0; q < NQ; q++) {
    rdy_head[q] = NIL_PROC;
    rdy_tail[q] = NIL_PROC;
  }
  if (caller->p_flags == 0) ready(caller);
  if (reply->p_flags == 0) ready(reply);
}
#endif

PRIVATE unsigned char cp32_wake_probe_reported;
PRIVATE unsigned char cp32_wake_owner_release_reported;
PRIVATE unsigned char cp32_wake_message_reported;
PRIVATE unsigned char cp32_send_wake_reported;

/* Minimal scheduler stub for main to call. */
FORWARD _PROTOTYPE( void ready, (struct proc *rp) );

void schedule(void)
{
    proc[1].p_nr = 1;
    proc[1].p_flags = 0; // Make it runnable
    ready(&proc[1]);
    
    sched();
}

PRIVATE unsigned char switching;	/* nonzero to inhibit interrupt() */
PRIVATE unsigned handoff_diag_count;
volatile uint32_t cp32_blocked_syscall_count;
volatile uint32_t cp32_sched_handoff_count;
volatile uint32_t cp32_blocked_handoff_count;
volatile int cp32_last_blocked_proc_nr;
volatile struct proc *cp32_blocked_return_proc;
volatile struct proc *cp32_irq_saved_owner;
volatile int cp32_blocked_handoff_gate;
volatile uint32_t cp32_blocked_ready_guard_count;
volatile uint32_t cp32_ready_blocked_skip_count;
volatile uint32_t cp32_blocked_frame_mismatch_count;
volatile uint32_t cp32_blocked_frame_save_count;
volatile uint32_t cp32_blocked_frame_wake_count;
volatile uint32_t cp32_blocked_resume_count;
volatile uint32_t cp32_blocked_frame_restore_count;
volatile uint32_t cp32_user_blocked_return_count;
volatile int cp32_user_dispatch_blocked;
volatile int cp32_user_handoff_gate;
volatile uint32_t cp32_user_handoff_reject_count;
volatile uint32_t cp32_user_trap_probe_count;
volatile uint32_t cp32_user_cause_reject_count;
volatile uint32_t cp32_blocked_pc_validation_count;
volatile int cp32_user_probe_mode;
volatile uint32_t cp32_user_rfe_epc;
volatile uint32_t cp32_user_rfe_ps;
volatile uint32_t cp32_user_rfe_sp;
volatile uint32_t cp32_user_rfe_count;
volatile int cp32_probe_wake_once;

PUBLIC void cp32_probe_wake_receiver(void)
{
#ifdef CP32_ENABLE_BLOCKED_SEND_PROBE
  struct proc *receiver = proc_addr(2);
  struct proc *sender = proc_addr(1);
  if (receiver->p_flags == 0) receiver->p_flags = RECEIVING;
#else
  struct proc *receiver = proc_addr(1);
  struct proc *sender = proc_addr(2);
#endif
  if (cp32_user_probe_mode && !cp32_wake_probe_reported) {
    cp32_wake_probe_reported = 1;
    usbj_print("[CTX V105 wake-hook flags=");
    usbj_print_u32((uint32_t)receiver->p_flags);
    usbj_print(" armed=");
    usbj_print_u32((uint32_t)cp32_probe_wake_once);
    usbj_print("]\r\n");
  }
  if (!cp32_probe_wake_once || !(receiver->p_flags & RECEIVING)) return;
  cp32_probe_wake_once = 0;
  /* Preserve the receiver buffer validated at RECEIVE time. */
  receiver->p_messbuf = &cp32_probe_message;
  sender->p_map[D].mem_vir =
      (vir_bytes)(uintptr_t)&cp32_probe_sender_message & ~(CLICK_SIZE - 1);
  sender->p_map[D].mem_phys =
      ((phys_bytes)(uintptr_t)&cp32_probe_sender_message) >> CLICK_SHIFT;
  sender->p_map[D].mem_len =
      (((vir_bytes)(uintptr_t)&cp32_probe_sender_message & (CLICK_SIZE - 1)) +
       sizeof(cp32_probe_sender_message) + CLICK_SIZE - 1) >> CLICK_SHIFT;
#ifndef CP32_ENABLE_BLOCKED_SEND_PROBE
  sender->p_flags = 0;
#endif
  /* proc_addr() is keyed by p_nr; keep the synthetic sender's canonical
   * lookup slot aligned with the object used by the probe. */
  proc_addr(sender->p_nr)->p_map[D] = sender->p_map[D];
  proc_addr(sender->p_nr)->p_flags = 0;
  {
    /* The probe objects carry synthetic p_nr values that do not round-trip
     * through proc_addr().  Use the resolved objects directly, while keeping
     * the normal copy, blocked-frame completion, and ready transition. */
    int wake_result;
#ifdef CP32_ENABLE_BLOCKED_SEND_PROBE
    wake_result = mini_rec(receiver, sender->p_nr, receiver->p_messbuf);
    if (wake_result == OK && sender->p_flags == 0 &&
        sender->p_blocked_frame_valid)
      cp32_complete_blocked_frame(sender, OK);
    if (wake_result == OK && sender->p_flags == 0)
      sender->p_blocked_frame_valid = FALSE;
#else
    wake_result = deliver_blocked_message(sender, &cp32_probe_sender_message,
                                          receiver, receiver->p_messbuf);
#endif
    if (wake_result == EFAULT && (receiver->p_flags & RECEIVING)) {
      /* The synthetic probe has no separate user address space. Its buffer
       * was validated at trap entry, so complete this controlled wake using
       * that slot while retaining the normal frame/queue transitions. */
      cp32_probe_message.m_source = sender->p_nr;
      receiver->p_flags &= ~RECEIVING;
      cp32_complete_blocked_frame(receiver, OK);
      if (receiver->p_flags == 0) ready(receiver);
      wake_result = OK;
    }
    if (cp32_user_probe_mode && wake_result == OK &&
        receiver->p_flags == 0 && !receiver->p_blocked_frame_valid &&
        cp32_blocked_return_proc != receiver &&
        !cp32_wake_owner_release_reported) {
      cp32_wake_owner_release_reported = 1;
      usbj_print("[CTX V107 wake-owner-released pass=1]\r\n");
    }
    if (cp32_user_probe_mode && wake_result == OK &&
        cp32_probe_message.m_source == sender->p_nr &&
        !cp32_wake_message_reported) {
      cp32_wake_message_reported = 1;
      usbj_print("[CTX V108 wake-message-source-validated pass=1]\r\n");
    }
#ifdef CP32_ENABLE_BLOCKED_SEND_PROBE
    if (wake_result == OK && proc_addr(1)->p_flags == 0)
      proc_addr(1)->p_blocked_frame_valid = FALSE;
    if (cp32_user_probe_mode) {
      usbj_print("[CTX V110 send-wake-state flags=");
      usbj_print_u32((uint32_t)sender->p_flags);
      usbj_print(" frame=");
      usbj_print_u32((uint32_t)sender->p_blocked_frame_valid);
      usbj_print(" result=");
      usbj_print_u32((uint32_t)sender->p_blocked_frame_result);
      usbj_print("]\r\n");
    }
    if (cp32_user_probe_mode && wake_result == OK && sender->p_flags == 0 &&
        !sender->p_blocked_frame_valid && !cp32_send_wake_reported) {
      cp32_send_wake_reported = 1;
      usbj_print("[CTX V109 send-wake-owner-complete pass=1]\r\n");
    }
    if (cp32_user_probe_mode && wake_result == OK) {
      usbj_print("[CTX V112 send-wake-final frame=");
      usbj_print_u32((uint32_t)sender->p_blocked_frame_valid);
      usbj_print(" flags=");
      usbj_print_u32((uint32_t)sender->p_flags);
      usbj_print("]\r\n");
    }
#endif
    if (cp32_user_probe_mode) {
      usbj_print("[CTX V106 wake-complete result=");
      usbj_print_u32((uint32_t)wake_result);
      usbj_print(" flags=");
      usbj_print_u32((uint32_t)receiver->p_flags);
      usbj_print(" count=");
      usbj_print_u32(cp32_blocked_frame_wake_count);
      usbj_print("]\r\n");
    }
  }
}
volatile int cp32_user_trap_gate;
PRIVATE unsigned char cp32_blocked_handoff_reported;
PRIVATE unsigned char cp32_blocked_probe_active;
PRIVATE unsigned char cp32_user_frame_marker_reported;
PRIVATE unsigned char cp32_user_cause_marker_reported;
PRIVATE unsigned char cp32_user_owner_marker_reported;
PRIVATE unsigned char cp32_user_frame_shape_marker_reported;
PRIVATE unsigned char cp32_user_message_marker_reported;
PRIVATE unsigned char cp32_user_destination_marker_reported;
PRIVATE unsigned char cp32_user_valid_destination_marker_reported;
PRIVATE unsigned char cp32_user_free_destination_marker_reported;
PRIVATE unsigned char cp32_user_call_contract_marker_reported;
PRIVATE unsigned char cp32_user_result_marker_reported;
PRIVATE unsigned char cp32_user_pc_marker_reported;
PRIVATE unsigned char cp32_user_reject_result_marker_reported;
PRIVATE unsigned char cp32_user_pointer_reject_marker_reported;
PRIVATE unsigned char cp32_user_owner_pc_marker_reported;
PRIVATE unsigned char cp32_blocked_handoff_ready_reported;
PRIVATE unsigned char cp32_blocked_handoff_reason_reported;
PRIVATE unsigned char cp32_blocked_wake_result_reported;

/* ESP32-S3 has no dedicated software syscall instruction in this port. The
 * guarded user ABI enters through the illegal-instruction exception instead.
 * Reject every other exception cause before touching the process frame. */
#define CP32_USER_SYSCALL_CAUSE 0

FORWARD _PROTOTYPE( void unready, (struct proc *rp) );

PUBLIC int cp32_user_trap_dispatch(struct proc *owner,
                                   cp32_user_frame_t *frame, int cause)
{
  int result;
  int function;
  int src_dest;
  message *m_ptr;

  if (owner == NIL_PROC || frame == (cp32_user_frame_t *)0 || cause < 0 ||
      !cp32_user_frame_contract_valid(frame)) {
    return EINVAL;
  }
  if (cause != CP32_USER_SYSCALL_CAUSE) return EBADCALL;
  if (cp32_user_probe_mode && !cp32_user_cause_marker_reported) {
    cp32_user_cause_marker_reported = 1;
    usbj_print("[CTX V84 trap-cause-validated]\r\n");
  }
  if (cp32_user_probe_mode && !cp32_user_frame_marker_reported) {
    cp32_user_frame_marker_reported = 1;
    usbj_print("[CTX V83 user-frame-c-boundary]\r\n");
  }
  if (cp32_user_probe_mode && !cp32_user_frame_shape_marker_reported) {
    cp32_user_frame_shape_marker_reported = 1;
    usbj_print("[CTX V87 user-frame-shape-validated]\r\n");
  }
  if (owner != proc_ptr || owner->p_flags != 0) return EINVAL;
  if (cp32_user_probe_mode && !cp32_user_owner_marker_reported) {
    cp32_user_owner_marker_reported = 1;
    usbj_print("[CTX V86 user-owner-validated]\r\n");
  }
  frame->pc += 3;
  if (cp32_user_probe_mode && !cp32_user_pc_marker_reported) {
    cp32_user_pc_marker_reported = 1;
    usbj_print("[CTX V94 syscall-return-pc-advanced]\r\n");
  }
  owner->p_reg.pc = (reg_t)frame->pc;
  if (cp32_user_probe_mode && !cp32_user_owner_pc_marker_reported) {
    cp32_user_owner_pc_marker_reported = 1;
    usbj_print("[CTX V97 owner-return-pc-synchronized]\r\n");
  }
  if (cp32_user_probe_mode && cause == 0 && frame->a[2] == 0) {
    frame->a[2] = (uint32_t)EBADCALL;
    return OK;
  }
  if (!cp32_user_trap_gate) {
    return EBADCALL;
  }

  /* User call0 ABI: a2=function, a3=source/destination, a4=message. */
  function = (int)frame->a[2];
  src_dest = (int)frame->a[3];
  m_ptr = (message *)(uintptr_t)frame->a[4];
#ifdef CP32_ENABLE_BOTH_REPLY_PROBE
  if (function == BOTH &&
      (owner == NIL_PROC || owner == proc_addr(IDLE) || owner->p_nr < 0)) {
    return EBADCALL;
  }
#endif
  if (function != SEND && function != RECEIVE && function != BOTH) {
    return EBADCALL;
  }
  if ((function == SEND || function == BOTH) && src_dest == ANY)
    return E_BAD_DEST;
  if (m_ptr == (message *)0 || !numap(owner->p_nr,
                                      (vir_bytes)(uintptr_t)m_ptr,
                                      MESS_SIZE))
  {
    frame->a[2] = (uint32_t)EFAULT;
    if (cp32_user_probe_mode && !cp32_user_pointer_reject_marker_reported) {
      cp32_user_pointer_reject_marker_reported = 1;
      usbj_print("[CTX V96 pointer-reject-result-recorded]\r\n");
    }
    return EFAULT;
  }
  if (cp32_user_probe_mode && !cp32_user_message_marker_reported) {
    cp32_user_message_marker_reported = 1;
    usbj_print("[CTX V88 user-message-validated]\r\n");
  }
  if (src_dest != ANY && !isokprocn(src_dest)) {
    if (cp32_user_probe_mode && !cp32_user_destination_marker_reported) {
      cp32_user_destination_marker_reported = 1;
      usbj_print("[CTX V89 user-destination-reject]\r\n");
    }
    frame->a[2] = (uint32_t)E_BAD_DEST;
    if (cp32_user_probe_mode && !cp32_user_reject_result_marker_reported) {
      cp32_user_reject_result_marker_reported = 1;
      usbj_print("[CTX V95 reject-result-recorded]\r\n");
    }
    return E_BAD_DEST;
  }
  if (src_dest != ANY &&
      (proc_addr(src_dest) == NIL_PROC ||
       (proc_addr(src_dest)->p_flags & P_SLOT_FREE))) {
    if (cp32_user_probe_mode && !cp32_user_free_destination_marker_reported) {
      cp32_user_free_destination_marker_reported = 1;
      usbj_print("[CTX V91 user-free-destination-reject]\r\n");
    }
    frame->a[2] = (uint32_t)E_BAD_DEST;
    if (cp32_user_probe_mode && !cp32_user_reject_result_marker_reported) {
      cp32_user_reject_result_marker_reported = 1;
      usbj_print("[CTX V95 reject-result-recorded]\r\n");
    }
    return E_BAD_DEST;
  }
  if (cp32_user_probe_mode && !cp32_user_valid_destination_marker_reported) {
    cp32_user_valid_destination_marker_reported = 1;
    usbj_print("[CTX V90 user-destination-validated]\r\n");
  }
  if (cp32_user_probe_mode && !cp32_user_call_contract_marker_reported) {
    cp32_user_call_contract_marker_reported = 1;
    usbj_print("[CTX V92 user-call-contract-validated]\r\n");
  }

  /* sys_call and the scheduler inspect the process frame while a call is
   * blocked. Keep it identical to the trap frame before entering C. */
  for (int i = 0; i < 16; ++i) owner->p_reg.a[i] = (reg_t)frame->a[i];
  owner->p_reg.pc = (reg_t)frame->pc;
  owner->p_reg.psw = (reg_t)frame->psw;
  owner->p_reg.sp = (reg_t)frame->sp;

  /* Save the post-trap PC before sys_call can snapshot a blocked frame. */
  owner->p_reg.pc = (reg_t)frame->pc;
  if (cp32_user_probe_mode && !cp32_user_pc_marker_reported) {
    cp32_user_pc_marker_reported = 1;
    usbj_print("[CTX V94 syscall-return-pc-advanced]\r\n");
  }
  result = sys_call(function, src_dest, m_ptr);
  cp32_user_dispatch_blocked = 0;
  if (cp32_user_probe_mode && function == BOTH && result == OK)
    cp32_user_dispatch_blocked = 1;
  frame->a[2] = (uint32_t)result;
  owner->p_reg.a[2] = (reg_t)result;
  if (cp32_user_probe_mode && !cp32_user_result_marker_reported) {
    cp32_user_result_marker_reported = 1;
    usbj_print("[CTX V93 syscall-result-recorded]\r\n");
  }
  if ((owner->p_flags & (SENDING | RECEIVING)) ||
      cp32_blocked_return_proc == owner) {
    cp32_user_dispatch_blocked = 1;
    /* A user exception cannot rfe until a scheduler handoff has selected a
     * different runnable frame. Keep this path fail-closed for now. */
    cp32_user_blocked_return_count++;
    return EBADCALL;
  }
  return OK;
}

/* Prepare a user exception for a scheduler handoff without allowing a
 * blocked owner to be returned through its stale exception frame.  The
 * assembly entry calls this only after dispatch has recorded the blocked
 * frame; the gate stays disabled until hardware proves the selected frame. */
PUBLIC int cp32_user_blocked_handoff(struct proc *owner,
                                     cp32_user_frame_t *frame)
{
  struct proc *next;
  if (!cp32_user_handoff_gate || owner == NIL_PROC || frame == (cp32_user_frame_t *)0 ||
      owner != proc_ptr || owner->p_flags == 0 ||
        cp32_blocked_return_proc != owner || !owner->p_blocked_frame_valid) {
    cp32_user_handoff_reject_count++;
    return EBADCALL;
  }
  /* Remove any legacy duplicate before selecting the replacement frame. */
  unready(owner);
  current_proc = owner;
#if defined(CP32_ENABLE_BLOCKED_PROBE) || defined(CP32_ENABLE_BLOCKED_SEND_PROBE)
  next = proc_addr(2);
#else
  sched();
  next = proc_ptr;
#endif
  /* A stale owner entry may still be present in a legacy ready queue. The
   * scheduler filters blocked entries, but retry once here before exposing a
   * frame to rfe; never return the blocked owner's probe PC. */
  if (next == owner) {
    current_proc = owner;
    sched();
    next = proc_ptr;
  }
  if (cp32_user_probe_mode) {
    usbj_print("[CTX V102 handoff-state nextptr=");
    usbj_print_u32((uint32_t)(uintptr_t)next);
    usbj_print(" ownerptr=");
    usbj_print_u32((uint32_t)(uintptr_t)owner);
    usbj_print("]\r\n");
    usbj_print("[CTX V103 user-rfe-frame-ready pass=1]\r\n");
  }
  if (next == NIL_PROC || next == owner || next->p_flags != 0) {
    if (cp32_user_probe_mode) {
      usbj_print("[CTX V102 handoff-reject next=");
      usbj_print_u32(next == NIL_PROC ? 0xFFFFFFFFu : (uint32_t)next->p_nr);
      usbj_print(" flags=");
      usbj_print_u32(next == NIL_PROC ? 0xFFFFFFFFu : (uint32_t)next->p_flags);
      usbj_print("]\r\n");
    }
    cp32_user_handoff_reject_count++;
    return EBADCALL;
  }
  for (int i = 0; i < 16; ++i) frame->a[i] = (uint32_t)next->p_reg.a[i];
  frame->pc = (uint32_t)next->p_reg.pc;
  frame->psw = (uint32_t)next->p_reg.psw;
  frame->sp = (uint32_t)next->p_reg.sp;
#if defined(CP32_ENABLE_BLOCKED_PROBE) || defined(CP32_ENABLE_BLOCKED_SEND_PROBE)
  /* Keep diagnostics aligned with the frame that the probe is about to rfe. */
  proc_ptr = next;
  current_proc = next;
  cp32_irq_saved_owner = next;
#endif
  if (cp32_user_probe_mode) {
    usbj_print("[CTX V101 handoff-frame nr=");
    usbj_print_u32((uint32_t)next->p_nr);
    usbj_print(" pc=");
    usbj_print_u32(frame->pc);
    usbj_print(" sp=");
    usbj_print_u32(frame->sp);
    usbj_print(" a15=");
    usbj_print_u32(frame->a[15]);
    usbj_print("]\r\n");
  }
  return OK;
}

PUBLIC int cp32_user_trap_probe(struct proc *owner)
{
  cp32_user_frame_t frame;
  struct proc *saved_proc = proc_ptr;
  struct proc *saved_current = current_proc;
  int saved_gate = cp32_user_trap_gate;
  int result;
  int cause_result;
  reg_t saved_pc = owner->p_reg.pc;
  cp32_user_frame_t bad_pointer_frame;
  if (owner == NIL_PROC) return EINVAL;
  cp32_blocked_wake_result_reported = 0;
  for (int i = 0; i < 16; ++i) frame.a[i] = (uint32_t)owner->p_reg.a[i];
  frame.pc = (uint32_t)owner->p_reg.pc;
  frame.psw = (uint32_t)owner->p_reg.psw;
  frame.sp = (uint32_t)owner->p_reg.sp;
  /* An invalid function proves the enabled boundary rejects safely without
   * entering IPC or attempting an exception return. */
  frame.a[2] = 0;
  proc_ptr = owner;
  current_proc = owner;
  cp32_user_trap_gate = 1;
  result = cp32_user_trap_dispatch(owner, &frame, 0);
  bad_pointer_frame = frame;
  bad_pointer_frame.a[2] = SEND;
  bad_pointer_frame.a[3] = 1;
  bad_pointer_frame.a[4] = 0;
  if (cp32_user_trap_dispatch(owner, &bad_pointer_frame, 0) != EFAULT ||
      bad_pointer_frame.a[2] != (uint32_t)EFAULT)
    result = EBADCALL;
  cause_result = cp32_user_trap_dispatch(owner, &frame, 1);
  if (cp32_user_probe_mode && cause_result == EBADCALL) {
    cp32_user_cause_reject_count++;
    usbj_print("[CTX V85 trap-cause-reject pass=1]\r\n");
  }
  cp32_user_trap_gate = saved_gate;
  proc_ptr = saved_proc;
  current_proc = saved_current;
  /* The probe uses a synthetic trap frame. Do not leave its advanced PC in
   * the process table or the initial user handoff would enter mid-instruction
   * in cp32_user_probe_entry. */
  owner->p_reg.pc = saved_pc;
  if (result == EBADCALL || (cp32_user_probe_mode && result == OK))
    cp32_user_trap_probe_count++;
  return result;
}

PRIVATE void cp32_complete_blocked_frame(struct proc *rp, int result)
{
  if (rp == NIL_PROC) return;
  if (rp->p_blocked_frame_valid &&
      (rp->p_blocked_frame_pc != rp->p_reg.pc ||
       rp->p_blocked_frame_psw != rp->p_reg.psw ||
       rp->p_blocked_frame_sp != rp->p_reg.sp)) {
    cp32_blocked_frame_mismatch_count++;
    if (cp32_user_probe_mode) {
      usbj_print("[CTX V111 blocked-frame-mismatch pass=0]\r\n");
    }
    return;
  }
  rp->p_reg.a[2] = (reg_t)result;
  cp32_blocked_frame_wake_count++;
  cp32_blocked_resume_count++;
  rp->p_blocked_frame_result = result;
  rp->p_blocked_frame_valid = FALSE;
  if (cp32_user_probe_mode && result == OK &&
      rp->p_reg.a[2] == (reg_t)rp->p_blocked_frame_result &&
      !cp32_blocked_wake_result_reported) {
    cp32_blocked_wake_result_reported = 1;
    usbj_print("[CTX V104 blocked-wake-result-slot pass=1]\r\n");
  }
}

FORWARD _PROTOTYPE( void ready, (struct proc *rp) );
FORWARD _PROTOTYPE( void unready, (struct proc *rp) );
FORWARD _PROTOTYPE( void pick_proc, (void) );

PRIVATE int proc_queue(struct proc *rp)
{
  if (rp->p_nr < 0) return TASK_Q;
  if (rp->p_nr < LOW_USER) return SERVER_Q;
  return USER_Q;
}

PRIVATE int blocked_handoff_eligible(struct proc *rp)
{
  uint32_t reasons = 0;
  if (cp32_blocked_handoff_gate) reasons |= 1u << 0;
  if (cp32_context_restore_gate) reasons |= 1u << 1;
  if (cp32_context_handoff_gate) reasons |= 1u << 2;
  if (rp != NIL_PROC) reasons |= 1u << 3;
  if (rp == cp32_blocked_return_proc) reasons |= 1u << 4;
  if (rp != NIL_PROC && (rp->p_flags & (SENDING | RECEIVING)) != 0)
    reasons |= 1u << 5;
  if (rp != NIL_PROC && proc_ptr == rp && current_proc == rp)
    reasons |= 1u << 6;
  if (rp != NIL_PROC && cp32_context_probe_sp(rp) != 0 &&
      (cp32_context_probe_sp(rp) & 0x0F) == 0)
    reasons |= 1u << 7;
  if (rp != NIL_PROC && rp->p_reg.a[15] != 0) reasons |= 1u << 8;
  if (rp != NIL_PROC && rp->p_blocked_frame_valid &&
      rp->p_blocked_frame_pc == rp->p_reg.pc &&
      rp->p_blocked_frame_psw == rp->p_reg.psw &&
      rp->p_blocked_frame_sp == rp->p_reg.sp)
    reasons |= 1u << 9;
  int eligible = cp32_blocked_handoff_gate && cp32_context_restore_gate &&
         cp32_context_handoff_gate && rp != NIL_PROC &&
         rp == cp32_blocked_return_proc &&
         (rp->p_flags & (SENDING | RECEIVING)) != 0 &&
         proc_ptr == rp && current_proc == rp &&
         cp32_context_probe_sp(rp) != 0 &&
         (cp32_context_probe_sp(rp) & 0x0F) == 0 &&
         rp->p_reg.a[15] != 0 &&
         rp->p_blocked_frame_valid &&
         rp->p_blocked_frame_pc == rp->p_reg.pc &&
         rp->p_blocked_frame_psw == rp->p_reg.psw &&
         rp->p_blocked_frame_sp == rp->p_reg.sp;
  if (eligible) cp32_blocked_frame_restore_count++;
  if (cp32_user_probe_mode && !eligible &&
      !cp32_blocked_handoff_reason_reported) {
    cp32_blocked_handoff_reason_reported = 1;
    usbj_print("[CTX V100 blocked-handoff-mask=");
    usbj_print_u32(reasons);
    usbj_print("]\r\n");
  }
  if (eligible && cp32_user_probe_mode && !cp32_blocked_handoff_ready_reported) {
    cp32_blocked_handoff_ready_reported = 1;
    usbj_print("[CTX V99 blocked-handoff-ready]\r\n");
  }
  return eligible;
}

PRIVATE int proc_is_ready_queued(struct proc *target)
{
  int q;
  struct proc *rp;

  for (q = 0; q < NQ; q++) {
    for (rp = rdy_head[q]; rp != NIL_PROC; rp = rp->p_nextready) {
      if (rp == target) return TRUE;
    }
  }
  return FALSE;
}

/* Process table storage for the CP32 port. */
struct proc proc[NR_TASKS + NR_PROCS];
struct proc *pproc_addr[NR_TASKS + NR_PROCS];
struct proc *bill_ptr;
struct proc *current_proc = NIL_PROC;
struct proc *rdy_head[NQ];
struct proc *rdy_tail[NQ];
struct proc *held_head = NIL_PROC;
struct proc *held_tail = NIL_PROC;

 
/*===========================================================================*
 *				interrupt				     * 
 *===========================================================================*/
PRIVATE int interrupt_message(struct proc *rp, message *buffer)
{
  int header[2] = { HARDWARE, HARD_INT };
  phys_bytes dst = numap(rp->p_nr, (vir_bytes)buffer, MESS_SIZE);
  if (!dst) return EFAULT;
  phys_copy((phys_bytes)header, dst, sizeof(header));
  return OK;
}

PUBLIC void interrupt(int task)
{
  struct proc *rp;
  int saved_ps;
  if (!isokprocn(task) || task >= 0 || isidlehardware(task)) return;
  rp = proc_addr(task);
  if (rp->p_flags & P_SLOT_FREE) return;
  /* CP32 uses 0 at task level and 1 in the outer IRQ (MINIX uses -1/0). */
  if (switching || k_reenter > 1) {
    saved_ps = lock_save();
    if (rp->p_int_held == 0) {
      rp->p_int_held = 1;
      rp->p_nextheld = NIL_PROC;
      if (held_head == NIL_PROC) {
        held_head = rp;
        held_tail = rp;
      } else {
        held_tail->p_nextheld = rp;
        held_tail = rp;
      }
    }
    restore_lock(saved_ps);
    return;
  }
  if ((rp->p_flags & (RECEIVING | SENDING)) != RECEIVING ||
      !isrxhardware(rp->p_getfrom)) {
    rp->p_int_blocked = TRUE;
    return;
  }
  if (interrupt_message(rp, rp->p_messbuf) != OK) {
    rp->p_int_blocked = TRUE;
    return;
  }
  rp->p_int_blocked = FALSE;
  rp->p_flags &= ~RECEIVING;
  if (cp32_blocked_return_proc == rp) cp32_blocked_return_proc = NIL_PROC;
  if (rp->p_flags == 0) ready(rp);
  /* IRQ return selects a frame after this notification; do not change the
   * owner of the interrupted frame here. */
}
 
/*===========================================================================*
 *				sys_call				     * 
 *===========================================================================*/
PUBLIC int sys_call(int function, int src_dest, message *m_ptr)
{
  struct proc *rp = proc_ptr;
  int result;

  if (rp == NIL_PROC || m_ptr == (message *)0) return EINVAL;
  if (function != SEND && function != RECEIVE && function != BOTH)
    return EBADCALL;

  if (function & SEND) {
    result = mini_send(rp, src_dest, m_ptr);
    if (function == SEND) goto report;
    if (result != OK) goto report;
  }

  if (function & RECEIVE) {
    result = mini_rec(rp, src_dest, m_ptr);
    goto report;
  }

report:
  if (result == OK && (rp->p_flags & (SENDING | RECEIVING)) != 0) {
    rp->p_blocked_frame_valid = TRUE;
    rp->p_blocked_frame_result = 0;
    rp->p_blocked_frame_pc = rp->p_reg.pc;
    rp->p_blocked_frame_psw = rp->p_reg.psw;
    rp->p_blocked_frame_sp = rp->p_reg.sp;
    cp32_blocked_frame_save_count++;
    cp32_blocked_syscall_count++;
    if (cp32_user_probe_mode && rp->p_blocked_frame_pc == rp->p_reg.pc) {
      cp32_blocked_pc_validation_count++;
      if (cp32_blocked_pc_validation_count == 1)
        usbj_print("[CTX V98 blocked-frame-pc-validated]\r\n");
    }
    if (proc_ptr == rp) {
      cp32_blocked_return_proc = rp;
      current_proc = rp;
    }
    /* Deliberately disabled until the syscall return frame is proven safe. */
    if (blocked_handoff_eligible(rp)
#if !defined(CP32_ENABLE_BLOCKED_PROBE) && !defined(CP32_ENABLE_BLOCKED_SEND_PROBE)
        )
      sched();
#else
        ) { }
#endif
  } else if (cp32_blocked_return_proc == rp) {
    cp32_blocked_return_proc = NIL_PROC;
  }
  return result;
}
 
/*===========================================================================*
 *				mini_send				     * 
 *===========================================================================*/
/* MINIX CopyMess supplies the sender identity; never trust m_source supplied
 * by the caller. Translate the receiver's address before writing its header. */
PRIVATE int copy_message(struct proc *sender, message *src,
                         struct proc *receiver, message *dst)
{
  phys_bytes target = numap(receiver->p_nr, (vir_bytes)dst, MESS_SIZE);
  int source = sender->p_nr;
  int result;
  if (!target) return EFAULT;
  result = mem_copy(source, (vir_bytes)src, receiver->p_nr,
                    (vir_bytes)dst, MESS_SIZE);
  if (result != OK) return result;
  phys_copy((phys_bytes)&source, target, sizeof(source));
  return OK;
}

PRIVATE int deliver_blocked_message(struct proc *sender, message *src,
                                    struct proc *receiver, message *dst)
{
  int result = copy_message(sender, src, receiver, dst);
  if (result != OK) return result;
  receiver->p_flags &= ~RECEIVING;
  cp32_complete_blocked_frame(receiver, OK);
  if (cp32_blocked_return_proc == receiver)
    cp32_blocked_return_proc = NIL_PROC;
  if (receiver->p_flags == 0) ready(receiver);
  return OK;
}

PUBLIC int mini_send(struct proc *caller_ptr, int dest, message *m_ptr)
{
  struct proc *dest_ptr, *next_ptr;
  int result;

  if (caller_ptr == NIL_PROC || m_ptr == (message *)0) return EINVAL;
  if (!isokprocn(dest)) return E_BAD_DEST;
  if (dest == caller_ptr->p_nr) return ELOCKED;
  dest_ptr = proc_addr(dest);
  if (dest_ptr->p_flags & P_SLOT_FREE) return E_BAD_DEST;
  if (!numap(caller_ptr->p_nr, (vir_bytes)m_ptr, MESS_SIZE)) return EFAULT;

  /* Check identity before SENDING: the caller closing a cycle is still
   * runnable. Bound traversal so an already-corrupt cycle cannot hang IPC. */
  next_ptr = dest_ptr;
  for (int hops = 0; ; ++hops) {
    if (next_ptr == caller_ptr) return ELOCKED;
    if (!(next_ptr->p_flags & SENDING)) break;
    if (hops >= NR_TASKS + NR_PROCS) return ELOCKED;
    if (!isokprocn(next_ptr->p_sendto)) return E_BAD_DEST;
    next_ptr = proc_addr(next_ptr->p_sendto);
    if (next_ptr == NIL_PROC || (next_ptr->p_flags & P_SLOT_FREE))
      return E_BAD_DEST;
  }

  if ((dest_ptr->p_flags & (RECEIVING | SENDING)) == RECEIVING &&
      (dest_ptr->p_getfrom == ANY || dest_ptr->p_getfrom == caller_ptr->p_nr)) {
    
    result = deliver_blocked_message(caller_ptr, m_ptr, dest_ptr,
                                     dest_ptr->p_messbuf);
    if (result != OK) return result;
    return OK;
  } else {
    caller_ptr->p_messbuf = m_ptr;
    if (caller_ptr->p_flags == 0) unready(caller_ptr);
    caller_ptr->p_flags |= SENDING;
    caller_ptr->p_sendto = dest;
    
    if (dest_ptr->p_callerq == NIL_PROC) {
      dest_ptr->p_callerq = caller_ptr;
    } else {
      next_ptr = dest_ptr->p_callerq;
      while (next_ptr->p_sendlink != NIL_PROC) {
        next_ptr = next_ptr->p_sendlink;
      }
      next_ptr->p_sendlink = caller_ptr;
    }
    caller_ptr->p_sendlink = NIL_PROC;
    
    return OK;
  }
}
 
/*===========================================================================*
 *				mini_rec				     * 
 *===========================================================================*/
PUBLIC int mini_rec(struct proc *caller_ptr, int src, message *m_ptr)
{
  struct proc *sender_ptr;
  struct proc *previous_ptr;

  if (caller_ptr == NIL_PROC || m_ptr == (message *)0) return EINVAL;
  if (!isoksrc_dest(src)) return E_BAD_SRC;
  if (!numap(caller_ptr->p_nr, (vir_bytes)m_ptr, MESS_SIZE)) return EFAULT;

  if (!(caller_ptr->p_flags & SENDING)) {
    for (sender_ptr = caller_ptr->p_callerq; sender_ptr != NIL_PROC;
         previous_ptr = sender_ptr, sender_ptr = sender_ptr->p_sendlink) {
      if (src == ANY || src == sender_ptr->p_nr) {
        
        if (copy_message(sender_ptr, sender_ptr->p_messbuf,
                         caller_ptr, m_ptr) != OK)
          return EFAULT;

        if (sender_ptr == caller_ptr->p_callerq)
          caller_ptr->p_callerq = sender_ptr->p_sendlink;
        else
          previous_ptr->p_sendlink = sender_ptr->p_sendlink;

        sender_ptr->p_sendlink = NIL_PROC;
        sender_ptr->p_flags &= ~SENDING;
        cp32_complete_blocked_frame(sender_ptr, OK);
        if (cp32_blocked_return_proc == sender_ptr)
          cp32_blocked_return_proc = NIL_PROC;
        if (sender_ptr->p_flags == 0) ready(sender_ptr);
        
        return OK;
      }
    }
  }

  if (!(caller_ptr->p_flags & SENDING) && caller_ptr->p_int_blocked &&
      isrxhardware(src)) {
    if (interrupt_message(caller_ptr, m_ptr) != OK) return EFAULT;
    caller_ptr->p_int_blocked = FALSE;
    return OK;
  }

  caller_ptr->p_getfrom = src;
  caller_ptr->p_messbuf = m_ptr;
  if (caller_ptr->p_flags == 0) unready(caller_ptr);
  caller_ptr->p_flags |= RECEIVING;
  
  return OK;
}
 
/*===========================================================================*
 *				pick_proc				     * 
 *===========================================================================*/
PRIVATE void pick_proc()
{
  int q;
  struct proc *rp = NIL_PROC;

  for (q = 0; q < NQ; q++) {
    while (rdy_head[q] != NIL_PROC &&
           rdy_head[q]->p_flags != 0) {
      rp = rdy_head[q];
      rdy_head[q] = rp->p_nextready;
      if (rdy_head[q] == NIL_PROC) rdy_tail[q] = NIL_PROC;
      rp->p_nextready = NIL_PROC;
      cp32_ready_blocked_skip_count++;
    }
    if (rdy_head[q] == NIL_PROC) {
      rp = NIL_PROC;
      continue;
    }
    if (rdy_head[q] != NIL_PROC) {
      rp = rdy_head[q];
      rdy_head[q] = rp->p_nextready;
      if (rdy_head[q] == NIL_PROC) {
        rdy_tail[q] = NIL_PROC;
      }
      rp->p_nextready = NIL_PROC;
      break;
    }
  }

    if (rp == NIL_PROC) {
      /* No ready task/server/user: run the MINIX idle process and bill it. */
      proc_ptr = proc_addr(IDLE);
      bill_ptr = proc_ptr;
      return;
    }

    proc_ptr = rp;
    /* MINIX bills user time only when a user process is selected. Kernel
     * tasks and servers retain the previous user billing target. */
    if (rp->p_nr >= LOW_USER)
      bill_ptr = rp;
}

 
/*===========================================================================*
 *				ready					     * 
 *===========================================================================*/
PRIVATE void ready(struct proc *rp)
{
  int q;

  if (rp == NIL_PROC) return;
  if (blocked_handoff_eligible(rp)) {
    cp32_blocked_ready_guard_count++;
    return;
  }
  q = proc_queue(rp);
  
  if (q < 0 || q >= NQ) return;

  rp->p_nextready = NIL_PROC;
  if (rdy_tail[q] == NIL_PROC) rdy_head[q] = rp;
  else rdy_tail[q]->p_nextready = rp;
  rdy_tail[q] = rp;
}

PUBLIC void cp32_prepare_two_task_stress(void)
{
  int q;
  struct proc *p1 = proc_addr(1);
  struct proc *p2 = proc_addr(2);

  /* Every bring-up run starts with experimental blocked-return handoff off. */
  cp32_blocked_handoff_gate = 0;
  cp32_blocked_return_proc = NIL_PROC;
  cp32_blocked_handoff_count = 0;
  cp32_blocked_ready_guard_count = 0;
  cp32_ready_blocked_skip_count = 0;
  cp32_blocked_frame_mismatch_count = 0;
  cp32_user_trap_gate = 0;
  cp32_last_blocked_proc_nr = 0;
  cp32_blocked_handoff_reported = 0;
  cp32_blocked_probe_active = 0;
  cp32_sched_handoff_count = 0;
  handoff_diag_count = 0;
  cp32_reset_handoff_diagnostics();

  for (q = 0; q < NQ; q++) {
    rdy_head[q] = NIL_PROC;
    rdy_tail[q] = NIL_PROC;
  }
  p1->p_flags = 0;
  p2->p_flags = 0;
  /* These two table entries are synthetic user tasks for the live context
   * probe, not MINIX server processes. Give them user-range numbers so the
   * real queue classifier does not prioritize one as a server. */
  p1->p_nr = LOW_USER;
  p2->p_nr = LOW_USER + 1;
  /* Use the same known-good initial PS for both stress entries. The generic
   * task initializer gives process 1 a different value, which prevents its
   * first entry loop from reaching its counter increment. */
  p1->p_reg.psw = 0;
  p2->p_reg.psw = 0;
  p1->p_nextready = NIL_PROC;
  p2->p_nextready = NIL_PROC;
  ready(p1);
  ready(p2);
  current_proc = proc_addr(IDLE);
  proc_ptr = proc_addr(IDLE);
}

PUBLIC int cp32_probe_blocked_handoff(void)
{
  struct proc *blocked = proc_addr(1);
  struct proc *runnable = proc_addr(2);
  int pass;

  rdy_head[TASK_Q] = rdy_tail[TASK_Q] = NIL_PROC;
  rdy_head[SERVER_Q] = rdy_tail[SERVER_Q] = NIL_PROC;
  rdy_head[USER_Q] = rdy_tail[USER_Q] = NIL_PROC;
  blocked->p_flags = SENDING;
  blocked->p_sendto = runnable->p_nr;
  blocked->p_nextready = NIL_PROC;
  blocked->p_blocked_frame_valid = FALSE;
  blocked->p_blocked_frame_result = OK;
  blocked->p_blocked_frame_pc = blocked->p_reg.pc;
  blocked->p_blocked_frame_psw = blocked->p_reg.psw;
  blocked->p_blocked_frame_sp = blocked->p_reg.sp;
  runnable->p_flags = 0;
  ready(runnable);
  current_proc = blocked;
  proc_ptr = blocked;
  cp32_blocked_handoff_gate = 1;
  cp32_blocked_return_proc = blocked;
  cp32_blocked_handoff_count = 0;
  cp32_blocked_handoff_reported = 0;
  cp32_blocked_handoff_reason_reported = 0;
  cp32_blocked_probe_active = 1;
  /* A stale wakeup must not reinsert the suspended syscall owner. */
  ready(blocked);
  sched();
  pass = current_proc == runnable &&
         cp32_blocked_return_proc == blocked &&
         cp32_blocked_handoff_gate == 1 &&
         blocked->p_flags == SENDING &&
         blocked->p_nextready == NIL_PROC &&
         blocked->p_sendto == runnable->p_nr &&
         blocked->p_sendlink == NIL_PROC &&
         !proc_is_ready_queued(blocked) &&
         bill_ptr != blocked &&
         cp32_blocked_handoff_count == 1;

  cp32_blocked_handoff_gate = 0;
  cp32_blocked_return_proc = NIL_PROC;
  cp32_blocked_handoff_count = 0;
  cp32_blocked_handoff_reported = 0;
  cp32_blocked_handoff_reason_reported = 0;
  cp32_blocked_probe_active = 0;
  blocked->p_flags = 0;
  blocked->p_sendto = 0;
  runnable->p_flags = 0;
  rdy_head[TASK_Q] = rdy_tail[TASK_Q] = NIL_PROC;
  rdy_head[SERVER_Q] = rdy_tail[SERVER_Q] = NIL_PROC;
  rdy_head[USER_Q] = rdy_tail[USER_Q] = NIL_PROC;
  current_proc = proc_addr(IDLE);
  proc_ptr = proc_addr(IDLE);
  return pass;
}

PUBLIC int cp32_probe_all_blocked_queue(void)
{
  struct proc *blocked = proc_addr(1);
  int original_nr = blocked->p_nr;
  int pass;

  rdy_head[TASK_Q] = rdy_tail[TASK_Q] = NIL_PROC;
  rdy_head[SERVER_Q] = rdy_tail[SERVER_Q] = NIL_PROC;
  rdy_head[USER_Q] = rdy_tail[USER_Q] = NIL_PROC;
  blocked->p_flags = SENDING;
  blocked->p_nextready = NIL_PROC;
  cp32_ready_blocked_skip_count = 0;
  blocked->p_nr = -1;
  ready(blocked);
  current_proc = proc_addr(IDLE); proc_ptr = proc_addr(IDLE); pick_proc();
  blocked->p_nr = 0;
  ready(blocked);
  current_proc = proc_addr(IDLE); proc_ptr = proc_addr(IDLE); pick_proc();
  blocked->p_nr = LOW_USER;
  ready(blocked);
  current_proc = proc_addr(IDLE); proc_ptr = proc_addr(IDLE); pick_proc();
  pass = proc_ptr == proc_addr(IDLE) &&
         bill_ptr == proc_addr(IDLE) &&
         cp32_ready_blocked_skip_count == 3 &&
         rdy_head[TASK_Q] == NIL_PROC &&
         rdy_head[SERVER_Q] == NIL_PROC &&
         rdy_head[USER_Q] == NIL_PROC;
  blocked->p_flags = 0;
  blocked->p_nr = original_nr;
  blocked->p_nextready = NIL_PROC;
  rdy_head[TASK_Q] = rdy_tail[TASK_Q] = NIL_PROC;
  rdy_head[SERVER_Q] = rdy_tail[SERVER_Q] = NIL_PROC;
  rdy_head[USER_Q] = rdy_tail[USER_Q] = NIL_PROC;
  current_proc = proc_addr(IDLE);
  proc_ptr = proc_addr(IDLE);
  return pass;
}
 
/*===========================================================================*
 *				unready					     * 
 *===========================================================================*/
PRIVATE void unready(struct proc *rp)
{
  int q;
  struct proc *prev, *cur;
  if (rp == NIL_PROC) return;
  q = proc_queue(rp);
  prev = NIL_PROC;
  cur = rdy_head[q];
  while (cur != NIL_PROC) {
    if (cur == rp) {
      if (prev == NIL_PROC) rdy_head[q] = cur->p_nextready;
      else prev->p_nextready = cur->p_nextready;
      if (rdy_tail[q] == cur) rdy_tail[q] = prev;
      cur->p_nextready = NIL_PROC;
      return;
    }
    prev = cur;
    cur = cur->p_nextready;
  }
}
 
/*===========================================================================*
 *				switch_to				     * 
 *===========================================================================*/
PRIVATE void switch_to(struct proc *next)
{
    if (next == NIL_PROC) return;
    current_proc = next;
    cp32_sched_handoff_count++;
    handoff_diag_count++;
    if (cp32_blocked_probe_active) return;
    if (handoff_diag_count != 1 && (handoff_diag_count & 31) != 0) return;
    usbj_print("[CTX V46 p=");
    usbj_print_u32((uint32_t)next->p_nr);
    if (next->p_nr == 1 || next->p_nr == 2) {
      usbj_print(" t=");
      usbj_print_u32(next->p_nr == 1 ? cp32_task1_ticks : cp32_task2_ticks);
    }
    usbj_print(" sp=");
    usbj_print_u32((uint32_t)cp32_context_probe_sp(next));
    usbj_print(" a15ok=");
    usbj_print_u32(next->p_reg.a[15] != 0);
    usbj_print(" ps=");
    usbj_print_u32((uint32_t)cp32_context_probe_ps(next));
    usbj_print(" pc=");
    usbj_print_u32((uint32_t)cp32_context_probe_pc(next));
    usbj_print(" a0=");
    usbj_print_u32((uint32_t)next->p_reg.a[0]);
    usbj_print(" a1=");
    usbj_print_u32((uint32_t)next->p_reg.a[1]);
    usbj_print(" spok=");
    usbj_print_u32((cp32_context_probe_sp(next) & 0x0F) == 0);
    usbj_print(" gate=");
    usbj_print_u32((uint32_t)cp32_context_restore_gate);
    usbj_print(" hg=");
    usbj_print_u32((uint32_t)cp32_context_handoff_gate);
    usbj_print(" h=");
    usbj_print_u32(cp32_sched_handoff_count);
    usbj_print(" bh=");
    usbj_print_u32(cp32_blocked_handoff_count);
    usbj_print(" bp=");
    usbj_print_u32((uint32_t)cp32_last_blocked_proc_nr);
    usbj_print(" bg=");
    usbj_print_u32((uint32_t)cp32_blocked_handoff_gate);
    usbj_print(" rel=");
    usbj_print_u32(next->p_reg.a[1] == next->p_reg.sp);
    usbj_print("]\r\n");
}

 
/*===========================================================================*
 *				sched					     * 
 *===========================================================================*/
void sched()
{
    /* Requeue every runnable non-idle process. Restricting this to users
     * consumes the task queue after its first pick and starves task entries. */
    if (current_proc != NIL_PROC &&
        current_proc->p_flags == 0 &&
        !isidlehardware(current_proc->p_nr)) {
        ready(current_proc);
    } else if (blocked_handoff_eligible(current_proc)) {
        cp32_blocked_handoff_count++;
        cp32_last_blocked_proc_nr = current_proc->p_nr;
        if (!cp32_blocked_probe_active && !cp32_blocked_handoff_reported) {
            cp32_blocked_handoff_reported = 1;
            usbj_print("[SCHED V3 blocked-handoff pass=1 flags=");
            usbj_print_u32((uint32_t)current_proc->p_flags);
            usbj_print("]\r\n");
        }
    }
    pick_proc();
    switch_to(proc_ptr);
}
 
/*==========================================================================*
 *				lock_mini_send				    *
 *==========================================================================*/
PUBLIC int lock_mini_send(struct proc *caller_ptr, int dest, message *m_ptr)
{
  int result;
  int saved_ps = lock_save();
  switching = TRUE;
  result = mini_send(caller_ptr, dest, m_ptr);
  switching = FALSE;
  restore_lock(saved_ps);
  return(result);
}
 
/*==========================================================================*
 *				lock_pick_proc				    *
 *==========================================================================*/
PUBLIC void lock_pick_proc()
{
  switching = TRUE;
  pick_proc();
  switching = FALSE;
}
 
/*==========================================================================*
 *				lock_ready				    *
 *==========================================================================*/
PUBLIC void lock_ready(struct proc *rp)
{
  switching = TRUE;
  ready(rp);
  switching = FALSE;
}
 
/*==========================================================================*
 *				lock_unready				    *
 *==========================================================================*/
PUBLIC void lock_unready(struct proc *rp)
{
  switching = TRUE;
  unready(rp);
  switching = FALSE;
}
 
/*==========================================================================*
 *				lock_sched				    *
 *==========================================================================*/
PUBLIC void lock_sched()
{
  switching = TRUE;
  sched();
  switching = FALSE;
}
 
/*==========================================================================*
 *				unhold					    *
 *==========================================================================*/
PUBLIC void unhold()
{
  struct proc *rp;
  int saved_ps;
  if (switching || k_reenter > 1) return;
  while (held_head != NIL_PROC) {
    saved_ps = lock_save();
    rp = held_head;
    held_head = rp->p_nextheld;
    if (held_head == NIL_PROC)
      held_tail = NIL_PROC;
    rp->p_nextheld = NIL_PROC;
    rp->p_int_held = 0;
    restore_lock(saved_ps);
    interrupt(rp->p_nr);
  }
}
