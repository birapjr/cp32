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

#endif
