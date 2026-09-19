/*
 * irq.S — Xtensa LX7 interrupt and exception handlers, CP32 kernel.
 *
 * RULES FOR THIS FILE:
 *   1. Literals ARE allowed (.literal_position marks the pool location).
 *   2. Each handler must save/restore every register it touches.
 *   3. Exit with rfi N (interrupt) or rfe (exception) — never ret/retw.
 *   4. Call C only after saving the context and establishing an ABI frame.
 *   5. Double exception handler must never rfi — it is a terminal state.
 *
 * Level-1 is the production dispatch path on ESP32-S3: mapped peripheral
 * IRQs arrive as EXCCAUSE_LEVEL1_INTERRUPT and use the full frame path below.
 *
 * Register ownership on interrupt entry (Xtensa LX7):
 *   a0..a15  — caller's registers, NOT saved by hardware
 *   EPC[N]   — saved PC for the interrupted context
 *   EPS[N]   — saved PS for high-priority interrupts (levels 2 and above)
 *   PS       — interrupted status plus EXCM for level-1 exceptions
 *   EXCCAUSE — reason code for exceptions
 *   EXCVADDR — faulting address (for load/store exceptions)
 *   excsave1 — software-owned scratch for level-1
 *   excsaveN — software-owned scratch for level N
 *
 * Our C-visible process frame mirrors what the kernel can observe directly:
 * the saved a0 scratch plus the saved processor status bits.
 */

#include "irq_const.h"


.section .text, "ax"

/* ── Macro: save and restore a scratch register via excsaveN ──────────────
 * Software saves a scratch register explicitly in excsaveN. For a minimal
 * handler we preserve a0 there on entry and restore it before RFI.
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
    /* No level-3 source is routed by CP32; preserve context and return. */
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
 * Route level-1 IRQs and the enabled syscall probe without clobbering the
 * interrupted registers. Other exceptions still report a terminal dump.
 */
.align 4
.global irq_kernel
.type   irq_kernel, @function
irq_kernel:
    /* EXCSAVE1 is software scratch, not a hardware-saved a0. Classify with
     * a0 alone so no interrupted argument/frame register is lost. */
    wsr     a0, excsave1
    rsr     a0, exccause
    beqi    a0, 4, irq_level1
    beqi    a0, 1, .Luser_saved_a0
    bnez    a0, .Lkernel_fatal
    movi    a0, cp32_user_probe_mode
    l32i    a0, a0, 0
    bnez    a0, .Luser_saved_a0
.Lkernel_fatal:
    rsr     a2, exccause
    rsr     a3, excvaddr
    rsr     a4, epc1
    rsr     a5, ps
    movi    a1, _stack_top
    addi    a1, a1, -16
    call0   exception_dump       /* terminal: does not return */
.size irq_kernel, .-irq_kernel

/* ── Level-1 IRQ ──────────────────────────────────────────────────────── */
/* Entered from either exception dispatcher for EXCCAUSE_LEVEL1_INTERRUPT
 * (0x04). */
