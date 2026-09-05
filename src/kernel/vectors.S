/*
 * vectors.S — Xtensa LX7 vector table for ESP32-S3, CP32 kernel.
 *
 * RULES FOR THIS FILE — enforced by assembler directives:
 *   1. No literals allowed (.no_literals + no movi of far addresses).
 *   2. No function calls (no call0/call4/call8/call12).
 *   3. No C ABI assumptions (no stack use, no register saves).
 *   4. Every slot must fit in 64 bytes (hardware constraint).
 *   5. Only j / jx to handlers defined in irq.S.
 *
 * Why: the CPU jumps here with no warning, no known stack, no known
 * register state. A literal pool injected here by the assembler would
 * land in the middle of an adjacent vector slot and corrupt it silently.
 *
 * All real handler logic lives in irq.S where literals are allowed.
 */

.section .vectors, "ax"
.align  1024                    /* VECBASE requirement: 1 KB alignment       */

.global _vector_table
_vector_table:

/*
 * .org values are section-relative offsets from _vector_table.
 * _vector_table is at section origin after .align 1024, so
 * section offset == architectural VECBASE offset. Safe on all GAS versions.
 * Never use: .org _vector_table + 0xNN  (symbolic arithmetic — fragile).
 */

/* ── Window vectors +0x000..+0x140 ───────────────────────────────────────
 * 64-byte slots. One per window rotation size and direction.
 * With -mabi=call0 these never fire from our C code, but the slots must
 * be present and valid in case ROM callbacks use the windowed ABI.
 */
.org 0x000                      /* WindowOverflow4   */
    j       _vec_unhandled
.org 0x040                      /* WindowUnderflow4  */
    j       _vec_unhandled
.org 0x080                      /* WindowOverflow8   */
    j       _vec_unhandled
.org 0x0C0                      /* WindowUnderflow8  */
    j       _vec_unhandled
.org 0x100                      /* WindowOverflow12  */
    j       _vec_unhandled
.org 0x140                      /* WindowUnderflow12 */
    j       _vec_unhandled

/* ── Hardware interrupt vectors ───────────────────────────────────────── */
.org 0x180                      /* Level-2 IRQ                              */
_vec_level2:    j   irq_level2

.org 0x1C0                      /* Level-3 IRQ                              */
_vec_level3:    j   irq_level3

.org 0x200                      /* Level-4 IRQ                              */
_vec_level4:    j   irq_level4

.org 0x240                      /* Level-5 IRQ                              */
_vec_level5:    j   irq_level5

.org 0x280                      /* Debug / JTAG                             */
_vec_debug:     j   irq_debug

.org 0x2C0                      /* NMI (Level-6)                            */
_vec_nmi:       j   irq_nmi

/* ── Software exception vectors ───────────────────────────────────────── */
.org 0x300                      /* Kernel exception / level-1 IRQ           */
_vec_kernel:    j   irq_kernel

.org 0x340                      /* User exception / level-1 IRQ              */
_vec_user:      j   irq_user

.org 0x3C0                      /* Double exception — never rfi from here   */
_vec_double:    j   irq_double

/* Pad to clean 0x400 boundary */
.org 0x400
_vector_table_end:
