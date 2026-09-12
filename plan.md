# CP32 implementation plan

This plan tracks the CP32 port feature-by-feature against `minix-2.0.0`.
MINIX is the behavioral reference; ESP32-S3 replacements are valid when they
preserve the MINIX invariant.

## Current boundary

CP32 currently boots through `start()` → `main()` → diagnostics → the
ESP32-S3 SYSTIMER probe → an idle loop. The repository is kernel-focused and
does not contain MM, FS, user processes, a shell, or applications.

Preserve these invariants: bare-metal Xtensa `call0`; the 64-byte saved-frame
contract; direct ESP32-S3 register access; MINIX process-number, queue, and
message-copy semantics; and removable, versioned validation probes.

## Feature status and remaining work

### 1. Reset, boot, and kernel image — Partially implemented

Reference: `minix-2.0.0/src/kernel/start.c`, `main.c`, `mpx386.s`, `table.c`.
CP32: `src/kernel/start.c`, `mpx32.S`, `vectors.S`, `main.c`, `esp32s3.ld`.

Present: Xtensa reset entry, BSS/stack setup, linker image, watchdog handling,
process-table initialization, task metadata, and clean image builds.
Missing: descriptor-driven production startup for all active tasks, a real boot
task table, user-image loading, and entry into a scheduled task set.
ESP32-S3 replaces BIOS/protected-mode setup and PIC/PIT startup with loader
segments, Xtensa vectors, and SYSTIMER registers.

Next: enable production task startup one task at a time and validate frame
restore and stack ownership on hardware.

### 2. Interrupt and exception dispatch — Partially implemented

Reference: `minix-2.0.0/src/kernel/mpx386.s`, `i8259.c`, `exception.c`,
`proc.c:interrupt()`.
CP32: `src/kernel/vectors.S`, `irq.S`, `irq_frame.h`, `irq_const.h`, `proc.c`.

Present: frame checks, exception diagnostics, masking, deferred/coalesced
hardware notifications, and SYSTIMER entry. Missing: the real dispatch path
(`src/kernel/irq.S` contains `TODO: dispatch`), complete cause/vector routing,
and production return into the selected process frame.
ESP32-S3 uses Xtensa causes and `rfe`, not an Intel frame or PIC acknowledgement.

Next: implement one complete IRQ-to-task return path, then test nested IRQs and
fatal panic behavior.

### 3. Process creation, scheduling, and context switching — Missing for production

Reference: `minix-2.0.0/src/kernel/proc.c`, `main.c`, `table.c`.
CP32: `src/kernel/proc.c`, `proc.h`, `mpx32.S`, `klib32.S`, `main.c`.

Present: descriptors, ready queues, ready/unready/schedule helpers, billing,
and frame-shape probes. Missing: a complete pick/dispatch boundary, real
suspension/resumption, correct task/process classification, quantum switching,
and production ownership of `proc_ptr`/`bill_ptr`.

Next: prove one task-owned handoff and blocked-caller resume, then enable
clock-driven scheduling.

### 4. Kernel IPC — Partially implemented

Reference: `minix-2.0.0/src/kernel/proc.c` (`sys_call`, `mini_send`,
`mini_rec`, `interrupt`, `unhold`, `cp_mess`).
CP32: `src/kernel/proc.c`, `port.c`, `mm.c`.

Present: SEND/RECEIVE/BOTH validation, deadlock checks, queues, interrupt
notification replay, translated copies, and buffer/endpoint rejection.
Missing: suspended wrapper execution and wake/resume through a real context
switch; concurrent task-owned exchanges remain unproven.

Next: implement the handoff contract and test both blocking directions, BOTH,
deadlock, and nested IRQ cases.

### 5. Clock, alarms, and time — Partially implemented

Reference: `minix-2.0.0/src/kernel/clock.c`; CP32: `src/kernel/clock.c`,
`include/esp32s3/systimer.h`.

Present: SYSTIMER setup, 60 Hz accounting, uptime/time/alarm logic, watchdog
and synchronous-alarm structures, and dispatch helpers. Missing: production
`clock_task()` execution, confirmed alarm delivery, `syn_alrm_task()`,
`clock_stop`, and validated quantum switching.
ESP32-S3 uses the 64-bit SYSTIMER and TARGET0 clear rather than PIT latching.

Next: start CLOCK from descriptors, route HARD_INT through IPC, validate alarms
and `milli_delay`, then connect scheduling.

### 6. Memory mapping and copy — Partially implemented

Reference: `minix-2.0.0/src/kernel/system.c` (`umap`, `do_copy`, `do_vcopy`,
`alloc_segments`) and `memory.c`.
CP32: `src/kernel/mm.c`, `mem.c`, `system.c`, `proc.h`.

