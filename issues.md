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

## Priority 3: execution currently provides no liveness or stack-overflow protection

The final `while (1) {}` in `main()` masks all later failures and gives no periodic heartbeat. Add a very simple diagnostic heartbeat or watchdog-safe idle loop once the early path is being tested. Also consider a guard pattern at `_stack_bottom` and periodic checking before introducing nested interrupts and tasks.

## Recommended order before continuing the port

1. Verify the initialized-memory sentinels on hardware.
2. Implement and test `.data`/`.rodata` initialization, if the loader does not already do it.
3. Add a minimal exception dump and validate vector contents.
4. Validate call0 stack/interrupt frames with disassembly and deliberate fault tests.
5. Replace magic boot values with named ESP32-S3 constants and correct RAM accounting.
6. Add assertions/diagnostics around click conversion and process-table indexing.
7. Initialize one timer interrupt and one controlled idle path before porting full scheduling/message passing.

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
```

The watchdog diagnostics remain stable: timer-group and RTC WDT configuration values are zero, and `RTC_SWD_CONF` is `0xC0000000`. The reported reset value `0x0000F041` is still a raw reset-state value and has not been decoded into a definitive reset cause.

Before continuing into scheduling, the next session should:

- verify `.data sentinel: 0xC032DA7A` and `.bss sentinel: 0x00000000` on hardware;
- add a minimal exception dump before enabling interrupts;
- validate vector contents at `_vectors_start` and confirm `VECBASE` alignment;
- inspect the call0 stack frame and generated prologue/epilogue in disassembly;
- only then continue into timer, scheduling, and message-passing work.

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

Hardware testing confirmed the controlled null-store path works. It produced
`EXCCAUSE: 0x0000001D`, `EXCVADDR: 0x00000000`, a valid IRAM `EPC1`, and then
halted as designed. The output label has been corrected from `EPS1` to `PS`.

The next boot image now reports the call0 stack bounds, current stack pointer,
and stack alignment. The pointer must be between the linker-defined stack
bottom and top, and its low nibble must be zero.

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
