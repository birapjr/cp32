/*
 * irq.S — Xtensa LX7 interrupt and exception handlers, CP32 kernel.
 *
 * RULES FOR THIS FILE:
 *   1. Literals ARE allowed (.literal_position marks the pool location).
 *   2. Each handler must save/restore every register it touches.
 *   3. Exit with rfi N (interrupt) or rfe (exception) — never ret/retw.
 *   4. Never use call0/call4 — the ABI stack frame is not set up on entry.
 *   5. Double exception handler must never rfi — it is a terminal state.
 *
 * Bring-up stubs: all handlers currently spin or return immediately.
 * Replace them one at a time as real functionality is needed.
 *
 * Register ownership on interrupt entry (Xtensa LX7):
 *   a0..a15  — caller's registers, NOT saved by hardware
 *   EPC[N]   — saved PC for the interrupted context
 *   EPS[N]   — saved PS for the interrupted context
 *   EXCCAUSE — reason code for exceptions
 *   EXCVADDR — faulting address (for load/store exceptions)
 *   excsave1 — scratch register saved by hardware for level-1
 *   excsaveN — scratch for level N
 *
 * Our C-visible process frame mirrors what the kernel can observe directly:
 * the saved a0 scratch plus the saved processor status bits.
 */

.section .text, "ax"

/* ── Macro: save and restore a scratch register via excsaveN ──────────────
 * Xtensa saves one register per interrupt level automatically into excsaveN.
 * For a minimal handler that only reads cause/addr registers we use a0
 * as our scratch and swap it with excsaveN on entry/exit.
 *
 * Usage:
 *   HANDLER_ENTRY 2    @ saves a0 into excsave2, frees a0 as scratch
 *   HANDLER_EXIT  2    @ restores a0 from excsave2, returns with rfi 2
 */
.macro HANDLER_ENTRY level
    wsr     a0, excsave\level   /* save caller's a0; a0 is now our scratch  */
.endm

.macro HANDLER_EXIT level
    rsr     a0, excsave\level   /* restore caller's a0                      */
    rfi     \level              /* restore PS from EPS[N], PC from EPC[N]   */
.endm

/* ── Fallback: unhandled window vectors and anything not yet wired ─────── */
/*
 * Spins forever. Makes any misfired vector a visible hard hang rather
 * than silent state corruption. Replace with a register dump + reboot
 * once USB serial is reliable.
 */
.align 4
.global _vec_unhandled
.type   _vec_unhandled, @function
_vec_unhandled:
    j       _vec_unhandled
.size _vec_unhandled, .-_vec_unhandled

/* ── Level-2 IRQ ──────────────────────────────────────────────────────── */
.align 4
.global irq_level2
.type   irq_level2, @function
irq_level2:
    HANDLER_ENTRY 2
    /* Minimal SYSTIMER probe handler: acknowledge TARGET0 only. */
    addi    a1, a1, -16
    s32i    a2, a1, 0
    s32i    a3, a1, 4
    movi    a2, 0x6002306C
    movi    a3, 1
    s32i    a3, a2, 0
    movi    a2, cp32_timer_irq_ticks
    l32i    a3, a2, 0
    addi    a3, a3, 1
    s32i    a3, a2, 0
    l32i    a3, a1, 4
    l32i    a2, a1, 0
    addi    a1, a1, 16
    HANDLER_EXIT  2
.size irq_level2, .-irq_level2

/* ── Level-3 IRQ ──────────────────────────────────────────────────────── */
.align 4
.global irq_level3
.type   irq_level3, @function
irq_level3:
    HANDLER_ENTRY 3
    /* TODO: dispatch */
    HANDLER_EXIT  3
.size irq_level3, .-irq_level3

/* ── Level-4 IRQ ──────────────────────────────────────────────────────── */
.align 4
.global irq_level4
.type   irq_level4, @function
irq_level4:
    HANDLER_ENTRY 4
    HANDLER_EXIT  4
.size irq_level4, .-irq_level4

/* ── Level-5 IRQ ──────────────────────────────────────────────────────── */
.align 4
.global irq_level5
.type   irq_level5, @function
irq_level5:
    HANDLER_ENTRY 5
    HANDLER_EXIT  5
.size irq_level5, .-irq_level5

