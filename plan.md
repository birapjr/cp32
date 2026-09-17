# CP32 implementation plan

This plan is validated against the current CP32 tree and `minix-2.0.0`.
“Implemented” means present in source; it does not imply a successful
hardware run. The MINIX reference supplies behavior, while Xtensa and
ESP32-S3 replacements are valid when they preserve that behavior.

## Current boundary

CP32 is a bare-metal, kernel-only port. `src/Makefile` builds 18 C sources
plus four Xtensa assembly units. The tree has no `src/mm/`, `src/fs/`, user
image, libc/syscall ABI, or application tree. `main()` initializes descriptors
and enters diagnostic/idle behavior; production context transfer remains
gated and must be hardware-validated.

## Validated status and remaining work

### 1. Reset, image, and boot — Partially implemented

Reference: `minix-2.0.0/src/kernel/start.c`, `main.c`, `table.c`, `mpx386.s`.
CP32: `src/kernel/start.c`, `mpx32.S`, `vectors.S`, `main.c`, `esp32s3.ld`.

Present: Xtensa entry, BSS/stack setup, boot parameters, watchdog handling,
descriptor initialization for CLOCK/SYS/TTY/MM, linker image, and initial
ready-queue setup. Task entry points are present, but there is no user-image
loader or proven transition from boot diagnostics to task-owned execution.
ESP32-S3 replaces BIOS, protected mode, PIC, and PIT with loader segments,
Xtensa vectors, direct registers, and SYSTIMER.

Next: enable one descriptor task at a time and validate restored PC/SP/PS and
stack ownership on hardware.

### 2. Interrupt and exception dispatch — Partially implemented

Reference: `minix-2.0.0/src/kernel/mpx386.s`, `i8259.c`, `exception.c`,
`proc.c:interrupt()`; CP32: `vectors.S`, `irq.S`, `irq_frame.h`, `proc.c`.

Present: Xtensa vector/exception entry, 64-byte frame checks, SYSTIMER setup,
pending/coalesced notification accounting, and source-level IRQ helpers.
Missing: complete assembly dispatch and production selected-frame return;
`irq.S` still contains `TODO: dispatch`. Nested IRQ and fatal exception
behavior remain hardware evidence items.

Next: complete one IRQ-to-task return path, then test nested IRQs and panic.

### 3. Scheduling and context switching — Partially implemented

Reference: `minix-2.0.0/src/kernel/proc.c`, `main.c`, `table.c`.
CP32: `proc.c`, `proc.h`, `mpx32.S`, `klib32.S`, `main.c`.

Present: descriptors, ready/unready queues, class-aware selection, billing,
blocked-frame bookkeeping, and contract tests. Missing: ungated production
handoff, full suspension/resumption under real execution, quantum switching,
and proven `proc_ptr`/`bill_ptr` ownership. `schedule()` is explicitly a
minimal bring-up entry, not the MINIX boot scheduler.

Next: prove task-owned handoff and blocked-caller wake/resume, then enable
clock preemption.

### 4. Kernel IPC — Partially implemented

Reference: `minix-2.0.0/src/kernel/proc.c` (`sys_call`, `mini_send`, `mini_rec`,
`interrupt`, `unhold`, `cp_mess`). CP32: `proc.c`, `port.c`, `mm.c`.

Present: SEND/RECEIVE/BOTH validation, deadlock checks, sender queues,
interrupt notifications, translated copies, endpoint/pointer rejection, and
host-side blocked-contract tests. Missing: real context-switch handoff for
blocked calls and concurrent task/server exchanges proven on hardware.

Next: exercise both blocking directions, BOTH, deadlock, invalid endpoints,
and deferred IRQ notification after handoff is live.

### 5. Clock, alarms, and time — Partially implemented

Reference: `minix-2.0.0/src/kernel/clock.c`; CP32: `clock.c`,
`include/esp32s3/systimer.h`.

