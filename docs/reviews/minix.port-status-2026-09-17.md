# CP32 MINIX Port Status

## Summary

As of 2026-09-17, CP32 is a kernel bring-up and architecture-validation port,
not yet a bootable MINIX 2.0.0 system. The source contains substantial MINIX
kernel logic: process descriptors and queues, IPC send/receive paths, clock
accounting, memory translation/copy helpers, system-call dispatch, a line
discipline, and ESP32-S3-specific startup/interrupt code. The current image
also initializes CLOCK, SYS, TTY, MM, and an FS read-client descriptor.

The decisive missing boundary is a production user/process environment: CP32
has no FS server, no MM server equivalent to MINIX's process manager, no
executable loader, no complete user address-space lifecycle, and no libc or
shell. Context handoff and peripheral behavior remain source-level claims
until validated on the ESP32-S3. All eight host-side tests pass, but they do
not establish hardware boot, interrupt return, or sustained task execution.

There is no repository `AGENTS.md` or `issues.md`; the conclusions below use
the source, `plan.md`, the MINIX reference tree, and the available tests.

## Implemented

- **Freestanding ESP32-S3 image structure:** `src/kernel/start.c`,
  `src/kernel/esp32s3.ld`, `src/kernel/vectors.S`, `src/kernel/mpx32.S`.
  Startup clears/initializes runtime state, sets the stack and vector entry,
  disables watchdogs, and links an Xtensa call0 image. This is an ESP32-S3
  replacement for MINIX x86 reset, BIOS, descriptor, and PIC setup; it is not
  equivalent to MINIX booting user servers.

- **Process table and ready queues:** `src/kernel/main.c` and
  `src/kernel/proc.c` implement MINIX-shaped process numbers, descriptors,
  ready/unready operations, billing pointers, blocked flags, and scheduler
  invariants. `cp32_*_check()` routines and `tests/test_irq_handoff_contract.sh`
  cover several structural contracts.

- **Kernel IPC core:** `src/kernel/proc.c` implements `sys_call`, `mini_send`,
  `mini_rec`, `interrupt`, `unhold`, deadlock checking, endpoint validation,
  message copying, and blocked-call bookkeeping corresponding to
  `minix-2.0.0/src/kernel/proc.c`. The implementation is substantially
  complete as an in-kernel algorithm, but its live context boundary is
  separately classified below.

- **Clock model and alarms:** `src/kernel/clock.c` contains MINIX-style
  uptime, alarm, synchronous-alarm, delay, quantum, and clock-task logic. The
  ESP32-S3 SYSTIMER replaces the PC PIT. Source includes target acknowledgement
  and interrupt-matrix setup.

- **Memory translation and physical copy primitives:** `src/kernel/mm.c` and
  `src/kernel/system.c` provide `numap`, `umap`, `mem_copy`, range checks, and
  flat-map handling. `cp32_mem_alloc/free/owned` implements a bounded,
  owner-checked click allocator with coalescing. This preserves MINIX mapping
  and ownership invariants without x86 segmentation.

- **System-task dispatch surface:** `src/kernel/system.c` has handlers for
  mapping, copy, fork/exec/exit-shaped requests, signals, time, tracing, and
  reboot dispatch. These are useful kernel interfaces, not proof that the
  corresponding MM/user semantics exist.

- **TTY line discipline and Cardputer input foundation:** `src/kernel/tty.c`
  retains substantial MINIX canonical/raw input, queues, termios, ioctl, and
  event handling. `src/kernel/cardputer.c` contains TCA8418 keyboard probing,
  initialization, FIFO/status reads, and matrix decoding. USB Serial/JTAG is
  used for diagnostics.

- **RAM-disk primitive:** `src/kernel/ramdisk.c` provides bounded sector and
  byte I/O, reset, checksum, format, and format validation. This is storage
  infrastructure only; it is not a filesystem or MINIX block driver.

- **Host verification:** `make -C src tests` passes
  `test_freestanding_klib`, `test_ipc_blocked_contract`,
  `test_irq_handoff_contract`, `test_klib_runtime`, `test_misc_unit`,
  `test_ramdisk`, `test_repository_layout`, and `test_source_inventory`.

## Partially Implemented

### Reset, boot, and task startup

- CP32: `src/kernel/start.c`, `src/kernel/main.c`, `src/kernel/mpx32.S`,
  `src/kernel/vectors.S`, `src/kernel/esp32s3.ld`.
