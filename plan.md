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
CP32: `src/kernel/mm.c`, `system.c`, `proc.h`.

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
- `src/kernel/tty.c`: ESP32-S3 device stubs are disconnected.
- `src/kernel/system.c`: `system_reset()` is a placeholder.
- `src/kernel/main.c`: `panic()` spins without required panic diagnostics.
- The existing libgcc `call0` ABI warning remains unresolved.

## Priority order

- [x] 1. Complete IRQ dispatch and one real context-switch/handoff path.
- [ ] 2. Make IPC suspension/resumption task-owned; validate queues, billing, and quantum.
  Basic CLOCK receive blocking is running; blocked SEND/RECEIVE resume coverage is still pending.
- [x] 3. Start CLOCK, SYS, and TTY through production descriptors.
  MM descriptor startup is also enabled; the MM message protocol remains incomplete.
- [x] 4a. Define the CP32 physical-click memory model and allocator ownership
  boundary; the MM server protocol remains in progress.
4. Define the CP32 memory model and implement the MM server.
5. Implement Cardputer console I/O.
- [x] 5a. Connect the Cardputer TCA8418 keyboard to the TTY input queue and
  decode matrix events into console characters.
- [x] 5b. Add a bounded user-read handoff probe and boot-time FS descriptor
  validation for the TTY read path.
- [x] 5c. Add scheduler selection tracing for the first runnable FS/user-read
  frame to isolate the remaining restore boundary.
- [x] 5d. Normalize the initial Xtensa saved PSW used by descriptor-based
  task/server returns.
- [x] 5e. Trace the selected process frame immediately before the Xtensa
  exception-return boundary.
- [x] 5f. Trace both scheduler owner pointers at the IRQ return boundary.
- [x] 5g. Keep scheduler selection and active return ownership synchronized.
- [x] 5h. Perform a final non-nested scheduler selection before IRQ return.
- [x] 5i. Preserve a runnable FS selection across the legacy clock/unhold path.
- [x] 5j. Publish and consume one final IRQ return-frame pointer across C and
  Xtensa assembly restoration.
- [x] 5k. Trace final return-pointer publication before assembly restoration.
- [x] 5l. Publish the IRQ return frame at process selection and preserve it
  through the timer wrapper.
- [x] 5m. Correlate scheduler selection and IRQ return with a shared sequence.
- [x] 5n. Report and consume the dedicated published return pointer rather
  than the later idle fallback pointer.
- [x] 5o. Trace return-pointer replacement and the FS state causing it.
- [x] 5p. Correlate FS selection with interrupt nesting to separate scheduler
  passes.
- [x] 5q. Gate return-frame publication on explicit IRQ-dispatch ownership.
- [x] 5r. Reconcile the final runnable selection inside the IRQ dispatch pass.
- [ ] 5s. Rotate scheduler queue priority to prevent task starvation (blocked
  by task-frame initialization fault).
- [x] 5t. Harden `numap()` against invalid or inconsistent process descriptors.
- [x] 5u. Isolate IPC blocked-frame capture for suspension/resumption.
- [x] 5v. Rate-limit repetitive CLOCK task execution diagnostics.
- [x] 5w. Initialize explicit flat task descriptor mappings for MM/IPC buffers.
- [x] 5x. Isolate MM IPC request validation before allocator/release handling.
- [x] 5y. Add an automatic MM allocator allocate/release smoke path.
- [x] 5z. Verify allocator ownership enforcement during automatic boot smoke.
- [x] 5aa. Validate allocator zero-size and oversized request rejection.
- [x] 5ab. Validate adjacent allocator blocks and reverse-order coalescing.
- [x] 5ac. Validate allocator ownership while allocated and after release.
- [x] 5ad. Add frame-safe MM requester validation at the receive boundary.
- [x] 5ae. Centralize MM allocation/release request handling for IPC replies.
- [ ] 5af. Connect the scheduled FS client to the MM allocation/release IPC path (deferred: the first activation caused an image-integrity regression; requires an isolated frame-safe client path).
- [x] 5ah. Make blocked IPC completion task-owned: wake result, blocked flags,
  stale return-owner cleanup, and ready-queue insertion now share one resume
  boundary for SEND and RECEIVE wakeups.
