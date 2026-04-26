#
# This file is part of the lowest layer of CP32 kernel. All processing
# switching and message handing is done here and in file proc.
#

#*====================================*
#*    CP32 - Cardputer ESP32-S3 OS    *
#*====================================*
.section .text, "ax"            # Executable code, a - Allocatable, x - Executable
.global CP32
CP32:                           # this is the entry point for the CP32 kernel
    #rsil    a8, 15             # disable all interrupts

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