# CP32 MINIX 2.0.0 Port Status

Review date: 2026-09-13

## Summary

CP32 has a functioning ESP32-S3 bare-metal kernel bring-up with validated
startup, exception vectors, SYSTIMER operation, process descriptors, IPC
invariants, Cardputer keyboard input, TTY line handling, and a minimal RAM-disk
diagnostic shell. The implementation is still a kernel bring-up platform, not
a complete MINIX 2.0.0 system: there is no production MM server, FS server,
user process image/runtime, libc, executable loader, or application suite.

Estimated port coverage: **about 32% of MINIX v2 functionality**.

This is a weighted subsystem estimate, not a source-line percentage. It counts
boot/CPU, scheduling, IPC, clock, memory, system services, TTY, storage, MM,
userland, and optional drivers by functional scope. Approximately **24% is
currently both implemented and hardware-validated**; the difference is code
that exists but still requires production-path or hardware validation.

## Implemented

- ESP32-S3 reset/startup, BSS and stack setup, linker image, watchdog support:
  `src/kernel/start.c`, `src/kernel/esp32s3.ld`, `src/kernel/wdt.c`.
- Xtensa vector layout and level-1/level-2 interrupt entry/return scaffolding:
  `src/kernel/vectors.S`, `src/kernel/irq.S`, `src/kernel/irq_frame.h`.
- MINIX-shaped process descriptors, ready queues, IPC send/receive queues,
  message validation, deadlock checks, and queue invariants:
  `src/kernel/proc.c`, `src/kernel/proc.h`.
- SYSTIMER tick accounting, uptime, alarm structures, and clock task support:
  `src/kernel/clock.c`.
- Flat ESP32-S3 memory translation, physical copy helpers, allocator ownership
  and coalescing, and MM request validation: `src/kernel/mm.c` and
  `src/kernel/system.c`.
- Cardputer TCA8418 keyboard probing, initialization, stale-FIFO handling,
  event decoding, and TTY character delivery: `src/kernel/cardputer.c` and
  `src/kernel/tty.c`.
- Bounded canonical input line handling and `ls`/`ramdisk` diagnostics:
  `src/kernel/main.c`, `src/kernel/ramdisk.c`.
- Build/image-layout and host contract tests under `tests/`; hardware evidence
  through marker 49 shows stable boot, keyboard/TTY input, `ls`, and sustained
  IRQ/clock operation.

## Partially Implemented

### Boot and kernel startup

- CP32: `src/kernel/start.c`, `src/kernel/main.c`, `src/kernel/mpx32.S`.
- MINIX reference: `minix-2.0.0/src/kernel/start.c`, `main.c`, `table.c`.
- MINIX initializes a complete boot task table and enters the normal task set.
  CP32 initializes descriptors and selected task entry points, but its live
  console path still uses bring-up scheduling and direct frame experiments.
- BIOS, PIC, protected mode, and x86 selectors are correctly replaced by
  ESP32-S3 loader state, Xtensa vectors, and flat SRAM.

### Interrupts and exceptions

- CP32: `src/kernel/irq.S`, `src/kernel/vectors.S`, `src/kernel/proc.c`.
- MINIX reference: `mpx386.s`, `exception.c`, `i8259.c`, `proc.c`.
- Entry and SYSTIMER return paths work on hardware, but vector coverage,
  nested IRQ policy, panic recovery, and production dispatch are incomplete.

### Scheduling, context switching, and IPC

- CP32: `src/kernel/proc.c`, `src/kernel/irq.S`, `src/kernel/port.c`.
- MINIX reference: `src/kernel/proc.c`, `mpx386.s`.
- MINIX queue and message semantics are substantially present. Full blocked
  SEND/RECEIVE suspension and resume through a production user frame remain
  incomplete. The blocked handoff probes are retained but disabled in the
  normal console build after hardware showed TTY starvation.

### Clock and alarms

- CP32: `src/kernel/clock.c`.
- MINIX reference: `src/kernel/clock.c`.
- Tick and uptime behavior are hardware-observed. Alarm delivery, quantum
  scheduling, clock-task service IPC, and all delay paths need full validation.

### Memory and system services

- CP32: `src/kernel/mm.c`, `src/kernel/system.c`.
- MINIX reference: `memory.c`, `system.c`.
- Flat address translation, copy, map structures, and a private allocator are
  present. The MM task protocol, user isolation, fork/exec image placement,
  and complete server integration are not complete.

### TTY and console

- CP32: `src/kernel/tty.c`, `src/kernel/cardputer.c`, `src/kernel/serial.c`.
- MINIX reference: `tty.c`, `console.c`, `keyboard.c`, `rs232.c`, `pty.c`.
- Cardputer keyboard input and a minimal canonical line path work. Display
  output, real TTY task service, UART device semantics, ptys, ioctl breadth,
  and user read/write syscalls remain incomplete.

## Missing