.align 4
.global irq_level1
.type   irq_level1, @function
irq_level1:
    /* a0 is the only scratch used by either exception classifier, and its
     * interrupted value is in EXCSAVE1. Save a2-a15 before using any of them.
     * The existing 80-byte temporary frame keeps the C call aligned. */
    addi    a1, a1, -CP32_IRQ_FRAME_BYTES
    s32i    a2, a1, 8
    s32i    a3, a1, 12
    s32i    a4, a1, 16
    s32i    a5, a1, 20
    s32i    a6, a1, 24
    s32i    a7, a1, 28
    s32i    a8, a1, 32
    s32i    a9, a1, 36
    s32i    a10, a1, 40
    s32i    a11, a1, 44
    s32i    a12, a1, 48
    s32i    a13, a1, 52
    s32i    a14, a1, 56
    s32i    a15, a1, 60
    addi    a0, a1, CP32_IRQ_FRAME_BYTES
    s32i    a0, a1, 4
    rsr     a0, excsave1
    s32i    a0, a1, 0

    /* Capture integer special state before the C handler can clobber it. */
    rsr     a0, sar
    s32i    a0, a1, 64
    rsr     a0, lbeg
    s32i    a0, a1, 68
    rsr     a0, lend
    s32i    a0, a1, 72
    rsr     a0, lcount
    s32i    a0, a1, 76

    /* 2. Now load the base pointer to p_reg */
    movi    a2, proc_ptr
    l32i    a2, a2, 0          /* a2 = &proc[current].p_reg */
    movi    a3, cp32_irq_saved_owner
    s32i    a2, a3, 0          /* preserve interrupted owner across dispatch */
    
    /* 3. Copy from stack frame to p_reg */
    mov     a3, a1
    l32i    a4, a3, 0
    s32i    a4, a2, 0          /* p_reg.a[0] */
    l32i    a4, a3, 4
    s32i    a4, a2, 4          /* p_reg.a[1] */
    l32i    a4, a3, 8
    s32i    a4, a2, 8          /* p_reg.a[2] */
    l32i    a4, a3, 12
    s32i    a4, a2, 12         /* p_reg.a[3] */
    l32i    a4, a3, 16
    s32i    a4, a2, 16         /* p_reg.a[4] */
    l32i    a4, a3, 20
    s32i    a4, a2, 20         /* p_reg.a[5] */
    l32i    a4, a3, 24
    s32i    a4, a2, 24         /* p_reg.a[6] */
    l32i    a4, a3, 28
    s32i    a4, a2, 28         /* p_reg.a[7] */
    l32i    a4, a3, 32
    s32i    a4, a2, 32         /* p_reg.a[8] */
    l32i    a4, a3, 36
    s32i    a4, a2, 36         /* p_reg.a[9] */
    l32i    a4, a3, 40
    s32i    a4, a2, 40         /* p_reg.a[10] */
    l32i    a4, a3, 44
    s32i    a4, a2, 44         /* p_reg.a[11] */
    l32i    a4, a3, 48
    s32i    a4, a2, 48         /* p_reg.a[12] */
    l32i    a4, a3, 52
    s32i    a4, a2, 52         /* p_reg.a[13] */
    l32i    a4, a3, 56
    s32i    a4, a2, 56         /* p_reg.a[14] */
    l32i    a4, a3, 60
    s32i    a4, a2, 60         /* p_reg.a[15] */
    
    /* Save PC and PS */
    rsr     a3, epc1
    s32i    a3, a2, 64          /* p_reg.pc */
    rsr     a3, ps
    s32i    a3, a2, 68          /* p_reg.psw */
    
    /* Save the interrupted process SP, not the temporary IRQ-frame SP.
     * The original a1 was captured at frame offset +4 before allocation;
     * using the temporary frame address here breaks a1==sp after handoff. */
    l32i    a3, a1, 4
    s32i    a3, a2, 72           /* p_reg.sp */

    
    l32i    a3, a1, 64
    s32i    a3, a2, CP32_REG_SAR_OFFSET
    l32i    a3, a1, 68
    s32i    a3, a2, CP32_REG_LBEG_OFFSET
    l32i    a3, a1, 72
    s32i    a3, a2, CP32_REG_LEND_OFFSET
    l32i    a3, a1, 76
    s32i    a3, a2, CP32_REG_LCOUNT_OFFSET

    /* Track interrupt nesting */
    movi    a2, k_reenter
    l32i    a3, a2, 0
    addi    a3, a3, 1
    s32i    a3, a2, 0
    
    /* Read the pending bitmap before any device acknowledge. Device
     * handlers own their peripheral clear/rearm and source accounting. */
    /* Call the scheduler */
    mov     a2, a1
    /* Use current stack pointer as a base for a15 to avoid NULL dereference in printk */
    mov     a15, a1
    /* Classify the pending CPU interrupt bitmap in C.  The dispatcher owns
     * device routing; the timer is only one registered source. */
    rsr     a3, interrupt
    call0   cp32_irq_dispatch
    
