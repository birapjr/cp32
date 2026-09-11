# CP32 MINIX 2.0 Port Status

## Summary

CP32 is a working ESP32-S3 bare-metal bring-up image, not yet a usable MINIX
system. Startup, memory checks, USB diagnostics, SYSTIMER IRQ delivery, IPC
state transitions, memory translation checks, lock preservation, and guarded
scheduler/frame probes are implemented and hardware-validated. The major
missing boundary is normal user/trap execution and true suspension/resumption
of SEND, RECEIVE, and SENDREC. The current image ends in a diagnostic idle
loop.

The comparison reference is `minix-2.0.0/src/kernel` and the corresponding
MINIX MM/FS source. Results below distinguish source evidence from hardware
evidence.

## Implemented

- Startup and linker integration — `src/kernel/mpx32.S`, `vectors.S`,
  `esp32s3.ld`, `start.c`. CP32 initializes `.bss`, vectors, call0 stack, and
  enters C using the ESP32-S3 image layout. This replaces the PC-specific
  MINIX `src/kernel/mpx.s` startup and BIOS/PIC assumptions.
- Direct diagnostic console and watchdog control — `serial.c`, `wdt.c`.
  USB Serial/JTAG and direct MMIO watchdog handling are architecture-specific
  replacements for MINIX console/device paths.
- Memory translation/copy primitives — `mm.c`, `system.c`. `numap`-style map
  validation, range checks, translated copies, and map operations exist.
  Hardware IPC/MM tests pass, but a complete MINIX MM server lifecycle does
  not yet exist.
- Core IPC state transitions — `proc.c`, compared with MINIX
  `src/kernel/proc.c`. `mini_send`, `mini_rec`, deadlock checks, sender/receiver
  links, interrupt notification, held notification replay, buffer validation,
  and message-source assignment are present. Hardware markers IPC V9–V21 and
  MM V9/V11/V12 pass.
- Ready queues and guarded selection — `proc.c`, compared with MINIX
  `pick_proc`, `ready`, and `unready`. Queue classification, stale blocked
  entry filtering, idle fallback, ownership checks, and billing checks are
  implemented. These are validated probes, not proof of a complete MINIX
  scheduler.
- SYSTIMER interrupt bridge — `clock.c`, `irq.S`, compared with MINIX
  `clock.c` and `mpx.s`. UNIT0/TARGET0 routing and a 64-byte CP32 IRQ frame are
  implemented. Hardware reaches IRQ 240-class runs with valid SP/A15 and no
  reported exception.
- Saved interrupt-mask primitives — `mpx32.S`, `proc.c`. `lock_save` and
  `restore_lock` preserve the caller PS around selected critical sections;
  LOCK V4 passes on hardware.
- Panic and invariant diagnostics — `main.c`, `misc.c`. Panic masks interrupts,
  emits a versioned marker, and halts rather than silently returning.
- TTY source base — `tty.c`. Much of the MINIX line discipline and terminal
  state machine is present, but the actual Cardputer input/display backend is
  not connected.

## Partially Implemented

### Process scheduling and context switching

- CP32: `src/kernel/proc.c`, `src/kernel/irq.S`, `src/kernel/clock.c`.
- MINIX reference: `minix-2.0.0/src/kernel/proc.c`, `mpx.s`, `clock.c`.
- MINIX selects runnable processes, saves/restores process registers, updates
  billing and quantum state, and returns from interrupts into the selected
  process.
- CP32 has queue operations, a guarded timer handoff, saved interrupted-owner
  state, and a diagnostic restore path. `switch_to()` is still a C diagnostic
  boundary and the normal process/task lifecycle is absent.
- Xtensa call0 register layout, PS/PC semantics, level-1 interrupt entry, and
  `rfi` differ from x86 and require the CP32 frame contract to remain explicit.
- Remaining: prove a real process handoff, quantum accounting, task/server
  initialization, and return into a process that executed outside the boot
  diagnostic loop.

### IPC syscall entry and blocking

- CP32: `src/kernel/port.c`, `proc.c`, `irq.S`.
- MINIX reference: `minix-2.0.0/src/kernel/proc.c`, `mpx.s`, and syscall
  library entry paths.
- MINIX blocks the caller without returning until a matching message or error
  is available, then resumes with the correct return state.
- CP32 routes `_send`, `_receive`, and `_sendrec` through `sys_call` and marks
  `SENDING`/`RECEIVING`, but the wrappers can return while those flags remain
  set. The blocked-return gate is deliberately guarded.
- ESP32-S3 needs a documented trap/interrupt frame and return-register ABI;
  x86 register/stack assumptions cannot be copied directly.
- Remaining: user/trap entry, saved syscall return frame, wake/resume path,
  and a task-owned two-process hardware exchange.

### Clock task and time accounting

- CP32: `src/kernel/clock.c`.
- MINIX reference: `minix-2.0.0/src/kernel/clock.c`.
- MINIX clock task processes HARD_INT and time/alarm messages, updates ticks,
  alarms, TTY timers, and scheduling quantum.
- CP32 has SYSTIMER reads, bridge dispatch, timer probes, and adapted clock
  routines, but `clock_task()` is not running as a normal MINIX task and the
  full message lifecycle is not active.
- SYSTIMER is a 52-bit/extended counter with direct MMIO and Xtensa IRQ routing,
  unlike the PIT/8259 path in the reference.
- Remaining: activate the clock task, validate lost ticks/alarms/quantum, and
  connect normal clock messages.

### System task and MM operations

