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

Bring-up step: `[FEATURE TASK-DESC 1]` validates the CLOCK descriptor's
entry point, aligned SP/a1, call0 frame pointer, PSW, and reserved stack
window immediately before IRQ startup. Host tests and the Xtensa ELF/image
build pass. Hardware validation passed: the marker reported `1`, CLOCK and
FS frames were restored, `TTY user-entry` was reached, and IRQs continued
through count 500 without an exception.

Next bring-up image: `[FEATURE IRQ-HANDOFF 2]` adds a runtime check that the
frame selected by the IRQ dispatcher is runnable, self-consistent, and owned
by both scheduler pointers before assembly restoration. Host tests and the
Xtensa ELF/image build pass. Hardware validation passed: the marker reported
`1`, the selected FS frame reported `return-frame=1`, and execution reached
`TTY user-entry`.

Next bring-up image: `[FEATURE IPC-BLOCKED 3]` adds a one-time runtime report
when a blocked SEND/RECEIVE frame is completed, its result is restored into
`a2`, and the owner is made runnable again. Host tests and the ELF/image build
pass; hardware validation is pending.

### 2. Interrupt and exception dispatch — Partially implemented

Reference: `minix-2.0.0/src/kernel/mpx386.s`, `i8259.c`, `exception.c`,
`proc.c:interrupt()`; CP32: `vectors.S`, `irq.S`, `irq_frame.h`, `proc.c`.

Present: Xtensa vector/exception entry, an 80-byte temporary IRQ frame,
registered device dispatch, SYSTIMER setup, pending/coalesced notification
accounting, and gated selected-frame return. Image 30 repairs level-1 PS/RFE
and general-register preservation, with host register-flow tests.
Missing: hardware proof for that repair, special-register context storage,
and nested IRQ/fatal exception validation.

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

Bring-up image `[FEATURE TTY-IPC 4]` adds reply validation to the TTY
`_sendrec()` adapter: completed exchanges must return `TASK_REPLY` with a
non-negative status, otherwise `EIO` is returned. Host tests and the
Xtensa ELF/image build pass; hardware validation and a dedicated IPC probe
remain pending. The direct interactive console path remains the production
path until that probe succeeds.

Image `[FEATURE TTY-IPC-PROBE 5]` currently exposes an `ipc` shell command
that reports `probe deferred`; the earlier synchronous request was removed
because it could suspend the interactive FS owner before TTY task handoff.
Hardware output confirms the console remains stable through IRQ 1500 and
`ls` continues to work. The asynchronous task-owned probe is still pending;
the marker is not considered hardware validation of TTY IPC.

Next image `[FEATURE TTY-IPC-ASYNC 6]` adds a kernel-owned asynchronous
probe slot: the shell queues `ipc`, `tty_task()` completes the task-owned
request, and FS observes the result without blocking. Host tests and the
ELF/image build pass; hardware validation is pending. This is the staging
boundary for replacing the slot result with a real IPC reply message.

Next image `[FEATURE TTY-IPC-ASYNC 10]` keeps the selected TTY task in a
cooperative nonblocking loop during bring-up. This avoids entering the
blocking `receive()` path before task-context suspension can return safely to
the scheduler. Host tests and the ELF/image build pass; hardware validation
is pending.

Next image `[FEATURE TTY-IPC-ASYNC 7]` extends quantum-expiry scheduling to
consider runnable task and server queues, not only users. This is required for
TTY to receive CPU time and complete the queued probe. Host tests and the
ELF/image build pass; hardware validation is pending.

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

Hardware evidence is recorded per image below; it does not validate later
images. See `issues.md` for the current SYSTIMER investigation and the exact
hardware acceptance criteria. Host tests and ELF layout checks are separate
from that acceptance gate.

## Historical handoff status — image 29, 2026-09-17

Latest hardware image: `[FEATURE TTY-IPC-ASYNC 29]1`.

Confirmed on hardware:

- Boot, memory/vector checks, process-table checks, scheduler checks, and
  initial FS handoff succeed.
- Cardputer keyboard input and shell display output work; `ls` executes and
  prints RAM-disk contents.
- The asynchronous `ipc` command queues and reports
  `[TTY IPC probe task-result=0]` without blocking the shell.
- SYSTIMER startup reports `conf=0xC7000000`, TARGET0 `ena=0x00000001`, and
  `raw=0x00000000` after setup.

Known unresolved issue:

- USB IRQ diagnostics stop after `[IRQ count=1 ...]`. The shell can continue
  processing input, but subsequent timer IRQ heartbeats are not observed.
- The real TTY task-owned IPC reply is not implemented yet; the probe result
  is currently a scheduler/FS-side bring-up completion fallback.
- MM, CLOCK, and TTY task entry loops remain cooperative bring-up paths and
  do not yet use their final blocking `receive()`/resume contract.

## Continue here

