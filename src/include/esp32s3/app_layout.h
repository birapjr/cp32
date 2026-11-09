#ifndef CP32_APP_LAYOUT_H
#define CP32_APP_LAYOUT_H
/* Fixed internal SRAM application slot, usable from assembly and C.
 * Instruction/data alias offset0x6f0000; last4KiB is the app stack. */
#define CP32_APP_TEXT 0x403d8000
#define CP32_APP_TEXT_END 0x403dc000
#define CP32_APP_DATA 0x3fcec000
#define CP32_APP_STACK 0x3fcef000
#define CP32_APP_TOP 0x3fcf0000
#endif