Present: 60-Hz SYSTIMER setup, tick accounting, uptime/time/alarm logic,
watchdog and synchronous-alarm structures, `clock_task()`, `syn_alrm_task()`,
and dispatch helpers. Missing: task-context execution through live IPC,
confirmed alarm delivery, `clock_stop` behavior, and quantum switching.
`clock.c` retains an architecture TODO.

Next: run CLOCK as a scheduled task, route HARD_INT through IPC, validate
alarms and `milli_delay`, then connect preemption.

### 6. Memory mapping and copy — Partially implemented

Reference: `minix-2.0.0/src/kernel/system.c` (`umap`, `numap`, `do_copy`,
`do_vcopy`, `alloc_segments`) and `memory.c`; CP32: `mm.c`, `system.c`,
`proc.h`.

Present: flat-address checks, `umap`/`numap`, physical copy, bounded memory
block allocation/free, and system handlers. Missing: complete inventory and
ownership model, MM integration, fork/exec image setup, user placement, and
isolation proof. ESP32-S3 has no x86 segmentation.

Next: define allocator regions and ownership, then test every map/copy case.

### 7. System task and signals — Partially implemented

Reference: `minix-2.0.0/src/kernel/system.c`; CP32: `system.c`.

Present: `sys_task()` loop and MINIX-shaped fork/map/exec/exit, time/copy,
signal/tracing/reboot handlers. Missing: live SYS_TASK execution, MM/FS
integration, complete user lifecycle and signal frames, and real reset;
`system_reset()` is an intentional infinite-loop placeholder.

Next: run SYS_TASK through IPC, define user lifecycle, implement documented
reset registers, then validate signals.

### 8. TTY and Cardputer console — Partially implemented

Reference: `minix-2.0.0/src/kernel/tty.c`, `console.c`, `keyboard.c`,
`rs232.c`, `pty.c`, `keymaps/`; CP32: `tty.c`, `serial.c`, `cardputer.c`,
`display.c`.

Present: MINIX line discipline, termios/ioctl support, Cardputer I2C keyboard
polling/decode, ST7789 display output, and USB diagnostics. Missing:
device-backed `tty_task()` I/O, keyboard/display IRQ integration, UART/RS232,
ptys, and user read/write syscalls. `scr_init()` and `rs_init()` install
`tty_devnop`.

Next: connect one Cardputer console device, start TTY as a task, and validate
canonical/raw input and display output on hardware.

### 9. RAM disk and filesystem — RAM disk partial; filesystem missing

Reference: `minix-2.0.0/src/fs/`, kernel `driver.c`, `memory.c`, and disk
drivers. CP32: `ramdisk.c/.h`.

Present: bounded sector read/write, format/reset, checksum, and host tests.
Missing: `src/fs/`, inode/cache/path/file-descriptor operations, block-driver
protocol, root filesystem, image loader, and boot population.

Next: define the block-device message ABI, then add the smallest FS server
over the tested RAM disk.

### 10. MM server and user process environment — Missing

Reference: `minix-2.0.0/src/mm/`; CP32 has kernel-side `mm.c` and `mm_task()`
but no MM server implementation. Missing: fork/exec/wait/exit server logic,
brk/sbrk, permissions, user signal delivery, executable format, and service
initialization.

### 11. Networking and optional PC drivers — Not applicable to first milestone

Reference: `minix-2.0.0/src/inet/` and optional network/audio/printer/CD/disk
drivers. These are not first-milestone blockers; revisit after user ABI and
storage exist.

### 12. C library, shell, commands, and applications — Missing

Reference: `minix-2.0.0/src/lib/`, `commands/`, `test/`, `boot/`. CP32 has
freestanding helpers and `cp32-shell.c`, but that is a kernel diagnostic loop,
not a user shell. Missing: user ABI/syscall stubs, libc, crt/start files,
real shell, commands, user tests, and image integration.

## Verification record

`make -C src tests` passes all current host-side tests. No `issues.md` or
hardware-validation result exists, so all hardware-dependent items remain
unverified. Rerun the firmware build and inspect ELF placement when the
Xtensa toolchain is available.
