# CP32 Early-Boot Implementation Issues

This review covers the currently working execution path:

`CP32` in `mpx32.S` → `start()` in `start.c` → `main()` in `main.c` → process-table initialization → intentional infinite loop.

The reported hardware output proves that the image reaches C code reliably, watchdog configuration is cleared, the diagnostic console works, and `mem_init()` returns. It does not yet prove that startup relocation, interrupt handling, ABI preservation, or MINIX process initialization are correct.

## Resolved for the current image: startup relocation concern

`esp32s3.ld` places `.vectors`, `.startup`, and `.text` with a VMA in IRAM and an LMA in the `irom` region. It also gives `.data` and `.rodata` DRAM VMAs with flash LMAs.

The current `mpx32.S` does not perform flash-to-RAM copies. It only:

- disables CPU interrupt sources;
- initializes the stack pointer;
- zeros `.bss`;
- writes `VECBASE`;
- calls `start()` and `main()`.

The generated ESP image was inspected with `esptool image-info`. It contains two runtime segments:

- DRAM segment: length `0x684`, load address `0x3fc88000`, containing `.data` and `.rodata`.
- IRAM segment: length `0x4b84`, load address `0x40370000`, containing `.vectors`, `.startup`, and `.text`.

This confirms that the ESP32-S3 image loader places both runtime segments at their linked VMAs before entering `CP32`. The kernel reaches `start()`, prints strings, returns to `main()`, and completes `mem_init()` without startup relocation code. The earlier linker comments describing software copies were wrong and have been corrected.

The attempted fix added `l32i` loops using `_data_lma` and `_rodata_lma` as CPU data addresses. That produced serial garbage and was reverted. The flash LMA values in this custom image are not automatically valid load addresses for ordinary data reads on this runtime path. Do not reintroduce those loops without first proving the ESP32 image format, cache mapping, and segment-loader behavior.

The remaining issue is documentation and validation, not an immediate boot failure. Keep this loader-owned relocation contract unless the image format or boot flow is intentionally changed.

## Deferred: independent `.data`/`.rodata` initialization validation

The assembly initializes `.bss`, but there is no explicit copy from `_data_lma` to `_data_start` or from `_rodata_lma` to `_rodata_start`.

The current output shows that the strings used by the early console are functioning, and `esptool image-info` confirms `.data`/`.rodata` are in the DRAM runtime segment. A deliberately initialized global should still be used as a simple hardware regression check, but software LMA copy loops are not needed for this image.

Possible long-term fixes, in order of safety, are:

1. Keep the current loader-owned relocation model and keep the linker script comments aligned with it.
2. Use a linker/image layout supported by the ESP32-S3 bootloader so the loader explicitly loads RAM sections at their VMAs.
3. If software copying is truly required, place the copy routine in a region that is executable while copying, and access flash through a verified mapped alias—not by directly dereferencing the ELF LMA.

Do not copy the IRAM image from `CP32`: that code is already executing from the destination and would self-overwrite.

## Priority 1: vector relocation is performed before proving vector contents are resident

`mpx32.S` sets `VECBASE` to `_vectors_start` immediately after clearing `.bss`. If `.vectors` has not been copied or loaded into IRAM as expected, the first exception or interrupt can jump through invalid memory.

The current path does not trigger normal interrupts, so this can be hidden indefinitely. Validate the vector address and contents in the ELF and on hardware. Then deliberately test an exception path and a timer interrupt before enabling general scheduling.

## Priority 1: interrupt masking is incomplete and should be made architecture-specific

The startup code writes `interrupt = 0` and `intenable = 0`, clears `intclear`, and moves compare registers to `-1`. This may disable the CPU interrupt sources, but it is not a complete reset of peripheral interrupt state or interrupt-matrix routing.

Before enabling interrupts, define a clear policy for:

- Xtensa PS interrupt level;
- CPU `INTENABLE` and pending interrupt state;
- ESP32-S3 interrupt matrix routing;
- peripheral raw/enable/clear registers;
- timer-group and SYSTIMER interrupt state.

The `lock()`/`unlock()` routines use `rsil`, but their return-value and nesting semantics must be checked against every caller. A later MINIX port will need nested lock handling, not just a global enable/disable toggle.

## Priority 1: no exception handler or safe fault-reporting path is complete

`vectors.S` and `irq.S` contain TODOs for exception cause/address decoding and dispatch. A bad pointer, alignment problem, illegal instruction, or stack fault can therefore produce a reset or an unhelpful hang.

