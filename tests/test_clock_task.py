#!/usr/bin/env python3
"""Run the production CLOCK receive loop, dispatch, tick and uptime bodies."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from test_idle_handoff import extract_function

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['clock_task', 'cp32_clock_task_dispatch', 'do_clocktick',
         'do_getuptime', 'get_uptime']
bodies = [extract_function(ROOT/'src/kernel/clock.c', n) for n in NAMES]
PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <setjmp.h>
#include <assert.h>
#define PUBLIC
#define PRIVATE static
#define CP32_IRAM_EXT
#define CP32_VERBOSE_DIAGNOSTICS 1
#define TRUE 1
#define NIL_PROC ((struct proc*)0)
#define SHADOWING 0
#define NO_NUM 0
#define SIGALRM 14
typedef long clock_t;
typedef struct { int m_source,m_type; long NEW_TIME; } message;
enum { OK=0, HARDWARE=-1, HARD_INT=2, GET_UPTIME=3, GET_TIME=4,
       SET_TIME=5, SET_ALARM=6, SET_SYNC_AL=7, ANY=104, NR_TASKS=9,
       NR_PROCS=4, SCHED_RATE=6 };
struct proc { int p_nr; clock_t p_alarm; };
static struct proc proc[NR_TASKS+NR_PROCS];
#define BEG_PROC_ADDR proc
#define END_PROC_ADDR (proc+NR_TASKS+NR_PROCS)
#define proc_number(p) ((p)->p_nr)
static struct proc *bill_ptr, *prev_ptr, *current_proc;
static clock_t realtime, pending_ticks, next_alarm;
static int sched_ticks, watchdog_proc;
static void (*watch_dog[NR_TASKS+NR_PROCS])(void);
static unsigned cp32_clock_alarm_expiries, cp32_clock_dispatch_count;
static message mc;
static int mask=3, receives, sends, traces, schedules, signals, watchdogs;
static jmp_buf stop;
static int lock_save(void) { int old=mask; mask=15; return old; }
static void restore_lock(int value) { mask=value; }
static void usbj_print(const char *s) { (void)s; }
static void panic(const char *s,int n) { (void)s; (void)n; assert(!"unexpected panic"); }
static void cp32_trace_clock_receive(void) { traces++; }
static void cause_sig(int n,int sig) { assert(n==0 && sig==SIGALRM); signals++; }
static void watchdog(void) { assert(watchdog_proc==-3); watchdogs++; }
void lock_sched(void) { schedules++; current_proc=&proc[0]; }
static void do_get_time(void) { assert(!"unexpected get_time"); }
static void do_set_time(message *m) { (void)m; assert(!"unexpected set_time"); }
static void do_setalarm(message *m) { (void)m; assert(!"unexpected alarm request"); }
static void do_setsyn_alrm(message *m) { (void)m; assert(!"unexpected sync alarm"); }
static int receive(int src,message *m) {
  assert(src==ANY && mask==3);
  if (++receives==3) longjmp(stop,1);
  if (receives==1) { pending_ticks+=5; m->m_source=HARDWARE; m->m_type=HARD_INT; }
  else { pending_ticks+=2; m->m_source=1; m->m_type=GET_UPTIME; }
  return OK;
}
static int send(int dest,message *m) {
  assert(dest==1 && m->m_type==OK && m->NEW_TIME==114); sends++; return OK;
}
'''
TESTS = r'''
int main(void) {
  for (int i=0;i<NR_TASKS+NR_PROCS;i++) proc[i].p_nr=i-NR_TASKS;
  bill_ptr=prev_ptr=current_proc=&proc[NR_TASKS+1];
  realtime=100; pending_ticks=7; next_alarm=LONG_MAX; sched_ticks=1;
  if (!setjmp(stop)) clock_task();
  assert(receives==3 && traces==2 && sends==1 && cp32_clock_dispatch_count==2);
  assert(realtime==114 && pending_ticks==0 && sched_ticks==SCHED_RATE);
  assert(current_proc==bill_ptr && schedules==0 && mask==3);
  /* Uptime includes undrained ticks without consuming them or changing PS. */
  pending_ticks=8; assert(get_uptime()==122 && pending_ticks==8 && mask==3);
  /* Real alarm processing still runs, once, while ownership stays stable. */
  proc[NR_TASKS].p_alarm=120; proc[NR_TASKS-3].p_alarm=119;
  proc[NR_TASKS+2].p_alarm=200; watch_dog[NR_TASKS-3]=watchdog;
  next_alarm=119;
  assert(cp32_clock_task_dispatch(HARD_INT)==0);
  assert(realtime==122 && pending_ticks==0 && next_alarm==200);
  assert(signals==1 && watchdogs==1 && cp32_clock_alarm_expiries==2);
  assert(current_proc==bill_ptr && mask==3);
  assert(cp32_clock_task_dispatch(HARD_INT)==0);
  assert(signals==1 && watchdogs==1);
  puts("CLOCK task: live receive/dispatch/reply, retained uptime, quantum owner and alarms passed");
  return 0;
}
'''
prototypes='\n'.join(b[:b.index('{')].rstrip()+';' for b in bodies)
with tempfile.TemporaryDirectory(prefix='cp32-clock-task-') as folder:
    p=Path(folder); (p/'test.c').write_text(PRELUDE+prototypes+'\n'+'\n'.join(bodies)+TESTS)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