1:
    movi    a2, k_reenter
    l32i    a3, a2, 0
    addi    a3, a3, -1
    s32i    a3, a2, 0
    
    /* Select one authoritative frame for every register, including PS/PC.
     * With handoff disabled, even a changed scheduler pointer must not alter
     * the interrupted context. */
    movi    a3, cp32_context_handoff_gate
    l32i    a3, a3, 0
    beqz    a3, .Lirq_restore_owner
    movi    a15, cp32_irq_return_proc
    j       .Lirq_restore_selected
.Lirq_restore_owner:
    movi    a15, cp32_irq_saved_owner
.Lirq_restore_selected:
    l32i    a15, a15, 0
    j       .Lrestore_process_frame

.size irq_level1, .-irq_level1

/* ── User exception ───────────────────────────────────────────────────── */
.align 4
.global irq_user
.type   irq_user, @function
irq_user:
    wsr     a0, excsave1
.Luser_saved_a0:
    /* A user-mode level-1 IRQ must reach the same untouched-register save
     * path as a kernel-mode IRQ, before building a syscall frame. */
    rsr     a0, exccause
    beqi    a0, 4, irq_level1
    /* Build the frame before reading exception CSRs.  In particular, the
     * original user a2 is the syscall number and must not be replaced by
     * EXCCAUSE.  a0 is used as the temporary frame base; original a0 is
     * recovered from EXCSAVE1 after original a1 has been saved.  Save every
     * general register before using one as the exception-cause scratch; this
     * preserves the interrupted user a5 as well as the other call0 state. */
    /* Each task owns its exception stack; never overwrite the boot stack. */
    addi    a0, a1, -128
    s32i    a1, a0, 4
    s32i    a2, a0, 8
    s32i    a3, a0, 12
    s32i    a4, a0, 16
    s32i    a5, a0, 20
    s32i    a6, a0, 24
    s32i    a7, a0, 28
    s32i    a8, a0, 32
    s32i    a9, a0, 36
    s32i    a10, a0, 40
    s32i    a11, a0, 44
    s32i    a12, a0, 48
    s32i    a13, a0, 52
    s32i    a14, a0, 56
    s32i    a15, a0, 60
    rsr     a1, excsave1
    s32i    a1, a0, 0
    rsr     a1, exccause
    rsr     a1, epc1
    s32i    a1, a0, 64
    rsr     a1, ps
    s32i    a1, a0, 68
    l32i    a1, a0, 4
    s32i    a1, a0, 72

    rsr     a1, sar
    s32i    a1, a0, CP32_REG_SAR_OFFSET
    rsr     a1, lbeg
    s32i    a1, a0, CP32_REG_LBEG_OFFSET
    rsr     a1, lend
    s32i    a1, a0, CP32_REG_LEND_OFFSET
    rsr     a1, lcount
    s32i    a1, a0, CP32_REG_LCOUNT_OFFSET

    movi    a2, proc_ptr
    l32i    a2, a2, 0
    mov     a3, a0
    rsr     a4, exccause
    mov     a1, a3
    mov     a15, a1
    beqi    a4, 1, .Ltask_ipc
    call0   cp32_user_trap_dispatch
    mov     a1, a15

    movi    a3, cp32_user_dispatch_blocked
    l32i    a3, a3, 0
    beqz    a3, 2f

    /* A successful gated dispatch owns the saved frame and may return to
     * user mode.  Keep the diagnostic path for every rejected frame. */
    movi    a3, 0
    bne     a2, a3, 1f
    /* Immediate syscall: restore the same frame and return to user mode. */
    j       2f

1:
    /* A blocked syscall cannot rfe its owner.  Give the guarded scheduler
     * handoff a chance to replace the frame with another runnable process. */
    movi    a2, proc_ptr
    l32i    a2, a2, 0
    mov     a3, a1
    call0   cp32_user_blocked_handoff
    movi    a3, 0
    bne     a2, a3, 3f
    mov     a15, a1
    j       2f

3:
    j       4f