Present: maps, wide range checks, `numap`, `umap`, physical copy, and basic
system handlers. Missing: a memory inventory/resource allocator, MM task loop,
fork/exec memory setup, user image placement, and complete isolation.
ESP32-S3 needs a flat DRAM/IRAM model instead of x86 segmentation.

Next: define the physical-memory model, implement allocator/map ownership,
and test every map/copy operation.

### 7. System task and signals — Partially implemented

Reference: `minix-2.0.0/src/kernel/system.c`; CP32: `src/kernel/system.c`.

Present: dispatcher and MINIX-shaped fork, map, exec, exit, time, copy,
signal, tracing, and reboot handlers. Missing: active `sys_task()` execution,
MM/FS integration, complete fork/exec/exit semantics, user signal frames, and
hardware reset (`system_reset()` is a placeholder).

Next: run SYS_TASK after IPC handoff, implement user address-space lifecycle,
then validate signals and reset policy.

### 8. TTY and console — Partially implemented

Reference: `minix-2.0.0/src/kernel/tty.c`, `console.c`, `keyboard.c`,
`rs232.c`, `pty.c`, `keymaps/`.
CP32: `src/kernel/tty.c`, `tty.h`, `serial.c`, `serial.h`.

Present: line discipline, termios/ioctl structures, queues, and USB
Serial/JTAG diagnostics. Missing: active `tty_task()`, Cardputer keyboard/
display driver, UART/RS232 device implementation, TTY IRQs, ptys, and user
read/write syscalls. USB Serial/JTAG and Cardputer peripherals replace VGA,
PC keyboard, UART, and BIOS services.

Next: 
1. Add code to access M5Stack Cardputer Adv display and keyboard.
2. connect one console device, start TTY as a task, and validate canonical
and raw I/O.
3. TTY and console is fully usable via M5StackCardputer hardware.

### 9. Storage, RAM disk, and filesystem — Missing

Reference: `minix-2.0.0/src/fs/` plus kernel `driver.c`, `memory.c`, and disk
drivers. CP32 has no `fs/` tree, block driver, RAM disk, VFS, or disk-image
loader.

Missing: FS server, inode/cache/path/file-descriptor operations, block I/O,
root filesystem, and boot-time filesystem population. Add `src/fs/` and a
documented CP32 storage-driver layer. Filesystem logic is portable; flash
partitioning, cache, and wear policy are hardware-specific.

### 10. MM server and user process environment — Missing

Reference: `minix-2.0.0/src/mm/`; CP32 has only kernel-side `mm.c`.
Missing: fork/exec/wait/exit, brk/sbrk, server-level signals, permissions,
and boot-time service initialization. Start after scheduling, IPC, maps, and
an executable format work.

### 11. Networking and optional device drivers — Not applicable to first milestone

Reference: `minix-2.0.0/src/inet/` and network/audio/printer/CD/disk drivers.
CP32 configuration disables these PC-oriented services. Add only after the
Cardputer peripheral target and user ABI are defined.

### 12. C library, shell, commands, and applications — Missing

Reference: `minix-2.0.0/src/lib/`, `commands/`, `test/`, and `boot/`.
CP32 has only `printk.c`, `klib.c`, and compatibility headers. Missing:
syscall stubs, libc, runtime/start files, shell, commands, tests, and image
integration. Define the user ABI after one user process can run.

## Explicitly incomplete code

- `src/kernel/irq.S`: dispatch TODO and bring-up handler stubs.
- `src/kernel/proc.c` and `port.c`: diagnostics and blocked flags exist, but
  no production suspension/resume boundary.
- `src/kernel/mm.c`: `mm_task()` is an initialization message plus idle loop.
- `src/kernel/mem.c`: memory inventory is hard-coded to zero.
- `src/kernel/tty.c`: ESP32-S3 device stubs are disconnected.
- `src/kernel/system.c`: `system_reset()` is a placeholder.
- `src/kernel/main.c`: `panic()` spins without required panic diagnostics.
- The existing libgcc `call0` ABI warning remains unresolved.

## Priority order

1. Complete IRQ dispatch and one real context-switch/handoff path.
2. Make IPC suspension/resumption task-owned; validate queues, billing, and quantum.
3. Start CLOCK, SYS, and TTY through production descriptors.
4. Define the CP32 memory model and implement the MM server.
5. Implement Cardputer console I/O.
6. Add storage/RAM disk, FS, executable loading, libc, shell, and commands.
7. Add optional networking and peripherals only when in scope.

## Validation policy

For each feature, compare MINIX behavior, document the ESP32-S3 substitution
and CP32 invariant, add a focused `tests/` test when feasible, run
`make clean && make` from `src/`, inspect ELF sections/segments for low-level
changes, and record hardware results in `issues.md`. Build success alone never
marks hardware behavior complete.
