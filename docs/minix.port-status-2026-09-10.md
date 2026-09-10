# CP32 MINIX Port Status Report

This file should not be used for the implementation. Its only reference.

## Summary

- CP32 port has implemented a minimal kernel skeleton including memory management, basic process table, minimal interrupt handling, and a rudimentary scheduler stub. However, many core MINIX functionalities—such as full IPC, process scheduling, file system, command shell, networking, and complete device drivers—remain missing or only partially ported. The port is still in a testing phase with several hardware validation markers (e.g., lock, IPC, MM) but key runtime features are not operational yet.

## Implemented

- **Kernel skeleton**: `src/kernel/` (clock.c, mm.c, proc.c, serial.c, vectors.S, etc.) provide the core of the MINIX kernel adapted for ESP32‑S3.
- **Memory management**: `src/kernel/mm.c` implements basic page table logic (although paging is not used).
- **Interrupt handling**: `src/kernel/irq.S` and `src/kernel/irq_const.h` contain minimal interrupt vector table and handlers, with stubs for IRQ routing.
- **Serial I/O**: `src/kernel/serial.c` provides a minimal UART driver for the ESP32‑S3.
- **Process table**: `src/kernel/proc.c` defines the `proc` array and minimal scheduling functions.
- **System reset placeholder**: `src/kernel/system.c` includes a reset placeholder that will be replaced with ESP32‑S3 reset logic.
- **Minimal scheduler**: `src/kernel/proc.c` and `src/kernel/main.c` contain a dummy scheduler that can be invoked for testing but does not perform context switching.

## Partially Implemented

- **Context switching**: `src/kernel/port.c` contains a context‑restore experiment that is disabled (`cp32_context_restore_gate`). The full context switch routine is not yet implemented.
- **Interrupt-to-C bridge**: `src/kernel/irq.S` includes a bridge that currently just spins or returns immediately.
- **Scheduler and IPC**: `src/kernel/main.c` calls `schedule()` but the function is a stub. IPC mechanisms (`ipc.c`, `proc.c`) are largely missing; test IPC functions exist but real IPC is not functional.
- **TTY and console**: `src/kernel/tty.c` contains minimal stubs for console and UART drivers; full console support is not yet integrated.
- **Clock handler**: `src/kernel/clock.c` has a TODO and incomplete implementation.
- **Reset and boot sequence**: `src/kernel/system.c` has a placeholder; the actual boot loader and reset logic are incomplete.
- **Device drivers**: No full driver stack for peripherals other than UART; memory mapping for flash/ram is not fully ported.

## Missing

- **File System**: No implementation of MINIX file system; no `fs.c`, `disk.c`, or related code.
- **Command Shell**: No shell (`ash`, `minix-shell`, etc.) or command utilities.
- **Networking**: No network stack or IP stack.
- **Process scheduling and preemption**: Full scheduler (time slicing, priority, ready list) is missing.
- **IPC and messaging**: No implementation of send/receive, message queues, or IPC primitives.
- **Device drivers**: Drivers for disks, network, graphics, etc. are missing.
- **Boot loader**: The boot loader is not ported; the code assumes a preloaded kernel image.
- **Paging and virtual memory**: The port has minimal support for page tables but no actual paging support.

## Requires Hardware Validation

- **Lock and IPC tests**: Issues.md lists V17, V16, V15, V14, V13, V12, V11, V10, V9; many are still pending hardware validation.
- **Clock handler gate**: Marked as disabled in issues.md; needs hardware validation.
- **Context switching**: Requires hardware to confirm that context restore and scheduler work.
- **Interrupt routing**: The IRQ vector table needs hardware validation for correct mapping.
- **Memory layout**: The linker script (`src/kernel/esp32s3.ld`) and memory map (`docs/CP32-Memory-Map.md`) need to be validated on hardware.

## Suspicious or Incomplete Code

- `TODO` in `src/kernel/clock.c` (line 572).
- `/* Minimal scheduler stub */` in `src/kernel/proc.c` (line 32).
- `/* Minimal ESP32‑S3 stubs */` in `src/kernel/tty.c` (line 1375).
- `/* Minimal reset placeholder */` in `src/kernel/system.c` (line 500).
- Disabled code blocks in `src/kernel/irq.S` and `src/kernel/port.c`.

## Recommended Next Steps

1. **Implement full context switching and scheduler**: Replace stubs in `src/kernel/proc.c` and `src/kernel/port.c` with working context switch code, including interrupt handling and task dispatch.
2. **Complete IPC**: Implement MINIX IPC primitives, message queues, and test with hardware.
3. **Port the file system**: Implement disk I/O and MINIX file system support.
4. **Implement boot loader**: Port the MINIX boot loader to the ESP32‑S3 or use a custom loader that loads the kernel into RAM.
5. **Add device drivers**: Implement drivers for storage, networking, and other peripherals.
6. **Hardware validation**: Run the hardware validation tests from `issues.md` to confirm correct behavior of locks, IPC, MM, and interrupt routing.
7. **Remove or replace stubbed code**: Once functionality is ported, clean up placeholder and TODO comments.

```
# Next
- Implement context switching (port `proc.c` and `port.c`)
- Port IPC primitives
- Implement file system
- Validate hardware: lock, IPC, MM, interrupt routing
```
