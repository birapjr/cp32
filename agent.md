# CP32 Project Guide for Codex

## Project identity

CP32 is a work-in-progress, bare-metal, Unix-like operating-system port for the M5Stack Cardputer Adv, built around the Espressif ESP32-S3FN8 (Xtensa LX7, dual core). The kernel is based on the MINIX 2.0 architecture and source style, adapted incrementally for the ESP32-S3 rather than running on a PC BIOS, 8259 PIC, or 8253 PIT.

The repository is currently kernel-focused. The long-term goal described by the project is a kernel, shell, and applications, but the current tree does not yet contain those user-space components.

## Repository layout

- `src/Makefile` — standalone cross-compilation, linking, image generation, flashing, and ELF inspection targets.
- `src/kernel/` — kernel C and Xtensa assembly sources.
  - `start.c` — early C startup and kernel environment handling.
  - `main.c` — kernel entry point; currently initializes memory/process-table state and then stops in an infinite loop.
  - `mpx32.S`, `vectors.S`, `irq.S`, `klib32.S` — reset/startup, exception/vector, interrupt, and low-level assembly support.
  - `proc.c` — MINIX process scheduling and message-passing framework; many core routines are still stubs.
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
- The linker entry point is `CP32`. The startup path copies load images from flash into IRAM/DRAM, zeros `.bss`, establishes the stack/vector base, disables or handles watchdog state, and enters C code.
- The linker deliberately places vectors and all kernel `.text` in IRAM, with flash as their load address. `.data` and `.rodata` execute/access from DRAM after startup copying. A fixed 128 KiB heap and 32 KiB downward-growing stack are reserved in DRAM.
- Peripheral access is direct memory-mapped I/O through `volatile` register macros. Do not use ESP-IDF APIs unless the project is explicitly migrated to that runtime.
- The USB Serial/JTAG endpoint is the current diagnostic console. Keep early diagnostics simple and safe before interrupts, scheduling, or normal TTY services are operational.
- The ESP32-S3 SYSTIMER is the intended clock source: UNIT0 is treated as a 16 MHz counter and the MINIX clock rate is 60 Hz. There is no PC interrupt controller or PIT to port.
- MINIX structures and APIs use historical K&R declarations and compatibility macros. Preserve existing ABI/layout expectations when changing headers or `struct proc`, `message`, TTY, and stack-frame definitions.

## Current WIP boundaries

Do not describe the kernel as boot-complete or a usable MINIX system. Known incomplete areas include:

- `main()` does not yet continue into task initialization/scheduling.
- `proc.c` process dispatch, system-call handling, send/receive, ready-queue operations, and context switching are incomplete or placeholders.
- `irq.S` still has TODOs for interrupt/exception decoding and dispatch.
- `port.c` `_send()` and `_receive()` are temporary stubs returning `OK`.
- Device-specific Cardputer input/display, storage, user-process loading, shell, filesystem, and applications are not present in this tree.
- Clock and TTY code is adapted from MINIX but requires validation against the actual ESP32-S3 interrupt and Cardputer device model.

When implementing features, prefer making one low-level path testable on real hardware and preserving diagnostic output before attempting broad MINIX subsystem integration.

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
