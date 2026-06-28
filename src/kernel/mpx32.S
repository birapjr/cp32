#
# This file is part of the lowest layer of CP32 kernel.
#
# Author: Ubirajara Cortes
# Date: 16.05.2026
#

#*====================================*
#*    CP32 - Cardputer ESP32-S3 OS    *
#*====================================*

# CRITICAL: this section must match the linker script .startup section name.
# It runs from flash initially — its job is to copy everything to IRAM/DRAM
# before jumping into fast memory.
.section .startup.text, "ax"    # Executable code, a - Allocatable, x - Executable
.global CP32
CP32:                           # this is the entry point for the CP32 kernel

    # ----------------------------------------------------------------
    # STEP 1 — Disable and clear all pending interrupts
    # Must be first: we don't want any IRQ firing during setup
    # ----------------------------------------------------------------
    movi    a8, 0               # store 0 on a8 for next instructions
    wsr     a8, interrupt       # clear pending software IRQs
    wsr     a8, intenable       # mask ALL interrupts at CPU
    movi    a8, -1              # save -1 to a8 (In two's complement, -1 is 0xFFFFFFFF — all 32 bits set.)
    wsr     a8, intclear        # clear pending hardware IRQs (edge-triggered)
    wsr     a8, ccompare0       # push timer 0 trigger far into future
    wsr     a8, ccompare1       # push timer 1 trigger far into future
    wsr     a8, ccompare2       # push timer 2 trigger far into future

    # ----------------------------------------------------------------
    # STEP 2 — Set stack pointer (we need a stack for nothing yet,
    # but set it early so any fault has a valid SP)
    # _stack_top is the HIGH address; stack grows downward.
    # ----------------------------------------------------------------
    movi    a1, _stack_top
    movi    a5, -16
    and     a1, a1, a5          # 16-byte align (Xtensa ABI requirement)

    # ----------------------------------------------------------------
    # STEP 3 — Zero .bss
    #
    # C standard requires zero-initialized globals to be zero.
    # No flash copy needed — just write zeros to _bss_start.._bss_end.
    # ----------------------------------------------------------------
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0
    beq     a2, a3, bss_done
bss_loop:
    s32i    a4, a2, 0
    addi    a2, a2, 4
    bltu    a2, a3, bss_loop
bss_done:

    # ----------------------------------------------------------------
    # STEP 4 — Relocate VECBASE to our vector table in IRAM
    #
    # At reset VECBASE points to ROM (0x40000000). We must point it
    # to our vector table that is now copied into IRAM.
    # rsync flushes the pipeline so the new VECBASE takes effect
    # before the next instruction that could trigger an exception.
    # ----------------------------------------------------------------
    movi    a2, _vectors_start
    wsr     a2, vecbase
    rsync

    # ----------------------------------------------------------------
    # STEP 4- Prepare environmnet for the Kernel
    # ----------------------------------------------------------------
    call0   start

    # ----------------------------------------------------------------
    # STEP 6- Enter Kernel
    # ----------------------------------------------------------------
    call0   main                # call main() in C

halt:
    j       halt                # this should be never executed

;*===========================================================================*
;*        lock                                                                *
;*===========================================================================*
; void lock(void);
; Disable CPU interrupts by clearing PS.INTLEVEL to maximum (level 15).
    .section .text
    .align  4
    .global lock
    .type   lock, @function
lock:
    rsil    a2, 15          /* read PS, then set INTLEVEL=15 (disable all interrupts) */
    ret
    .size   lock, . - lock


;*===========================================================================*
;*        unlock                                                              *
;*===========================================================================*
; void unlock(void);
; Enable CPU interrupts by setting PS.INTLEVEL to 0.
    .section .text
    .align  4
    .global unlock
    .type   unlock, @function
unlock:
    rsil    a2, 0           /* read PS, then set INTLEVEL=0 (enable all interrupts) */
    ret
    .size   unlock, . - unlock