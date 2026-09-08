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

PRIVATE unsigned char switching;	/* nonzero to inhibit interrupt() */

FORWARD _PROTOTYPE( int mini_send, (struct proc *caller_ptr, int dest,
		message *m_ptr) );
FORWARD _PROTOTYPE( int mini_rec, (struct proc *caller_ptr, int src,
		message *m_ptr) );
FORWARD _PROTOTYPE( void ready, (struct proc *rp) );
FORWARD _PROTOTYPE( void sched, (void) );
FORWARD _PROTOTYPE( void unready, (struct proc *rp) );
FORWARD _PROTOTYPE( void pick_proc, (void) );

#if (CHIP == M68000)
FORWARD _PROTOTYPE( void cp_mess, (int src, struct proc *src_p, message *src_m,
		struct proc *dst_p, message *dst_m) );
#endif

#if (CHIP == INTEL)
#define CopyMess(s,sp,sm,dp,dm) \
	cp_mess(s, (sp)->p_map[D].mem_phys, (vir_bytes)sm, (dp)->p_map[D].mem_phys, (vir_bytes)dm)
#endif

#if (CHIP == M68000)
#define CopyMess(s,sp,sm,dp,dm) \
	cp_mess(s,sp,sm,dp,dm)
#endif

/* Process table storage for the CP32 port.  The historical MINIX headers
 * keep these as EXTERN declarations, so we provide the actual definitions
 * here in the kernel translation unit that owns process management.
 */
struct proc proc[NR_TASKS + NR_PROCS];
struct proc *pproc_addr[NR_TASKS + NR_PROCS];
struct proc *bill_ptr;
struct proc *rdy_head[NQ];
struct proc *rdy_tail[NQ];

/*===========================================================================*
 *				interrupt				     * 
 *===========================================================================*/
PUBLIC void interrupt(task)
int task;			/* number of task to be started */
{
/* An interrupt has occurred.  Schedule the task that handles it. */
  int q;
  struct proc *rp = NIL_PROC;

  /* Lower queue number means higher priority. */
  for (q = 0; q < NQ; q++) {
    if (rdy_head[q] != NIL_PROC) {
      rp = rdy_head[q];
      break;
    }
  }
  if (rp == NIL_PROC)
    return;

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
 *				mini_send				     * 
 *===========================================================================*/
PRIVATE int mini_send(caller_ptr, dest, m_ptr)
register struct proc *caller_ptr;	/* who is trying to send a message? */
int dest;			/* to whom is message being sent? */
message *m_ptr;			/* pointer to message buffer */
{
/* Send a message from 'caller_ptr' to 'dest'. If 'dest' is blocked waiting
 * for this message, copy the message to it and unblock 'dest'. If 'dest' is
 * not waiting at all, or is waiting for another source, queue 'caller_ptr'.
 */
  /* TODO: message delivery is not implemented yet. */
}

/*===========================================================================*
 *				mini_rec				     * 
 *===========================================================================*/
PRIVATE int mini_rec(caller_ptr, src, m_ptr)
register struct proc *caller_ptr;	/* process trying to get message */
int src;			/* which message source is wanted (or ANY) */
message *m_ptr;			/* pointer to message buffer */
{
/* A process or task wants to get a message.  If one is already queued,
 * acquire it and deblock the sender.  If no message from the desired source
 * is available, block the caller.  No need to check parameters for validity.
 * Users calls are always sendrec(), and mini_send() has checked already.  
 * Calls from the tasks, MM, and FS are trusted.
 */
// to be implemented
}

/*===========================================================================*
 *				pick_proc				     * 
 *===========================================================================*/
PRIVATE void pick_proc()
{
/* Decide who to run now.  A new process is selected by setting 'proc_ptr'.
 * When a fresh user (or idle) process is selected, record it in 'bill_ptr',
 * so the clock task can tell who to bill for system time.
 */
// to be implemented
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
 *				sched					     * 
 *===========================================================================*/
PRIVATE void sched()
{
/* The current process has run too long.  If another low priority (user)
 * process is runnable, put the current process on the end of the user queue,
 * possibly promoting another user to head of the queue.
 */
// to be implemented
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
  result = mini_send(caller_ptr, dest, m_ptr);
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
/* Flush any held-up interrupts.  k_reenter must be 0.  held_head must not
 * be NIL_PROC.  Interrupts must be disabled.  They will be enabled but will
 * be disabled when this returns.
 */
// to be implemented
}
