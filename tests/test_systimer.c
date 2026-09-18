#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t model_read(uintptr_t reg);
static void model_write(uintptr_t reg, uint32_t value);
#define REG_READ(reg) model_read(reg)
#define REG_WRITE(reg, value) model_write(reg, value)
#include "../src/include/esp32s3/systimer.h"

/* Model the counter/APB crossing, W1C interrupt flags and one-shot comparator.
 * This verifies software behavior; it does not emulate CPU interrupt delivery. */
static struct {
    uint32_t conf, target_conf, int_ena, int_raw;
    uint32_t target_hi, target_lo;
    uint64_t counter, snapshot, real_target;
    uint64_t relatch_value, target_reload_value;
    unsigned valid, latch_pending, latch_delay, polls, updates, clears, loads;
    unsigned armed, relatch_after_hi, reload_after_hi, value_reads;
} hw;

static void reset_model(void)
{
    memset(&hw, 0, sizeof(hw));
    hw.conf = SYSTIMER_CLK_EN | SYSTIMER_TIMER_UNIT0_WORK_EN |
              SYSTIMER_TIMER_UNIT1_WORK_EN | (1UL << 23) | (1UL << 27);
    hw.int_ena = SYSTIMER_TARGET1_INT_BIT | SYSTIMER_TARGET2_INT_BIT;
    hw.latch_delay = 3;
}

static uint32_t model_read(uintptr_t reg)
{
    uint32_t hi;
    switch (reg) {
    case SYSTIMER_CONF_REG: return hw.conf;
    case SYSTIMER_TARGET0_CONF_REG: return hw.target_conf;
    case SYSTIMER_INT_ENA_REG: return hw.int_ena;
    case SYSTIMER_INT_RAW_REG: return hw.int_raw;
    case SYSTIMER_INT_ST_REG: return hw.int_raw & hw.int_ena;
    case SYSTIMER_UNIT0_OP_REG:
        assert(++hw.polls < 1000);
        if (hw.latch_pending && --hw.latch_pending == 0) {
            hw.snapshot = hw.counter;
            hw.valid = 1;
        }
        return hw.valid ? SYSTIMER_UNIT0_VALUE_VALID : 0;
    case SYSTIMER_UNIT0_VALUE_LO_REG:
        ++hw.value_reads;
        return (uint32_t)hw.snapshot;
    case SYSTIMER_UNIT0_VALUE_HI_REG:
        hi = (uint32_t)(hw.snapshot >> 32);
        if (hw.relatch_after_hi) {
            /* Simulate a complete IRQ-owned snapshot between the task's
             * high and low reads, including rollover of the low word. */
            hw.snapshot = hw.relatch_value;
            hw.relatch_after_hi = 0;
        }
        return hi;
    case SYSTIMER_REAL_TARGET0_LO_REG: return (uint32_t)hw.real_target;
    case SYSTIMER_REAL_TARGET0_HI_REG:
        hi = (uint32_t)(hw.real_target >> 32);
        if (hw.reload_after_hi) {
            hw.real_target = hw.target_reload_value;
            hw.reload_after_hi = 0;
        }
        return hi;
    default: assert(!"unexpected MMIO read"); return 0;
    }
}

static void model_write(uintptr_t reg, uint32_t value)
{
    switch (reg) {
    case SYSTIMER_CONF_REG:
        if (!(value & SYSTIMER_TARGET0_WORK_EN)) hw.armed = 0;
        else if (!(hw.conf & SYSTIMER_TARGET0_WORK_EN)) hw.armed = 1;
        hw.conf = value;
        break;
    case SYSTIMER_TARGET0_CONF_REG: hw.target_conf = value; break;
    case SYSTIMER_INT_ENA_REG: hw.int_ena = value; break;
    case SYSTIMER_INT_CLR_REG:
        hw.int_raw &= ~value;
        ++hw.clears;
        break;
    case SYSTIMER_UNIT0_OP_REG:
        if (value & SYSTIMER_UNIT0_VALUE_VALID) hw.valid = 0;
        if (value & SYSTIMER_UNIT0_UPDATE) {
            assert(hw.conf & SYSTIMER_TIMER_UNIT0_WORK_EN);
            hw.latch_pending = hw.latch_delay;
            ++hw.updates;
        }
        break;
    case SYSTIMER_TARGET0_HI_REG:
        assert(!(hw.conf & SYSTIMER_TARGET0_WORK_EN));
        assert(!(value & ~SYSTIMER_COUNTER_HI_MASK));
        hw.target_hi = value;
        break;
    case SYSTIMER_TARGET0_LO_REG:
        assert(!(hw.conf & SYSTIMER_TARGET0_WORK_EN));
        hw.target_lo = value;
        break;
    case SYSTIMER_COMP0_LOAD_REG:
        assert(value == 1);
        assert(!(hw.conf & SYSTIMER_TARGET0_WORK_EN));
        hw.real_target = ((uint64_t)hw.target_hi << 32) | hw.target_lo;
        ++hw.loads;
        break;
    default: assert(!"unexpected MMIO write"); break;
    }
}

