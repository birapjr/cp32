#include <stdint.h>

/* ── delay ────────────────────────────────────────────────────────────────────
 * Busy-wait loop calibrated for 240 MHz with -O0.
 * Each iteration is ~7 CPU cycles, so delay(200000) ≈ 5.8 ms. */
void delay(volatile uint32_t n) { while (n--); }
