#
# This file is part of the lowest layer of CP32 kernel. All processing
# switching and message handing is done here and in file proc.
#
# Author: Ubirajara Cortes
# Date: 2026
#

#*====================================*
#*    CP32 - Cardputer ESP32-S3 OS    *
#*====================================*
.section .text, "ax"            # Executable code, a - Allocatable, x - Executable
.global CP32
CP32:                           # this is the entry point for the CP32 kernel
    # disable and clear all pending interrupts including cpu timer interrupts
    movi    a8, 0           # store 0 on a8 for next instructions
    wsr     a8, interrupt   # clear pending software IRQs
    wsr     a8, intenable   # mask ALL interrupts at CPU
    movi    a8, -1          # save -1 to a8 (In two's complement, -1 is 0xFFFFFFFF — all 32 bits set.)
    wsr     a8, intclear    # clear pending hardware IRQs (edge-triggered)
    wsr     a8, ccompare0   # push timer 0 trigger far into future
    wsr     a8, ccompare1   # push timer 1 trigger far into future
    wsr     a8, ccompare2   # push timer 2 trigger far into future

    # Stack pointer: _stack_top is the high address (stack grows down)
    movi    a1, _stack_top
    movi    a5, -16
    and     a1, a1, a5          # 16-byte align

    # Zero .bss — skip entirely if empty (start == end)
    movi    a2, _bss_start
    movi    a3, _bss_end
    movi    a4, 0
    beq     a2, a3, bss_done    # skip if bss is empty
bss_loop:
    s32i    a4, a2, 0
    addi    a2, a2, 4
    bltu    a2, a3, bss_loop
bss_done:
    call0   main                # call the main on main.c

halt:
    j       halt                # this should be never executed