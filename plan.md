# CP32 implementation plan

## Current state — 2026-09-10

Latest hardware runs pass IPC/MM, lock, scheduler, and context probes. The
live IRQ/context loop is stable through the latest reported IRQ samples, with
`CTX V46` showing aligned stacks, preserved `a15`, `gate=1`, `hg=1`, and zero
owner-mismatch and blocked-target rejections.

Validated marker families retained in the current image:

- `IPC V18/V19/V20/V21`: blocked-state accounting and ownership reset.
- `SCHED V1/V2/V4/V6/V7/V8/V9`: classification, baseline handoff,
  blocked-owner protection, stale-ready filtering, and all-queue idle fallback.
- `CTX V46/V47/V48/V49/V50/V51`: frame shape, gate readiness, owner alignment,
  reset state, and rejected-handoff counters.
- `LOCK V1/V4`, `IPC V9–V17`, and `MM V9/V11/V12` remain passing.

The current image safely selects validated runnable frames. It does not yet
resume a blocked SEND/RECEIVE syscall: wrappers still return after marking the
process `SENDING` or `RECEIVING`, and the blocked-return gate remains guarded.

## Immediate next task

- [x] Defined and documented the CP32 syscall return-frame contract in
      `src/kernel/irq_frame.h`: saved `pc/sp/psw`, call0 result in `a2`, and
      owner preservation across a blocked SEND/RECEIVE.
- [x] Added `[CTX V52 syscall-frame pass=1]` to expose the contract layout
      checks in the boot diagnostics.
- [x] Added blocked-frame validity/result bookkeeping and `[CTX V53
      blocked-frame-state pass=1]`; the experimental return gate remains off.
- [x] Finalized the saved `a2` result slot on both sender and receiver wakeup
      paths and added `[CTX V54 wake-result-slot pass=1]`.
- [x] Centralized sender/receiver wake completion and added `[CTX V55
      wake-contract pass=1]` for consistent saved-result bookkeeping.
- [x] Added blocked-frame `pc/psw/sp` snapshots and `[CTX V56
      frame-snapshot pass=1]`; restoration remains gated.
- [ ] Implement blocked SEND/RECEIVE/SENDREC suspension and resumption using
      that contract; do not enable the blocked-return gate until the frame is
      saved and restored end to end.
- [x] Use the task-owned `p1`/`p2` exchange for blocked sender/receiver
      transitions, verifying message data, return values, and queues.
- [x] Extended that task-owned exchange with blocked-frame
      snapshot and wake-clear assertions (`[IPC V22 blocked-frame-wake]`).
- [x] Guarded wakeup with saved `pc/psw/sp` preservation checking and added
      `[CTX V57 frame-preservation-mismatch count=0]`.
- [x] Centralized blocked-frame restore preconditions and added `[CTX V58
      restore-guard pass=1]`; the handoff gate remains disabled pending trap
      entry and end-to-end return validation.
- [x] Added a distinct user/trap frame contract and `[CTX V59
      user-frame-contract pass=1]`; trap entry wiring remains pending.
- [ ] Add hardware markers for blocked-frame save, wake, restore, and resumed
      syscall return.

## Remaining kernel work

- [ ] Complete Xtensa user exception/trap entry and separate user-frame layout
      from the level-1 interrupt frame.
- [ ] Route SEND, RECEIVE, and BOTH from the trap frame through `sys_call`,
      copy results back to the caller frame, and validate privilege/cause.
- [ ] Complete MINIX interrupt delivery, held-interrupt replay, lock nesting,
      and nested-entry policy against the reference `proc.c` behavior.
- [ ] Port clock tick accounting, lost ticks, alarms, TTY timers, quantum
      expiration, and deferred rescheduling onto ESP32-S3 SYSTIMER.
- [ ] Replace simplified process setup with explicit task/server descriptors,
      stacks, initial frames, maps, and privilege state.
- [ ] Implement a panic/fatal path that preserves a short diagnostic marker.
- [ ] Start real kernel tasks incrementally: system, clock, and TTY.

## Hardware abstraction

- [ ] Add Cardputer Adv board configuration: GPIO, I2C, SPI, USB Serial/JTAG,
      display, keyboard, battery/power, and storage wiring.
- [ ] Implement and validate GPIO, I2C, SPI, console, and timeout primitives.
- [ ] Implement keyboard input and prove raw events, modifiers, repeat, and
      interrupt/polling behavior.
- [ ] Implement a bounded display/framebuffer text path and connect keyboard
      and display to the MINIX TTY line discipline.

## Storage, filesystem, and process loading

- [ ] Select and validate the first persistent medium with read-only tests.
- [ ] Port the block-device interface and cache after stable bounded reads.
- [ ] Port filesystem essentials: superblock, inode lookup, directories,
      open/close, read, then write/create/unlink.
- [ ] Implement executable loading and CP32 user address-space setup with
      bounds, alignment, permissions, stack, and initial PC validation.

## User space and milestone

- [ ] Add syscall/library support for TTY, process lifecycle, memory, files,
      and time.
- [ ] Boot one statically linked user program through the complete trap path.
- [ ] Add a bounded diagnostic shell and incremental utilities.
- [ ] Reach the first usable milestone: shell input/output, one readable file,
      two user processes that block/resume on IPC or TTY, and a stress run
      without exceptions or queue leaks.

## Build status

The image builds and flashes with the kernel's call0 ABI and no known linker
ABI warning. The 64-bit timer conversion uses a freestanding divider instead
of importing the incompatible libgcc `__udivdi3` routine.