- Reference: `minix-2.0.0/src/kernel/start.c`, `main.c`, `mpx386.s`,
  `table.c`.
- MINIX initializes the kernel and enters a service/task set with defined
  stacks, registers, maps, and startup messages. CP32 creates descriptors for
  kernel tasks and services and assigns entry points, but the image still
  depends on guarded bring-up handoff and lacks a boot task table, executable
  user image, and complete service initialization. Xtensa saved PS/EPC and
  call0 frames replace x86 register/segment state. Validate reset-to-main,
  first task return, stack ownership, and repeated returns on hardware.

### Interrupts, exceptions, and context return

- CP32: `src/kernel/irq.S`, `src/kernel/vectors.S`, `src/kernel/clock.c`,
  `src/kernel/proc.c`.
- Reference: `minix-2.0.0/src/kernel/mpx386.s`, `i8259.c`, `exception.c`,
  `proc.c:interrupt`.
- CP32 now has a level-1 save/dispatch/restore path, SYSTIMER acknowledgement,
  pending-source dispatch, `rfi/rfe`, and saved-frame publication. Other
  vectors (debug, NMI, double exception, and unhandled vectors) deliberately
  spin; device routing and nested interrupt semantics are incomplete. The
  Xtensa exception CSRs and interrupt matrix are architectural differences,
  not omissions. Hardware must prove that the saved `a1`, EPC, PS/EPS, a15
  call0 frame pointer, and selected owner survive a real IRQ.

### Scheduling and IPC suspension/resumption

- CP32: `src/kernel/proc.c`, `src/kernel/mpx32.S`, `src/kernel/irq.S`.
- Reference: `minix-2.0.0/src/kernel/proc.c`, `main.c`, `table.c`.
- MINIX blocks callers, selects a runnable process, and resumes through the
  assembly switch boundary. CP32 implements queue selection, blocked-frame
  capture, wake results, ownership markers, and a gated handoff, but still has
  bring-up probes, fallback paths, and unresolved scheduler starvation/frame
  initialization concerns recorded in `plan.md` (notably item 5s). A real
  CLOCK-driven switch and blocked SEND/RECEIVE resume require hardware proof.

### Clock task and device notification

- CP32: `src/kernel/clock.c`.
- Reference: `minix-2.0.0/src/kernel/clock.c`.
- The task loop, alarms, quantum bookkeeping, and SYSTIMER bridge exist, but
  sustained task execution, alarm delivery, `clock_stop`, and exact interrupt
  notification behavior are not established on hardware. The SYSTIMER target
  clear and interrupt-matrix source/line mapping are ESP32-S3-specific.

### MM boundary

- CP32: `src/kernel/mm.c`; reference: `minix-2.0.0/src/mm/` and kernel
  `memory.c`, `system.c`.
- CP32 has a private allocator and an `mm_task()` receive/reply loop for two
  private request types. MINIX MM additionally owns fork/exit, exec, brk,
  signals, permissions, process metadata, and executable memory setup. CP32's
  allocator smoke tests prove host-level logic only; the MM IPC protocol and
  user memory isolation remain incomplete.

### TTY and console

- CP32: `src/kernel/tty.c`, `src/kernel/cardputer.c`, `src/kernel/serial.c`,
  `src/kernel/display.c`; reference: `minix-2.0.0/src/kernel/tty.c`,
  `console.c`, `keyboard.c`, `rs232.c`, `pty.c`.
- The line discipline and Cardputer keyboard support are present, but
  `scr_init()` and `rs_init()` assign `tty_devnop`; TTY device read/write,
  UART interrupt delivery, display integration, ptys, and user read/write
  syscalls are not complete. VGA/PC keyboard/UART/BIOS behavior must be
  replaced by documented Cardputer and ESP32-S3 behavior and tested on the
  target.

## Missing

- **Filesystem server and VFS:** `minix-2.0.0/src/fs/` is absent from CP32.
  Inode, path, directory, file descriptor, block cache, root filesystem, and
  block-driver behavior are architecture-independent; flash layout and wear
  policy are hardware-specific. Suggested location: `src/fs/` plus a CP32
  storage driver.

- **Production MM server/user process environment:** `minix-2.0.0/src/mm/`
  has no CP32 counterpart beyond kernel helpers. Missing are process metadata,
  fork/exec/wait/exit, brk/sbrk, permissions, user signal lifecycle, loader,
  and boot-time server protocol. Mostly architecture-independent after a
  defined ESP32-S3 memory model, with loader/cache/isolation details requiring
  hardware documentation.

