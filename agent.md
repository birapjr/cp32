# CP32 Project Guide for Codex

## Project identity

CP32 is a work-in-progress, bare-metal, Unix-like operating-system port for the M5Stack Cardputer Adv, built around the Espressif ESP32-S3FN8 (Xtensa LX7, dual core). The kernel is based on the MINIX 2.0 architecture and source style, adapted incrementally for the ESP32-S3 rather than running on a PC BIOS, 8259 PIC, or 8253 PIT.

The repository is currently kernel-focused. The long-term goal described by the project is a kernel, shell, and applications, but the current tree does not yet contain those user-space components.

## AI Code Agent rules to follow

Before modifying kernel/assembly code:

1. Identify the MINIX v2 original behavior. Use folder minix-2.0.0 as reference.
2. Identify the ESP32-S3 architectural difference.
3. Identify the CP32 invariant being preserved.
4. Make the smallest testable change.
5. Build with the existing Makefile.
6. Inspect ELF sections/symbols when relevant.
7. Never assume hardware behavior without documentation or a
   hardware validation result.
8. Never replace bare-metal code with ESP-IDF unless explicitly requested.
9. Update issues.md with hardware validation results.
10. Update plan.md with tasks in progress, so next sessions can pick-up where was stopped
10. Update plan.md with all completed task and validated on hardware.
11. General Testing Procedure – For every change:
11.1. Modify the code.
11.2. `make clean && make` in `src/`.
11.3. fix compilation errors
11.4. jump back to 11.2. and repeat

This incremental plan keeps each change small, build‑test‑validate cycles, and provides clear console output to aid human and agent debugging.

## Repository layout

- `src/Makefile` — standalone cross-compilation, linking, image generation, flashing, and ELF inspection targets.
- `src/kernel/` — kernel C and Xtensa assembly sources.
  - `start.c` — early C startup and kernel environment handling.
  - `main.c` — kernel entry point; validates startup/process/memory state, starts the SYSTIMER probe, and runs a diagnostic idle loop.
  - `mpx32.S`, `vectors.S`, `irq.S`, `klib32.S` — reset/startup, exception/vector, interrupt, and low-level assembly support.
  - `proc.c` — MINIX process scheduling and message-passing framework; many core routines are still stubs.
  - `irq_const.h`, `irq_frame.h` — assembly-safe interrupt-frame constants and the C-visible 64-byte frame contract.
  - `clock.c` — MINIX clock task adapted to the ESP32-S3 SYSTIMER.
  - `tty.c` — largely MINIX-style TTY/line-discipline implementation.
  - `serial.c` — USB Serial/JTAG diagnostic console.
  - `wdt.c` — watchdog disable/feed support for the timer-group, RTC, and Super WDTs.
  - `system.c` — MINIX system-task operations and memory/process support, still dependent on unfinished porting work.
  - `port.c` — temporary ESP32-S3 glue and placeholder `_send`/`_receive` implementations.
  - `esp32s3.ld` — custom linker script placing vectors/code in internal IRAM and data/heap/stack in DRAM.
- `src/include/` — compatibility headers and MINIX/ESP32-S3 definitions.
- `src/lib/other/printk.c` — small kernel support library source.
- `docs/` — ESP32-S3 memory map and Xtensa assembly/linker notes.
- `README.md` — basic build, flash, and serial-console instructions.

## Build and hardware workflow

Run commands from `src/`, because the only Makefile is `src/Makefile`:

```sh
make clean
make
make size       # section sizes
make headers    # ELF sections
make segments   # ELF load segments and LMA/VMA
make sections   # detailed section table
make nm         # symbols sorted by address
make disasm     # source-interleaved disassembly
make flash
```

Feature-target workflow:

```sh
make clean && make test-task-startup
make clean && make test-task-startup-clock
make clean && make test-task-startup-sys
```

The normal image keeps task startup guarded. These targets enable one
descriptor-driven startup slice at a time; hardware completion requires the
matching `[TASK Vn ... pass=1]` marker and continued IRQ/context stability.

