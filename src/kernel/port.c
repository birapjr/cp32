/* Minimal ESP32-S3 kernel glue required while process dispatch is being ported. */
#include "kernel.h"
#include "proc.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "tty.h"

phys_bytes code_base;
phys_bytes data_base;
int sig_procs;
volatile int k_reenter;
/* V12 live context restore experiment; deliberately disabled during bring-up. */
volatile int cp32_context_restore_gate;
struct proc *proc_ptr;
unsigned lost_ticks;
clock_t tty_timeout;
tty_t tty_table[NR_CONS + NR_RS_LINES + NR_PTYS];
tty_t *tty_timelist;

/* Temporary stubs to satisfy linker until Task 3.2 (sys_call) is complete */
int _send(int dest, message *m) {
    return send(dest, m);
}

int _receive(int src, message *m) {
    return receive(src, m);
}