/* ── Debug / JTAG ─────────────────────────────────────────────────────── */
/*
 * Entered when JTAG breakpoint fires or ICOUNT trigger trips.
 * rfi 4 returns to the interrupted instruction at debug level.
 * With no debugger attached this spins — avoids corrupting state
 * by blindly returning from an unexpected debug entry.
 */
.align 4
.global irq_debug
.type   irq_debug, @function
irq_debug:
    j       irq_debug           /* spin until JTAG connects */
.size irq_debug, .-irq_debug

/* ── NMI (Level-6) ────────────────────────────────────────────────────── */
/*
 * Non-maskable interrupt. On ESP32-S3 this is typically the
 * Super WDT or a cache error. Spin for now — a real handler
 * would snapshot state and trigger a clean reset.
 */
.align 4
.global irq_nmi
.type   irq_nmi, @function
irq_nmi:
    j       irq_nmi
.size irq_nmi, .-irq_nmi

/* ── Kernel exception ─────────────────────────────────────────────────── */
/*
 * Handles early kernel exceptions by reporting the saved exception state.
 * EXCVADDR holds the faulting address for load/store causes. The handler is
 * terminal during bring-up; it does not attempt to resume with rfe because
 * no process exception policy or complete saved register frame exists yet.
 */
.align 4
.global irq_kernel
.type   irq_kernel, @function
irq_kernel:
    wsr     a0, excsave1        /* save a0; excsave1 is the level-1 scratch */
    rsr     a2, exccause        /* arg0: exception cause                    */
    /* CPU interrupt line 2 is Xtensa level 1 on ESP32-S3.  Level-1
     * interrupts share this kernel-exception vector. */
    movi    a3, 0x04            /* EXCCAUSE_LEVEL1_INTERRUPT */
    beq     a2, a3, irq_level1
    rsr     a3, excvaddr        /* arg1: faulting address                   */
    rsr     a4, epc1            /* arg2: saved program counter              */
    rsr     a5, ps              /* arg3: current processor status             */
    movi    a1, _stack_top
    addi    a1, a1, -16
    call0   exception_dump       /* terminal: does not return                */
.size irq_kernel, .-irq_kernel

/* ── Level-1 IRQ ──────────────────────────────────────────────────────── */
/* Entered from irq_kernel for EXCCAUSE_LEVEL1_INTERRUPT (0x04). */
.align 4
.global irq_level1
.type   irq_level1, @function
irq_level1:
    wsr     a0, excsave1
    addi    a1, a1, -32
    s32i    a2, a1, 0
    s32i    a3, a1, 4
    s32i    a4, a1, 8
    s32i    a5, a1, 12
    /* Acknowledge TARGET0. Periodic mode keeps the source enabled and
     * reloads the next deadline in hardware. */
    movi    a2, 0x6002306C
    movi    a3, 1
    s32i    a3, a2, 0
    movi    a2, cp32_timer_irq_ticks
    l32i    a3, a2, 0
    addi    a3, a3, 1
    s32i    a3, a2, 0
    l32i    a5, a1, 12
    l32i    a4, a1, 8
    l32i    a3, a1, 4
    l32i    a2, a1, 0
    addi    a1, a1, 32
    /* Level 1 arrived through the exception vector, so PS.EXCM is set.
     * Return with RFE; RFI 1 is only for a dedicated level-1 interrupt
     * vector and would re-enter the exception path on this core. */
    rsr     a0, excsave1
    rsync
    rfe
.size irq_level1, .-irq_level1

/* ── User exception ───────────────────────────────────────────────────── */
.align 4
.global irq_user
.type   irq_user, @function
irq_user:
    wsr     a0, excsave1
    rsr     a2, exccause
    rsr     a3, excvaddr
    rsr     a4, epc1
    rsr     a5, ps
    movi    a1, _stack_top
    addi    a1, a1, -16
    call0   exception_dump
.size irq_user, .-irq_user

/* ── Double exception ─────────────────────────────────────────────────── */
/*
 * An exception occurred inside an exception handler — almost always
 * a kernel bug (bad pointer in an ISR, corrupted stack, etc.).
 *
 * CRITICAL: never rfi or rfe here. The interrupted handler's state
 * is unrecoverable. Spin forever or trigger a hardware reset via
 * the RTC WDT. A real implementation would dump EXCCAUSE, EXCVADDR,
 * EPC1, and all registers before resetting.
 */
.align 4
.global irq_double
.type   irq_double, @function
irq_double:
    j       irq_double          /* terminal — kernel is broken */
.size irq_double, .-irq_double
