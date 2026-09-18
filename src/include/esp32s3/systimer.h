#ifndef _ESP32S3_SYSTIMER_H
#define _ESP32S3_SYSTIMER_H

#include <stdint.h>

/*===========================================================================*
 * ESP32-S3 SYSTIMER base address
 * TRM section 11.6 — Register Summary
 *===========================================================================*/
#define SYSTIMER_BASE           0x60023000UL

/* CORE0 interrupt-matrix register for SYSTIMER TARGET0. */
#define INTERRUPT_CORE0_BASE                         0x600C2000UL
#define INTERRUPT_CORE0_SYSTIMER_TARGET0_INT_MAP_REG (INTERRUPT_CORE0_BASE + 0x0E4)
#define CP32_SYSTIMER_CPU_INT                        2u

/*===========================================================================*
 * Raw register offsets (ESP32-S3 TRM v1.8, section 11.6)
 * https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
 *===========================================================================*/
#define SYSTIMER_CONF_REG           (SYSTIMER_BASE + 0x000)
#define SYSTIMER_UNIT0_OP_REG       (SYSTIMER_BASE + 0x004)
#define SYSTIMER_UNIT1_OP_REG       (SYSTIMER_BASE + 0x008)
#define SYSTIMER_UNIT0_LOAD_HI_REG  (SYSTIMER_BASE + 0x00C)
#define SYSTIMER_UNIT0_LOAD_LO_REG  (SYSTIMER_BASE + 0x010)
#define SYSTIMER_UNIT1_LOAD_HI_REG  (SYSTIMER_BASE + 0x014)
#define SYSTIMER_UNIT1_LOAD_LO_REG  (SYSTIMER_BASE + 0x018)
#define SYSTIMER_TARGET0_HI_REG     (SYSTIMER_BASE + 0x01C)
#define SYSTIMER_TARGET0_LO_REG     (SYSTIMER_BASE + 0x020)
#define SYSTIMER_TARGET1_HI_REG     (SYSTIMER_BASE + 0x024)
#define SYSTIMER_TARGET1_LO_REG     (SYSTIMER_BASE + 0x028)
#define SYSTIMER_TARGET2_HI_REG     (SYSTIMER_BASE + 0x02C)
#define SYSTIMER_TARGET2_LO_REG     (SYSTIMER_BASE + 0x030)
#define SYSTIMER_TARGET0_CONF_REG   (SYSTIMER_BASE + 0x034)
#define SYSTIMER_TARGET1_CONF_REG   (SYSTIMER_BASE + 0x038)
#define SYSTIMER_TARGET2_CONF_REG   (SYSTIMER_BASE + 0x03C)
#define SYSTIMER_UNIT0_VALUE_HI_REG (SYSTIMER_BASE + 0x040)
#define SYSTIMER_UNIT0_VALUE_LO_REG (SYSTIMER_BASE + 0x044)
#define SYSTIMER_UNIT1_VALUE_HI_REG (SYSTIMER_BASE + 0x048)
#define SYSTIMER_UNIT1_VALUE_LO_REG (SYSTIMER_BASE + 0x04C)
#define SYSTIMER_COMP0_LOAD_REG     (SYSTIMER_BASE + 0x050)
#define SYSTIMER_COMP1_LOAD_REG     (SYSTIMER_BASE + 0x054)
#define SYSTIMER_COMP2_LOAD_REG     (SYSTIMER_BASE + 0x058)
#define SYSTIMER_UNIT0_LOAD_REG     (SYSTIMER_BASE + 0x05C)
#define SYSTIMER_UNIT1_LOAD_REG     (SYSTIMER_BASE + 0x060)
#define SYSTIMER_INT_ENA_REG        (SYSTIMER_BASE + 0x064)
#define SYSTIMER_INT_RAW_REG        (SYSTIMER_BASE + 0x068)
#define SYSTIMER_INT_CLR_REG        (SYSTIMER_BASE + 0x06C)
#define SYSTIMER_INT_ST_REG         (SYSTIMER_BASE + 0x070)
#define SYSTIMER_REAL_TARGET0_LO_REG (SYSTIMER_BASE + 0x074)
#define SYSTIMER_REAL_TARGET0_HI_REG (SYSTIMER_BASE + 0x078)