static void advance_counter(uint64_t delta)
{
    uint64_t distance = (hw.real_target - hw.counter) & SYSTIMER_COUNTER_MASK;
    hw.counter = (hw.counter + delta) & SYSTIMER_COUNTER_MASK;
    if (hw.armed && distance <= delta) {
        hw.int_raw |= SYSTIMER_TARGET0_INT_BIT;
        hw.armed = 0; /* An acknowledged one-shot needs a fresh arm. */
    }
}

static void test_fresh_latch(void)
{
    reset_model();
    hw.counter = UINT64_C(0x123456789ABCD);
    hw.snapshot = 77;
    hw.valid = 1; /* Previous snapshot remains valid until software clears it. */
    assert(systimer_unit0_read() == hw.counter);
    assert(hw.polls == hw.latch_delay);
    assert(hw.updates == 1);
    hw.counter += 1234;
    assert(systimer_unit0_read() == hw.counter);
    assert(hw.polls == 2 * hw.latch_delay);
}

static void test_interrupted_snapshot(void)
{
    reset_model();
    hw.counter = UINT64_C(0x00000000FFFFFFFF);
    hw.relatch_value = UINT64_C(0x0000000100000000);
    hw.relatch_after_hi = 1;
    assert(systimer_unit0_read() == hw.relatch_value);
    assert(hw.value_reads == 3); /* First pair is torn; the retry is coherent. */
}

static void test_consecutive_rearms(void)
{
    uint32_t preserved_conf;
    uint64_t next;
    unsigned i;
    reset_model();
    preserved_conf = hw.conf;
    hw.counter = UINT64_C(0x100000010);
    hw.snapshot = 5;
    hw.valid = 1;
    hw.int_raw = SYSTIMER_TARGET0_INT_BIT | SYSTIMER_TARGET1_INT_BIT;
    hw.conf |= SYSTIMER_TARGET0_WORK_EN;
    for (i = 0; i < 4; ++i) {
        uint64_t now = hw.counter;
        next = systimer_target0_rearm();
        assert(next == now + SYSTIMER_TICKS_PER_CLOCK);
        assert(systimer_target0_read() == next);
        assert(hw.conf == (preserved_conf | SYSTIMER_TARGET0_WORK_EN));
        assert(hw.int_ena == (SYSTIMER_TARGET0_INT_BIT |
                             SYSTIMER_TARGET1_INT_BIT | SYSTIMER_TARGET2_INT_BIT));
        assert(hw.int_raw == SYSTIMER_TARGET1_INT_BIT);
        assert(hw.clears == i + 1 && hw.loads == i + 1);
        advance_counter(SYSTIMER_TICKS_PER_CLOCK - 1);
        assert(!(model_read(SYSTIMER_INT_ST_REG) & SYSTIMER_TARGET0_INT_BIT));
        advance_counter(1);
        assert(model_read(SYSTIMER_INT_ST_REG) & SYSTIMER_TARGET0_INT_BIT);
    }
}

static void test_counter_wrap(void)
{
    uint64_t next;
    reset_model();
    hw.counter = SYSTIMER_COUNTER_MASK - 10;
    next = systimer_target0_rearm();
    assert(next == SYSTIMER_TICKS_PER_CLOCK - 11);
    assert(systimer_target0_read() == next);
    advance_counter(SYSTIMER_TICKS_PER_CLOCK);
    assert(hw.int_raw & SYSTIMER_TARGET0_INT_BIT);
    assert(systimer_target0_rearm() == next + SYSTIMER_TICKS_PER_CLOCK);
}

static void test_target_read_during_reload(void)
{
    reset_model();
    hw.real_target = UINT64_C(0x00000000FFFFFFFF);
    hw.target_reload_value = UINT64_C(0x0000000100000010);
    hw.reload_after_hi = 1;
    assert(systimer_target0_read() == hw.target_reload_value);
}

static void test_field_widths(void)
{
    reset_model();
    systimer_set_target0(UINT64_MAX);
    assert(hw.real_target == SYSTIMER_COUNTER_MASK);
    systimer_enable_target0_periodic(UINT32_MAX);
    assert(hw.target_conf == UINT32_C(0x43FFFFFF));
    systimer_enable_target0_alarm();
    assert(hw.target_conf == 0);
}

int main(void)
{
    test_fresh_latch();
    test_interrupted_snapshot();
    test_consecutive_rearms();
    test_counter_wrap();
    test_target_read_during_reload();
    test_field_widths();
    return 0;
}