- **User ABI, libc, runtime, shell, commands, and image population:** the
  reference `src/lib/`, `src/commands/`, `src/boot/`, and `src/test/` content
  is not ported. CP32 currently has compatibility headers, kernel `klib`, and
  `printk`, but no usable user executable ABI or filesystem-backed programs.

- **Most optional MINIX drivers and networking:** `minix-2.0.0/src/inet/` and
  PC disk/audio/printer/CD/network drivers are absent. They are not required
  for the first Cardputer milestone; implement only after the target device
  and user ABI are defined.

## Requires Hardware Validation

- `src/kernel/start.c:CP32`, `main.c:main`: confirm image reset entry, BSS/
  stack safety, watchdog behavior, and no early exception.
- `src/kernel/irq.S:irq_level1`, `irq_user`, `cp32_enter_initial_user`: confirm
  real SYSTIMER/user exception save and restore, EPC/PS, SP, a15, and owner
  selection. Existing `tests/test_irq_handoff_contract.sh` is structural only.
- `src/kernel/clock.c:systimer_irq_start`, `cp32_timer_irq_dispatch`,
  `clock_task`: confirm 60 Hz cadence, target acknowledgement, interrupt
  re-arm, alarm delivery, and quantum rotation.
- `src/kernel/proc.c:mini_send`, `mini_rec`, `sched`: confirm blocked caller
  suspension and wake/resume for SEND, RECEIVE, BOTH, deadlock rejection, and
  no ready-queue insertion of blocked processes.
- `src/kernel/cardputer.c` and `src/kernel/tty.c`: confirm TCA8418 electrical
  access, FIFO draining, matrix decoding, keyboard interrupt behavior,
  canonical/raw input, and display output. No `issues.md` evidence exists.
- `src/kernel/serial.c`, `display.c`, `wdt.c`: confirm USB Serial/JTAG output,
  display register behavior, and watchdog feeding/reset behavior.
- `src/kernel/mm.c:numap`, `mem_copy`, allocator: confirm actual ESP32-S3
  address aliases, permitted SRAM windows, cache effects, and fault behavior;
  host tests cannot establish this.

## Suspicious or Incomplete Code

- `src/kernel/irq.S`: debug, NMI, double-exception, and unhandled vectors spin;
  these are deliberate terminal bring-up handlers, not production recovery.
- `src/kernel/tty.c:scr_init` and `rs_init`: both use `tty_devnop`; device I/O
  is visibly disconnected. There is also a disabled compatibility block at
  `src/kernel/tty.c:#if 0`.
- `src/kernel/system.c:system_reset`: placeholder infinite loop rather than
  ESP32-S3 reset-register or documented reset policy.
- `src/kernel/main.c:panic`: infinite `nop` loop with no complete panic dump.
- `src/kernel/proc.c`: comments and guarded diagnostic state describe a
  compatibility/bring-up handoff path; `schedule()` is explicitly labeled a
  minimal scheduler entry.
- `src/kernel/clock.c`: unresolved TODO near the clock interrupt setup.
- `src/kernel/mm.c:mm_task`: private two-operation protocol only; it is not
  MINIX's full MM service.
- `src/kernel/cp32-shell.c`: an idle/diagnostic loop, not a user shell.
- Build state: `src/Makefile` and `src/kernel/start.c` have pre-existing local
  modifications, and `src/build-debug/` is untracked. They were not changed by
  this review and should not be treated as committed port evidence.

## Recommended Next Steps

1. Establish a repeatable hardware boot log and close the first real
   scheduler/context handoff: one timer IRQ, one task selection, one return,
   and one blocked IPC wake/resume. Resolve the frame-initialization/starvation
   issue before adding more servers.
2. Validate the SYSTIMER interrupt-matrix route and Xtensa `rfi/rfe` frame
   contract on the ESP32-S3, including nested/fatal exception behavior.
3. Define the CP32 physical-memory ownership model and complete the MM server
   protocol, then add a minimal executable/user image loader.
4. Connect one Cardputer console device through TTY, including device read/
   write and interrupt paths, and validate canonical/raw reads.
5. Add a minimal RAM-disk block driver and FS server; only then define the
   user ABI, libc syscall stubs, init process, shell, and commands.
6. Implement reset/panic diagnostics and replace terminal vector spins with a
   documented recovery policy where target requirements justify it.
7. Keep optional networking and PC-specific compatibility out of the critical
   path until the Cardputer kernel/user milestone is stable.
