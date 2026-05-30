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

/* start.c */
_PROTOTYPE( char *k_getenv, (char *name)				);

/* printk.c */
_PROTOTYPE( void printk, (const char *fmt, ...)			);

#endif /* PROTO_H */
