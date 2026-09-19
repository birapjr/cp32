/* Minimal ESP32-S3 kernel glue required while process dispatch is being ported. */
#include "kernel.h"
#include "proc.h"
#include <minix/com.h>
#include <minix/callnr.h>
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
extern struct proc *current_proc;
unsigned lost_ticks;
clock_t tty_timeout;
tty_t tty_table[NR_CONS + NR_RS_LINES + NR_PTYS];
tty_t *tty_timelist;
/* A blocking IPC operation returns only through its saved task frame. */
extern int cp32_ipc_trap(int function, int endpoint, message *m);
int _send(int dest, message *m) { return cp32_ipc_trap(SEND, dest, m); }
int _receive(int src, message *m) { return cp32_ipc_trap(RECEIVE, src, m); }
int _sendrec(int dest, message *m) { return cp32_ipc_trap(BOTH, dest, m); }