- [x] 5ai. Guard ready-queue insertion against duplicate runnable links during
  repeated IPC wakeups and interrupt replay using the existing queue scan.
- [ ] 5aj. Bound IPC caller-queue append and receive traversal (deferred after
  image-integrity regression; requires an assembly/layout-safe implementation).
- [x] 5ak. Reject duplicate SEND attempts from an already blocked sender so
  each suspended process retains one owned caller-queue link and wakeup frame.
- [ ] 5al. Reject duplicate RECEIVE attempts from an already blocked receiver
  (deferred after early image-integrity regression; requires an
  assembly/layout-safe implementation).
- [x] 5am. Confirm marker 89 hardware checkpoint: valid `.data`/`.bss`, stable
  IRQ handoff, CLOCK task execution, and continued RFE returns.
- [x] 5an. Confirm marker 90 hardware checkpoint: stable IRQ dispatch and
  repeated task handoffs after IPC queue hardening rollbacks.
- [x] 5ao. Confirm marker 91 hardware checkpoint: clean image sentinel and
  stable repeated RFE/IPC/CLOCK handoffs with marker-only rebuild.
- [x] 5ap. Add a host-side blocked-RECEIVE ownership contract test while the
  hardware frame-boundary implementation remains deferred.
- [x] 5aq. Extend the host-side IPC ownership contract coverage to blocked
  SEND completion and mixed SEND/RECEIVE flag cleanup.
- [x] 5ar. Add host-side bounded caller-queue traversal coverage for valid
  chains and cycle/limit rejection.
- [x] 5as. Add host-side IPC endpoint validation coverage for accepted and
  rejected source/destination ranges.
- [x] 6a. Add a link-isolated CP32 RAM-disk sector core with bounds-checked
  read/write/reset operations and host-side tests.
- [x] 6b. Confirm marker 97 hardware checkpoint: RAM-disk code addition leaves
  the boot sentinel, IRQ dispatch, IPC, CLOCK, and RFE handoffs stable.
- [x] 6c. Reserve RAM-disk storage inside DRAM before the heap, expose linker
  bounds, start allocation after it, and activate reset without an ELF data
  segment or static C storage.
- [ ] 6e. Expand the contiguous RAM-disk reservation to the full 128 KiB
  reserved DRAM window (deferred: expanded layout corrupts `.data`).
- [x] 6g. Implement the RAM-disk as a permanent ordinary kernel `.bss` array
  in the proven SRAM1 model, accessed through sector interfaces and reset once
  at boot.
- [x] 6f. Remove the accidental remaining RAM-disk boot write and restore a
  marker-only baseline after marker 110 still corrupted `.data`.
- [ ] 5ag. Rotate ready-queue selection across task, server, and user classes
  (reverted after the FS handoff fault; requires a frame-safe scheduler
  handoff redesign).
- [x] 5ah. Synchronize the kernel IPC wrapper owner from `current_proc` before
  user/task `_send`, `_receive`, and `_sendrec` calls.
6. Add storage/RAM disk, FS, executable loading, libc, shell, and commands.
7. Add optional networking and peripherals only when in scope.

## Validation policy

For each feature, compare MINIX behavior, document the ESP32-S3 substitution
and CP32 invariant, add a focused `tests/` test when feasible, run
`make clean && make` from `src/`, inspect ELF sections/segments for low-level
changes, and record hardware results in `issues.md`. Build success alone never
marks hardware behavior complete.
- [x] 6h. Reduce the active RAM-disk probe to one 10-byte sector and use a
  byte-wise reset to isolate storage-size and alignment effects.
- [x] 6i. Disable boot-time RAM-disk activation after the 10-byte probe still
  corrupted the `.data` sentinel; retain the isolated interface for later use.
- [x] 6j. Expand the dormant ordinary-memory RAM-disk probe to 1 KiB while
  keeping boot-time activation disabled, isolating static-size image effects.
- [x] 6k. Expand the dormant ordinary-memory RAM-disk probe to 64 KiB while
  keeping boot-time activation disabled, isolating the larger `.bss` footprint.