Implement a minimal exception handler that records `EXCCAUSE`, `EXCVADDR`, `EPC`, `PS`, and the current stack pointer through USB Serial/JTAG, then halts safely. This is especially important while validating relocation and context switching.

## Priority 1: `call0` ABI and stack-frame assumptions need a documented contract

The build uses `-mabi=call0`, and `CP32` calls C with `call0`. The assembly initializes `a1` and aligns it downward to 16 bytes, which is a reasonable starting point, but there is no explicit stack-frame reservation or ABI validation around the calls.

Check the generated disassembly for `start()` and `main()` and confirm:

- the stack remains inside `_stack_bottom.._stack_top`;
- every call0 callee preserves the registers required by the selected ABI;
- interrupt entry saves enough state before calling C;
- interrupt return restores the exact expected frame;
- no code relies on windowed-ABI conventions.

The current simple functions may work even if these assumptions are incomplete.

## Priority 2: watchdog handling is working but too dependent on repeated writes

The output shows all reported watchdog configuration registers at zero and the Super WDT configuration at `0xC0000000`, which is consistent with the current disable strategy. The raw offsets also agree with the register aliases used by the diagnostics.

Still verify the reset-reason decoding. `0x0000F041` is a raw register value, not automatically a single watchdog reason. The diagnostic should identify the exact reset-state register and mask/shift its documented fields before labeling a reset as power-on, watchdog, software, or USB/JTAG related.

Once startup is stable, reduce repeated watchdog disable/feed calls and isolate them to the documented boot requirement. Repeated writes can hide timing or write-protection mistakes and make later interrupt timing harder to reason about.

## Priority 2: `start()` leaves important boot state implicit

`start()` initializes `boot_parameters`, sets `processor` to the literal `32`, and returns. Potential issues:

- `k_environ` is zero-initialized but never populated, so `k_getenv()` always finds no variables.
- `bp_ramsize = 480 * 1024` describes the whole DRAM region, although the linker reserves space for `.bss`, heap, and stack. MINIX memory accounting must use the actual usable range, not the nominal hardware region.
- The processor value should be a named ESP32-S3 constant rather than a magic number.
- `start()` does not initialize interrupt routing, the clock, TTY state, process registers, or the kernel task table.
- The function has no explicit declaration visible in the inspected path; ensure the prototype and return type are consistent across headers and assembly.

## Priority 2: `main()` initializes only a fraction of the MINIX kernel state

The current `main()` correctly reaches `mem_init()` and clears process slots, but then stops permanently. Before continuing, it will need a deliberate initialization sequence for memory maps, task descriptors, ready queues, `proc_ptr`, `bill_ptr`, clock/TTY state, and the first runnable task.

The process-table loop should be checked against the exact values of `NR_TASKS` and `NR_PROCS`. It currently relies on the MINIX negative task-number indexing convention:

```c
(pproc_addr + NR_TASKS)[t] = rp;
```

That convention is valid only if the allocated array, index range, and `proc_addr`/`proc_number` macros all agree. Add assertions or diagnostics for the first and last mapped process numbers before enabling message passing.

Several local variables in `main()` are currently unused (`sizeindex`, segment sizes, `ktsb`, `memp`, and `ttp`). They are remnants of the original MINIX initialization path and should either be implemented or removed as each initialization stage is ported; leaving them in place makes it harder to see which state is actually established.

## Priority 2: `mem_init()` needs address-unit validation

The ESP32-S3 port uses 4 KiB logical clicks (`CLICK_SIZE = 4096`, `CLICK_SHIFT = 12`). `mem_init()` derives the usable range from `_heap_start` to `_stack_bottom`, which is appropriate for the linker-reserved free DRAM area.

Validate the following before using the result for process allocation:

- `_heap_start`, `_stack_bottom`, and `_stack_top` are in the intended DRAM address range;
- the subtraction is performed as an address difference and cannot underflow;
- both endpoints and the resulting size are click-aligned or deliberately rounded;
- `tot_mem_size` represents free usable memory, not total physical SRAM;
- the MINIX `mem[]` base convention matches the ESP32-S3 flat-address implementation in `system.c`.

The historical MINIX word “physical” does not mean that an ESP32-S3 click can be treated like an MMU page. The port currently stores click units in process structures while the hardware uses flat mapped addresses, so this boundary needs tests.

## Resolved for bring-up: execution liveness

`main()` now enters a watchdog-safe idle loop after process-table validation.
It feeds the watchdogs, reapplies the Super WDT disable, waits briefly, and
prints a dot. This confirms the CPU remains alive while the scheduler is not
yet implemented. It is not a scheduler and must be replaced by the first
controlled timer-driven idle/task path.