- FS server and VFS: `minix-2.0.0/src/fs/`; no `src/fs/` exists.
- Inodes, directory traversal, file descriptors, block cache, and filesystem
  image loading.
- MM server and user process environment: `minix-2.0.0/src/mm/`; only kernel
  `mm.c` exists.
- Production fork/exec/wait/exit, brk/sbrk, permissions, and user signal
  frames.
- User executable format/loader, startup runtime, libc, syscall stubs, shell,
  and command suite: `minix-2.0.0/src/lib/`, `commands/`, `test/`, `boot/`.
- PC-specific networking, floppy/IDE/SCSI, printer, sound, and VGA drivers are
  not ported. Most are not applicable to the first Cardputer milestone.

## Requires Hardware Validation

- `src/kernel/irq.S`: complete nested interrupt and selected-frame return.
  Needed result: repeated task/user returns without stale-frame faults.
- `src/kernel/proc.c`: blocked SEND/RECEIVE resume and scheduler quantum.
  Needed result: both IPC directions complete while TTY remains responsive.
- `src/kernel/clock.c`: alarms, delays, and service-message delivery.
  Needed result: deterministic tick/alarm observations.
- `src/kernel/system.c`: system-task requests, reset behavior, fork/exec/exit.
  Needed result: real messages through `sys_task()` and safe reset policy.
- `src/kernel/cardputer.c` and `tty.c`: keyboard interrupt-driven production
  ownership and display output. Current evidence validates polling and TTY
  characters, not the final IRQ-only driver.
- `src/kernel/mm.c`: allocator under repeated fragmentation and server IPC.
  Current smoke tests validate basic allocation, ownership, release, and merge.

## Suspicious or Incomplete Code

- `src/kernel/irq.S`: unhandled/debug/NMI paths spin; production dispatch is
  still a bring-up implementation.
- `src/kernel/proc.c`: blocked context probes and diagnostic guards remain;
  blocked handoff is not the production path.
- `src/kernel/mm.c`: allocator is a private bounded CP32 heap, not MINIX MM.
- `src/kernel/system.c`: `system_reset()` is a non-returning placeholder.
- `src/kernel/main.c`: panic path and direct idle-loop frame entry are
  bring-up mechanisms; shell intentionally supports only `ls` and `ramdisk`.
- `src/kernel/tty.c`: runtime keyboard ownership is polling-oriented and not a
  complete MINIX TTY driver.
- `src/kernel/clock.c`: contains architecture-port TODO commentary and
  incomplete production service integration.

## Recommended Next Steps

1. Keep the stable marker-49 build as the baseline.
2. Build an isolated IPC test task that can exercise blocked SEND/RECEIVE
   without hijacking the console TTY owner.
3. Complete one production Xtensa context-switch contract and use it for
   CLOCK, SYS, and TTY tasks.
4. Turn `sys_task()` into a real serviced endpoint, then implement the MM
   server protocol and map ownership.
5. Add a minimal user ABI and executable loader before expanding commands.
6. Add a small CP32 filesystem/RAM-disk layer; keep `cat` deferred until the
   production FS/message path exists.
7. Port only Cardputer-relevant drivers after the kernel/user ABI is stable.

---
Newly implemented between the September 11 and September 13 reports:

- Cardputer TCA8418 keyboard probing and initialization.
- Keyboard stale-FIFO draining and stale-event accounting.
- Keyboard matrix event decoding into characters.
- TTY character delivery from the Cardputer keyboard.
- Bounded canonical TTY line buffering.
- Backspace and Enter handling in the TTY line discipline.
- Minimal `ls` command.
- Minimal `ramdisk` command.
- Unknown-command diagnostics.
- RAM-disk capacity and formatting-state reporting.
- Private CP32 memory allocator.
- Allocation ownership enforcement.
- Allocation release and adjacent-block coalescing.
- Zero-size and oversized allocation rejection.
- Invalid-owner and invalid-base rejection.
- Expanded IPC queue and process-table integrity validation.
- Held/deferred interrupt queue validation.
- Saved-context, blocked-frame, and runtime-owner validation.
- Kernel-task and system-task descriptor validation.
- More complete ESP32-S3 keyboard status/FIFO diagnostics.
- Rate-limited runtime heartbeat, clock, IPC, and scheduler diagnostics.
- Hardware validation through stable marker 49, including keyboard/TTY input and `ls`.

---
CP32 currently has **11,253 physical source lines**, excluding generated build artifacts and the MINIX reference tree:

- C: 7,180 lines
- Headers: 3,011 lines
- Xtensa assembly: 929 lines
- Makefile: 133 lines

The tracked CP32 source total is **11,253 lines**.
---
The MINIX 2.0.0 reference contains:

- **365,266 lines** of comparable C/header/assembly source
- **388,253 lines** across all files under `minix-2.0.0/src`

Comparison:

- CP32: **11,253 lines**
- Comparable MINIX source: **365,266 lines**
- CP32 is about **3.1% of the reference source volume**

This is source volume only—not functional port coverage.