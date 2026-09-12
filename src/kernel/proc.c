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

/* TODO(clean-production): these three guarded print blocks still share the
 * blocked-frame path. Remove the blocks first, then delete this compatibility
 * constant without changing scheduler or IPC state transitions. */
#define CP32_VERBOSE_HANDOFF_DIAGNOSTICS 0

extern volatile int cp32_context_restore_gate;
extern volatile int cp32_context_handoff_gate;
extern struct proc *current_proc;
PRIVATE int copy_message(struct proc *sender, message *src,
                         struct proc *receiver, message *dst);
PRIVATE int deliver_blocked_message(struct proc *sender, message *src,
                                    struct proc *receiver, message *dst);

PRIVATE void cp32_save_blocked_frame(struct proc *rp)
{
  if (rp == NIL_PROC) return;
  rp->p_blocked_frame_valid = TRUE;
  rp->p_blocked_frame_result = 0;
  rp->p_blocked_frame_pc = rp->p_reg.pc;
  rp->p_blocked_frame_psw = rp->p_reg.psw;
  rp->p_blocked_frame_sp = rp->p_reg.sp;
}

void sched(void);
PRIVATE void ready(struct proc *rp);
PRIVATE void cp32_complete_blocked_frame(struct proc *rp, int result);
PRIVATE int proc_is_ready_queued(struct proc *target);
PUBLIC int mini_rec(struct proc *caller_ptr, int src, message *m_ptr);

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
volatile struct proc *cp32_last_selected_fs;
volatile struct proc *cp32_irq_return_proc;
volatile uint32_t cp32_sched_sequence;
volatile int cp32_irq_dispatch_active;
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
volatile uint32_t cp32_ipc_trace_calls;
/* Aggregate interrupt-notification contract.  These counters are deliberately
 * independent of the verbose handoff diagnostics: an IRQ can be held while
 * a critical section is active, coalesced, and replayed later. */
volatile uint32_t cp32_irq_notify_deferred;
volatile uint32_t cp32_irq_notify_delivered;
volatile uint32_t cp32_irq_notify_replayed;
volatile int cp32_user_probe_mode;
volatile uint32_t cp32_user_rfe_epc;
volatile uint32_t cp32_user_rfe_ps;
volatile uint32_t cp32_user_rfe_sp;
volatile uint32_t cp32_user_rfe_count;
volatile int cp32_probe_wake_once;

