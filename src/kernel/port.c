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
struct proc *proc_ptr;
unsigned lost_ticks;
clock_t tty_timeout;
tty_t tty_table[NR_CONS + NR_RS_LINES + NR_PTYS];
tty_t *tty_timelist;

int _send(int dest, message *m_ptr)
{
  (void) dest; (void) m_ptr;
  return OK;
}

int _receive(int src, message *m_ptr)
{
  (void) src; (void) m_ptr;
  return OK;
}
