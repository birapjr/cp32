/* Minimal ESP32-S3 kernel glue required while process dispatch is being ported. */
#include "kernel.h"
#include "proc.h"
#include <minix/com.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "tty.h"

phys_bytes code_base;
phys_bytes data_base;
int sig_procs;
volatile int k_reenter;
/* V12 live context restore experiment; deliberately disabled during bring-up. */
volatile int cp32_context_restore_gate;
/* Separate gate for the first live scheduler handoff experiment. */
volatile int cp32_context_handoff_gate;
struct proc *proc_ptr;
unsigned lost_ticks;
clock_t tty_timeout;
tty_t tty_table[NR_CONS + NR_RS_LINES + NR_PTYS];
tty_t *tty_timelist;

/* Kernel-side MINIX message wrappers.  The syslib macros map send/receive to
 * these symbols, so call the primitive IPC functions directly; calling the
 * macro names here would recurse back into this file. */
int _send(int dest, message *m) {
    if (proc_ptr == NIL_PROC || m == (message *)0) return EINVAL;
    return sys_call(SEND, dest, m);
}

int _receive(int src, message *m) {
    if (proc_ptr == NIL_PROC || m == (message *)0) return EINVAL;
    return sys_call(RECEIVE, src, m);
}

int _sendrec(int dest, message *m) {
    if (proc_ptr == NIL_PROC || m == (message *)0) return EINVAL;
    return sys_call(BOTH, dest, m);
}