- [x] 6l. Add non-mutating RAM-disk geometry accessors for sector size and
  sector count, with host-side contract coverage.
- [x] 6m. Add a non-mutating total-capacity accessor to complete the minimal
  RAM-disk block-device geometry contract.
- [x] 6n. Add bounded byte-range RAM-disk access for filesystem metadata and
  records, with zero-length and out-of-range contract tests.
- [x] 6o. Add deterministic bounded RAM-disk checksums for metadata integrity
  checks, with host-side validation.
- [x] 6p. Add an explicit RAM-disk format signature and validation operation,
  leaving formatting dormant during boot for filesystem integration.
- [x] 6q. Add version and checksum validation to the dormant RAM-disk format
  header, preventing stale or partially corrupted metadata from being used.
- [x] 8a. Change the CP32 TTY bring-up client to request bounded canonical
  lines and report completed line data, preserving the existing TTY task path.
- [x] 8b. Revert the line-buffered TTY bring-up client after it changed the
  boot image sentinel; restore the proven one-byte diagnostic read path.
- [x] 8c. Add phased startup diagnostics for `.data`/`.bss` bounds and the
  data sentinel to localize loader versus startup corruption.
- [x] 8d. Revert phased startup diagnostics after they changed the image
  layout and reproduced sentinel corruption; restore the compact startup path.
- [x] 8e. Add one retained main-stage data-layout diagnostic reporting the
  sentinel and linker section bounds before marker 131.
- [x] 8f. Remove the oversized diagnostic and enforce the discovered 32 KiB
  CP32 loader IRAM window with a link-time assertion before marker 132.
- [x] 8g. Add a minimal post-entry stability trace after the kernel reaches
  `kernel_idle_loop()`, preserving the enforced 32 KiB image limit.
- [x] 8h. Encode the stability trace in the existing idle-entry status line
  after the additional print exceeded the hard 32 KiB loader window.
- [x] 8i. Split IRAM into a 32 KiB bootstrap window and a second contiguous
  internal-IRAM window so kernel text can grow beyond the loader bootstrap limit.
- [x] 8j. Keep the complete pre-boot C dependency set in the bootstrap IRAM
  segment while placing the remaining kernel text in extended IRAM.
- [x] 8k. Revert the multi-segment IRAM experiment after it produced no USB
  output; restore the validated single-segment 32 KiB loader model.
- [x] 8l. Compile out repetitive IRQ, RFE, CLOCK, and keyboard polling traces
  by default, preserving functional input and the compact validated image.
- [x] 8m. Add an ESP-IDF-style D/IRAM linker window with its DRAM alias
  reservation, and place the isolated RAM-disk code in the extended window.
- [x] 8n. Move the D/IRAM DRAM-alias reservation before `.data` placement so
  initialized data cannot overlap extended executable SRAM.
- [x] 8o. Execute the RAM-disk capacity accessor from extended D/IRAM during
  boot, providing a minimal hardware validation of the new code window.
- [x] 8p. Re-enable the retained verbose diagnostics after confirming that
  extended D/IRAM code executes correctly on hardware.
- [x] 8q. Relocate timer IRQ dispatch and its diagnostic helper into D/IRAM
  after verbose diagnostics exceeded the bootstrap window by 44 bytes.
- [x] 8r. Relocate the runtime `clock_task()` implementation into D/IRAM,
  keeping reset and startup dependencies in the bootstrap IRAM window.
- [x] 8s. Relocate the runtime `tty_task()` implementation into D/IRAM,
  preserving the keyboard and TTY execution path while reducing low-IRAM use.
- [x] 8t. Relocate the runtime `sys_task()` implementation into D/IRAM,
  preserving the assembly interrupt entry and reset path in low IRAM.
- [x] 8u. Relocate the runtime `mm_task()` memory server into D/IRAM while
  keeping reset/startup and interrupt-entry code in low IRAM.
- [x] 8v. Relocate runtime keyboard event translation into D/IRAM while
  retaining boot-time keyboard probing, initialization, and I²C access below.
- [x] 8w. Relocate runtime keyboard event reads and interrupt-state checks
  into D/IRAM while retaining boot-time probe/configuration code in low IRAM.