- CP32: `src/kernel/system.c`, `mm.c`.
- MINIX reference: `minix-2.0.0/src/kernel/system.c` and `src/mm/main.c`.
- Several system-task handlers, map operations, copying, signals, and boot
  interfaces are present in MINIX-style form.
- CP32 contains adapted handlers and direct map/copy support, but no complete
  server startup, privilege model, user address-space lifecycle, or normal
  trap-driven syscall path.
- ESP32-S3 flat-memory/MMIO layout replaces segmentation, 8259 state, and PC
  physical-memory assumptions.
- Remaining: initialize descriptors and privileges, run MM/system tasks, and
  validate all pointer and address-space transitions on hardware.

### TTY and console

- CP32: `src/kernel/tty.c`, `serial.c`.
- MINIX reference: `minix-2.0.0/src/kernel/tty.c` and device drivers.
- The line discipline, terminal queues, termios behavior, and TTY message
  structure are largely inherited/adapted.
- USB Serial/JTAG is a working recovery/debug channel; Cardputer keyboard and
  display devices are not integrated into the TTY task.
- The board requires direct GPIO/SPI/I2C/MMIO work rather than PC UART/VGA
  drivers.
- Remaining: board input/output drivers, actual TTY task lifecycle, and
  hardware validation of terminal behavior.

## Missing

- User exception/trap syscall entry and return frame — reference
  `minix-2.0.0/src/kernel/mpx.s`; expected CP32 location `src/kernel/irq.S`
  and a new documented frame contract. Architecture-independent behavior is
  required, but entry/return assembly is ESP32-S3-specific.
- Complete task/server initialization and boot sequence — reference
  `minix-2.0.0/src/kernel/main.c` and `table.c`; expected in `main.c` plus
  process/table support. The current main path remains a diagnostic harness.
- Real MINIX system, clock, MM, and TTY task loops — references their MINIX
  `main.c` files. The current tree has functions but not a complete running
  server lifecycle.
- Filesystem and block device — reference `minix-2.0.0/src/fs`, `src/fs/table.c`
  and block drivers. No CP32 persistent-medium, cache, inode, directory, or
  file-operation implementation is present.
- Process loading and user space — reference MINIX exec/process setup and
  `minix-2.0.0/src/commands`. No CP32 executable loader, user address-space
  launcher, shell, or applications are present.
- Cardputer keyboard/display/storage drivers — hardware-specific and absent.
- Full signals, pipes, process lifecycle, wait/exit, and user syscall library
  — absent from the current CP32 tree.

## Requires Hardware Validation

- `src/kernel/irq.S`: confirm the saved-owner fallback and gated `proc_ptr`
  restore over long IRQ runs; current evidence reaches IRQ 240-class samples,
  but does not prove a real user/process return.
- `src/kernel/proc.c`: validate concurrent task-owned IPC, true blocked caller
  suspension, wake, and resumed return values. Existing IPC V18–V21 output
  validates state probes, not suspended execution.
- `src/kernel/clock.c`: validate real tick accounting, alarms, lost ticks,
  quantum expiry, and re-entry under an active clock task.
- `src/kernel/tty.c` and board drivers: validate keyboard events, display
  output, terminal queues, and recovery from malformed device operations.
- `src/kernel/system.c`/`mm.c`: validate translated copies, maps, privilege
  boundaries, and user address spaces with actual running tasks.
- `src/kernel/mpx32.S` and `irq.S`: validate nested interrupts, exception
  entry, trap return, and all call0 register preservation.
- `src/kernel/clock.c:milli_elapsed`: the freestanding 64-bit divider now
  builds without the incompatible libgcc ABI warning; a hardware time-delay
  test should still confirm rollover and elapsed-millisecond accuracy.

Existing hardware evidence in `issues.md` supports the IPC/MM, LOCK V4,
SCHED V1/V4/V6/V7/V8/V9, CTX V46/V47/V48/V49/V50/V51, and IRQ stability
claims. It does not establish a usable MINIX boot or user-process execution.

## Suspicious or Incomplete Code

- `src/kernel/port.c`: syscall wrappers are still transitional glue.
- `src/kernel/irq.S`: several vectors and general exception paths remain
  bring-up stubs; the complete trap policy is absent.
- `src/kernel/proc.c`: scheduler and IPC probes are extensive, but the real
  blocked syscall return contract is not implemented.
- `src/kernel/main.c`: ends in diagnostic idle/probe loops rather than normal
  MINIX task initialization.
- `src/kernel/clock.c`: TODO remains around hardware IRQ registration; the
  adapted clock task is not the active server lifecycle.
- `src/kernel/tty.c`: contains explicit minimal ESP32-S3 driver stubs.
- `src/kernel/system.c`: several paths depend on unfinished process/MM/task
  integration and contain terminal or placeholder behavior.
- Board-specific display, keyboard, storage, filesystem, loader, shell, and
  application code is absent.

## Recommended Next Steps

1. Define the CP32 call0 user/trap and syscall return-frame layout, including
   the return register and ownership rules.
2. Implement one real task-owned blocked IPC exchange and validate save,
   wake, restore, and resumed return on hardware.
3. Complete process/task initialization and activate one real kernel task.
4. Activate the clock task and validate ticks, alarms, TTY timers, and quantum
   scheduling.
5. Complete user trap/syscall support, then process loading and one user
   program.
6. Add Cardputer keyboard/display support and connect it to TTY.
7. Add a validated block device, read-only filesystem path, and shell only
   after the kernel/user return path is stable.
