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
 */

#include "kernel.h"
#include <minix/callnr.h>
#include <minix/com.h>
#include "proc.h"

void sched(void);

// Minimal scheduler stub for main to call.
void schedule(void)
{
    // Initialize dummy task for Step 2
    proc[1].p_nr = 1;
    proc[1].p_flags = 0; // Make it runnable
    
    sched();
}





PRIVATE unsigned char switching;	/* nonzero to inhibit interrupt() */

FORWARD _PROTOTYPE( int mini_send, (struct proc *caller_ptr, int dest,
		message *m_ptr) );
FORWARD _PROTOTYPE( int mini_rec, (struct proc *caller_ptr, int src,
		message *m_ptr) );
FORWARD _PROTOTYPE( void ready, (struct proc *rp) );
void sched(void);
FORWARD _PROTOTYPE( void unready, (struct proc *rp) );
FORWARD _PROTOTYPE( void pick_proc, (void) );

/* Process table storage for the CP32 port.  The historical MINIX headers
 * keep these as EXTERN declarations, so we provide the actual definitions
 * here in the kernel translation unit that owns process management.
 */
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
PUBLIC void interrupt(task)
int task;			/* number of task to be started */
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
  
  /* An interrupt has occurred.  Schedule the task that handles it. */
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
PUBLIC int sys_call(function, src_dest, m_ptr)
int function;			/* SEND, RECEIVE, or BOTH */
int src_dest;			/* source to receive from or dest to send to */
message *m_ptr;			/* pointer to message */
{
/* The only system calls that exist in MINIX are sending and receiving
 * messages.  These are done by trapping to the kernel with an INT instruction.
 * The trap is caught and sys_call() is called to send or receive a message
 * (or both). The caller is always given by proc_ptr.
 */
// to be implemented
}

/*===========================================================================*
 *				send				     * 
 *===========================================================================*/
PUBLIC int send(dest, m_ptr)
int dest;			/* to whom is message being sent? */
message *m_ptr;			/* pointer to message buffer */
{
  usbj_print("[IPC] send to proc[");
  usbj_print_u32(dest);
  usbj_print("] -> OK\r\n");
  return OK;
}

/*===========================================================================*
 *				receive				     * 
 *===========================================================================*/
PUBLIC int receive(src, m_ptr)
int src;			/* which message source is wanted (or ANY) */
message *m_ptr;			/* pointer to message buffer */
{
  usbj_print("[IPC] receive from proc[");
  usbj_print_u32(src);
  usbj_print("] -> OK\r\n");
  return OK;
}

/*===========================================================================*
 *				pick_proc				     * 
 *===========================================================================*/
PRIVATE void pick_proc()
{
  /* Decide who to run now.  A new process is selected by setting 'proc_ptr'.
   * We check queues in priority order: TASK_Q, SERVER_Q, USER_Q.
   */
  int q;
  struct proc *rp = NIL_PROC;

  for (q = 0; q < NQ; q++) {
    if (rdy_head[q] != NIL_PROC) {
      rp = rdy_head[q];
      
      /* Remove from head of the queue */
      rdy_head[q] = rp->p_nextready;
      if (rdy_head[q] == NIL_PROC) {
        rdy_tail[q] = NIL_PROC;
      }
      rp->p_nextready = NIL_PROC;
      break;
    }
  }

  if (rp == NIL_PROC) {
    /* Fallback to IDLE if no one is ready */
    rp = &proc[0]; 
  }

  proc_ptr = rp;
  bill_ptr = rp;
}

/*===========================================================================*
 *				ready					     * 
 *===========================================================================*/
PRIVATE void ready(rp)
register struct proc *rp;	/* this process is now runnable */
{
/* Add 'rp' to the end of one of the queues of runnable processes. Three
 * queues are maintained:
 *   TASK_Q   - (highest priority) for runnable tasks
 *   SERVER_Q - (middle priority) for MM and FS only
 *   USER_Q   - (lowest priority) for user processes
 */
  int q;

  if (rp == NIL_PROC)
    return;
  if (rp->p_nr < NR_TASKS)
    q = TASK_Q;
  else if (rp->p_nr < NR_TASKS + LOW_USER)
    q = SERVER_Q;
  else
    q = USER_Q;
  
  // Guard against uninitialized queues
  if (q < 0 || q >= NQ) return;

  rp->p_nextready = NIL_PROC;
  if (rdy_tail[q] == NIL_PROC)
    rdy_head[q] = rp;
  else
    rdy_tail[q]->p_nextready = rp;
  rdy_tail[q] = rp;
}

/*===========================================================================*
 *				unready					     * 
 *===========================================================================*/
PRIVATE void unready(rp)
register struct proc *rp;	/* this process is no longer runnable */
{
/* A process has blocked. */
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
PRIVATE void switch_to(next)
struct proc *next;
{
  /* Skeleton context switch: updates current process pointer.
   * Real register saving/restoring will happen in assembly in Step 4.
   */
  current_proc = next;
}


/*===========================================================================*
 *				sched					     * 
 *===========================================================================*/
void sched()
{
    if (current_proc != NIL_PROC && isuserp(current_proc)) {
        ready(current_proc);
    }
    
    pick_proc();
    switch_to(proc_ptr);
}

/*==========================================================================*
 *				lock_mini_send				    *
 *==========================================================================*/
PUBLIC int lock_mini_send(caller_ptr, dest, m_ptr)
struct proc *caller_ptr;	/* who is trying to send a message? */
int dest;			/* to whom is message being sent? */
message *m_ptr;			/* pointer to message buffer */
{
/* Safe gateway to mini_send() for tasks. */
  int result;

  switching = TRUE;
  result = send(proc_ptr->p_nr, (message *)0); // updated to use send()
  switching = FALSE;
  return(result);
}

/*==========================================================================*
 *				lock_pick_proc				    *
 *==========================================================================*/
PUBLIC void lock_pick_proc()
{
/* Safe gateway to pick_proc() for tasks. */

  switching = TRUE;
  pick_proc();
  switching = FALSE;
}

/*==========================================================================*
 *				lock_ready				    *
 *==========================================================================*/
PUBLIC void lock_ready(rp)
struct proc *rp;		/* this process is now runnable */
{
/* Safe gateway to ready() for tasks. */

  switching = TRUE;
  ready(rp);
  switching = FALSE;
}

/*==========================================================================*
 *				lock_unready				    *
 *==========================================================================*/
PUBLIC void lock_unready(rp)
struct proc *rp;		/* this process is no longer runnable */
{
/* Safe gateway to unready() for tasks. */

  switching = TRUE;
  unready(rp);
  switching = FALSE;
}

/*==========================================================================*
 *				lock_sched				    *
 *==========================================================================*/
PUBLIC void lock_sched()
{
/* Safe gateway to sched() for tasks. */

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
    usbj_print("[DEBUG] unhold() - processing held interrupt\r\n");
    usbj_print(" task=");
    usbj_print_u32(rp->p_nr);
    usbj_print("\r\n");
    held_head = rp->p_nextheld;
    if (held_head == NIL_PROC)
      held_tail = NIL_PROC;
    
    rp->p_int_held = 0;
    interrupt(rp->p_nr);
  }
}