- [x] 8x. Relocate non-boot keyboard status/configuration and diagnostic
  accessors into D/IRAM, retaining the boot probe/init path in low IRAM.
- [x] 8y. Relocate the post-start `kernel_idle_loop()` into D/IRAM while
  retaining all reset and boot diagnostics in low IRAM.
- [x] 8z. Relocate the runtime TTY client loop into D/IRAM while preserving
  the boot-time process setup and diagnostic path.
- [x] 8aa. Relocate the runtime scheduler implementation into D/IRAM while
  preserving low-IRAM reset, startup, and interrupt-entry dependencies.
- [x] 8ab. Relocate runtime `lock_sched()` into D/IRAM while preserving the
  low-IRAM reset and startup path.
- [x] 8ac. Relocate the runtime millisecond delay helper into D/IRAM while
  retaining low-level timer interrupt and startup setup in low IRAM.
- [x] 8ad. Relocate the runtime `get_uptime()` helper into D/IRAM while
  retaining timer initialization and interrupt entry in low IRAM.
- [x] 8ae. Relocate runtime clock time/uptime service handlers into D/IRAM
  while retaining timer initialization and interrupt entry in low IRAM.
- [x] 8af. Relocate runtime alarm service handlers into D/IRAM while retaining
  timer initialization and interrupt entry in low IRAM.
- [x] 8ag. Relocate clock-task dispatch, tick, synchronous-alarm, and alarm
  delivery helpers into D/IRAM while retaining the timer ISR path in low IRAM.
- [x] 8ah. Relocate runtime clock timing helpers into D/IRAM while retaining
  timer initialization and interrupt entry in low IRAM.
- [x] 8ai. Relocate the C-side IRQ registration and dispatch layer into
  D/IRAM while retaining assembly interrupt entry in low IRAM.
- [x] 8aj. Relocate the runtime TTY ioctl handler into D/IRAM while retaining
  boot-time keyboard and TTY initialization in low IRAM.
- [x] 8ak. Relocate the compatibility TTY ioctl handler into D/IRAM while
  retaining boot-time keyboard and TTY initialization in low IRAM.
- [x] 8al. Relocate runtime process unready helpers into D/IRAM while
  retaining reset and startup process-table initialization in low IRAM.
- [x] 8am. Relocate ready-queue validation and insertion helpers into D/IRAM
  while retaining the public lock wrapper and scheduler entry contracts.
- [x] 8an. Relocate the runtime `switch_to()` process handoff helper into
  D/IRAM while retaining scheduler and startup handoff behavior.
- [x] 8ao. Relocate the locked ready-queue wrapper into D/IRAM while
  preserving its interrupt-lock protocol.
- [x] 8ap. Relocate the locked process-selection wrapper into D/IRAM while
  preserving its interrupt-lock protocol.
- [x] 8aq. Relocate held-interrupt replay (`unhold`) into D/IRAM while
  preserving lock ordering and interrupt delivery semantics.
- [x] 8ar. Relocate the locked mini-send wrapper into D/IRAM while preserving
  its lock-save/send/restore sequence.
- [x] 8as. Relocate blocked-frame snapshot capture into D/IRAM while
  preserving the process-frame validity contract.
- [x] 8at. Relocate IPC message-copy and blocked-message delivery helpers into
  D/IRAM while preserving sender validation and wakeup ordering.
- [x] 8au. Relocate the IRQ message-buffer copy helper into D/IRAM while
  retaining the interrupt entry and notification state machine in place.
- [x] 8av. Relocate the optional IPC trace helper into D/IRAM without
  changing IPC state transitions.
- [x] 8aw. Relocate blocked-frame completion and wake/resume bookkeeping into
  D/IRAM while preserving result publication and ready-queue insertion.
- [x] 8ax. Relocate the C-level hardware notification handler into D/IRAM
  while retaining assembly IRQ entry and frame handling in low IRAM.
- [x] 8ay. Relocate the runtime mini-receive IPC implementation into D/IRAM
  while preserving receive matching and blocked-wakeup semantics.