## Recommended order before continuing the port

1. Verify the initialized-memory sentinels on hardware.
2. Add assertions/diagnostics around click conversion and process-table indexing.
3. Initialize one timer interrupt and replace the temporary heartbeat loop with a controlled idle/task path before porting full scheduling/message passing.

## What the current hardware output establishes

The output establishes that the current image can:

- execute the early assembly entry;
- reach `start()`;
- access the watchdog and USB Serial/JTAG registers;
- print diagnostics;
- return from `start()` to `main()`;
- execute `mem_init()`;
- iterate through the process table without an immediately visible fault.

The sentinel test now establishes that the current image loader initializes the tested `.data` value and that startup clears the tested `.bss` value. It still does not establish that vectors, exceptions, interrupts, scheduler state, or MINIX message passing are correct.

## Current handoff status for the next session

The last tested state is the pre-relocation-loop version. It boots and prints:

```text
CP32 OS kernel booting
setup boot parameters to kernel memory
exiting start()
main() starting
setup initial kernel variables
initialize memory
cleaning proccess table
checking process table
process slots: 41 (mapping valid)
checking click memory accounting
```

The watchdog diagnostics remain stable: timer-group and RTC WDT configuration values are zero, and `RTC_SWD_CONF` is `0xC0000000`. The reported reset value `0x0000F041` is still a raw reset-state value and has not been decoded into a definitive reset cause.

Before continuing into scheduling, the next session should:

- verify the temporary idle heartbeat remains alive on hardware;
- add a stack guard pattern at `_stack_bottom`;
- initialize one timer interrupt and validate its acknowledge/clear path;
- replace the heartbeat with a controlled idle/task path;
- only then continue into scheduler and message-passing work.

### Exception diagnostic status

The exception path now captures `EXCCAUSE`, `EXCVADDR`, `EPC1`, and the
current `PS` in `irq.S`, calls `exception_dump()` using the early boot stack,
prints the values through USB Serial/JTAG, and halts. The assembler accepted
`ps` but not the attempted `eps1`/`eps_1` spelling, so this diagnostic reports
the current PS rather than the saved exception PS. The path is build-tested
but has not yet been triggered on hardware.

When tested, expected output includes:

```text
!! CP32 EXCEPTION !!
EXCCAUSE: 0x........
EXCVADDR: 0x........
EPC1:     0x........
EPS1:     0x........
system halted
```

The label should be interpreted as the processor-status diagnostic; the
current implementation passes `PS`, not a saved `EPS1` register.

### SYSTIMER interrupt route probe added

The boot path now writes and reads back the ESP32-S3 CORE0 SYSTIMER TARGET0
interrupt-matrix register at offset `0x0E4`. It maps TARGET0 to CPU interrupt
2, but leaves both the peripheral TARGET0 interrupt and CPU interrupt line
disabled. Expected output is `TARGET0 mapped to CPU interrupt 2 (IRQ disabled)`.
This validates the route without firing the still-incomplete ISR.

The next image enables a one-shot TARGET0 interrupt after the route probe.
The level-2 assembly handler only clears `SYSTIMER_INT_CLR_REG` and returns;
it does not enter MINIX clock or scheduler code. Expected output is
`TARGET0 one-shot IRQ enabled (level 2)` followed by a continuing heartbeat.
This is the first hardware test of interrupt delivery and `rfi 2`; if the
device hangs or resets, disable this call and inspect the level-2 frame/save
logic before adding clock-task behavior.

The level-2 probe handler now increments `cp32_timer_irq_ticks` after clearing
TARGET0, and the idle loop prints the counter after each heartbeat dot. The
counter should increase steadily (approximately 60 ticks per second). This
distinguishes a working periodic ISR from a heartbeat produced only by the
busy-wait loop.

### Stack guard added

`main()` now writes four guard words at `_stack_bottom` and checks them during
the temporary idle loop. Expected output includes `installing stack guard`,
followed by continuing heartbeat dots. If any word changes, the kernel prints
`FATAL: stack guard corrupted` and halts. This guard is only for the current
single-stack bring-up; each future task will need its own stack bounds and
guard policy.

### SYSTIMER probe added

The boot path now starts the ESP32-S3 SYSTIMER UNIT0 counter and verifies that
two reads advance. TARGET0 interrupts remain explicitly disabled because the
ESP32-S3 interrupt-matrix route is not yet implemented. Expected output is
`systimer UNIT0 advancing (TARGET0 IRQ disabled)`. This validates the clock
source without introducing an unhandled interrupt.

