#!/usr/bin/env python3
"""Run all of production serial.c with simulated GPIO, USB and watchdog MMIO."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
registers = [
    'USBJ_EP1', 'USBJ_INT_RAW', 'USBJ_INT_CLR',
    'CP32_GPIO_ENABLE_W1TC', 'CP32_GPIO_IN', 'CP32_GO_IO_MUX',
    'TIMG0_WDTCONFIG0', 'TIMG1_WDTCONFIG0', 'RTC_WDTCONFIG0',
    'RTC_WDTWPROTECT', 'RTC_WDTFEED', 'RTC_SWD_CONF', 'RTC_SWD_WPROTECT',
    'TIMG0_WDTWPROTECT', 'TIMG0_WDTFEED', 'TIMG1_WDTWPROTECT', 'TIMG1_WDTFEED',
]
header = r'''
#include <stdint.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "esp32s3/const.h"
#define BASE_PRINT_WIDTH 30
static void delay(unsigned count);
static void printk(const char *format, ...) { (void)format; }
static unsigned fifo_reads;
static uint32_t fifo_conf;
static uint32_t *fifo_register(void) { ++fifo_reads; return &fifo_conf; }
#undef USBJ_EP1_CONF
#define USBJ_EP1_CONF (*fifo_register())
'''
for reg in registers:
    header += f'\n#undef {reg}\nstatic uint32_t mock_{reg};\n#define {reg} mock_{reg}\n'

test = r'''
#include "serial.c"
static unsigned steps, scenario, resets;
static jmp_buf reset_target;

void cp32_hardware_reset(void) {
    ++resets;
    longjmp(reset_target, 1);
}

static void delay(unsigned count) {
    assert(count == 100000);
    /* Model W1C before the next hardware sample. */
    USBJ_INT_RAW &= ~USBJ_INT_CLR;
    USBJ_INT_CLR = 0;
    ++steps;
    assert(steps < 100); /* Fail a gate which never accepts either input. */
    assert(RTC_WDTCONFIG0 == 0 && RTC_WDTFEED == 1);
    assert(RTC_WDTWPROTECT == WDT_LOCK_KEY);
    assert(RTC_SWD_CONF == (SWD_DISABLE_BIT | SWD_FEED_BIT));
    assert(RTC_SWD_WPROTECT == 0);
    assert(TIMG0_WDTFEED == 1 && TIMG1_WDTFEED == 1);
    assert(TIMG0_WDTWPROTECT == WDT_LOCK_KEY);
    assert(TIMG1_WDTWPROTECT == WDT_LOCK_KEY);
    if (scenario == 1) {
        /* Empty FIFO and USB SOF/enumeration do not mean a reader exists. */
        USBJ_INT_RAW |= 1u << 1;
        if (steps == 12) USBJ_INT_RAW |= USBJ_IN_TOKEN_REC;
    } else if (scenario == 2) {
        /* Two short bounces, followed by three consecutive low samples. */
        CP32_GPIO_IN = (steps == 1 || steps == 3 || steps >= 6) ? 0 : CP32_GO_MASK;
    } else if (scenario == 3) {
        CP32_GPIO_IN = 0;
        if (steps == 3) USBJ_INT_RAW |= USBJ_IN_TOKEN_REC;
    }
}

static void setup(unsigned which) {
    scenario = which;
    steps = resets = fifo_reads = 0;
    console_standalone = usbj_stalled = 0;
    USBJ_EP1 = 0xdead;
    fifo_conf = USBJ_IN_EP_DATA_FREE;
    /* A stale loader IN token must not choose development mode. */
    USBJ_INT_RAW = USBJ_IN_TOKEN_REC;
    USBJ_INT_CLR = 0;
    CP32_GPIO_IN = CP32_GO_MASK;
    CP32_GO_IO_MUX = 0xa5a5;
    CP32_GPIO_ENABLE_W1TC = 0;
}

int main(void) {
    setup(1);
    startup_usb_conn();
    assert(steps == 12 && !console_standalone);
    assert(CP32_GPIO_ENABLE_W1TC == CP32_GO_MASK);
    assert(CP32_GO_IO_MUX == ((0xa5a5 & ~(CP32_IO_MUX_PD | CP32_IO_MUX_FUNC_MASK)) |
        CP32_IO_MUX_PU | CP32_IO_MUX_IE | CP32_IO_MUX_GPIO));
    usbj_print("A");
    assert(USBJ_EP1 == 'A' && (fifo_conf & USBJ_WR_DONE));
    cp32_console_poll();
    assert(!resets); /* No reset loop in development mode. */

    /* Host disappears: pay one bounded wait, then drop subsequent bytes. */
    fifo_conf = 0;
    fifo_reads = 0;
    usbj_print("x");
    assert(fifo_reads == 200000 && usbj_stalled && USBJ_EP1 == 'A');
    fifo_reads = 0;
    usbj_print("not connected");
    assert(fifo_reads == strlen("not connected") && USBJ_EP1 == 'A');
    fifo_conf = USBJ_IN_EP_DATA_FREE;
    usbj_print("R");
    assert(USBJ_EP1 == 'R' && !usbj_stalled);

    setup(2);
    startup_usb_conn();
    assert(steps == 8 && console_standalone);
    assert(!fifo_reads); /* No FIFO writes or waits during standalone boot. */
    fifo_conf = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        usbj_print("IRQ and shell diagnostics\r\n");
        usbj_print_u32(i);
        usbj_print_hex32(i);
        cp32_console_poll();
    }
    assert(!fifo_reads && USBJ_EP1 == 0xdead && !resets);
    /* A charge cable/no reader does not restart a running shell. */
    USBJ_INT_RAW = 1u << 1;
    CP32_GPIO_IN = CP32_GO_MASK;
    cp32_console_poll();
    assert(!resets);
    USBJ_INT_RAW |= USBJ_IN_TOKEN_REC;
    CP32_GPIO_IN = 0;
    cp32_console_poll();
    assert(!resets); /* G0 must be released before a system reset. */
    CP32_GPIO_IN = CP32_GO_MASK;
    if (setjmp(reset_target) == 0) {
        cp32_console_poll();
        assert(0 && "new USB reader must request a full reset");
    }
    assert(resets == 1);

    setup(3);
    startup_usb_conn();
    assert(steps == 3 && !console_standalone); /* USB wins a simultaneous press. */
    cp32_console_poll();
    assert(!resets);
    puts("console boot, debounce, disconnected output and USB restart: PASS");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='cp32-console-') as tmp:
    work = Path(tmp)
    # Copy verbatim so the quoted kernel.h include resolves to the MMIO shim.
    (work / 'serial.c').write_text((ROOT / 'src/kernel/serial.c').read_text())
    (work / 'kernel.h').write_text(header)
    (work / 'test.c').write_text(test)
    executable = work / 'test'
    subprocess.run(['cc', '-std=c99', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-idirafter', str(ROOT / 'src/include'), str(work / 'test.c'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)