- [x] 8az. Relocate the runtime mini-send IPC implementation into D/IRAM
  while preserving destination validation and blocked-sender semantics.
- [x] 8ba. Relocate the C-level `sys_call()` dispatcher into D/IRAM while
  retaining the assembly trap entry and IPC contracts.
- [x] 8bb. Relocate the user blocked-handoff dispatcher into D/IRAM while
  retaining exception-vector entry and saved-frame validation.
- [x] 8bc. Relocate five RAM-disk sector/byte I/O and checksum functions into
  D/IRAM as one runtime storage-helper group.
- [x] 8bd. Relocate five runtime TTY input/output, event, reply, and signal
  helpers into D/IRAM while retaining boot-time TTY initialization in IRAM.
- [x] 8be. Relocate five runtime TTY read/write/open/close/cancel handlers into
  D/IRAM while retaining boot-time TTY initialization in IRAM.
- [x] 8bf. Relocate five runtime TTY transfer/ioctl/attribute/cancel/wakeup
  helpers into D/IRAM while retaining boot-time TTY initialization in IRAM.
- [x] 8bg. Relocate five runtime TTY editing/output/no-op helpers into D/IRAM
  while retaining boot-time TTY initialization in IRAM.
- [x] 8bh. Relocate five runtime TTY timer, compatibility, and trace helpers
  into D/IRAM while retaining serial/console initialization in IRAM.
- [x] 8bi. Relocate five runtime keyboard/TTY compatibility helpers into
  D/IRAM while retaining serial and console initialization in IRAM.
- [x] 8bj. Relocate ten system-task runtime handlers into D/IRAM while
  retaining system-task entry and startup plumbing in low IRAM.
- [x] 8bk. Relocate the remaining ten system-task signal, memory, boot, and
  tracing handlers into D/IRAM while retaining system-task entry in IRAM.
- [x] 8bl. Relocate ten runtime memory-management allocator, mapping, copy,
  ownership, and validation functions into D/IRAM.
- [x] 8bm. Relocate ten Cardputer I2C GPIO and transaction helpers into D/IRAM
  as one hardware-access group.
- [x] 8bn. Relocate ten remaining TTY/Cardputer/clock/memory runtime and
  setup helpers into D/IRAM while retaining vector and assembly entry points.
- [x] 8bo. Relocate ten remaining runtime system, library, and memory helpers
  into D/IRAM while retaining loader and vector-critical code in IRAM.
- [x] 8bp. Relocate five remaining diagnostic/environment/serial-output helpers
  into D/IRAM while retaining panic and vector/timer-critical entry routines.
- [x] 8bq. Reject duplicate RECEIVE requests from an already-blocked receiver
  without overwriting its saved source selector or message buffer.
- [ ] 5aj. Validate frame-gated ready-queue rotation for the FS/TTY user task
  (experimental; hardware validation pending).
- [x] 5ak. Remove duplicate timer-IRQ scheduling so an FS/TTY selection is
  preserved for the single frame publication and return path.
- [x] 5al. Publish the preserved runnable FS selection through the IRQ return
  owner before `rfe`, preventing the legacy idle frame from overwriting it.
- [ ] 5am. Diagnose post-publication FS return-owner invalidation with a
  one-shot flags/PC/SP trace before changing the handoff contract.
- [ ] 5an. Publish FS directly from `pick_proc()` when IRQ dispatch ownership
  is active, then validate user entry and TTY character completion.
- [ ] 5ao. Arm IRQ dispatch ownership before the timer's first scheduler pass
  so the selected FS frame can become the IRQ return frame.
- [ ] 5ap. Capture the direct `sched()` result as the IRQ return owner before
  later clock bookkeeping can overwrite the selected FS frame.
- [ ] 5aq. Publish runnable FS selection before IRQ dispatch activation so the
  assembly return path can use the clock-task selection.
- [ ] 5ar. Trace IRQ return-owner, selected process, and preserved FS state at
  dispatch finalization to identify the remaining owner overwrite.
- [ ] 5as. Publish FS as the IRQ return owner when TTY wakeup requeues it from
  task context, before the next gated interrupt return.