The expected toolchain is `xtensa-esp32s3-elf-gcc` and related binutils. Image generation and flashing use `esptool`, with the current default serial device set to `/dev/cu.usbmodem2101` in the Makefile. Treat the port as machine-specific and change it when necessary. The README documents viewing early kernel output at 115200 baud with `screen`.

## Architecture and important invariants

- This is freestanding code: no hosted libc, startup files, or operating-system services are available. Use the local implementations in `src/kernel/klib.c` and the project headers instead of assuming a normal libc.
- The build uses `-mabi=call0`, `-ffreestanding`, `-nostdlib`, `-nostartfiles`, `-O0`, and `-mlongcalls`. Assembly must preserve the calling convention and match the C-visible stack/register assumptions.
- The linker entry point is `CP32`. The ESP image loader places the linked IRAM/DRAM runtime segments at their VMAs before `CP32`; `mpx32.S` zeros `.bss`, establishes the stack/vector base, and enters C. Do not add software LMA copy loops without changing and revalidating the image format.
- The linker places vectors and kernel `.text` in IRAM and `.data`/`.rodata` in DRAM. A fixed 128 KiB heap and 32 KiB downward-growing stack are reserved in DRAM.
- Peripheral access is direct memory-mapped I/O through `volatile` register macros. Do not use ESP-IDF APIs unless the project is explicitly migrated to that runtime.
- The USB Serial/JTAG endpoint is the current diagnostic console. Keep early diagnostics simple and safe before interrupts, scheduling, or normal TTY services are operational.
- The ESP32-S3 SYSTIMER is the intended clock source: UNIT0 is treated as a 16 MHz counter and TARGET0 runs periodically at the 60 Hz MINIX rate. TARGET0 maps to CPU interrupt 2, an Xtensa level-1 interrupt on this core; it enters through the kernel/user exception dispatchers and returns with `rfe`.
- MINIX structures and APIs use historical K&R declarations and compatibility macros. Preserve existing ABI/layout expectations when changing headers or `struct proc`, `message`, TTY, and stack-frame definitions.

## Current WIP boundaries

Do not describe the kernel as boot-complete or a usable MINIX system. Known incomplete areas include:

- `main()` still stops in a diagnostic idle loop; descriptor-driven IDLE, CLOCK,
  and SYS frame initialization are validated, but their production service
  loops are not started.
- `proc.c` has validated ready/unready, IPC, interrupt replay, and guarded
  handoff slices; production scheduler ownership and full context switching
  remain incomplete.
- `irq.S` has a validated level-1 SYSTIMER probe frame, but general interrupt dispatch, nested-context policy, and scheduler return are incomplete.
- `port.c` retains compatibility wrappers; production user-process syscall
  entry and complete task lifecycle remain incomplete.
- Device-specific Cardputer input/display, storage, user-process loading, shell, filesystem, and applications are not present in this tree.
- Clock and TTY code is adapted from MINIX but requires validation against the actual ESP32-S3 interrupt and Cardputer device model.

Validated descriptor startup currently covers IDLE, CLOCK, and SYS. TTY
descriptor startup is the next slice; service loops, scheduler ownership, and
full task lifecycle remain gated until each task has a bounded startup probe.

When implementing features, migrate one coherent MINIX feature at a time. Keep
the original file/function structure where practical, isolate ESP32-S3 changes
to IRQ/register/assembly/ABI boundaries, add one compact aggregate marker for
failure-prone state, build the feature target, and require serial hardware
evidence before marking the slice complete. Remove obsolete high-frequency
diagnostics after the slice is validated.

## Current project status

The current hardware-validated path reaches `main()`, initializes the process
table and ready queues, validates the MINIX task descriptor table, performs
guarded IDLE/CLOCK/SYS descriptor startup, starts the ESP32-S3 SYSTIMER probe,
validates IPC/MM and syscall rejection, and enters the diagnostic idle loop.
The latest SYS startup run remained stable through IRQ 176.

Validated bring-up areas:

- `.data`/`.bss`, vectors, call0 stack alignment, stack guard, and click
  accounting.
