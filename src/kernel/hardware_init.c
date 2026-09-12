/*
 * Early ESP32-S3 hardware setup.
 *
 * This runs after the IRAM stack and .bss are available, but before start()
 * enters the kernel proper.  The kernel image is loaded entirely into the
 * contiguous instruction SRAM window, so no flash cache or MMU setup is
 * required here.
 */
#include "kernel.h"

void cp32_hardware_init(void)
{
    /* Match the first part of the ESP-IDF reset path: leave the CPU with no
     * pending or enabled interrupts while peripheral/cache state is staged. */
    __asm__ volatile (
        "movi a8, 0\n"
        "wsr  a8, interrupt\n"
        "wsr  a8, intenable\n"
        "movi a8, -1\n"
        "wsr  a8, intclear\n"
        "rsync\n"
        ::: "a8", "memory");

}
