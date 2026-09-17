# CP32 MINIX Port Status

## Summary

This review compares CP32 with the actual `minix-2.0.0/src` tree. CP32 has
validated ESP32-S3 startup, memory translation, substantial IPC, guarded frame
handoff, SYSTIMER delivery, clock accounting, alarm-state initialization, and
an adapted TTY line discipline. It remains a bring-up kernel, not a usable
MINIX system: normal task/server startup, production user syscall return,
filesystem, executable loading, and Cardputer drivers are incomplete.

## Implemented

- Startup and image layout — CP32 `src/kernel/mpx32.S`, `vectors.S`, `start.c`,
  `esp32s3.ld`; MINIX reference `src/kernel/mpx.s`. CP32 replaces x86 BIOS,
  PIC, and segment assumptions with ESP32-S3 call0 startup and image layout.
- IPC state machinery — CP32 `src/kernel/proc.c`; reference
  `src/kernel/proc.c`. SEND, RECEIVE, BOTH, queue links, deadlock checks,
  source assignment, pointer validation, interrupt notification, and blocked
  frame bookkeeping exist. Hardware V162/V163 pass.
- Ready queues and guarded selection — CP32 `proc.c`; reference `proc.c`.
  Queue classification, stale blocked filtering, idle fallback, owner checks,
  and billing checks are implemented and hardware-tested.
- Memory translation/copy — CP32 `mm.c`, `system.c`; references
  `src/kernel/system.c` and `src/mm/main.c`. Map/range validation and
  translated copies exist; IPC/MM probes pass on hardware.
- SYSTIMER and clock accounting — CP32 `clock.c`, `irq.S`; references
  `src/kernel/clock.c`, `mpx.s`. UNIT0/TARGET0 routing, periodic IRQs,
  pending/lost tick accumulation, and process charging are validated.
- Alarm foundation — CP32 `clock.c`; reference `src/kernel/clock.c`.
  Expiry counting, nearest-deadline recomputation, and `LONG_MAX` unarmed
  initialization are present. Hardware confirmed zero expiries and
  `next=2147483647` through IRQ 160.
- Lock primitives — CP32 `mpx32.S`, `proc.c`; reference `proc.c` lock paths.
  LOCK V1/V4 pass on hardware.
- TTY line-discipline base — CP32 `tty.c`; reference `src/kernel/tty.c`.
  Queues, termios, input/output processing, and timeout hooks are largely
  adapted.
- Panic/invariant diagnostics — CP32 `misc.c`, `main.c`, `irq.S`.

## Partially Implemented

### Process scheduling and context switching

Reference: `minix-2.0.0/src/kernel/proc.c` (`sched`, `pick_proc`, `ready`,
`unready`) and `mpx.s`. CP32 has queue primitives, guarded selection, saved
frames, and synthetic process-2 handoff; V162–V164 pass in the reported runs.
The production return gate remains guarded and `main()` owns the diagnostic
lifecycle. Xtensa call0 registers, PS/PC semantics, and `rfi/rfe` require a
CP32-specific frame ABI rather than an x86 copy.

### IPC blocking and syscall entry

Reference: `src/kernel/proc.c` and `mpx.s`. CP32 `sys_call`, `mini_send`, and
`mini_rec` model blocked state and the guarded SENDREC probe wakes a blocked
sender. Normal user exception entry and fully productionized suspension and
resumption remain incomplete.

### Clock task, alarms, TTY timers, and quantum

Reference: `src/kernel/clock.c`. CP32 `clock_task`, `clock_handler`, and
`do_clocktick` contain the adapted logic, but the clock task is not started as
a normal server. Hardware proves the SYSTIMER bridge/accounting and unarmed
alarm state, not the complete message-driven clock task or callbacks.

### System/MM and TTY devices

References: `src/kernel/system.c`, `src/mm/main.c`, and `src/kernel/tty.c`.
CP32 has adapted handlers and line discipline, but task startup, privilege
descriptors, real address-space lifecycle, and Cardputer device backends are
absent. ESP32-S3 flat memory/MMIO replaces x86 physical/segmented behavior.

## Missing

- Normal task/server startup: reference `src/kernel/main.c`, `table.c`; CP32
  `main.c` remains a diagnostic harness.
- Production user trap/syscall return: reference `src/kernel/mpx.s`; CP32
  `irq.S` retains guarded probe and terminal fallback paths.
- Full process lifecycle and user ABI: reference `src/mm/main.c` and
  `src/commands`; fork, exec, exit, wait, signals, and active syscall library
  support are not complete.
- Filesystem and block storage: reference `src/fs`, `src/fs/table.c`, and
  block drivers; no CP32 cache, inode, directory, or persistent-medium path.
- Cardputer GPIO, keyboard, display, I2C, SPI, power, and storage drivers.
- Complete normal MM/system/clock/TTY task message lifecycles.

## Requires Hardware Validation

- `src/kernel/irq.S`/`proc.c`: real user trap entry, production blocked syscall
  suspension, frame restore, and resumed user execution.
- `src/kernel/clock.c`: active clock task, alarms/callbacks, lost ticks under
  load, TTY timers, and quantum scheduling.
- `src/kernel/system.c`/`mm.c`: real maps, privilege boundaries, translated
  copies, and fork/exec address-space changes.
- `src/kernel/tty.c` and future board drivers: keyboard, display, terminal
  queues, and malformed-device recovery.
- `milli_elapsed`: rollover and elapsed-time accuracy.

Existing hardware evidence confirms IPC/MM, lock nesting, guarded handoff and
wake, SYSTIMER delivery, monotonic tick accounting, and unarmed alarm state.
It does not confirm a usable MINIX boot or production user-space execution.

## Suspicious or Incomplete Code

- `src/kernel/irq.S`: level-3 dispatch is TODO; unhandled/debug/NMI/double
  exception paths remain terminal bring-up behavior.
- `src/kernel/proc.c`: probe-specific handoff paths are extensive and
  production blocked return remains gated.
- `src/kernel/main.c`: diagnostic initialization and idle loop replace normal
  MINIX task activation.
- `src/kernel/port.c`: syscall wrappers are transitional glue.
- `src/kernel/clock.c`: IRQ registration remains commented/TODO and the clock
  task is not active.
- `src/kernel/tty.c`: ESP32-S3 device operations remain minimal stubs.
- `src/kernel/system.c`/`mm.c`: operations depend on missing task/MM/server
  integration.

## Recommended Next Steps

1. Activate one real kernel task, preferably `clock_task`, with explicit
   descriptors, stacks, frames, and queue ownership.
2. Connect clock messages and validate alarms, TTY timers, lost ticks, and
   quantum scheduling as one complete feature slice.
3. Complete production Xtensa user trap/syscall return.
4. Bring up MM/system lifecycle and load one minimal user program.
5. Add Cardputer console/input/display drivers and connect them to TTY.
6. Add storage and a read-only filesystem path after user return is stable.