Image 30 now has hardware evidence for two consecutive timer IRQs, but
crashes after FS console entry. Image 31 removes unsafe direct FS handoffs
and is built for the user to flash manually. Start with the image-31 section
below: validate continued FS resumption and IRQ counts 500/1000 before
enabling real TTY IPC or CLOCK/MM blocking receives.

## SYSTIMER continuation — image 30, 2026-09-17

Image identity: `[FEATURE SYSTIMER-IRQ 30]1` and, immediately before idle
entry, `[TEST SYSTIMER-IRQ 30]`.

Source investigation found three concrete defects before task activation:

- Level-1 assembly acknowledged TARGET0 before reading the CPU pending
  bitmap, so the level source could disappear before its rearm handler ran.
  Device acknowledgement now belongs to the registered timer handler; its
  counter counts actual timer dispatches.
- Level-1 return wrote status to SR192 (DEPC) and used undefined `rfi 1`.
  IRQ and syscall return now restore one selected frame through named PS
  and `rfe`, keeping EXCM set until the final return. Entry/exit also preserve
  the general registers previously overwritten during classification and
  frame restoration.
- UNIT0 snapshot polling could accept the preceding VALUE_VALID indication.
  The counter helper clears that W1C bit with UPDATE and retries split reads
  if another context relatches UNIT0. Boot, CLOCK init, and ISR rearm share
  one disable/clear/fresh-counter/load/enable sequence.

Diagnostics `[SYSTIMER V30 ...]` capture the UNIT0 counter and actual target,
CONF, RAW/ST before and after rearm, then report the programmed deadline,
peripheral enable, CPU INTENABLE/pending/PS and selected return PC/SP/PS.
Samples are limited to the first two timer dispatches and every 500th.

Ordered next steps:

[x] 1. All 11 host test scripts pass, including the new SYSTIMER register
   model and assembly register-flow tests. A clean ELF/image build and image
   layout check pass. Inspected sections/segments: `_iram_end=0x40374264`,
   `_iram_ext_end=0x4037FF14`, `_stack_top=0x3FCCCD50`, all within linker
   limits. These checks cannot certify silicon interrupt delivery.

2. Validate image 30 on hardware: observe timer counts 2, 500 and 1000,
   compare before/after target and counter values, confirm enabled CPU line 2
   and a return PS that permits level-1 interrupts, then exercise the shell.
3. Only after step 2, complete the missing special-register context storage
   needed for arbitrary instruction preemption, then replace all
   shell/scheduler probe completion fallbacks with a real TTY-owned IPC
   request/reply and verify blocked caller resume.
4. Then activate CLOCK's HARD_INT receive/resume path and MM's blocking
   receive path separately, checking each task's ownership and continued
   timer/shell operation.

The task scheduler and cooperative CLOCK/MM/TTY gates are unchanged by this
step. The 76-byte process-frame ABI still omits SAR and other special state;
the register-flow tests cover general registers, PC, PS and SP, not a complete
Xtensa task-context or nested-interrupt proof.

## Current handoff status — image 31, 2026-09-18

Latest supplied hardware logs are image 30, not image 31. Both show TARGET0
rearmed correctly at IRQs 1 and 2, RAW/ST clearing, CPU interrupt 2 enabled,
and FS entry reached. They then halt with these two outcomes:

- `EXCCAUSE=0x00000002`, `EPC1=EXCVADDR=0x3FCD6D40` (FS entry SP minus 16).
- `EXCCAUSE=0x00000014`, `EPC1=0x00000001`, `EXCVADDR=0`.

The source contains a reproducible ownership bug: LOW_USER runs
`kernel_idle_loop()`, which replayed `cp32_last_selected_fs` without updating
the running owner or saving the outgoing context. This allows the next IRQ
to save FS execution into LOW_USER's descriptor and later replay stale FS
call state. C `switch_to()` had a second direct restore that could abandon
an unfinished scheduler/IPC call. Image 31 removes both bypasses; actual
frame restoration remains at IRQ/syscall return. Hardware confirmation that
this resolves the reported crashes is still required.

[x] Regression tests execute the production idle/switch C bodies against
fresh and suspended FS selections. The original code fails with a restore
under the wrong owner; the corrected code passes all five cases.

[x] All 12 host test scripts pass. Clean Xtensa ELF/bin build, image layout,
section/segment inspection, and `git diff --check` pass without compiler
warnings. `_iram_end=0x40374268`, `_iram_ext_end=0x4037FFAC`, and
`_stack_top=0x3FCCCE20` remain within linker limits.

Next hardware image: `[FEATURE SYSTIMER-IRQ 31]1` and
`[TEST SYSTIMER-IRQ 31]`. Two `[CTX V31 FS-RETURN ...]` lines report the first
FS selections, including PC/SP/a0/a15 and frame ownership checks. The user
will flash manually; no automatic flash was performed.

Acceptance: shell remains responsive, FS resumes without an exception, and
IRQs reach 500 and 1000. Only then proceed to special-register context
preservation, real TTY request/reply, and CLOCK/MM receive activation. Those
steps and arbitrary task-context scheduling remain pending.