- [ ] 5at. Enter the saved FS call0 frame from the idle loop when the clock
  task selects FS outside IRQ context.
- [ ] 5au. Trigger the idle-loop FS handoff from the preserved scheduler
  selection rather than the transient live process pointer.
- [ ] 5av. Transfer directly from `switch_to()` while the non-IRQ FS frame is
  still runnable, before the clock task blocks it again.
- [ ] 5aw. Map the FS SRAM stack window as flat D memory so TTY read buffers
  pass `numap()` validation.
- [ ] 5ax. Rate-limit repeated TTY user-read error diagnostics to once per
  1000 failed attempts.
- [ ] 5ay. Rebind FS ownership before user-client IPC so requests are not
  emitted with the interrupted IDLE process as their source.
- [ ] 5az. Fix TTY input transfer to translate absolute flat SRAM addresses
  once instead of adding the FS segment base a second time.
- [ ] 5ba. Remove the artificial successful-BOTH blocked probe and restore FS
  ownership on successful user syscall return.
- [ ] 5bb. Permit repeated FS saved-frame entry after syscall return so the
  TTY client can issue one read request per keyboard character.
- [ ] 5bc. Restore the IDLE return frame to `kernel_idle_loop()` so C-level
  FS wakeup handoff remains reachable after user syscalls.
- [ ] 5bd. Publish FS on every successful user syscall before transient BOTH
  receive flags are normalized by the IPC path.
- [ ] 5be. Allow successful user syscalls to bypass transient receive flags
  and return directly to the FS client frame.
- [ ] 5bf. Restore the updated FS process frame in the user exception assembly
  path instead of the stale pre-syscall trap frame.
- [ ] 5bg. Use nonblocking TTY reads in the bring-up user client to avoid the
  incomplete blocked-receive return handoff while validating keyboard input.
- [ ] 5bh. Make successful user exception returns consume the explicit
  `cp32_irq_return_proc` publication instead of mutable `proc_ptr`.
- [ ] 5bi. Use the actual TTY `O_NONBLOCK` flag for the user read client;
  `NO_BLOCK` is unrelated and evaluates to zero.
- [ ] 5bj. Validate TTY reply status and clear the user byte before printing,
  preventing stale stack data from appearing as a typed character.
- [ ] 5bk. Write CP32 flat-SRAM TTY input bytes directly to the validated user
  destination, bypassing the legacy segmented copy path.
- [ ] 5bl. Report the first user TTY IPC return code and reply status to
  distinguish empty input from a malformed `BOTH` transaction.
- [ ] 5bm. Move the diagnostic TTY byte buffer from the transient FS stack to
  persistent kernel SRAM to isolate user-stack return corruption.
- [ ] 5bn. Trace the first TTY transfer destination and input character at
  `in_transfer()` to locate loss between keyboard queue and user buffer.
- [ ] 5bo. Remove the ineffective CHIP preprocessor guard so CP32 flat-SRAM
  TTY transfer code is included in the actual build.
- [x] 5bp. Allow the CP32 one-byte FS TTY client to receive canonical input
  immediately, without waiting for an EOL; preserve canonical gating for other
  terminal consumers.
- [x] 5bq. Guard TTY transfer against draining an empty input queue, which was
  producing false zero-valued user characters.
- [x] 5br. Make the CP32 TTY client report the byte read back from the exact
  transfer destination, eliminating a misleading stale-buffer diagnostic.
- [x] 5bs. Suppress zero-byte TTY replies in the bring-up client so stale or
  empty replies cannot be reported as keyboard input.
- [x] 5bt. Preserve the TTY destination pointer across `_sendrec()`; reply
  messages may overwrite the request's `ADDRESS` field.
- [x] 5bu. Keep the diagnostic TTY destination in persistent kernel SRAM so
  reconnect/context-switch paths cannot invalidate a stack-local pointer.
- [x] 5bv. Remove unsafe post-IPC user-pointer readback; publish the delivered
  byte from the kernel TTY transfer into persistent diagnostic SRAM.
- [x] 5bw. Pin synthetic TTY `_sendrec()` calls to the FS process descriptor so
  scheduler publication cannot replace the IPC caller between SEND and RECEIVE.
