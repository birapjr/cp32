#ifndef _ESP32S3_SYSTIMER_H
#define _ESP32S3_SYSTIMER_H

#include <stdint.h>

/*===========================================================================*
 * ESP32-S3 SYSTIMER base address
 * TRM section 11.5 — Register Summary
 *===========================================================================*/
#define SYSTIMER_BASE           0x60023000UL

/* CORE0 interrupt-matrix register for SYSTIMER TARGET0. */
#define INTERRUPT_CORE0_BASE                         0x600C2000UL
#define INTERRUPT_CORE0_SYSTIMER_TARGET0_INT_MAP_REG (INTERRUPT_CORE0_BASE + 0x0E4)
#define CP32_SYSTIMER_CPU_INT                        2u

/*===========================================================================*
 * Raw register offsets (TRM Table 11-2)
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

/*===========================================================================*
 * SYSTIMER_CONF_REG bits
 *===========================================================================*/
#define SYSTIMER_CLK_EN             (1 << 31)
#define SYSTIMER_TIMER_UNIT0_WORK_EN (1 << 30)
#define SYSTIMER_TIMER_UNIT1_WORK_EN (1 << 29)

/*===========================================================================*
 * SYSTIMER_TARGET_CONF_REG bits (TARGET0/1/2 share same layout)
 *===========================================================================*/
#define SYSTIMER_TARGET_TIMER_UNIT_SEL  (1 << 31)  /* 0=UNIT0, 1=UNIT1    */
#define SYSTIMER_TARGET_PERIOD_MODE     (1 << 30)  /* 1=periodic, 0=alarm  */
#define SYSTIMER_TARGET_PERIOD_SHIFT    0
#define SYSTIMER_TARGET_PERIOD_MASK     0x3FFFFFFFUL

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
#define REG_READ(reg)           (*(volatile uint32_t *)(reg))
#define REG_WRITE(reg, val)     (*(volatile uint32_t *)(reg) = (val))
#define REG_SET_BIT(reg, bit)   REG_WRITE(reg, REG_READ(reg) | (bit))
#define REG_CLR_BIT(reg, bit)   REG_WRITE(reg, REG_READ(reg) & ~(bit))

/*===========================================================================*
 * Inline helpers
 *===========================================================================*/

/* Read the 52-bit UNIT0 counter safely (TRM: latch before reading) */
static inline uint64_t systimer_unit0_read(void)
{
    /* Writing any value to UNIT0_OP latches the counter into VALUE regs */
    REG_WRITE(SYSTIMER_UNIT0_OP_REG, 1 << 30);

    /* Wait for latch to complete (bit 29 = UPDATE done) */
    while (!(REG_READ(SYSTIMER_UNIT0_OP_REG) & (1 << 29)))
        ;

    uint64_t hi = REG_READ(SYSTIMER_UNIT0_VALUE_HI_REG) & 0xFFFFF;
    uint64_t lo = REG_READ(SYSTIMER_UNIT0_VALUE_LO_REG);
    return (hi << 32) | lo;
}

/* Set TARGET0 alarm value */
static inline void systimer_set_target0(uint64_t ticks)
{
    REG_WRITE(SYSTIMER_TARGET0_HI_REG, (uint32_t)(ticks >> 32) & 0xFFFFF);
    REG_WRITE(SYSTIMER_TARGET0_LO_REG, (uint32_t)(ticks));
    /* Load the written value into the comparator */
    REG_WRITE(SYSTIMER_COMP0_LOAD_REG, 1);
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