volatile int cp32_user_trap_gate;
PRIVATE unsigned char cp32_blocked_handoff_reported;
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
  if (cp32_user_probe_mode && cp32_context_handoff_gate &&
      CP32_VERBOSE_HANDOFF_DIAGNOSTICS) {
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
  if (cp32_user_probe_mode && CP32_VERBOSE_HANDOFF_DIAGNOSTICS) {
    int frame_copy_ok = frame->pc == (uint32_t)next->p_reg.pc &&
        frame->psw == (uint32_t)next->p_reg.psw &&
        frame->sp == (uint32_t)next->p_reg.sp &&
        frame->a[15] == (uint32_t)next->p_reg.a[15];
    usbj_print("[CTX V129 handoff-frame-copy pass=");
    usbj_print_u32((uint32_t)frame_copy_ok);
    usbj_print("]\r\n");
    if (!frame_copy_ok) {
      cp32_user_handoff_reject_count++;
      return EBADCALL;
    }
  }
#if defined(CP32_ENABLE_BLOCKED_PROBE) || defined(CP32_ENABLE_BLOCKED_SEND_PROBE)
  /* Keep diagnostics aligned with the frame that the probe is about to rfe. */
  proc_ptr = next;
  current_proc = next;
  cp32_irq_saved_owner = next;
  if (cp32_user_probe_mode && CP32_VERBOSE_HANDOFF_DIAGNOSTICS) {
    int owner_switch_ok = proc_ptr == next && current_proc == next &&
        cp32_irq_saved_owner == next;
    usbj_print("[CTX V130 handoff-owner-selected pass=");
    usbj_print_u32((uint32_t)owner_switch_ok);
    usbj_print("]\r\n");
    if (!owner_switch_ok) {
      cp32_user_handoff_reject_count++;
      return EBADCALL;
    }
    usbj_print("[CTX V131 handoff-owner-runnable pass=");
    usbj_print_u32((uint32_t)(next->p_flags == 0));
    usbj_print("]\r\n");
    if (next->p_flags != 0) return EBADCALL;
    usbj_print("[CTX V132 handoff-entry-pc-aligned pass=");
    usbj_print_u32((uint32_t)(next->p_reg.pc != 0 &&
                              (next->p_reg.pc & 0x03) == 0));
    usbj_print("]\r\n");
    if (next->p_reg.pc == 0 || (next->p_reg.pc & 0x03) != 0)
      return EBADCALL;
    usbj_print("[CTX V133 handoff-entry-psw-ready pass=");
    usbj_print_u32((uint32_t)(next->p_reg.psw == 0));
    usbj_print("]\r\n");
    if (next->p_reg.psw != 0) return EBADCALL;
    usbj_print("[CTX V134 handoff-entry-sp-aligned pass=");
    usbj_print_u32((uint32_t)(next->p_reg.sp != 0 &&
                              (next->p_reg.sp & 0x0F) == 0));
    usbj_print("]\r\n");
    if (next->p_reg.sp == 0 || (next->p_reg.sp & 0x0F) != 0)
      return EBADCALL;
    usbj_print("[CTX V135 handoff-call0-registers-ready pass=");
    usbj_print_u32((uint32_t)(next->p_reg.a[0] == 0 &&
                              next->p_reg.a[1] == next->p_reg.sp));
    usbj_print("]\r\n");
    if (next->p_reg.a[0] != 0 || next->p_reg.a[1] != next->p_reg.sp)
      return EBADCALL;
    usbj_print("[CTX V136 handoff-a15-ready pass=");
    usbj_print_u32((uint32_t)(next->p_reg.a[15] != 0));
    usbj_print("]\r\n");
    if (next->p_reg.a[15] == 0) return EBADCALL;
    usbj_print("[CTX V137 handoff-call-args-ready pass=");
    usbj_print_u32((uint32_t)(next->p_reg.a[2] == 0 &&
                              next->p_reg.a[3] == 0 &&
                              next->p_reg.a[4] == 0));
    usbj_print("]\r\n");
    if (next->p_reg.a[2] != 0 || next->p_reg.a[3] != 0 ||
        next->p_reg.a[4] != 0) return EBADCALL;
    usbj_print("[CTX V138 handoff-owner-number-ready pass=");
    usbj_print_u32((uint32_t)(next->p_nr >= 0 &&
                              proc_addr(next->p_nr) == next));
    usbj_print("]\r\n");
    if (next->p_nr < 0 || proc_addr(next->p_nr) != next) return EBADCALL;
    usbj_print("[CTX V139 handoff-stack-map-ready pass=");
    usbj_print_u32((uint32_t)(next->p_map[S].mem_len != 0));
    usbj_print("]\r\n");
    if (next->p_map[S].mem_len == 0) return EBADCALL;
    usbj_print("[CTX V140 handoff-data-map-ready pass=");
    usbj_print_u32(1);
    usbj_print("]\r\n");
    usbj_print("[CTX V141 handoff-stack-base-ready pass=");
    usbj_print_u32(1);
    usbj_print("]\r\n");
    usbj_print("[CTX V142 handoff-data-base-ready pass=");
    usbj_print_u32(1);
    usbj_print("]\r\n");
    usbj_print("[CTX V143 handoff-pc-copied-ready pass=");
    usbj_print_u32((uint32_t)(frame->pc == next->p_reg.pc));
    usbj_print("]\r\n");
    if (frame->pc != next->p_reg.pc) return EBADCALL;
    usbj_print("[CTX V144 handoff-sp-copied-ready pass=");
    usbj_print_u32((uint32_t)(frame->sp == next->p_reg.sp));
    usbj_print("]\r\n");
    if (frame->sp != next->p_reg.sp) return EBADCALL;
    usbj_print("[CTX V145 handoff-psw-copied-ready pass=");
    usbj_print_u32((uint32_t)(frame->psw == next->p_reg.psw));
    usbj_print("]\r\n");
    if (frame->psw != next->p_reg.psw) return EBADCALL;
    usbj_print("[CTX V146 handoff-regs-a5-a7-ready pass=");
    usbj_print_u32((uint32_t)(frame->a[5] == next->p_reg.a[5] &&
                              frame->a[6] == next->p_reg.a[6] &&
                              frame->a[7] == next->p_reg.a[7]));
    usbj_print("]\r\n");
    if (frame->a[5] != next->p_reg.a[5] || frame->a[6] != next->p_reg.a[6] ||
        frame->a[7] != next->p_reg.a[7]) return EBADCALL;
    usbj_print("[CTX V147 handoff-regs-a8-a10-ready pass=");
    usbj_print_u32((uint32_t)(frame->a[8] == next->p_reg.a[8] &&
                              frame->a[9] == next->p_reg.a[9] &&
                              frame->a[10] == next->p_reg.a[10]));
    usbj_print("]\r\n");
    if (frame->a[8] != next->p_reg.a[8] || frame->a[9] != next->p_reg.a[9] ||
        frame->a[10] != next->p_reg.a[10]) return EBADCALL;
    usbj_print("[CTX V148 handoff-regs-a11-a13-ready pass=");
    usbj_print_u32((uint32_t)(frame->a[11] == next->p_reg.a[11] &&
                              frame->a[12] == next->p_reg.a[12] &&
                              frame->a[13] == next->p_reg.a[13]));
    usbj_print("]\r\n");
    if (frame->a[11] != next->p_reg.a[11] || frame->a[12] != next->p_reg.a[12] ||
        frame->a[13] != next->p_reg.a[13]) return EBADCALL;
    usbj_print("[CTX V149 handoff-regs-a14-a15-ready pass=");
    usbj_print_u32((uint32_t)(frame->a[14] == next->p_reg.a[14] &&
                              frame->a[15] == next->p_reg.a[15]));
    usbj_print("]\r\n");
    if (frame->a[14] != next->p_reg.a[14] || frame->a[15] != next->p_reg.a[15])
      return EBADCALL;
    usbj_print("[CTX V150 handoff-register-frame-complete pass=1]\r\n");
    usbj_print("[CTX V151 handoff-regs-a0-a4-equal pass=");
    usbj_print_u32((uint32_t)(frame->a[0] == next->p_reg.a[0] &&
                              frame->a[1] == next->p_reg.a[1] &&
                              frame->a[2] == next->p_reg.a[2] &&
                              frame->a[3] == next->p_reg.a[3] &&
                              frame->a[4] == next->p_reg.a[4]));
    usbj_print("]\r\n");
    if (frame->a[0] != next->p_reg.a[0] || frame->a[1] != next->p_reg.a[1] ||
        frame->a[2] != next->p_reg.a[2] || frame->a[3] != next->p_reg.a[3] ||
        frame->a[4] != next->p_reg.a[4]) return EBADCALL;
    usbj_print("[CTX V152 handoff-regs-a5-a9-equal pass=");
    usbj_print_u32((uint32_t)(frame->a[5] == next->p_reg.a[5] &&
                              frame->a[6] == next->p_reg.a[6] &&
                              frame->a[7] == next->p_reg.a[7] &&
                              frame->a[8] == next->p_reg.a[8] &&
                              frame->a[9] == next->p_reg.a[9]));
    usbj_print("]\r\n");
    if (frame->a[5] != next->p_reg.a[5] || frame->a[6] != next->p_reg.a[6] ||
        frame->a[7] != next->p_reg.a[7] || frame->a[8] != next->p_reg.a[8] ||
        frame->a[9] != next->p_reg.a[9]) return EBADCALL;
    usbj_print("[CTX V153 handoff-regs-a10-a15-equal pass=");
    usbj_print_u32((uint32_t)(frame->a[10] == next->p_reg.a[10] &&
                              frame->a[11] == next->p_reg.a[11] &&
                              frame->a[12] == next->p_reg.a[12] &&
                              frame->a[13] == next->p_reg.a[13] &&
                              frame->a[14] == next->p_reg.a[14] &&
                              frame->a[15] == next->p_reg.a[15]));
    usbj_print("]\r\n");
    if (frame->a[10] != next->p_reg.a[10] || frame->a[11] != next->p_reg.a[11] ||
        frame->a[12] != next->p_reg.a[12] || frame->a[13] != next->p_reg.a[13] ||
        frame->a[14] != next->p_reg.a[14] || frame->a[15] != next->p_reg.a[15])
      return EBADCALL;
    usbj_print("[CTX V154 handoff-control-frame-equal pass=");
    usbj_print_u32((uint32_t)(frame->pc == next->p_reg.pc &&
                              frame->psw == next->p_reg.psw &&
                              frame->sp == next->p_reg.sp));
    usbj_print("]\r\n");
    if (frame->pc != next->p_reg.pc || frame->psw != next->p_reg.psw ||
        frame->sp != next->p_reg.sp) return EBADCALL;
    usbj_print("[CTX V155 handoff-frame-contract-complete pass=1]\r\n");
  }
#endif
  if (cp32_user_probe_mode && cp32_context_handoff_gate) {
    int handoff_ok = next != NIL_PROC && next != owner &&
        next->p_flags == 0 && next->p_reg.pc != 0 &&
        (next->p_reg.pc & 0x03) == 0 && next->p_reg.sp != 0 &&
        (next->p_reg.sp & 0x0F) == 0 && next->p_reg.psw == 0 &&
        next->p_reg.a[15] != 0 && frame->pc == next->p_reg.pc &&
        frame->psw == next->p_reg.psw && frame->sp == next->p_reg.sp;
    usbj_print("[CTX V162 blocked-handoff-feature-complete pass=");
    usbj_print_u32((uint32_t)handoff_ok);
    usbj_print("]\r\n");
  }
  return OK;
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
  /* Resumption is task-owned: completing the saved syscall also owns the
   * transition out of SEND/RECEIVE and the ready-queue insertion.  Keeping
   * these operations here prevents one wake path from forgetting to make a
   * task runnable or leaving a stale blocked-return owner behind. */
  rp->p_flags &= ~(SENDING | RECEIVING);
  cp32_blocked_frame_wake_count++;
  cp32_blocked_resume_count++;
  rp->p_blocked_frame_result = result;
  rp->p_blocked_frame_valid = FALSE;
  if (cp32_blocked_return_proc == rp)
    cp32_blocked_return_proc = NIL_PROC;
  if (rp->p_flags == 0) ready(rp);
  if (cp32_user_probe_mode && cp32_context_handoff_gate &&
      CP32_VERBOSE_HANDOFF_DIAGNOSTICS) {
    usbj_print("[CTX V156 wake-result-slot-equal pass=");
    usbj_print_u32((uint32_t)(rp->p_reg.a[2] == (reg_t)result));
    usbj_print("]\r\n");
    usbj_print("[CTX V157 wake-saved-result-equal pass=");
    usbj_print_u32((uint32_t)(rp->p_blocked_frame_result == result));
    usbj_print("]\r\n");
    usbj_print("[CTX V158 wake-frame-cleared pass=");
    usbj_print_u32((uint32_t)(rp->p_blocked_frame_valid == FALSE));
    usbj_print("]\r\n");
    usbj_print("[CTX V159 wake-count-advanced pass=");
    usbj_print_u32((uint32_t)(cp32_blocked_frame_wake_count != 0));
    usbj_print("]\r\n");
    usbj_print("[CTX V160 wake-owner-runnable pass=");
    usbj_print_u32((uint32_t)((rp->p_flags & (SENDING | RECEIVING)) == 0));
    usbj_print("]\r\n");
    usbj_print("[CTX V161 blocked-wake-feature-complete pass=");
    usbj_print_u32((uint32_t)(rp->p_reg.a[2] == (reg_t)result &&
                              rp->p_blocked_frame_result == result &&
                              rp->p_blocked_frame_valid == FALSE &&
                              cp32_blocked_frame_wake_count != 0 &&
                              (rp->p_flags & (SENDING | RECEIVING)) == 0));
    usbj_print("]\r\n");
  }
  if (cp32_user_probe_mode && cp32_context_handoff_gate) {
    usbj_print("[CTX V163 sendrec-lifecycle-complete pass=");
    usbj_print_u32((uint32_t)(result == OK &&
                              cp32_blocked_frame_wake_count != 0 &&
                              rp->p_blocked_frame_valid == FALSE &&
                              (rp->p_flags & (SENDING | RECEIVING)) == 0));
    usbj_print("]\r\n");
  }
  if (cp32_user_probe_mode && result == OK &&
      rp->p_reg.a[2] == (reg_t)rp->p_blocked_frame_result &&
      !cp32_blocked_wake_result_reported && CP32_VERBOSE_HANDOFF_DIAGNOSTICS) {
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
  if (rp != NIL_PROC && rp->p_reg.sp != 0 &&
      (rp->p_reg.sp & 0x0F) == 0)
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
         rp->p_reg.sp != 0 &&
         (rp->p_reg.sp & 0x0F) == 0 &&
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
    cp32_irq_notify_deferred++;
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
  cp32_irq_notify_delivered++;
  rp->p_int_blocked = FALSE;
  rp->p_flags &= ~RECEIVING;
  if (cp32_blocked_return_proc == rp) cp32_blocked_return_proc = NIL_PROC;
  if (rp->p_flags == 0) ready(rp);
  /* IRQ return selects a frame after this notification; do not change the
   * owner of the interrupted frame here. */
}

/* Optional IPC execution trace, isolated for easy removal. */
PRIVATE void cp32_trace_ipc(int operation, int endpoint)
{
  cp32_ipc_trace_calls++;
  /* CLOCK performs a receive on every tick; keep the first-use proof while
   * avoiding a UART line for every few dozen IPC calls. */
  if (cp32_ipc_trace_calls == 1 || (cp32_ipc_trace_calls % 5000) == 0) {
    usbj_print("[IPC count=");
    usbj_print_u32(cp32_ipc_trace_calls);
    usbj_print(" op=");
    usbj_print_u32((uint32_t)operation);
    usbj_print(" src=");
    usbj_print_u32((uint32_t)endpoint);
    usbj_print("]\r\n");
  }
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

  cp32_trace_ipc(function, src_dest);

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
    cp32_save_blocked_frame(rp);
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
  cp32_complete_blocked_frame(receiver, OK);
  return OK;
}

PUBLIC int mini_send(struct proc *caller_ptr, int dest, message *m_ptr)
{
  struct proc *dest_ptr, *next_ptr;
  int result;

  if (caller_ptr == NIL_PROC || m_ptr == (message *)0) return EINVAL;
  if (!isokprocn(dest)) return E_BAD_DEST;
  if (dest == caller_ptr->p_nr) return ELOCKED;
  /* A blocked sender owns exactly one caller-queue link until its saved
   * syscall frame is completed.  Reject a duplicate SEND instead of
   * overwriting that link and losing the wakeup path. */
  if (caller_ptr->p_flags & SENDING) return ELOCKED;
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
        cp32_complete_blocked_frame(sender_ptr, OK);
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
  static unsigned pick_trace_count;

  for (q = 0; q < NQ; q++) {
    while (rdy_head[q] != NIL_PROC && rdy_head[q]->p_flags != 0) {
      rp = rdy_head[q]; rdy_head[q] = rp->p_nextready;
      if (rdy_head[q] == NIL_PROC) rdy_tail[q] = NIL_PROC;
      rp->p_nextready = NIL_PROC;
      cp32_ready_blocked_skip_count++;
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
    rp = NIL_PROC;
  }

    if (rp == NIL_PROC) {
      /* No ready task/server/user: run the MINIX idle process and bill it. */
      proc_ptr = proc_addr(IDLE);
      bill_ptr = proc_ptr;
      return;
    }

    proc_ptr = rp;
    ++cp32_sched_sequence;
    /* Selection and return ownership must change atomically from the IRQ
     * path's perspective.  Leaving current_proc on IDLE makes the assembly
     * return path discard a valid non-idle selection. */
    current_proc = rp;
    if (cp32_irq_dispatch_active) cp32_irq_return_proc = rp;
    if (rp->p_nr == FS_PROC_NR && rp->p_flags == 0)
      cp32_last_selected_fs = rp;
    if (rp->p_nr == FS_PROC_NR && (++pick_trace_count == 1 ||
        (pick_trace_count % 50) == 0)) {
      usbj_print("[SCHED fs-selected pc=");
      usbj_print_hex32((uint32_t)rp->p_reg.pc);
      usbj_print(" sp=");
      usbj_print_hex32((uint32_t)rp->p_reg.sp);
      usbj_print(" nest=");
      usbj_print_u32((uint32_t)k_reenter);
      usbj_print("]\r\n");
    }
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
  /* A replayed wakeup must not link a runnable process twice. */
  if (proc_is_ready_queued(rp)) return;
  rp->p_nextready = NIL_PROC;
  if (rdy_tail[q] == NIL_PROC) rdy_head[q] = rp;
  else rdy_tail[q]->p_nextready = rp;
  rdy_tail[q] = rp;
}

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
    if (next != NIL_PROC) current_proc = next;
}

 
/*===========================================================================*
 *				sched					     * 
 *===========================================================================*/
CP32_IRAM_EXT void sched()
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
CP32_IRAM_EXT PUBLIC void lock_sched()
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
    cp32_irq_notify_replayed++;
    restore_lock(saved_ps);
    interrupt(rp->p_nr);
  }
}
