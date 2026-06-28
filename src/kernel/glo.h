/**
 * Global varialbles use in the kernel
 * 
 * Author: Ubirajara Cortes
 * Date: 22.05.2026
 */

#ifndef GLO_H
#define GLO_H

#include <stdint.h>
#include <minix/type.h>
#include "kernel.h"

/* Global variables used in the kernel. */

/* EXTERN is defined as extern except in table.c. */
#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

/* Kernel memory. */
EXTERN phys_bytes code_base;	/* base of kernel code */
EXTERN phys_bytes data_base;	/* base of kernel data */

/* Signals. */
EXTERN int sig_procs;		/* number of procs with p_pending != 0 */

/* ── Persistent kernel state ──────────────────────────────────────────────────
 * Placing at least one symbol in .bss and one in .data ensures the linker
 * script pins both sections into DRAM and that the startup code in mpx32.S
 * actually zeroes / copies them.
 */
static volatile uint32_t g_bss_magic;            /* .bss — zeroed by startup */
static volatile uint32_t g_magic = 0xC0320000u;  /* .data — copied by startup */

/* Kernel memory map populated by mem_init(). */
extern struct memory mem[3];
extern phys_clicks tot_mem_size;
EXTERN unsigned int processor;	/* 32 for esp32s3 */

/* Interrupt re-entrancy counter for ESP32-S3 (Xtensa LX7).
 * Incremented on interrupt entry, decremented on exit.
 * If > 0 when clock_handler fires, we interrupted another interrupt
 * handler — charge time to HARDWARE instead of proc_ptr.
 * Replaces the Intel-specific k_reenter variable.
 */
EXTERN int k_reenter;           /* kernel reenter count (0 = from user/task) */

#endif
