#ifndef CP32_MM_EXEC_H
#define CP32_MM_EXEC_H
#include <stdint.h>
#ifndef CP32_IRAM_EXT
#ifdef __XTENSA__
#define CP32_IRAM_EXT __attribute__((section(".iram_ext.text")))
#else
#define CP32_IRAM_EXT
#endif
#endif
#define CP32_EXEC_STACK_MAX 4096U
#define CP32_EXEC_VECTOR_MAX 32U
/* Trusted MM-owned vectors only: caller validates/copies user pointers first.
 * Output is argc, argv[], NULL, envp[], NULL, then NUL-terminated strings.
 * The future crt0 reads this at SP; this is not a native C function frame.
 * No overlap between input strings/vectors, output buffer and *sp is allowed.
 * capacity/base describe the staging buffer's eventual target stack region.
 * Success: 0. Invalid input: -1. Too large: -2. Failure leaves output intact.
 */
CP32_IRAM_EXT int cp32_exec_stack(unsigned char *buffer, unsigned capacity,
    uint32_t base, unsigned argc, const char *const argv[],
    unsigned envc, const char *const envp[], uint32_t *sp);
#endif