- [x] 5bx. Poll/service the TTY input queue before synthetic user IPC, avoiding
  the unsafe empty-read blocking return path.
- [x] 5by. Stop invoking the kernel `handle_events()` directly from user
  context; let the TTY task own queue servicing and prevent a null `tty_t`
  handoff argument.
- [x] 5bz. Drain the controller FIFO during keyboard initialization and tag
  keyboard events with the firmware session marker to prevent stale replay
  after USB-UART reconnects.
- [x] 5ca. Add a cooperative empty-queue keyboard poll for the synthetic FS
  client so its wait loop cannot starve the normal TTY polling task.
- [x] 5cb. Prevent synthetic TTY retries while FS is already blocked, avoiding
  repeated `SENDING|RECEIVING` state overwrites and lost user reads.
- [x] 5cc. Add a direct atomic CP32 TTY queue read for the bring-up client,
  bypassing the unstable synthetic blocked-IPC path while preserving normal
  TTY IPC for later user processes.
- [x] 5cd. Add a bounded diagnostic line buffer with backspace and Enter
  submission handling on top of the stable per-character TTY path.
- [x] 5ce. Add minimal RAM-disk shell dispatch for `ls` and `ramdisk`, plus a
  bounded unknown-command diagnostic.
- [x] 5cf. Diagnose individual keyboard initialization register failures and
  correct duplicate `0x` formatting in TTY non-printable character logs.
- [x] 5cg. Reduce periodic scheduler/IPC diagnostics and add compact keyboard
  interrupt, controller-status, and FIFO-depth boot diagnostics.

## Core-kernel phase after console bring-up

- [x] 5ch. Bound IPC caller-queue append and receive traversal, reject cyclic
  or malformed sender links, and preserve the existing blocked-message wakeup
  contract.
- [x] 8cl. Complete the stable Cardputer TTY milestone: immediate per-character
  delivery and bounded `ls`/`ramdisk` dispatch are validated on hardware.
- [ ] 8cm. Keep `cat` and additional shell commands deferred until the kernel
  phase has a production filesystem/message path.
- [ ] 8cn. Implement the next core-kernel feature with host coverage before
  expanding the command surface.
- [x] 8co. Guard bit-banged keyboard I²C transactions against concurrent
  scheduler/IRQ poll re-entry; marker 2 boots and reaches the stable FS/TTY
  loop without the prior exception.
- [x] 8cp. Bound ready-queue integrity scans and stale-entry removal so a
  cyclic or malformed ready link cannot hang scheduler maintenance; marker 3.
- [x] 8cq. Fail closed when keyboard register initialization is incomplete;
  runtime I²C polling is disabled until the controller is fully ready; marker 4.

## TTY/user handoff diagnostic tree — marker 219

- [x] Boot image remains structurally valid: `.data` and `.bss` sentinels pass,
  vectors are present, and the 64 KiB SRAM RAM disk initializes.
- [x] Keyboard hardware path works: `[KBD probe=1]`, `[KBD init=1]`, keyboard
  events, and kernel-side `[TTY char=...]` output are observed.
- [x] Scheduler reaches the FS client: `[SCHED fs-selected ... nest=0]` and
  `[TTY user-entry]` appear.
- [x] The first user TTY transaction completes and copies data correctly:
  `[TTY user-char=k]` confirms the earlier double-address translation bug is
  fixed.
- [ ] User client resumes for subsequent reads. After the first character the
  return path reports `RFE target=0 current=0` and resumes at an internal idle
  address (`0x40372C0B`), so the FS client loop is not re-entered.
- [ ] Next investigation: trace `irq_user` labels 2/3/4 and the exact value of
  `cp32_user_dispatch_blocked`, `proc_ptr`, and `cp32_irq_return_proc` after
  `cp32_user_trap_dispatch()` returns successfully.
- [ ] Verify whether `cp32_enter_initial_user()` must establish a dedicated
  user-return context rather than relying on the generic `rfe` path.
- [ ] Keep the current nonblocking `NO_BLOCK` experiment isolated until the
  user-return ownership problem is resolved; do not expand diagnostics before
  capturing the first post-syscall assembly state.