/*===========================================================================*
 * SYSTIMER_CONF_REG bits
 *===========================================================================*/
#define SYSTIMER_CLK_EN             (1UL << 31)
#define SYSTIMER_TIMER_UNIT0_WORK_EN (1UL << 30)
#define SYSTIMER_TIMER_UNIT1_WORK_EN (1UL << 29)
#define SYSTIMER_TARGET0_WORK_EN     (1UL << 24)

/* UNIT0_OP (+0x004): VALUE_VALID is write-one-to-clear, UPDATE is a trigger. */
#define SYSTIMER_UNIT0_VALUE_VALID   (1UL << 29)
#define SYSTIMER_UNIT0_UPDATE        (1UL << 30)
#define SYSTIMER_COUNTER_HI_MASK     0x000FFFFFUL
#define SYSTIMER_COUNTER_MASK        UINT64_C(0x000FFFFFFFFFFFFF)

/*===========================================================================*
 * SYSTIMER_TARGET_CONF_REG bits (TARGET0/1/2 share same layout)
 *===========================================================================*/
#define SYSTIMER_TARGET_TIMER_UNIT_SEL  (1UL << 31) /* 0=UNIT0, 1=UNIT1   */
#define SYSTIMER_TARGET_PERIOD_MODE     (1UL << 30) /* 1=periodic, 0=alarm */
#define SYSTIMER_TARGET_PERIOD_SHIFT    0
/* TARGET0_CONF (+0x034), period is bits 25:0; bits 29:26 are reserved. */
#define SYSTIMER_TARGET_PERIOD_MASK     0x03FFFFFFUL

/*===========================================================================*
 * SYSTIMER_INT_ENA / INT_RAW / INT_CLR / INT_ST bits
 *===========================================================================*/
#define SYSTIMER_TARGET0_INT_BIT    (1 << 0)
#define SYSTIMER_TARGET1_INT_BIT    (1 << 1)
#define SYSTIMER_TARGET2_INT_BIT    (1 << 2)

/*===========================================================================*
 * Clock tick configuration
 * SYSTIMER runs at 16MHz on ESP32-S3 (TRM section 11.2)
 * For a 60 Hz Minix tick: 16,000,000 / 60 = 266,666 ticks
 *===========================================================================*/
#define SYSTIMER_CLK_HZ             16000000UL
#define MINIX_CLOCK_HZ              60UL
#define SYSTIMER_TICKS_PER_CLOCK    (SYSTIMER_CLK_HZ / MINIX_CLOCK_HZ)  /* 266666 */

/*===========================================================================*
 * Register access macros (bare metal — memory-mapped I/O)
 *===========================================================================*/
#ifndef REG_READ
#define REG_READ(reg)           (*(volatile uint32_t *)(reg))
#endif
#ifndef REG_WRITE
#define REG_WRITE(reg, val)     (*(volatile uint32_t *)(reg) = (val))
#endif
#define REG_SET_BIT(reg, bit)   REG_WRITE(reg, REG_READ(reg) | (bit))
#define REG_CLR_BIT(reg, bit)   REG_WRITE(reg, REG_READ(reg) & ~(bit))

/*===========================================================================*
 * Inline helpers
 *===========================================================================*/

