ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
MEMORY {
 text (rx) : ORIGIN = 0x403d8000, LENGTH = 16K
 data (rw) : ORIGIN = 0x3fcec000, LENGTH = 12K
}
SECTIONS {
 .text : { *(.literal .literal.*) *(.text.start) *(.text .text.*) } > text :text
 .data : { *(.rodata .rodata.*) *(.data .data.*) *(.sdata .sdata.*) } > data :data
 .bss (NOLOAD) : ALIGN(4) { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } > data :data
 /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
 ASSERT(ADDR(.bss)+SIZEOF(.bss)<=0x3fcef000,"Application data overlaps stack")
}
