/* General constants used by the kernel. */

#include "irq_const.h"

/* Temporary guard for the downward-growing early kernel stack. */
#define CP32_STACK_GUARD_WORD 0xC0325A7Au

#define K_STACK_BYTES   4096	/* stack space for the kernel on DRAM */

/* Sizes of memory tables. */
#define NR_MEMS            3	/* number of chunks of memory */

#define NR_REGS           16	/* Xtensa core register window is not used here */

#define TRACEBIT       0x0000	/* no legacy tracing bit on this port */
#define SETPSW(rp, new)		/* no x86-style PSW mask on Xtensa */ \
	((rp)->p_reg.psw = (reg_t) (new))

/* The following items pertain to the scheduling queues. */
#define TASK_Q             0	/* ready tasks are scheduled via queue 0 */
#define SERVER_Q           1	/* ready servers are scheduled via queue 1 */
#define USER_Q             2	/* ready users are scheduled via queue 2 */
#define NQ                 3	/* # of scheduling queues */

/* Env_parse() return values. */
#define EP_UNSET	0	/* variable not set */
#define EP_OFF		1	/* var = off */
#define EP_ON		2	/* var = on (or field left blank) */
#define EP_SET		3	/* var = 1:2:3 (nonblank field) */

/* To translate an address in kernel space to a physical address.  This is
 * the same as umap(proc_ptr, D, vir, sizeof(*vir)), but a lot less costly.
 */
#define vir2phys(vir)	(data_base + (vir_bytes) (vir))

#define printf        printk	/* the kernel really uses printk, not printf */

/* Hardware interrupt numbers. */
#define NR_IRQ_VECTORS    16
#define CLOCK_IRQ          0
#define KEYBOARD_IRQ       1
#define CASCADE_IRQ        2	/* cascade enable for 2nd AT controller */
#define ETHER_IRQ          3	/* default ethernet interrupt vector */
#define SECONDARY_IRQ      3	/* RS232 interrupt vector for port 2 */
#define RS232_IRQ          4	/* RS232 interrupt vector for port 1 */
#define XT_WINI_IRQ        5	/* xt winchester */
#define FLOPPY_IRQ         6	/* floppy disk */
#define PRINTER_IRQ        7
#define AT_WINI_IRQ       14	/* at winchester */


#define BASE_PRINT_WIDTH 40 /* used to format the status_line() output size */