/* Read the 52-bit UNIT0 counter safely (TRM: latch before reading) */
static inline uint64_t systimer_unit0_read(void)
{
    uint32_t lo, hi, next_lo;

    /* Discard the previous VALID indication before requesting a fresh latch.
     * Otherwise polling can accept the preceding sample while UPDATE crosses
     * from the APB register clock into the counter clock domain. */
    REG_WRITE(SYSTIMER_UNIT0_OP_REG,
              SYSTIMER_UNIT0_VALUE_VALID | SYSTIMER_UNIT0_UPDATE);

    while (!(REG_READ(SYSTIMER_UNIT0_OP_REG) & SYSTIMER_UNIT0_VALUE_VALID))
        ;

    /* An IRQ can latch UNIT0 again between reads in task context. Retry if
     * the low word changed, matching Espressif's systimer_hal_get_counter_value:
     * https://github.com/espressif/esp-idf/blob/v5.5.3/components/hal/systimer_hal.c
     */
    next_lo = REG_READ(SYSTIMER_UNIT0_VALUE_LO_REG);
    do {
        lo = next_lo;
        hi = REG_READ(SYSTIMER_UNIT0_VALUE_HI_REG) & SYSTIMER_COUNTER_HI_MASK;
        next_lo = REG_READ(SYSTIMER_UNIT0_VALUE_LO_REG);
    } while (lo != next_lo);
    return ((uint64_t)hi << 32) | lo;
}

/* Set TARGET0 alarm value */
static inline void systimer_set_target0(uint64_t ticks)
{
    REG_WRITE(SYSTIMER_TARGET0_HI_REG,
              (uint32_t)(ticks >> 32) & SYSTIMER_COUNTER_HI_MASK);
    REG_WRITE(SYSTIMER_TARGET0_LO_REG, (uint32_t)(ticks));
    /* Load the written value into the comparator */
    REG_WRITE(SYSTIMER_COMP0_LOAD_REG, 1);
}

/* Read the actual comparator value, which can lag the programming registers
 * while COMP0_LOAD crosses clock domains. Call with TARGET0 in one-shot mode;
 * the returned value is a coherent observation, not a load-completion flag. */
static inline uint64_t systimer_target0_read(void)
{
    uint32_t lo, hi, next_lo;
    next_lo = REG_READ(SYSTIMER_REAL_TARGET0_LO_REG);
    do {
        lo = next_lo;
        hi = REG_READ(SYSTIMER_REAL_TARGET0_HI_REG) & SYSTIMER_COUNTER_HI_MASK;
        next_lo = REG_READ(SYSTIMER_REAL_TARGET0_LO_REG);
    } while (lo != next_lo);
    return ((uint64_t)hi << 32) | lo;
}

/* Rearm the configured UNIT0 one-shot alarm for the next MINIX tick. Keep
 * peripheral acknowledgment and rearming together in the device handler.
 * Disable/load/enable follows Espressif's systimer_hal_set_alarm_target(). */
static inline uint64_t systimer_target0_rearm(void)
{
    uint64_t next;
    REG_CLR_BIT(SYSTIMER_CONF_REG, SYSTIMER_TARGET0_WORK_EN);
    REG_WRITE(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_BIT);
    next = (systimer_unit0_read() + SYSTIMER_TICKS_PER_CLOCK) &
           SYSTIMER_COUNTER_MASK;
    systimer_set_target0(next);
    REG_SET_BIT(SYSTIMER_CONF_REG, SYSTIMER_TARGET0_WORK_EN);
    REG_SET_BIT(SYSTIMER_INT_ENA_REG, SYSTIMER_TARGET0_INT_BIT);
    return next;
}

/* Enable TARGET0 in alarm (one-shot) mode tied to UNIT0 */
static inline void systimer_enable_target0_alarm(void)
{
    /* bit31=0 → UNIT0, bit30=0 → alarm mode */
    REG_WRITE(SYSTIMER_TARGET0_CONF_REG, 0);
}

/* Enable TARGET0 in periodic mode */
static inline void systimer_enable_target0_periodic(uint32_t period_ticks)
{
    REG_WRITE(SYSTIMER_TARGET0_CONF_REG,
              SYSTIMER_TARGET_PERIOD_MODE |
              (period_ticks & SYSTIMER_TARGET_PERIOD_MASK));
}

#endif /* _ESP32S3_SYSTIMER_H */
