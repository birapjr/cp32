/* ============================================================
   CP32 Kernel — M5Stack Cardputer Adv (ESP32-S3FN8)
   Everything in internal SRAM for maximum speed.
   Code has LMA in flash so esptool can write it,
   startup copies it to IRAM before jumping to main.
   ============================================================ */

MEMORY {
    /* Flash: LMA home for all IRAM sections               */
    irom  (RX) : ORIGIN = 0x42010000, LENGTH = 7936K

    /* SRAM0: instruction bus only, 320 KB                 */
    iram  (RX) : ORIGIN = 0x40370000, LENGTH = 320K

    /* SRAM1: data bus, 480 KB                             */
    dram  (RW) : ORIGIN = 0x3FC88000, LENGTH = 480K
}

SECTIONS {

    /* --------------------------------------------------
       VECTOR TABLE — first in IRAM, 1 KB aligned
       VMA = IRAM (fast), LMA = flash (esptool writes it)
       startup copies flash → IRAM, then sets VECBASE
    -------------------------------------------------- */
    .vectors : ALIGN(1024) {
        _vectors_start = .;
        KEEP(*(.vectors))
        *(.vectors.literal)
        . = ALIGN(4);
        _vectors_end = .;
    } > iram AT > irom

    /* --------------------------------------------------
       STARTUP — entry point, must run from IRAM
       Copies IRAM image and .data, zeroes .bss
       Literal pool before code (L32R reach rule)
    -------------------------------------------------- */
    .startup : ALIGN(4) {
        KEEP(*(.startup.literal))
        KEEP(*(.startup.text))
    } > iram AT > irom

    /* --------------------------------------------------
       KERNEL + ALL OTHER CODE — all in IRAM
       Literals must precede the code that uses them
    -------------------------------------------------- */
    .text : ALIGN(4) {
        _text_start = .;
        *(.literal .literal.*)
        *(.text .text.*)
        . = ALIGN(4);
        _text_end = .;
    } > iram AT > irom

    /* Mark end of IRAM image (used by startup copy loop) */
    _iram_end = .;

    /* LMA symbols for startup copy routine */
    _iram_lma = LOADADDR(.vectors);   /* where flash image starts */
    _iram_vma = ORIGIN(iram);         /* where to copy it to      */

    /* --------------------------------------------------
       INITIALIZED DATA — VMA in DRAM, LMA in flash
       startup copies _data_lma → _data_start.._data_end
    -------------------------------------------------- */
    .data : ALIGN(4) {
        _data_start = .;
        *(.data  .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _data_end = .;
    } > dram AT > irom

    _data_lma = LOADADDR(.data);

    /* --------------------------------------------------
       READ-ONLY DATA — kept in DRAM for speed
       (avoids flash D-cache latency on kernel strings
       and lookup tables — costs DRAM, worth it here)
       Also needs LMA so esptool writes it to flash;
       startup copies it just like .data
    -------------------------------------------------- */
    .rodata : ALIGN(4) {
        _rodata_start = .;
        *(.rodata  .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        _rodata_end = .;
    } > dram AT > irom

    _rodata_lma = LOADADDR(.rodata);

    /* --------------------------------------------------
       BSS — zero-initialized, no flash storage needed
    -------------------------------------------------- */
    .bss : ALIGN(4) {
        _bss_start = .;
        *(.bss  .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > dram

    /* --------------------------------------------------
       HEAP — grows upward from _bss_end
    -------------------------------------------------- */
    .heap : ALIGN(8) {
        _heap_start = .;
        . = . + 128K;
        _heap_end = .;
    } > dram

    /* --------------------------------------------------
       STACK — 32 KB, grows downward
       a1 = _stack_top in startup
    -------------------------------------------------- */
    .stack : ALIGN(16) {
        _stack_bottom = .;
        . = . + 32K;
        _stack_top = .;
    } > dram

    /* --------------------------------------------------
       Discard C++ / ARM unwind tables
    -------------------------------------------------- */
    /DISCARD/ : {
        *(.eh_frame)       /* C++ unwind — GCC emits even for pure C */
        *(.eh_frame_hdr)
        *(.comment)        /* compiler version string */
        *(.note*)          /* build-id hash */
    }

    /* --------------------------------------------------
       Sanity assertions — caught at link time
    -------------------------------------------------- */
    ASSERT(_iram_end <= 0x403C0000,
        "ERROR: kernel code overflows IRAM (320 KB limit)")
    ASSERT(_stack_top <= 0x3FD00000,
        "ERROR: DRAM overflow — reduce heap or stack")
    ASSERT(_heap_end <= _stack_bottom,
        "ERROR: heap and stack overlap")
}