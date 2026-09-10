#ifndef PROC_H
#define PROC_H

/* Here is the declaration of the process table.  It contains the process'
 * registers, memory map, accounting, and message send/receive information.
 * Many assembly code routines reference fields in it.  The offsets to these
 * fields are defined in the assembler include file sconst.h.  When changing
 * 'proc', be sure to change sconst.h to match.
 */

#if (CHIP == ESP32_S3)
#include "irq_const.h"
/* Xtensa LX7 does not have the Intel-style segment/register frame.  Keep
 * only the process state that the ESP32-S3 kernel actually saves/restores:
 * the scratch return register, frame pointer, program counter, stack pointer,
 * and processor status.
 */
struct stackframe_s {           /* proc_ptr points here */
  reg_t a[16];			/* general purpose registers a0-a15 */
  reg_t pc;			/* next instruction to execute */
  reg_t psw;			/* saved processor status */
  reg_t sp;			/* stack pointer */
};

/* Keep the assembly contract explicit: a[16], pc, psw, sp are contiguous. */
typedef char cp32_stackframe_layout_must_be_76[
  sizeof(struct stackframe_s) == CP32_PROC_FRAME_BYTES ? 1 : -1];
typedef char cp32_stackframe_a0_offset_must_be_0[
  __builtin_offsetof(struct stackframe_s, a[0]) == CP32_REG_A0_OFFSET ? 1 : -1];
typedef char cp32_stackframe_a1_offset_must_be_4[
  __builtin_offsetof(struct stackframe_s, a[1]) == CP32_REG_A1_OFFSET ? 1 : -1];
typedef char cp32_stackframe_a15_offset_must_be_60[
  __builtin_offsetof(struct stackframe_s, a[15]) == CP32_REG_A15_OFFSET ? 1 : -1];
typedef char cp32_stackframe_pc_offset_must_be_64[
  __builtin_offsetof(struct stackframe_s, pc) == CP32_REG_PC_OFFSET ? 1 : -1];
typedef char cp32_stackframe_psw_offset_must_be_68[
  __builtin_offsetof(struct stackframe_s, psw) == CP32_REG_PSW_OFFSET ? 1 : -1];
typedef char cp32_stackframe_sp_offset_must_be_72[
  __builtin_offsetof(struct stackframe_s, sp) == CP32_REG_SP_OFFSET ? 1 : -1];
#endif

struct proc {
  struct stackframe_s p_reg;	/* process' registers saved in stack frame */

  reg_t *p_stguard;		/* stack guard word */
  reg_t p_shadow;		/* shadow pointer for MM */

  int p_nr;			/* number of this process (for fast access) */

  int p_int_blocked;		/* nonzero if int msg blocked by busy task */
  int p_int_held;		/* nonzero if int msg held by busy syscall */
  struct proc *p_nextheld;	/* next in chain of held-up int processes */

  int p_flags;			/* P_SLOT_FREE, SENDING, RECEIVING, etc. */
  struct mem_map p_map[NR_SEGS];/* memory map */
  pid_t p_pid;			/* process id passed in from MM */

  clock_t user_time;		/* user time in ticks */
  clock_t sys_time;		/* sys time in ticks */
  clock_t child_utime;		/* cumulative user time of children */
  clock_t child_stime;		/* cumulative sys time of children */
  clock_t p_alarm;		/* time of next alarm in ticks, or 0 */

  struct proc *p_callerq;	/* head of list of procs wishing to send */
  struct proc *p_sendlink;	/* link to next proc wishing to send */
  message *p_messbuf;		/* pointer to message buffer */
  int p_getfrom;		/* from whom does process want to receive? */
  int p_sendto;

  struct proc *p_nextready;	/* pointer to next ready process */
  sigset_t p_pending;		/* bit map for pending signals */
  unsigned p_pendcount;		/* count of pending and unfinished signals */

  char p_name[16];		/* name of the process */
};

/* Guard word for task stacks. */
#define STACK_GUARD	((reg_t) (sizeof(reg_t) == 2 ? 0xBEEF : 0xDEADBEEF))

/* Bits for p_flags in proc[].  A process is runnable iff p_flags == 0. */
#define P_SLOT_FREE      001	/* set when slot is not in use */
#define NO_MAP           002	/* keeps unmapped forked child from running */
#define SENDING          004	/* set when process blocked trying to send */
#define RECEIVING        010	/* set when process blocked trying to recv */
#define PENDING          020	/* set when inform() of signal pending */
#define SIG_PENDING      040	/* keeps to-be-signalled proc from running */
#define P_STOP		0100	/* set when process is being traced */

/* Magic process table addresses. */
#define BEG_PROC_ADDR (&proc[0])
#define END_PROC_ADDR (&proc[NR_TASKS + NR_PROCS])
#define END_TASK_ADDR (&proc[NR_TASKS])
#define BEG_SERV_ADDR (&proc[NR_TASKS])
#define BEG_USER_ADDR (&proc[NR_TASKS + LOW_USER])

#define NIL_PROC          ((struct proc *) 0)
#define isidlehardware(n) ((n) == IDLE || (n) == HARDWARE)
#define isokprocn(n)      ((unsigned) ((n) + NR_TASKS) < NR_PROCS + NR_TASKS)
#define isoksrc_dest(n)   (isokprocn(n) || (n) == ANY)
#define isoksusern(n)     ((unsigned) (n) < NR_PROCS)
#define isokusern(n)      ((unsigned) ((n) - LOW_USER) < NR_PROCS - LOW_USER)
#define isrxhardware(n)   ((n) == ANY || (n) == HARDWARE)
#define issysentn(n)      ((n) == FS_PROC_NR || (n) == MM_PROC_NR)
#define istaskp(p)        ((p) < END_TASK_ADDR && (p) != proc_addr(IDLE))
#define isuserp(p)        ((p) >= BEG_USER_ADDR)
#define proc_addr(n)      (pproc_addr + NR_TASKS)[(n)]
#define cproc_addr(n)     (&(proc + NR_TASKS)[(n)])
#define proc_number(p)    ((p)->p_nr)
#define proc_vir2phys(p, vir) \
			  (((phys_bytes)(p)->p_map[D].mem_phys << CLICK_SHIFT) \
							+ (vir_bytes) (vir))

void schedule(void);
void cp32_prepare_two_task_stress(void);
extern phys_bytes numap(int proc_nr, vir_bytes vir, vir_bytes len);
extern void phys_copy(phys_bytes src, phys_bytes dst, phys_bytes len);
extern int mem_copy(int src_proc, vir_bytes src_vir, int dst_proc, vir_bytes dst_vir, vir_bytes len);
extern int mini_send(struct proc *caller, int dest, message *m);
extern int mini_rec(struct proc *caller, int src, message *m);


#if (SHADOWING == 1)
#define isshadowp(p)      ((p)->p_shadow != 0)
#endif

EXTERN struct proc proc[NR_TASKS + NR_PROCS];	/* process table */
EXTERN struct proc *pproc_addr[NR_TASKS + NR_PROCS];
/* ptrs to process table slots; fast because now a process entry can be found
   by indexing the pproc_addr array, while accessing an element i requires
   a multiplication with sizeof(struct proc) to determine the address */
EXTERN struct proc *bill_ptr;	/* ptr to process to bill for clock ticks */
EXTERN struct proc *rdy_head[NQ];	/* pointers to ready list headers */
EXTERN struct proc *rdy_tail[NQ];	/* pointers to ready list tails */

#endif /* PROC_H */
