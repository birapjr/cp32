#!/usr/bin/env python3
"""Execute production IPC/dispatch/ready-queue bodies with host memory mapping.

Only hardware address translation and serial output are stubbed. Actual SEND,
RECEIVE, BOTH, saved-frame completion and scheduler functions run unchanged.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from test_idle_handoff import extract_function

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['cp32_save_blocked_frame', 'cp32_complete_blocked_frame',
         'blocked_handoff_eligible', 'proc_queue', 'proc_is_ready_queued',
         'ready', 'unready', 'pick_proc', 'switch_to', 'sched',
         'copy_message', 'deliver_blocked_message', 'mini_send', 'mini_rec',
         'sys_call', 'cp32_irq_return_frame_check', 'cp32_task_ipc_dispatch',
         'cp32_ipc_state_check', 'cp32_ready_queue_check', 'cp32_trace_task_ipc',
         'interrupt_message', 'interrupt', 'unhold']
BODIES = '\n'.join(extract_function(ROOT/'src/kernel/proc.c', n) for n in NAMES)
PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define PUBLIC
#define PRIVATE static
#define CP32_IRAM_EXT
#define CP32_VERBOSE_HANDOFF_DIAGNOSTICS 0
#define TRUE 1
#define FALSE 0
#define NIL_PROC ((struct proc *)0)
enum { OK=0, EINVAL=-1, EFAULT=-2, ELOCKED=-3, E_BAD_DEST=-4,
       E_BAD_SRC=-5, EBADCALL=-6, SEND=1, RECEIVE=2, BOTH=3,
       P_SLOT_FREE=1, SENDING=4, RECEIVING=8, NR_TASKS=9, NR_PROCS=4,
       TTY_PROC_NR=-9, IDLE=-7, CLOCK=-3, HARDWARE=-1, HARD_INT=2, FS_PROC_NR=1, LOW_USER=2,
       ANY=104, NQ=3, TASK_Q=0, SERVER_Q=1, USER_Q=2 };
typedef uintptr_t reg_t;
typedef uintptr_t vir_bytes;
typedef uintptr_t phys_bytes;
typedef struct { int m_source, m_type, payload; } message;
#define MESS_SIZE sizeof(message)
typedef struct { reg_t a[16], pc, psw, sp, sar, lbeg, lend, lcount; } cp32_user_frame_t;
struct proc {
  cp32_user_frame_t p_reg;
  int p_nr, p_flags, p_getfrom, p_sendto, p_int_blocked, p_int_held;
  int p_blocked_frame_valid, p_blocked_frame_result;
  reg_t p_blocked_frame_pc, p_blocked_frame_psw, p_blocked_frame_sp;
  struct proc *p_callerq, *p_sendlink, *p_nextready, *p_nextheld;
  message *p_messbuf;
};
static struct proc proc[NR_TASKS+NR_PROCS], *pproc_addr[NR_TASKS+NR_PROCS];
#define BEG_PROC_ADDR proc
#define END_PROC_ADDR (proc+NR_TASKS+NR_PROCS)
#define proc_addr(n) (&proc[(n)+NR_TASKS])
#define isokprocn(n) ((n)>=-NR_TASKS && (n)<NR_PROCS)
#define isoksrc_dest(n) ((n)==ANY || isokprocn(n))
#define isrxhardware(n) ((n)==ANY || (n)==HARDWARE)
#define isidlehardware(n) ((n)==IDLE || (n)==HARDWARE)
static struct proc *proc_ptr, *current_proc, *bill_ptr;
static struct proc *cp32_irq_return_proc, *cp32_last_selected_fs, *cp32_blocked_return_proc;
static struct proc *rdy_head[NQ], *rdy_tail[NQ];
static int k_reenter, switching, irq_mask;
static struct proc *held_head, *held_tail;
static int lock_save(void) { int old=irq_mask; irq_mask=1; return old; }
static void restore_lock(int state) { irq_mask=state; }
static void usbj_print(const char *s) { (void)s; }
static void usbj_print_u32(uint32_t n) { (void)n; }
static void usbj_print_hex32(uint32_t n) { (void)n; }
static phys_bytes numap(int n, vir_bytes p, size_t size) {
  (void)size; return isokprocn(n) && p>4096 ? p : 0;
}
static int mem_copy(int src, vir_bytes a, int dst, vir_bytes b, size_t n) {
  if (!numap(src,a,n) || !numap(dst,b,n)) return EFAULT;
  memcpy((void*)b,(void*)a,n); return OK;
}
static void phys_copy(phys_bytes a, phys_bytes b, size_t n) { memcpy((void*)b,(void*)a,n); }
static void cp32_trace_ipc(int op, int endpoint) { (void)op; (void)endpoint; }
static int cp32_user_frame_contract_valid(const cp32_user_frame_t *f) {
  return f && f->pc && f->sp && !(f->sp&15) && f->a[1]==f->sp && f->a[15];
}
'''
TESTS = r'''
static void reset(void) {
  memset(proc,0,sizeof(proc)); memset(rdy_head,0,sizeof(rdy_head)); memset(rdy_tail,0,sizeof(rdy_tail));
  for (int i=0;i<NR_TASKS+NR_PROCS;i++) {
    pproc_addr[i]=&proc[i]; proc[i].p_nr=i-NR_TASKS; proc[i].p_flags=P_SLOT_FREE;
  }
  int active[]={IDLE,TTY_PROC_NR,FS_PROC_NR,CLOCK};
  held_head=held_tail=NIL_PROC; k_reenter=switching=irq_mask=0;
  for (unsigned i=0;i<sizeof(active)/sizeof(active[0]);i++) {
    struct proc *p=proc_addr(active[i]); p->p_flags=0;
    p->p_reg.pc=0x40378000+i*256; p->p_reg.psw=0x110;
    p->p_reg.sp=0x3fcd0000+i*4096; p->p_reg.a[1]=p->p_reg.sp;
    p->p_reg.a[15]=p->p_reg.sp; p->p_reg.sar=17+i;
    p->p_reg.lbeg=123; p->p_reg.lend=456; p->p_reg.lcount=11+i;
  }
  cp32_context_restore_gate=cp32_context_handoff_gate=cp32_blocked_handoff_gate=1;
  cp32_user_probe_mode=0; cp32_blocked_return_proc=NIL_PROC; cp32_last_selected_fs=NIL_PROC;
  proc_ptr=current_proc=bill_ptr=proc_addr(IDLE);
}
static void invoke(struct proc *p,int op,int endpoint,message *m) {
  unready(p); proc_ptr=current_proc=p;
  cp32_user_frame_t frame=p->p_reg;
  frame.a[2]=op; frame.a[3]=(reg_t)endpoint; frame.a[4]=(reg_t)m;
  reg_t oldpc=frame.pc;
  assert(cp32_task_ipc_dispatch(p,&frame,1)==OK);
  assert(p->p_reg.pc==oldpc+3);
  assert(p->p_reg.sp==frame.sp && p->p_reg.sar==frame.sar && p->p_reg.lcount==frame.lcount);
  assert(cp32_irq_return_frame_check());
  assert(cp32_ipc_state_check()); assert(cp32_ready_queue_check());
  if (p->p_flags) assert(cp32_irq_return_proc!=p);
}
static void exchange(int receiver_first) {
  reset();
  struct proc *fs=proc_addr(FS_PROC_NR), *tty=proc_addr(TTY_PROC_NR);
  message request={999,5,0x1234}, incoming={0}, reply={999,42,0x5678};
  ready(fs); ready(tty);
  if (receiver_first) {
    invoke(tty,RECEIVE,ANY,&incoming);
    assert(tty->p_flags==RECEIVING && tty->p_blocked_frame_valid);
  }
  invoke(fs,BOTH,TTY_PROC_NR,&request);
  assert(fs->p_blocked_frame_valid && fs->p_flags&RECEIVING);
  reg_t savedpc=fs->p_reg.pc;
  if (!receiver_first) {
    assert(fs->p_flags==(SENDING|RECEIVING));
    invoke(tty,RECEIVE,ANY,&incoming);
  }
  assert(incoming.m_source==FS_PROC_NR && incoming.payload==0x1234);
  assert(fs->p_flags==RECEIVING && fs->p_blocked_frame_valid);
  assert(!proc_is_ready_queued(fs)); /* Accepting request must not finish BOTH. */
  invoke(tty,SEND,FS_PROC_NR,&reply);
  assert(proc_ptr==tty && current_proc==tty); /* A wake is not a context switch. */
  assert(fs->p_flags==0 && !fs->p_blocked_frame_valid && fs->p_reg.a[2]==OK);
  assert(fs->p_reg.pc==savedpc && request.m_source==TTY_PROC_NR && request.payload==0x5678);
  assert(proc_is_ready_queued(fs));
  invoke(tty,RECEIVE,ANY,&incoming);
  assert(cp32_irq_return_proc==fs); /* Real saved caller is selected for resume. */
}
static void hardware_receive(int deferred) {
  reset();
  struct proc *clock=proc_addr(CLOCK), *owner=proc_addr(FS_PROC_NR);
  message notification={999,999,42};
  ready(owner);
  invoke(clock,RECEIVE,ANY,&notification);
  assert(clock->p_flags==RECEIVING && clock->p_blocked_frame_valid);
  reg_t pc=clock->p_reg.pc;
  assert(proc_ptr==owner);
  k_reenter=deferred ? 2 : 1;
  interrupt(CLOCK);
  if (deferred) {
    interrupt(CLOCK); /* Coalesce held notifications; do not duplicate links. */
    assert(clock->p_int_held && held_head==clock && held_tail==clock);
    assert(clock->p_nextheld==NIL_PROC && clock->p_flags==RECEIVING);
    k_reenter=1; unhold();
    assert(!clock->p_int_held && held_head==NIL_PROC && held_tail==NIL_PROC);
  }
  assert(irq_mask==0 && proc_ptr==owner && current_proc==owner);
  assert(notification.m_source==HARDWARE && notification.m_type==HARD_INT);
  assert(clock->p_flags==0 && !clock->p_blocked_frame_valid);
  assert(clock->p_reg.pc==pc && clock->p_reg.a[2]==OK && proc_is_ready_queued(clock));
  assert(cp32_ipc_state_check() && cp32_ready_queue_check());
  k_reenter=0;
  /* An interrupt arriving before RECEIVE is consumed immediately, once. */
  interrupt(CLOCK); interrupt(CLOCK);
  assert(clock->p_int_blocked);
  invoke(clock,RECEIVE,HARDWARE,&notification);
  assert(!clock->p_int_blocked && clock->p_flags==0 && !clock->p_blocked_frame_valid);
  invoke(clock,RECEIVE,HARDWARE,&notification);
  assert(clock->p_flags==RECEIVING && clock->p_blocked_frame_valid);
}
static void hardware_filtered_receive(void) {
  reset();
  struct proc *clock=proc_addr(CLOCK), *fs=proc_addr(FS_PROC_NR);
  message incoming={0}, reply={0,23,45};
  invoke(clock,RECEIVE,FS_PROC_NR,&incoming);
  interrupt(CLOCK);
  assert(clock->p_flags==RECEIVING && clock->p_int_blocked);
  invoke(fs,SEND,CLOCK,&reply);
  assert(incoming.m_source==FS_PROC_NR && incoming.m_type==23);
  invoke(clock,RECEIVE,ANY,&incoming);
  assert(incoming.m_source==HARDWARE && incoming.m_type==HARD_INT);
  assert(!clock->p_int_blocked && clock->p_flags==0);
}
static void task_queue_fairness(void) {
  reset();
  /* Five runnable tasks after TTY blocks, matching the live bootstrap queue.
   * Enqueue CLOCK first so a fixed nine-rotation preference cannot pick it
   * accidentally as the tail on every visit to TASK_Q. */
  int tasks[]={CLOCK,-8,-6,-5,-4};
  unsigned runs[5]={0};
  for (unsigned i=0;i<5;i++) {
    struct proc *p=proc_addr(tasks[i]);
    p->p_reg=proc_addr(CLOCK)->p_reg; p->p_flags=0; ready(p);
  }
  ready(proc_addr(FS_PROC_NR));
  proc_ptr=current_proc=proc_addr(IDLE);
  for (int tick=0;tick<100;tick++) {
    sched();
    for (unsigned i=0;i<5;i++) if (proc_ptr->p_nr==tasks[i]) runs[i]++;
    assert(cp32_ready_queue_check());
  }
  for (unsigned i=0;i<5;i++) {
    if (!runs[i]) fprintf(stderr,"starved runnable task %d\n",tasks[i]);
    assert(runs[i]>0);
  }
}
int main(void) {
  for (int i=0;i<100;i++) { exchange(i&1); hardware_receive(i&1); }
  hardware_filtered_receive();
  task_queue_fairness();
  reset();
  struct proc *fs=proc_addr(FS_PROC_NR),*tty=proc_addr(TTY_PROC_NR);
  message out={0,5,7},in={0};
  invoke(fs,SEND,TTY_PROC_NR,&out);
  assert(fs->p_flags==SENDING);
  invoke(tty,RECEIVE,ANY,&in);
  assert(fs->p_flags==0 && !fs->p_blocked_frame_valid && in.m_source==FS_PROC_NR);
  invoke(fs,BOTH,NR_PROCS,&out); assert((int)fs->p_reg.a[2]==E_BAD_DEST && fs->p_flags==0);
  invoke(fs,SEND,FS_PROC_NR,&out); assert((int)fs->p_reg.a[2]==ELOCKED);
  invoke(fs,RECEIVE,ANY,(message*)1); assert((int)fs->p_reg.a[2]==EFAULT && fs->p_flags==0);
  invoke(fs,99,ANY,&out); assert((int)fs->p_reg.a[2]==EBADCALL);
  reset(); fs=proc_addr(FS_PROC_NR); tty=proc_addr(TTY_PROC_NR);
  invoke(fs,SEND,TTY_PROC_NR,&out);
  invoke(tty,SEND,FS_PROC_NR,&in); assert((int)tty->p_reg.a[2]==ELOCKED && tty->p_flags==0);
  puts("production IPC: BOTH exchanges, HARD_INT direct/deferred/pending/filtered receive and owner/frame/queue checks passed");
  return 0;
}
'''
# Declare instrumentation counters independently of production initialization.
known=set(NAMES)|set(re.findall(r'\bcp32_\w+',PRELUDE))
counters=set(re.findall(r'\bcp32_\w+', BODIES))-known
prototypes='\n'.join(body[:body.index('{')].rstrip()+';' for body in
                      [extract_function(ROOT/'src/kernel/proc.c', n) for n in NAMES])
source=PRELUDE+'\n'+''.join('static unsigned '+n+';\n' for n in sorted(counters))+prototypes+'\n'+BODIES+TESTS
with tempfile.TemporaryDirectory(prefix='cp32-task-ipc-') as folder:
    p=Path(folder); (p/'test.c').write_text(source)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
