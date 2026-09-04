/* Function prototypes. */

#ifndef PROTO_H
#define PROTO_H

/* Struct declarations. */
struct dpeth;
struct proc;
struct tty;

/* main.c */
_PROTOTYPE( void main, (void)						);
_PROTOTYPE( void panic, (const char *s, int n)				);

/* misc.c */
_PROTOTYPE( void mem_init, (void)					);

/* klib32.S */
_PROTOTYPE( void phys_copy, (phys_bytes source, phys_bytes destination, phys_bytes bytecount)				);
_PROTOTYPE( void enable_irq, (unsigned irq)				);
_PROTOTYPE( int disable_irq, (unsigned irq)				);

/* mpx32.S */
_PROTOTYPE( void lock, (void)						);
_PROTOTYPE( void unlock, (void)						);

/* start.c */
_PROTOTYPE( char *k_getenv, (char *name)				);

/* printk.c */
_PROTOTYPE( void printk, (const char *fmt, ...)			);

/* proc.c */
_PROTOTYPE( void interrupt, (int task)					);
_PROTOTYPE( int lock_mini_send, (struct proc *caller_ptr, int dest, message *m_ptr));
_PROTOTYPE( void lock_pick_proc, (void)					);
_PROTOTYPE( void lock_ready, (struct proc *rp)				);
_PROTOTYPE( void lock_sched, (void)					);
_PROTOTYPE( void lock_unready, (struct proc *rp)			);
_PROTOTYPE( int sys_call, (int function, int src_dest, message *m_ptr)	);
_PROTOTYPE( void unhold, (void)						);

/* system.c */
_PROTOTYPE( void cause_sig, (int proc_nr, int sig_nr)			);
_PROTOTYPE( void inform, (void)						);
_PROTOTYPE( void alloc_segments, (struct proc *rp)			);
_PROTOTYPE( phys_bytes numap, (int proc_nr, vir_bytes vir_addr, 
		vir_bytes bytes)					);
_PROTOTYPE( void sys_task, (void)					);
_PROTOTYPE( phys_bytes umap, (struct proc *rp, int seg, vir_bytes vir_addr,
		vir_bytes bytes)					);

/* clock.c */
_PROTOTYPE( void clock_task, (void)					);
_PROTOTYPE( void clock_stop, (void)					);
_PROTOTYPE( clock_t get_uptime, (void)					);
_PROTOTYPE( void syn_alrm_task, (void)					);

/* tty.c */
_PROTOTYPE( void handle_events, (struct tty *tp)                 );
_PROTOTYPE( void tty_reply, (int code, int replyee, int proc_nr,
		int status)                                      );
_PROTOTYPE( void tty_wakeup, (clock_t now)				);

#endif /* PROTO_H */