Hardware testing confirmed the controlled null-store path works. It produced
`EXCCAUSE: 0x0000001D`, `EXCVADDR: 0x00000000`, a valid IRAM `EPC1`, and then
halted as designed. The output label has been corrected from `EPS1` to `PS`.

The first timer-ISR test did not increment `cp32_timer_irq_ticks`; the idle
heartbeat continued as `.0`. The idle loop now periodically reports SYSTIMER
RAW/STATUS and the Xtensa CPU interrupt register. Use those values to
distinguish a comparator configuration problem from an interrupt-matrix or
CPU-vector problem before changing the ISR again.

The readback showed `CONF=0x400411AA` and a valid TARGET0 low word, but RAW and
STATUS remained zero. The diagnostic now also prints the live UNIT0 counter so
the target can be compared directly with the running counter. If the target is
behind the counter, the comparator load sequence or target programming order
must be corrected next.

The next diagnostic includes the read-only `REAL_TARGET0` registers, which
represent the comparator's active value rather than the staging TARGET0
registers. Compare `real_*` with `target_*` to determine whether
`COMP0_LOAD_REG` synchronized the programmed target.

Hardware showed `REAL_TARGET0=0` while the staged target was valid. The missing
piece was the separate TARGET0 comparator work-enable bit in `SYSTIMER_CONF`.
`systimer_irq_start()` now enables `SYSTIMER_TARGET0_WORK_EN` before enabling
the interrupt. This should allow the one-shot comparator to become active.

Hardware confirmed the target was behind the counter while RAW/STATUS stayed
zero, so periodic mode was not arming the comparator. The probe now uses the
simpler one-shot alarm mode with an absolute target. Once one-shot delivery is
confirmed, periodic reload behavior can be implemented separately.

The follow-up configuration-order change still produced zero RAW/STATUS and
zero CPU interrupt state. The heartbeat diagnostic now also reports TARGET0
HI/LO and TARGET0 CONF readback, allowing the next correction to distinguish a
bad target load from an incorrect periodic-mode bitfield.

The diagnostic output showed `RAW=0`, `STATUS=0`, and CPU `INTERRUPT=0`, so
TARGET0 was never reaching the interrupt matrix. The cause was that the probe
started UNIT0 but left TARGET0 without an initial absolute compare value before
switching to periodic mode. `systimer_irq_start()` now programs the first
target to `current + SYSTIMER_TICKS_PER_CLOCK` and clears stale status before
enabling TARGET0 and CPU interrupt 2.

The follow-up test still showed zero RAW/STATUS, so the setup order was also
corrected: periodic mode is selected first, then the initial absolute target
is written and synchronized, followed by status clear and interrupt enable.

The next boot image now reports the call0 stack bounds, current stack pointer,
and stack alignment. The pointer must be between the linker-defined stack
bottom and top, and its low nibble must be zero.

The stack check now enforces those conditions and halts with
`FATAL: invalid call0 stack` if they fail. The current image was build-tested;
hardware output is still needed to confirm the runtime pointer and alignment.

### Vector validation added

`start()` now reports `_vectors_start`, `_vectors_end`, the first vector word,
and the level-2 vector word at offset `0x180`. Expected values are:

- vector start: `0x40370000`;
- vector end: `0x40370400`;
- vector start must be 1 KiB aligned;
- vector word 0 and word 6 should be nonzero branch instructions.

This is a read-only validation step. It does not enable interrupts or trigger
an exception. If these values are correct on hardware, the next step is a
deliberately controlled exception test followed by call0 frame validation.

The hardware output confirms this validation passed: the vector table starts
at `0x40370000`, ends at `0x40370400`, and both inspected words are nonzero
instructions. An opt-in test is now available in `start.c`; compile with
`-DCP32_TEST_EXCEPTION=1` to trigger a null store after vector validation.
Leave it at the default `0` for normal boot. The expected result is the
terminal exception report followed by `system halted`; do not expect the
normal `start()`/`main()` output after enabling the test.

The reproducible build command is:

```sh
cd src
make test-exception
make flash
```

After testing, rebuild the normal image with `make clean && make`. The
exception image is intentionally not suitable for continuing boot.

### Process-table and click-accounting validation added

`main()` now validates every process-table slot and its reverse pointer mapping
after initialization. It also prints the usable memory base, size, and total
in 4 KiB clicks. Expected process output is `process slots: 41 (mapping valid)`.
A mismatch halts before future scheduling code can use corrupt mappings.

### Level-1 SYSTIMER interrupt dispatch fixed

The first hardware test with TARGET0 enabled produced:

