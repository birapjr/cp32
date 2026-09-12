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
  (reverted after task-frame/image-integrity regression; requires a frame-safe
  scheduler handoff design).
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