2:
    /* Restore the authoritative process frame.  The C syscall dispatcher
     * advances FS->p_reg.pc and records the syscall result there; restoring
     * the stale trap frame loses that progress after the first read. */
    movi    a15, cp32_irq_return_proc
    l32i    a15, a15, 0
    /* Record return diagnostics before restoring any live register. */
    l32i    a2, a15, 64
    movi    a3, cp32_user_rfe_epc
    s32i    a2, a3, 0
    l32i    a2, a15, 68
    movi    a3, cp32_user_rfe_ps
    s32i    a2, a3, 0
    l32i    a2, a15, 72
    movi    a3, cp32_user_rfe_sp
    s32i    a2, a3, 0
    movi    a3, cp32_user_rfe_count
    l32i    a2, a3, 0
    addi    a2, a2, 1
    s32i    a2, a3, 0
    j       .Lrestore_process_frame

.Ltask_ipc:
    call0   cp32_task_ipc_dispatch
    bnez    a2, 4f
    j       2b

4:

    rsr     a2, exccause
    rsr     a3, excvaddr
    rsr     a4, epc1
    rsr     a5, ps
    movi    a1, _stack_top
    addi    a1, a1, -16
    call0   exception_dump
.size irq_user, .-irq_user

/* call0 arguments a2=function, a3=endpoint, a4=message. The exception
 * advances EPC1 past this three-byte instruction; a0 remains the caller's
 * return address, including after a blocked request resumes. */
.align 4
.global cp32_ipc_trap
.type cp32_ipc_trap, @function
cp32_ipc_trap:
    syscall
    ret
.size cp32_ipc_trap, .-cp32_ipc_trap

.align 4
.global cp32_enter_initial_user
.type   cp32_enter_initial_user, @function
cp32_enter_initial_user:
    mov     a15, a2
    j       .Lrestore_process_frame
.size cp32_enter_initial_user, .-cp32_enter_initial_user

/* MINIX restart restores one complete selected process frame. Xtensa level-1
 * exceptions use live PS and EPC1, unlike higher levels' EPSn/RFI n pair.
 * Cadence Xtensa ISA Summary sections 7.4.9 and 8.3.258-259: SR192 is DEPC;
 * RFI 0/1 is undefined. Espressif xtensa_vectors.S likewise writes PS + RFE.
 *
 * Keep EXCM set until RFE, including for a newly created task whose saved PS
 * has no EXCM bit, so level-1 cannot interrupt this partial restore. Never
 * borrow a general register again after restoring its final value.
 *
 * Integer SAR/loop state belongs to the selected process too. Floating-point,
 * MAC and other optional extension state still require a separate policy.
 */
.align 4
.Lrestore_process_frame:
    l32i    a2, a15, CP32_REG_PC_OFFSET
    wsr     a2, epc1
    l32i    a2, a15, CP32_REG_PSW_OFFSET
    movi    a3, 0x10             /* PS.EXCM */
    or      a2, a2, a3
    wsr     a2, ps
    l32i    a2, a15, CP32_REG_SAR_OFFSET
    wsr     a2, sar
    l32i    a2, a15, CP32_REG_LBEG_OFFSET
    wsr     a2, lbeg
    l32i    a2, a15, CP32_REG_LEND_OFFSET
    wsr     a2, lend
    l32i    a2, a15, CP32_REG_LCOUNT_OFFSET
    wsr     a2, lcount
    l32i    a0, a15, 0
    l32i    a1, a15, CP32_REG_SP_OFFSET
    l32i    a2, a15, 8
    l32i    a3, a15, 12
    l32i    a4, a15, 16
    l32i    a5, a15, 20
    l32i    a6, a15, 24
    l32i    a7, a15, 28
    l32i    a8, a15, 32
    l32i    a9, a15, 36
    l32i    a10, a15, 40
    l32i    a11, a15, 44
    l32i    a12, a15, 48
    l32i    a13, a15, 52
    l32i    a14, a15, 56
    l32i    a15, a15, 60
    rsync
    rfe

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