```text
EXCCAUSE: 0x00000004
EPC1:     0x40374D41
```

The timer was firing, but CPU interrupt line 2 was incorrectly assumed to be
Xtensa level 2. The ESP32-S3 toolchain configuration identifies CPU interrupt
2 as level 1. Level-1 interrupts enter through the kernel-exception vector,
not the level-2 vector at offset `0x180`; the old path treated the timer IRQ
as a fatal exception and returned with the wrong interrupt level.

The first follow-up still produced `EXCCAUSE=0x04`, proving that the
previous cause constant was wrong for this Xtensa configuration. Here,
`EXCCAUSE=0x04` is the level-1 interrupt cause. `irq.S` now recognizes
`EXCCAUSE_LEVEL1_INTERRUPT` (`0x04`) in `irq_kernel`,
dispatches to `irq_level1`, acknowledges TARGET0, increments the probe tick
counter, and returns with `rfi 1`. Other kernel exceptions retain the existing
terminal diagnostic path. The image builds successfully.

Next hardware validation:

- the one-shot probe should return to `delay()` without an exception;
- the idle output should show the tick counter increasing;
- if it hangs or faults, capture the new exception report and confirm whether
  the level-1 handler itself needs a dedicated interrupt stack/register frame.

The next image additionally masks CPU interrupt 2 at handler entry and saves
the extra registers required by that operation. This is a diagnostic guard
against immediate level-1 re-entry while TARGET0 is being acknowledged; it is
not the final interrupt masking policy.

The next revision also masks the SYSTIMER TARGET0 source itself as the first
handler action, before clearing the pending flag and updating diagnostics.
This distinguishes a continuously asserted peripheral source from a broken
level-1 return path.

Hardware still reported `EXCCAUSE=0x04`, so the level-1 entry was recognized
but did not return correctly. Because level 1 uses the kernel-exception vector,
its entry has `PS.EXCM` set and must use `rfe`; `rfi 1` is inappropriate here.
The handler now restores `a0` and returns with `rfe`. This is the next hardware
validation point.

The subsequent test still reached the fatal report, so the shared dispatch
branch is now removed temporarily: `_vec_kernel` directly enters
`irq_level1`, which explicitly saves `a0`. This is a controlled bring-up
change, not a finished exception design; synchronous kernel exceptions are
temporarily routed through the timer probe until the hardware return path is
validated.

The linked image confirms the vector is a direct jump to `irq_level1`, so a
continuing `EXCCAUSE=0x04` report is recursive entry during the handler or
return, not failure of the `irq_kernel` cause branch. The level-1 exit now also
executes `rsync` before `rfe`. If this still fails, the next step is to replace
the hand-built handler with a minimal vector-safe frame implementation and
capture interrupt-enable/pending registers before returning.

The confirmed flash test still produced the fatal report. The boot context is
therefore reaching the user-exception vector at offset `0x340`; `irq_user` was
still treating the level-1 interrupt cause as fatal. The bring-up vectors now
route both `0x300` and `0x340` directly to `irq_level1`. Proper privilege-aware
level-1 dispatch must replace this temporary routing before user processes are
introduced.

The timer remains a bring-up probe only. It is not yet connected to MINIX's
`clock_handler`, and the level-1 assembly frame still needs hardening before
general interrupt-driven kernel code is enabled.

Hardware validation then succeeded:

```text
entering kernel idle                    ...
.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.
```

This proves CPU interrupt 2 reaches the level-1 handler and returns to idle
without an exception. The repeated `1` is expected because the current probe
masks TARGET0 in the handler: it delivers one interrupt, then leaves the
counter unchanged. Interrupt entry/return is validated. Remaining timer work
is periodic reload/acknowledgement and integration with MINIX `clock_handler`.

The next probe changes TARGET0 to hardware periodic mode. The handler no
longer masks CPU interrupt 2 or the SYSTIMER source; it only acknowledges the
flag and increments the diagnostic counter. This tests hardware periodic
reload independently of MINIX clock accounting. If stable, the following
step is to replace the counter with a minimal `clock_handler` bridge.

Hardware validation succeeded:

```text
TARGET0 periodic IRQ enabled ...
.27.54.81.108.135.162.189.216.243.270...
```

The counter advances repeatedly without an exception, confirming SYSTIMER
periodic reload, interrupt routing, level-1 entry, and return to idle. The
startup message was corrected to report periodic mode and the actual CPU
interrupt level. The next remaining step is a guarded bridge into MINIX
`clock_handler`, including a proper interrupt frame before scheduling work.
