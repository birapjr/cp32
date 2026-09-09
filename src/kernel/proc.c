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

extern reg_t cp32_context_probe_pc(struct proc *next);
extern reg_t cp32_context_probe_sp(struct proc *next);
extern reg_t cp32_context_probe_ps(struct proc *next);
extern volatile int cp32_context_restore_gate;
extern volatile uint32_t cp32_task1_ticks;
extern volatile uint32_t cp32_task2_ticks;

void sched(void);

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

FORWARD _PROTOTYPE( void ready, (struct proc *rp) );
FORWARD _PROTOTYPE( void unready, (struct proc *rp) );
FORWARD _PROTOTYPE( void pick_proc, (void) );

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
PUBLIC void interrupt(int task)
{
  if (switching && task >= 0) {
    struct proc *rp = &proc[task];
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
  } 
  
  int q;
  struct proc *rp = NIL_PROC;
  
  for (q = 0; q < NQ; q++) {
    if (rdy_head[q] != NIL_PROC) {
      rp = rdy_head[q];
      break;
    }
  }
  if (rp == NIL_PROC) return;
  
  proc_ptr = rp;
  bill_ptr = rp;
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
  usbj_print("[SYS V6 f=");
  usbj_print_u32((uint32_t)function);
  usbj_print(" r=");
  usbj_print_u32((uint32_t)result);
  usbj_print("]\r\n");
  return result;
}
 
/*===========================================================================*
 *				mini_send				     * 
 *===========================================================================*/
PUBLIC int mini_send(struct proc *caller_ptr, int dest, message *m_ptr)
{
  struct proc *dest_ptr, *next_ptr;
  int result;

  if (!isokprocn(dest)) return E_BAD_DEST;
  dest_ptr = proc_addr(dest);
  if (dest_ptr->p_flags & P_SLOT_FREE) return E_BAD_DEST;

  /* Reject a send cycle before blocking the caller. */
  if (dest_ptr->p_flags & SENDING) {
    next_ptr = proc_addr(dest_ptr->p_sendto);
    while (next_ptr != NIL_PROC && (next_ptr->p_flags & SENDING)) {
      if (next_ptr == caller_ptr) return ELOCKED;
      next_ptr = proc_addr(next_ptr->p_sendto);
    }
  }

  if ((dest_ptr->p_flags & (RECEIVING | SENDING)) == RECEIVING &&
      (dest_ptr->p_getfrom == ANY || dest_ptr->p_getfrom == caller_ptr->p_nr)) {
    
    result = mem_copy(caller_ptr->p_nr, (vir_bytes)m_ptr, dest,
                      (vir_bytes)dest_ptr->p_messbuf, MESS_SIZE);
    if (result != OK) return result;
    
    dest_ptr->p_flags &= ~RECEIVING;
    if (dest_ptr->p_flags == 0) ready(dest_ptr);
    
    usbj_print("[IPC] mini_send: delivered immediately\r\n");
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
    
    usbj_print("[IPC B]");
    usbj_print_u32((uint32_t)caller_ptr->p_nr);
    usbj_print("->");
    usbj_print_u32((uint32_t)dest);
    usbj_print("\r\n");
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

  if (!(caller_ptr->p_flags & SENDING)) {
    for (sender_ptr = caller_ptr->p_callerq; sender_ptr != NIL_PROC;
         previous_ptr = sender_ptr, sender_ptr = sender_ptr->p_sendlink) {
      if (src == ANY || src == sender_ptr->p_nr) {
        
        if (mem_copy(sender_ptr->p_nr, (vir_bytes)sender_ptr->p_messbuf,
                     caller_ptr->p_nr, (vir_bytes)m_ptr, MESS_SIZE) != OK)
          return EFAULT;

        if (sender_ptr == caller_ptr->p_callerq)
          caller_ptr->p_callerq = sender_ptr->p_sendlink;
        else
          previous_ptr->p_sendlink = sender_ptr->p_sendlink;

        sender_ptr->p_flags &= ~SENDING;
        if (sender_ptr->p_flags == 0) ready(sender_ptr);
        
        return OK;
      }
    }
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
    if (rp->p_nr >= NR_TASKS + LOW_USER)
      bill_ptr = rp;
}

 
/*===========================================================================*
 *				ready					     * 
 *===========================================================================*/
PRIVATE void ready(struct proc *rp)
{
  int q;

  if (rp == NIL_PROC) return;
  if (rp->p_nr < NR_TASKS) q = TASK_Q;
  else if (rp->p_nr < NR_TASKS + LOW_USER) q = SERVER_Q;
  else q = USER_Q;
  
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

  for (q = 0; q < NQ; q++) {
    rdy_head[q] = NIL_PROC;
    rdy_tail[q] = NIL_PROC;
  }
  p1->p_flags = 0;
  p2->p_flags = 0;
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
 
/*===========================================================================*
 *				unready					     * 
 *===========================================================================*/
PRIVATE void unready(struct proc *rp)
{
  int q;
  struct proc *prev, *cur;
  if (rp == NIL_PROC) return;
  if (rp->p_nr < NR_TASKS) q = TASK_Q;
  else if (rp->p_nr < NR_TASKS + LOW_USER) q = SERVER_Q;
  else q = USER_Q;
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
    handoff_diag_count++;
    if (handoff_diag_count != 1 && (handoff_diag_count & 31) != 0) return;
    usbj_print("[CTX V43 p=");
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
  switching = TRUE;
  result = send(proc_ptr->p_nr, (message *)0); 
  switching = FALSE;
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
  while (held_head != NIL_PROC) {
    rp = held_head;
    held_head = rp->p_nextheld;
    if (held_head == NIL_PROC)
      held_tail = NIL_PROC;
    
    rp->p_int_held = 0;
    interrupt(rp->p_nr);
  }
}
