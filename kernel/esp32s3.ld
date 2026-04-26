MEMORY {
    iram (RX)  : ORIGIN = 0x40370000, LENGTH = 384K
    dram (RW)  : ORIGIN = 0x3FC88000, LENGTH = 384K
}

SECTIONS {
    .text : ALIGN(4) {
        *(.startup.literal)   /* startup literals FIRST */
        *(.startup.text)      /* startup code right after its literals */
        *(.literal .literal.*)
        *(.vectors.literal)
        *(.vectors)
        *(.text .text.*)
    } > iram

    /* .data: VMA in DRAM, LMA also in DRAM (no ROM copy needed here) */
    .data : ALIGN(4) {
        _data_start = .;
        *(.data .data.*)
        *(.rodata .rodata.*)
        _data_end = .;
    } > dram

    /* .bss: zero-init, VMA in DRAM, no LMA needed */
    .bss : ALIGN(4) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    } > dram

    /* Stack: reserve 32K above .bss */
    .stack : ALIGN(16) {
        _stack_bottom = .;
        . = . + 32K;
        _stack_top = .;
    } > dram
}