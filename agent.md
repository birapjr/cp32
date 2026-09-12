# CP32 Project Guide for Codex

## Project identity

CP32 is a work-in-progress, bare-metal, Unix-like operating-system port for the M5Stack Cardputer Adv, built around the Espressif ESP32-S3FN8 (Xtensa LX7, dual core). The kernel is based on the MINIX 2.0 architecture and source style, adapted incrementally for the ESP32-S3 rather than running on a PC BIOS, 8259 PIC, or 8253 PIT.

The repository is currently kernel-focused. The long-term goal described by the project is a kernel, shell, and applications, but the current tree does not yet contain those user-space components.

## AI Code Agent rules to follow

Before modifying kernel/assembly code:

1. Identify the MINIX v2 original behavior. Use folder minix-2.0.0 as reference.
2. Identify the ESP32-S3 architectural difference.
3. Identify the CP32 invariant being preserved.
4. Make the a full feature testable change.
5. Build with the existing Makefile.
6. Inspect ELF sections/symbols when relevant.
7. Never assume hardware behavior without documentation or a
   hardware validation result.
8. Never replace bare-metal code with ESP-IDF unless explicitly requested.
11. General Testing Procedure – For every change:

1. Modify the code.
2. `make clean && make` in `src/`.
3. fix compilation errors
4. jump back to 11.2. and repeat


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

## Bring-up debug and marker strategy

Serial diagnostics are deliberately compact because timer IRQs can interleave
with output. Every hardware-visible diagnostic change gets a monotonically
increasing version marker so the flashed image can be identified immediately.
Also emit a short test-identity tag immediately before `main()` calls
`kernel_idle_loop()`, naming the feature under test (for example,
`[TEST IRQ-REG]`). Keep this tag stable for that image and update it when the
feature under test changes, so every hardware log identifies what was flashed.
The marker must be emitted immediately before the `kernel_idle_loop()` call
in `main()` on every feature-change build.
Append an incrementing decimal counter to the marker, for example
`[TEST CARDPUTER-KBD 2]`, and increment it for every later image change so
hardware logs can prove which build is running.
When a plan item is completed, mark its line with `[x]` at the beginning in
`plan.md`. Do not mark partially implemented or hardware-unverified work as
complete; describe the remaining work on a following line instead.
For execution tracing, put feature markers in a dedicated removable function,
call it only from the normal production path (never from a manual trigger),
and rate-limit it. IPC markers should identify the operation and count, with
the first use visible and later output limited to an infrequent interval
(currently every 5000 calls for the clock receive loop).
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

Do not add test probes between production code and test code, the probes should be complete easy to remove, and do not remove any functionality of the kernel code.

## Coding guidance for future changes

1. Inspect the relevant MINIX-compatible declarations in `src/include/` and `src/kernel/` before changing C code; many globals are declared through `EXTERN`/`PUBLIC`/`PRIVATE` conventions.
2. Keep hardware register definitions centralized in the ESP32-S3 headers. Add comments with the register offset, bit meaning, and the source of any hardware-verified correction.
3. For startup, vectors, interrupts, and context switching, inspect both the assembly and linker script together. Section placement, literal pools, alignment, entry symbols, and load-vs-virtual addresses are interdependent.
4. Avoid dynamic allocation or large automatic objects in early boot and interrupt paths. Respect the linker assertions for IRAM and DRAM and verify with `make size`, `make segments`, and `make sections`.
5. Do not silently replace the bare-metal build with ESP-IDF, Arduino, or a hosted toolchain. Such a change would be an architectural migration and must be explicit.
6. I autometed test suite should updated in the folter `tests` on root repo level, and colled with `make tests`. Every new feature, when possible should be added a test for it. Test suite folder structure should follow kernel folder when possible.
7. The next test is build the ELF/bin image when the cross-toolchain is available, inspect the generated sections for placement, and validate behavior through the USB serial console on hardware.
8. Preserve unrelated working-tree modifications. Before making overlapping edits, inspect `git diff` and keep the user’s changes intact.

## Documentation note

The project documentation identifies MINIX v2 as the source base. Some compatibility headers retain historical MINIX terminology and structures.