- SYSTIMER UNIT0 progress and TARGET0 routing to CPU interrupt 2.
- IPC message delivery with `Hello IPC!`, `flags=0`, and `mem_copy len=36`.
- Syscall invalid-function rejection with `EBADCALL` (`-102`).
- Context-frame PC/SP/PS inspection, `a1 == sp`, `a15ok=1`, and the gated
  V14/V15/V16 restore experiments without timer exceptions.

Current gates and limitations:

- The timer-to-clock/scheduler bridge remains gated during early validation so
  the first IRQ cannot switch away from `main()` prematurely.
- The V16 context gate is enabled, but the complete proven register restore is
  retained as the fallback; a production scheduler handoff is not complete.
- `switch_to()` remains a diagnostic C boundary, not a complete production
  context switch; descriptor startup currently validates frames without
  launching service loops.
- User trap entry, normal syscall entry from user mode, process execution,
  clock accounting, and the full MINIX task lifecycle remain unfinished.

Do not describe CP32 as boot-complete or as a usable MINIX system. The latest
hardware result is documented in `plan.md` and `issues.md`; future changes
must update both files and add a new versioned marker when diagnostics change.

Current hardware markers and diagnostics are tracked in `issues.md`. Use the
aggregate feature markers `[IRQ V1]`, `[TASK V1]`–`[TASK V4]`, `[CLOCK V1/V2]`,
`[CTX V46]`, and the compact IPC/MM/LOCK markers. Add detailed fields only
while diagnosing a failure, then remove them after validation.

## Bring-up debug and marker strategy

Serial diagnostics are deliberately compact because timer IRQs can interleave
with output. Every hardware-visible diagnostic change gets a monotonically
increasing version marker so the flashed image can be identified immediately.
Use the marker families consistently:

- `[IMG Vn]` identifies an image only when a new image distinction is needed.
- `[BOOT Vn]` identifies timer/boot sequencing changes.
- `[IPC Vn]` and `[MM Vn]` identify IPC delivery and memory-translation checks.
- `[SYS Vn]` identifies syscall-dispatch checks.
- `[CTX Vn]` identifies process-frame and context-switch checks. Include only
  small, actionable fields such as `p`, `sp`, `a15ok`, `ps`, `pc`, `a1`,
  `spok`, `gate`, and relationship checks such as `rel`.
- `[IRQ n r=... f=...]` is rate-limited (currently every 16 ticks). Preserve
  the compact counters and do not print once per interrupt.

When changing a diagnostic, bump its version rather than reusing an old one.
Record the marker and hardware result in both `plan.md` and `issues.md`.
Validation results must distinguish build-only status from hardware status;
never mark a hardware check complete without a serial-console result. Use
unsigned hexadecimal/decimal values consistently with the existing console
helpers, and keep error markers short, for example `[MM E ...]`.

## Coding guidance for future changes

1. Inspect the relevant MINIX-compatible declarations in `src/include/` and `src/kernel/` before changing C code; many globals are declared through `EXTERN`/`PUBLIC`/`PRIVATE` conventions.
2. Keep hardware register definitions centralized in the ESP32-S3 headers. Add comments with the register offset, bit meaning, and the source of any hardware-verified correction.
3. For startup, vectors, interrupts, and context switching, inspect both the assembly and linker script together. Section placement, literal pools, alignment, entry symbols, and load-vs-virtual addresses are interdependent.
4. Avoid dynamic allocation or large automatic objects in early boot and interrupt paths. Respect the linker assertions for IRAM and DRAM and verify with `make size`, `make segments`, and `make sections`.
5. Do not silently replace the bare-metal build with ESP-IDF, Arduino, or a hosted toolchain. Such a change would be an architectural migration and must be explicit.
6. There is no automated test suite currently visible. At minimum, build the ELF/bin image when the cross-toolchain is available, inspect the generated sections for placement, and validate behavior through the USB serial console on hardware.
7. Preserve unrelated working-tree modifications. Before making overlapping edits, inspect `git diff` and keep the user’s changes intact.

## Documentation note

The project documentation identifies MINIX v2 as the source base. Some compatibility headers retain historical MINIX terminology and structures, including a `V1` filesystem zone type; that is a filesystem-format compatibility detail, not an indication that CP32 targets MINIX v1.
