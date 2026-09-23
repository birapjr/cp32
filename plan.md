# CP32 implementation plan

This plan is validated against the current CP32 tree and `minix-2.0.0`.
“Implemented” means present in source; it does not imply a successful
hardware run. The MINIX reference supplies behavior, while Xtensa and
ESP32-S3 replacements are valid when they preserve that behavior.

## Current boundary

CP32 is a bare-metal, kernel-focused port. `src/Makefile` builds 27 C sources
plus four Xtensa assembly units. Portable filesystem code now follows the
MINIX filenames in `src/fs/` and MM code in `src/mm/`. The tree has no user
image, libc/syscall ABI, or application tree. Kernel-linked service tasks and
the diagnostic shell run through IPC. See `minix.port-status.md` for the current
assessment; the sections below retain the chronological bring-up history.

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
Image 49 adds the MEM device task's OPEN/CLOSE/READ/WRITE IPC service over
the RAM disk; host tests/build pass, hardware validation is pending.
Missing: `src/fs/`, inode/cache/path/file-descriptor operations, vectored
block I/O, root filesystem, image loader, and boot population.

Next: validate the MEM IPC path on hardware, then add a filesystem layer.
The implemented device ABI is documented in `docs/ramdisk-ipc.md`.

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

Image 57 hardware confirms root/boot listing and rejection of ls on a regular
file, with heartbeat 3087. Image 58 aligns MM with the book: src/mm/main.c and
alloc.c, plus mm.h/proto.h. numap/mem_copy now reside in kernel/system.c,
matching the reference kernel boundary. All 23 tests and clean build pass.
Hardware pending: repeat mm, then cat boot/README, ls boot, disk and services.
See docs/minix-source-layout.md. Next porting work: indirect zones and FS APIs.

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

The preceding image-30 hardware logs show TARGET0
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
frame restoration remains at IRQ/syscall return. The subsequent image-31
hardware run below no longer reproduces the crash through IRQ 1500.

[x] Regression tests execute the production idle/switch C bodies against
fresh and suspended FS selections. The original code fails with a restore
under the wrong owner; the corrected code passes all five cases.

[x] All 12 host test scripts pass. Clean Xtensa ELF/bin build, image layout,
section/segment inspection, and `git diff --check` pass without compiler
warnings. `_iram_end=0x40374268`, `_iram_ext_end=0x4037FFAC`, and
`_stack_top=0x3FCCCE20` remain within linker limits.

Validated hardware image: `[FEATURE SYSTIMER-IRQ 31]1` and
`[TEST SYSTIMER-IRQ 31]`. Two `[CTX V31 FS-RETURN ...]` lines report the first
FS selections, including PC/SP/a0/a15 and frame ownership checks. The user
flashed manually; no automatic flash was performed.

[x] Supplied hardware log reaches IRQ counts 500, 1000 and 1500 without an
exception. All sampled rearms clear RAW/ST, enable CPU interrupt 2 and report
`frame=1`. FS first enters at tick 5 and resumes at tick 11 with a saved code
return address (`a0=0x4037D241`) and `frame=1`.

[x] Keyboard input and shell command dispatch work during continued IRQs:
the log captures `ipc`, its queued marker and fallback result `0`. A heartbeat
interleaves with that result line; this is not an exception or IPC proof.

Evidence: `docs/hardware/systimer-v31.log`. This capture does not show `ls`,
long-duration stress, or real TTY message delivery. Special-register context
preservation, real TTY request/reply, CLOCK/MM receive activation, and
arbitrary task-context scheduling remain pending.

## Integer special-register context — image 32

[x] Save and restore SAR, LBEG, LEND and LCOUNT for level-1 interrupts,
syscall frames and selected-task return. The process/syscall frame is now
92 bytes; existing GPR/PC/PS/SP offsets stay unchanged. IRQ temporary frames
remain 80 bytes by using their former padding. Fresh descriptors initialize
the four registers to zero, and syscall/blocked-handoff copies include them.

[x] All 12 host scripts pass, including assembly tests that clobber the four
registers in the C-handler mock and verify interrupted versus selected
context restoration. Compile-time checks cover the extended frame offsets.
A clean Xtensa build and image-layout/section checks pass.

[x] Hardware regression passed in `docs/hardware/context-special-v32.log`:
`[FEATURE CONTEXT-SPECIAL 32]1` and `[TEST CONTEXT-SPECIAL 32]` identify the
image. IRQs reach 500/1000/1500 without an exception; sampled returns report
`frame=1`, RAW/ST clear after rearm, and CPU interrupt 2 remains enabled.
FS resumes at tick 11 with saved `SAR=0x15`, `LCOUNT=0`, and `frame=1`.
Keyboard input and `ipc` command dispatch work; the result remains fallback
`0`, interleaved with heartbeat output. No automatic flash was performed.

This is a successful hardware regression, not an exhaustive register test:
the log shows no active nonzero hardware-loop count, no `ls`, and no real
TTY IPC exchange. Assembly host tests cover distinct nonzero loop state.

This covers integer shift/loop state, not floating-point, MAC or other
optional extension state. Full task-context IPC suspension and real TTY
request/reply are next; CLOCK/MM blocking receive paths remain gated.


## Real TTY request/reply — image 33, 2026-09-18

Identity: `[FEATURE TTY-IPC 33]1`, `[TEST TTY-IPC 33]`.

MINIX reference: `minix-2.0.0/src/kernel/proc.c` mini_rec clears only
SENDING when a queued request is accepted. A BOTH caller remains RECEIVING
until the server replies. CP32 previously cleared both bits and returned
from ordinary C wrappers even when their caller had blocked.

The wrappers now execute Xtensa SYSCALL (EXCCAUSE 1, confirmed by the local
ESP32-S3 Xtensa corebits definitions). Assembly saves the full 92-byte context
in a 128-byte aligned frame on the caller's stack. The dispatcher records the
post-SYSCALL PC, performs IPC under EXCM, selects a runnable process if blocked,
and returns through the shared RFE epilogue. Owner pointers and restored frame
must agree. No wrapper fabricates the FS owner. Completing SEND for a BOTH
caller leaves its receive buffer and saved continuation intact until reply.
Completed frame metadata is cleared consistently with the runtime invariants.

TTY now runs its real receive/dispatch/reply loop. Shell `ipc` sends DEV_IOCTL
TCGETS with a local message and termios buffer, and validates TASK_REPLY,
source TTY_PROC_NR and REP_PROC_NR before reporting status. All four shared-slot
completion fallbacks are removed. CLOCK and SYS descriptors are stopped until
separate activation; MM retains its cooperative loop. Keyboard polling still
belongs to the FS bring-up client; this is not a full FS server or DEV_READ path.

Removable `[IPC V33 op=... n=... owner=... blocked=... next=... frame=...]`
reports the first call of each operation and every 5000th. Expect RECEIVE
(op=2) to block TTY; BOTH (op=3) to block FS; SEND (op=1) to complete the
reply without changing the currently executing TTY owner. A resumed shell
prints `[TTY IPC V33 reply-result=0]` for each successful exchange.

[x] Host verification: all 13 test scripts pass. New tests execute production
IPC, dispatcher and scheduler functions for 100 exchanges in both arrival
orders, SEND-only wake, invalid endpoint/buffer/opcode, self-send and deadlock.
They check message provenance, blocked BOTH state, saved PC/SP/special registers,
ready queues and selection of the resumed caller. Assembly tests cover both
exception vectors with immediate and blocked SYSCALL returns and private
stack frame placement. These are host models, not silicon execution.

[x] Clean build, ELF sections/segments and image layout pass without warnings.
Disassembly confirms the three-byte SYSCALL followed by RET at PC+3.
`_iram_end=0x4037432C`, `_iram_ext_end=0x40380390`,
`_stack_top=0x3FCCD500`.

Hardware follow-up: the user has manually flashed image 33; the first exchange
is confirmed below. Repeated `ipc`, then `ls` and keyboard input after the
reply, plus IRQ 1500 remain pending. Do not activate CLOCK/MM blocking
receives until these checks are confirmed. Nested interrupts and optional
floating-point/MAC context remain outside this image's validation.


[x] Image-33 first hardware exchange: `docs/hardware/tty-ipc-v33.log`
identifies TTY-IPC 33 and shows all boot CORE checks passing. TTY (-9,
unsigned 4294967287) blocks on RECEIVE; FS (1) blocks on BOTH; TTY resumes
and sends its reply while retaining current ownership. Each sampled IPC
return reports `frame=1`. The shell resumes and prints reply-result 0,
with heartbeats interleaved between the prefix and `0]`. IRQ 500 and 1000
continue with RAW/ST cleared after rearm, INTENABLE 0x4 and `frame=1`.
No exception appears in this capture.

This log contains one `ipc` command, no `ls`, and ends at IRQ 1000. Repeated
exchanges, post-reply keyboard/shell use and IRQ 1500 remain unverified;
the full image-33 hardware regression is not yet marked complete.


## CLOCK activation and scheduler fairness — images 34/35, 2026-09-19

MINIX reference: interrupt() delivers HARDWARE/HARD_INT to a waiting task;
clock_task receives, accounts pending ticks, handles timer work, and receives
again. MINIX pick_proc selects a queue head. Xtensa task-context C cannot
change the selected owner without first capturing the outgoing register frame.

Image 34 (`[FEATURE CLOCK-IPC 34]1`, `[TEST CLOCK-IPC 34]`) enabled CLOCK's
real receive loop and HARD_INT dispatch. Interrupt delivery now completes the
saved RECEIVE frame with the same result/metadata cleanup as ordinary IPC.
Boot owns SYSTIMER initialization once: CLOCK no longer resets live uptime,
rearms an existing deadline, or enables the legacy PC IRQ 0. clock_stop masks
the actual CPU line 2. CLOCK quantum handling preserves task ownership;
timer IRQ scheduling already rotates contexts using captured frames. Pending
tick accounting and get_uptime preserve the caller's interrupt-mask state.

Hardware evidence: `docs/hardware/clock-ipc-v34.log` identifies image 34,
passes boot checks, enters CLOCK, and reports a blocked CLOCK wake. However,
no CLOCK receive-return marker follows, and clock-msgs stays zero at IRQ 500
and 1000. TTY still completes a real exchange with result 0; no exception
appears. This is a failed CLOCK activation check, not a successful dispatch.

Image 35 (`[FEATURE CLOCK-FAIR 35]1`, `[TEST CLOCK-FAIR 35]`) removes the
fixed nine-rotation TTY preference from pick_proc. With TTY asleep and five
runnable tasks, that workaround repeatedly selected the same tail rather
than progressing through the queue. FIFO selection within each class now
preserves the existing rotation between task/server/user classes.

[x] A production-code fairness regression reproduced starvation of task -3
before the fix and passes afterward. All 14 host test scripts pass, including
100 TTY BOTH exchanges; HARD_INT delivery while blocked, before RECEIVE,
with source filtering, and held/coalesced/replayed notifications; saved frame
cleanup and unchanged interrupted ownership; CLOCK receive/dispatch/reply,
retained uptime, interrupt-mask preservation, quantum ownership and alarms.
The deferred-notification model is not a hardware nested-interrupt proof.

[x] Clean image-35 build, sections/segments and image-layout checks pass
without compiler warnings. `_iram_end=0x403741C0`,
`_iram_ext_end=0x4038056C`, `_stack_top=0x3FCCD770`.

Hardware pending: `[CLOCK V35 received=... type=2 source=4294967295 ...
owner=4294967293 resumed=1]` appears on the first two receives and every
5000th. Periodic IRQ diagnostics include clock-msgs, which must increase.
Confirm IRQ 500/1000/1500, repeated `[TTY IPC V35 reply-result=0]`, and `ls`
after the exchanges. Then proceed to MM receive activation as a separate
image. No automatic flashing was performed.


[x] Image-35 CLOCK progress hardware check: `docs/hardware/clock-fair-v35.log`
identifies CLOCK-FAIR 35 and reports CLOCK receiving HARDWARE/HARD_INT at
ticks 15 and 32 with owner -3 (unsigned 4294967293) and resumed=1.
clock-msgs advances from 57 at IRQ 500 to 115 at IRQ 1000, confirming repeated
service progress after the scheduler fix. All boot CORE checks pass, sampled
return frames report frame=1, timer RAW/ST clear after rearm and CPU line 2
remains enabled. The real TTY exchange returns 0, with heartbeats interleaved
inside the result line. Heartbeats continue through tick 1106; no exception
appears. The capture contains one ipc command, no ls, and no IRQ 1500 sample.
CLOCK progress is confirmed; the longer shell regression remains outstanding.


## MM receive/request/reply — image 36, 2026-09-19

Identity: `[FEATURE MM-IPC 36]1`, `[TEST MM-IPC 36]`.
MINIX reference: `minix-2.0.0/src/mm/main.c` get_work receives ANY, dispatches
by request type, and replies with status in m_type. CP32 uses that server
loop with its existing small allocate/release protocol, not full MINIX MM
process management. Xtensa suspension uses the proven task-owned SYSCALL/RFE
path; MM is endpoint 0 and runs in SERVER_Q rather than the kernel-task queue.

MM's cooperative delay/continue is replaced by receive, validated-source
dispatch and reply. Failed receive/send reports a panic rather than silently
continuing with stale state. Its nonnegative descriptor cannot use numap's
negative-task shortcut: boot now gives MM the same flat SRAM D map as the
bring-up FS client, allowing messages on MM's own stack. This is a bring-up
mapping, not process memory isolation.

The shared `minix/cp32_mm.h` describes existing allocate/release request codes
and click-based fields. The new shell `mm` command allocates one click via
SENDREC and releases its returned base through a second SENDREC. It checks
MM reply provenance and both statuses and does not directly call the allocator
or dereference allocated memory. MM assigns ownership using IPC m_source.

Removable `[MM V36 received=... op=... source=... resumed=1]` reports the first
two requests and every 5000th. Successful shell completion prints one USB line
`[MM IPC V36 alloc-release-result=0]` with interrupt state preserved and a
short LCD status. Existing TTY/CLOCK/timer diagnostics identify V36.

[x] All 15 host scripts pass. New MM tests execute the production server loop,
allocator, protocol handler and numap: 200 allocate/release cycles, full-region
reuse, ownership rejection, duplicate release, zero/negative allocation,
unknown operation, invalid source and MM stack-buffer translation. Production
IPC/scheduler tests now cover both arrival orders for MM's server endpoint as
well as TTY, in addition to CLOCK interrupt wakeup and queue fairness checks.

[x] Clean build without warnings, ELF sections/segments and image layout pass.
`_iram_end=0x403741C4`, `_iram_ext_end=0x403807E0`, `_stack_top=0x3FCCDA90`.

Hardware pending: repeat `mm`, check resumed=1 and alloc-release-result=0,
then exercise `ipc` and `ls` with increasing clock-msgs at IRQ 500/1000/1500.
MM initially blocking must not stop timer or shell operation. SYS stays stopped.
No automatic flashing was performed. This does not establish full MINIX MM,
user isolation, allocated-memory access, or nested-interrupt correctness.


[x] Image-36 hardware validation: `docs/hardware/mm-ipc-v36.log` identifies
MM-IPC 36, shows both MM requests with resumed=1, and four
alloc-release-result=0 completions. The ls command reports capacity 65536
and formatted=0; a later real TTY IPC reply returns 0. CLOCK messages reach
1175 at IRQ 10000, with heartbeats through tick 10193 and no exception.
Shell output is slow and interleaved with diagnostics; successful completion
does not establish acceptable console latency.

## Quieter heartbeat — image 37, 2026-09-19

User requested less frequent heartbeat output. `[FEATURE QUIET-HEARTBEAT 37]1`
and `[TEST QUIET-HEARTBEAT 37]` identify the image. The shared idle-loop
heartbeat now reports once per 400 iterations instead of 20: a 20-fold
reduction, approximately every 30 seconds at the image-36 observed rate.
This is an iteration limit, not a guaranteed wall-clock interval. Timer and
IPC sampling intervals are unchanged; versioned diagnostics now say V37.
Clean build, all 15 existing test scripts and image-layout verification pass.
Manual flashing and observation of the quieter cadence remain pending.


[x] Image-37 hardware result: `docs/hardware/quiet-heartbeat-v37.log` shows
heartbeat 1 at tick 1698 and heartbeat 2 at tick 3368. MM allocate/release and
TTY IPC both return 0, CLOCK reports resumed=1 and reaches 411 messages at
IRQ 3500. No exception appears in the supplied capture.

## Quieter SYSTIMER diagnostics — image 38, 2026-09-19

At the user's request, recurring timer diagnostics now share
CP32_TIMER_TRACE_INTERVAL=5000 rather than 500 (10 times less frequent).
The first two SYSTIMER before/after/return samples and initial IRQ/RFE reports
remain visible. Recurring SYSTIMER and IRQ status report every 5000 ticks;
RFE sampling uses the same interval on its existing return-trace counter.
At nominal 60 Hz this is about 83 seconds, not a wall-clock guarantee.
Heartbeat remains every 400 idle-loop iterations; IPC sampling is unchanged.
Identity: `[FEATURE QUIET-SYSTIMER 38]1`, `[TEST QUIET-SYSTIMER 38]`.
Versioned diagnostics now say V38. All 15 existing test scripts, clean build
and ELF/image-layout checks pass; hardware cadence validation is pending.


[x] Image-38 hardware result: `docs/hardware/quiet-systimer-v38.log` confirms
two MM successes, TTY result 0, complete ls output, and CLOCK dispatch count
587 at IRQ 5000. Initial timer samples and the next sample at tick 5000
confirm the reduced cadence. Heartbeats reach tick 5063; no exception appears.

## SYS task activation — image 39, 2026-09-19

Identity: `[FEATURE SYS-IPC 39]1`, `[TEST SYS-IPC 39]`.
MINIX reference: `minix-2.0.0/src/kernel/system.c` sys_task receives ANY,
dispatches the requested service and replies with status in m_type. CP32 now
makes the existing SYS descriptor runnable and uses that production loop
through the same Xtensa SYSCALL/RFE suspension contract as TTY/CLOCK/MM.
Receive/reply errors now panic rather than silently continuing with stale data.

Shell `sys` sends SYS_GETSP for its own FS descriptor, validates the reply
source/status and nonzero aligned stack pointer, then requests SYS_TIMES and
reports uptime. These are read-only requests. Both handlers now reject unused
process slots; SYS_TIMES preserves the interrupt-mask state while sampling
accounting counters. Other existing SYS services are not validated by this
image: fork/exec/exit, signals, trace, memory mutation and reset remain work.

Removable `[SYS V39 received=... op=... source=... resumed=1]` records the
first two receives and every 5000th. `[SYS IPC V39 result=0 uptime=...]`
reports successful command completion. Existing reduced heartbeat/timer
intervals are retained; other versioned markers now say V39.

[x] All 16 host test scripts pass. New SYS tests execute the production
receive/dispatch/reply loop for 250 valid/invalid requests, actual GETSP/TIMES
handlers and shell query helper; they check saved SP, accounting, uptime,
free/out-of-range targets, unknown requests and interrupt-mask preservation.
Other SYS handlers are stubbed in this bounded host test. Production IPC tests
now include SYS endpoint -2 with both arrival orders, alongside MM and TTY.

[x] Clean build without warnings, ELF sections/segments and image layout pass:
`_iram_end=0x403741B4`, `_iram_ext_end=0x40380AC8`, `_stack_top=0x3FCCDE30`.
Hardware pending: repeat sys and confirm result=0, increasing uptime and SYS
resumed=1, then exercise mm/ipc/ls with continued CLOCK progress to tick 5000.
Manual flashing only. Full SYS service correctness and CPU accounting accuracy
are not established by these read-only bring-up queries.


[x] Image-39 SYS hardware result: `docs/hardware/sys-ipc-v39.log` records
SYS resumed=1 and four result=0 queries at uptimes 255, 614, 966 and 1495.
TTY and MM return 0, ls completes, CLOCK reaches 1175 dispatches at IRQ
10000, and heartbeats continue through tick 11732 without an exception.
The long gaps between ls lines show console latency remains a real issue.

## Console scroll batching — image 40, 2026-09-19

Identity: `[FEATURE CONSOLE-BATCH 40]1`, `[TEST CONSOLE-BATCH 40]`.
MINIX console.c buffers output and flushes it after processing a write.
CP32's ST7789 uses software SPI and a text shadow buffer, not PC video RAM.
The existing batch API never set redraw_pending on scroll and was unused by
shell commands. Every newline at the bottom cleared/repainted the whole LCD.

Nested display writes now share the outer shell-command batch, including
its newline and prompt. A scroll updates the text buffer and marks one repaint
pending; later scrolls coalesce until the outer batch ends. Repainting preserves
the logical cursor. Glyph rendering no longer advances or clamps that cursor:
putc owns wrapping, so long bottom-row lines scroll the buffer correctly.
Spaces now paint background pixels, allowing them to erase previous glyphs.
Interrupts remain enabled during rendering; no SPI register/timing change is
made. Reduced heartbeat and timer diagnostic intervals are preserved.

[x] All 17 host test scripts pass. A new pixel-addressed LCD model runs the
production renderer and compares batched and unbatched output: a representative
command produces identical final pixels/text/cursor with 7 full scroll redraws
reduced to 1. It covers nested batches, bottom-row wrap, exact-width newline,
space erasure and faulted-display behavior. Existing SYS/MM/TTY/CLOCK tests pass.

[x] Clean build without warnings and ELF/image checks pass:
`_iram_end=0x40374224`, `_iram_ext_end=0x40380AD0`, `_stack_top=0x3FCCDE50`.
Hardware pending: repeat ls on a full display; verify complete lines, correct
scrolling and prompt position, and compare responsiveness with image 39.
Then run sys/mm/ipc and capture continued CLOCK progress. Fewer modeled redraws
do not prove a specific on-device speedup. A single full redraw still uses
software SPI; further transport work may be needed after this validation.


[x] Image-40 hardware: user confirms batching produces one scroll pass.
`docs/hardware/console-batch-v40.log` records three ls commands, MM result 0,
SYS result 0 at uptime 5143, TTY result 0, and CLOCK 587 messages at IRQ 5000.
No exception appears. The remaining slow black pass is a user-observed LCD
issue, not something a serial log alone can validate.

## Changed-cell LCD updates — image 41, 2026-09-19

Identity: `[FEATURE LCD-CELLS 41]1`, `[TEST LCD-CELLS 41]`.
Keep a 192-byte shadow of characters actually painted on the LCD. Scrolls
modify textbuf; the outer batch compares final text with the painted shadow
and redraws only differing opaque 15x10 cells, including spaces. Glyphs
already paint foreground and background, so no full-screen black pass is
needed. Unchanged cells and inter-row black gaps remain untouched. Explicit
clear resets both shadows; the one-time boot clear still initializes pixels.
This extends the existing MINIX-style buffered-output approach using CP32's
text grid; SPI timing/commands are unchanged.

[x] All 17 host scripts pass. The pixel model confirms zero full-screen scroll
clears, equality to clean-screen rasterization, correct space erasure/wrapping,
cursor preservation and clear/shadow reset. The representative sequence uses
161 cell writes batched versus 430 unbatched (including initial text setup).
These counts are not a measured hardware speedup.
[x] Clean build without warnings and ELF/image layout pass:
`_iram_end=0x40374264`, `_iram_ext_end=0x40380AD0`, `_stack_top=0x3FCCDF00`.
Hardware pending: repeat ls with a full LCD and check absence of the black
cleanup pass, correct blank cells and prompt placement; then sys/mm/ipc.
Software SPI can still limit the speed of the changed cells themselves.


## Punctuation follow-up — images 42/43, 2026-09-19

Image 42 (`LCD-HYPHEN 42`) added a horizontal hyphen glyph with a pixel test.
User reports that it still looks incorrect. The pasted log confirms the image
identity, SYS result 0/uptime 401, MM result 0, TTY result 0 and ls completion;
it contains no typed hyphen event. Root cause of the visual mismatch remains
unconfirmed. Image-41 scrolling was explicitly accepted by the user; its log
is saved as `docs/hardware/lcd-cells-v41.log`.

Image 43: `[FEATURE LCD-PUNCT 43]1`, `[TEST LCD-PUNCT 43]`. Hyphen is centered
within the text cell; underscore, slash and colon now have explicit glyphs
instead of generated placeholder patterns. Slash appears in the existing
MM alloc/release status. This is a plausible distinct source of a bad symbol,
not an established explanation for the user's hyphen report.
Shell font prints labelled hyphen, underscore, slash and equals samples through
the same normal display path. It provides a reproducible check independent of
keyboard punctuation input. Keyboard table storage now includes string
terminators (15 bytes for 14 key columns); mapping and column bounds are unchanged.

[x] All 17 tests pass, including pixel-exact hyphen/underscore/slash checks and
production keyboard decoding for minus, shifted underscore and release events.
Clean build and image layout pass: `_iram_end=0x403742F8`,
`_iram_ext_end=0x40380B18`, `_stack_top=0x3FCCDF90`.
Hardware pending: run font and compare the labelled samples with typed '-' and
Shift+'-'. If either differs, capture its LCD appearance and USB character code.
Existing scrolling and quiet diagnostic intervals are preserved.


[x] Image-43 punctuation hardware confirmation: user states characters now
look correct. `docs/hardware/lcd-punct-v43.log` includes minus (0x2D), underscore
(0x5F), lower/uppercase input and CLOCK count 587 at IRQ 5000 with no exception.

## Real TTY device output — image 44, 2026-09-19

Identity: `[FEATURE TTY-WRITE 44]1`, `[TEST TTY-WRITE 44]`.
MINIX reference: kernel/console.c cons_write copies bounded chunks, updates
write counts, flushes console output and replies with consumed bytes. CP32
scr_init now connects tty_devwrite to a real Cardputer LCD backend instead
of tty_devnop. numap validates each source chunk, at most 64 bytes are copied
at a time, and existing out_process applies termios newline/tab processing.
LCD writes preserve command batching and changed-cell updates. Completion
resets counts and replies once with input bytes consumed; mapping/display
errors return EFAULT/EIO. Existing inhibited-output behavior remains intact.

Shell write sends DEV_WRITE containing "TTY write via IPC\n" from its own
stack and validates reply provenance. Expect `[TTY WRITE V44 result=18
expected=18]` and the text on LCD. The count is input bytes, independent of
newline expansion. A surrounding shell batch may defer physical LCD flush
until the command/prompt is complete. This does not yet route every shell
output through TTY or implement device-backed reads/escape sequences.

[x] All 18 host scripts pass. New tests execute production do_write,
cp32_console_write and out_process with modeled memory/LCD/reply boundaries:
150-byte multi-chunk write, newline/tab expansion, byte counts, zero/negative
counts, bad mapping, busy output, LCD fault and inhibited output paths.
Existing IPC, scheduler and LCD pixel tests also pass.
[x] Clean build without warnings and ELF/image layout pass:
`_iram_end=0x40374310`, `_iram_ext_end=0x40380DD0`, `_stack_top=0x3FCCE280`.
Hardware pending: repeat write, verify the LCD text and matching byte counts,
then regress sys/mm/ipc/ls and CLOCK progress. Manual flashing only.

## TTY input transfer repair — image 45

Identity: `[FEATURE TTY-READ-COPY 45]1`, `[TEST TTY-READ-COPY 45]`.

[x] Restore MINIX `tty.c:in_transfer`'s single buffered copy and byte-count
accounting. CP32 translates the remaining destination through numap instead
of adding an x86 segment base; failed translation returns EFAULT before queue
consumption. Remove the FS-specific canonical bypass and shared-byte side
channel from this transfer routine. The direct shell reader still works
through its existing separate helper.

[x] Add production-function host coverage in tests/kernel/test_tty_read.py,
called by make tests: translated guarded destinations, 1/63/64/65/150-byte
reads, ring wrap, canonical newline/EOF, partial completion, and invalid maps.
All 19 host scripts pass; clean build, ELF/image layout, section/segment
inspection and diff whitespace checks pass without compiler warnings.
`_iram_end=0x40374310`, `_iram_ext_end=0x40380DD4`,
`_stack_top=0x3FCCE270`; input transfer stays in extended IRAM.

Hardware validation pending; no flash or serial capture performed. This is
an input-copy prerequisite, not a completed device-backed DEV_READ path.
Keyboard line-discipline ingestion, TTY wakeups and SUSPEND/REVIVE integration
remain required. Image 44's write acceptance is also still pending.


## Image 45 hardware regression — LCD and TTY write confirmed

Evidence: `docs/hardware/tty-read-copy-v45.log`; the user confirms everything
appears as expected on the LCD. Both `[FEATURE TTY-READ-COPY 45]1` and
`[TEST TTY-READ-COPY 45]` identify the image. Unchanged subsystem diagnostics
retain V44 labels; these do not identify a different flashed image.

[x] One real TTY DEV_WRITE exchange reports `result=18 expected=18` and the
user confirms correct LCD output. This validates the first hardware write
exchange for the backend introduced in image 44, running in image 45.

[x] Subsequent MM allocate/release returns 0, SYS returns 0 with uptime 1461,
TTY ioctl IPC returns 0, and ls prints capacity 65536/formatted=0. CLOCK
receives its initial two HARD_INT messages at ticks 14 and 33; kernel
heartbeats advance through ticks 1596 and 3266. No exception appears in the
capture, and initial selected-frame checks report frame=1.

Remaining: the capture has one write, not repeated-write stress, and ends
before the IRQ-5000 sample, so it does not show a later CLOCK message count.
The shell still reads through its direct helper; this regression does not
exercise the repaired DEV_READ transfer on hardware. Connect line-discipline
input, TTY wakeups and SUSPEND/REVIVE handling before claiming that feature
complete. No code or image change was made when recording this evidence.

## Task-owned console input and output — image 46, 2026-09-20

Identity: `[FEATURE TTY-READ 46]1`, `[TEST TTY-READ 46]`.
The first completed shell read emits `[TTY READ V46 reply=... n=1]`, then
every 500 reads. Existing unchanged service diagnostics retain V44 labels.

MINIX references: `minix-2.0.0/src/kernel/tty.c` do_read/in_process/
handle_events/tty_reply and `minix-2.0.0/src/fs/device.c` device I/O and
SUSPEND/REVIVE handling. CP32 keeps the caller buffer live across the initial
TASK_REPLY and any subsequent REVIVE, using its existing Xtensa syscall-frame
IPC. The existing Cardputer I2C decoder replaces the PC keyboard source;
SYSTIMER posts a coalesced TTY notification after initialization, and all I2C,
line processing, echo and replies execute in TTY task context.

[x] Connect bounded eight-event keyboard polling to in_process. Canonical
erase, line kill, EOF and echo now use MINIX queue flags. Map physical BS to
VERASE in canonical mode, preserving BS in raw mode. Initialize c_cc by named
indexes because CP32's header differs from MINIX's original positional order.

[x] Replace the shell's shared queue reader with DEV_READ and validated
TASK_REPLY/SUSPEND/REVIVE handling. Use the same reply adapter for DEV_WRITE.
Batch shell output in a bounded 256-byte buffer and submit it through TTY:
TTY is now the sole runtime LCD owner, preventing echo from interrupting a
shell-owned software-SPI transaction. Boot display setup precedes task entry.
LCD BS crosses a wrapped row so canonical erase can remove wrapped input.

[x] All 20 host scripts pass. Tests execute production driver/line-discipline
functions with decoded key events, canonical edit/echo, Ctrl-U/Ctrl-D, raw BS,
queued/immediate and suspended reads, bounded FIFO draining and invalid/busy
requests. Adapter tests cover both I/O directions, transport/malformed replies,
EOF and bounded output batching. Actual IPC/scheduler tests cover REVIVE both
before and after the client receives, plus timer notification during a blocked
TTY SEND. Pixel-model tests cover erase across a wrapped LCD row.

[x] Clean Xtensa ELF/bin build without warnings, image-layout validation,
sections/segments and whitespace checks pass. `_iram_end=0x40374334`,
`_iram_ext_end=0x40380E50`, `_stack_top=0x3FCCE420`; new I/O helpers and their
literals are placed in internal memory and fit the linker limits.

Hardware pending; no flashing or serial capture performed for image 46.
Manual acceptance:

1. Confirm both TTY-READ 46 identity markers. Type lx, Backspace, s, Enter;
   expect visible ls, `[TTY READ V46 reply=3 n=1]`, `[TTY line=ls]` and output.
2. Exercise Backspace across a wrapped LCD row, and Ctrl-U followed by ls.
   Input should echo once and execute only after Enter. An empty Ctrl-D read
   should return zero and leave the kernel diagnostic shell accepting input.
3. Repeat write (result=18 expected=18), sys, mm, ipc and ls. Type while a
   command is printing to check that TTY-owned display access stays coherent.
4. Capture IRQ 5000 with advancing clock-msgs and no exception. This is a new
   wakeup workload; earlier images' timer results do not certify it.

Raw/timed modes, cancellation/signals, sustained FIFO stress, and full user
terminal semantics remain separate hardware work. The diagnostic shell still
waits for newline to dispatch a command; it is not a user-space POSIX shell.

## Console CPU contention — image 46 observed, image 47 fix

Evidence excerpts: `docs/hardware/tty-read-v46.log`. The user confirms working
Backspace but reports severe display latency. The capture contains a 3-byte
read, successful MM/SYS/TTY exchanges, ls, and CLOCK count 673 at IRQ 5000 with
frame=1 and no exception in the supplied output. After `[TTY line=write]`,
heartbeats at ticks 7081 and 8285 precede the correct 18-byte reply. That proves
at least 1204 intervening ticks, not the complete command duration.

[x] Image-46 bounded functional hardware regression: canonical shell reads,
Backspace, service exchanges and continued timer operation. Repeated writes,
Ctrl-U/EOF/raw/timed input and latency acceptance are not established.

Source cause: main initialized four unused task endpoints (-8/-6/-5/-4) and
LOW_USER with kernel_idle_loop and flags=0. Queue-class rotation gave the old
FS renderer substantially more CPU than the new TTY renderer, which shares its
class with these idle copies. The IRQ-5000 selection (-8) directly demonstrates
one such placeholder running while the kernel has active services.

[x] Image 47 initializes those reserved descriptors with P_STOP while keeping
their frames, maps and entry points. TTY/CLOCK/SYS/MM/FS remain active; IDLE and
HARDWARE retain their special roles and are not enqueued. This follows MINIX's
use of IDLE as a fallback, without altering CP32's IRQ/RFE ownership contract,
queue fairness, keyboard wakeup, display transport or IPC behavior.

[x] Production scheduler test reproduces 100 TTY selections in 1200 ticks
with the old boot workload, versus 600 with stopped placeholders and CLOCK
continuously runnable. This model establishes CPU-share improvement, not a
sixfold hardware latency claim. All 20 host scripts and clean ELF/bin build
pass without compiler warnings. Sections/segments and image layout pass:
`_iram_end=0x4037433C`, `_iram_ext_end=0x40380F70`, `_stack_top=0x3FCCE570`.

Markers: `[FEATURE TTY-LATENCY 47]1`, `[TEST TTY-LATENCY 47]`. A removable
production trace reports `[TTY WRITE V47 bytes=... ticks=...]` for the first
eight device writes and every 500th thereafter. Ticks include rendering and
time scheduled out, but exclude time waiting for TTY to accept the request.

Hardware pending; no automatic flash. Compare repeated write/ls and typing/
erase on the LCD, capture timing markers, and verify mm/sys/ipc plus continued
CLOCK counts through IRQ 5000. Idle heartbeats may disappear during busy
rendering because placeholder processes no longer run idle code alongside it.
Software SPI still bounds display speed; no hardware speedup is claimed yet.

## Final-layout scrolling — image 48

[x] Image-47 hardware result: the user confirms improved display speed.
`docs/hardware/tty-latency-v47.log` preserves excerpts showing successful
MM/SYS/TTY, ls and write. Initial output takes 22/69 ticks; later scrolling
writes take 205–211 ticks and the separate prompt takes one tick. No exception
appears in the supplied capture. That log does not include IRQ 5000, and it
does not timestamp Enter echo, so it cannot measure the complete scroll cost.
The reported two repaint cycles remain the next display issue.

Reference: MINIX `kernel/console.c` queues output and flushes writes/echo to
PC video memory. CP32 retains its logical text/cursor behavior while deferring
costly ST7789 transfers. No panel register or bus timing changes are involved.

[x] Build the complete final text layout before pixel output. Printable cells,
wrapping, erase and scrolling only modify the in-memory text buffer inside a
batch. The outer commit compares it with painted cells, visiting each changed
cell once; intermediate and off-screen glyphs are never transmitted.

[x] Defer Enter echo's pixel repaint, retaining its logical newline/scroll
until the next output or ordinary character echo. Begin-batch preserves that
pending state. This combines the echo scroll and command response into one
paint pass without holding a display batch across IPC. Normal DEV_WRITE
newlines still commit with their output, and explicit clear resets pending
state. TTY retains sole runtime LCD ownership.

[x] All 20 host scripts and the clean ELF/bin build pass without warnings.
Pixel tests verify identical final pixels/text/cursor, no transfers before
outer commit, no clears during scrolling, and at most one paint per cell.
A distinct-row Enter-plus-command case drops from 344 cell writes across two
passes to 178 in one pass. Tests also cover output longer than the screen,
pending echo followed by a standalone character, nested batches, overwritten
glyphs and wrapped erase. These counts are model results, not measured panel
latency. ELF/image and section/segment checks pass:
`_iram_end=0x40374354`, `_iram_ext_end=0x40380F78`, `_stack_top=0x3FCCE570`.

Identity: `[FEATURE LCD-SCROLL 48]1`, `[TEST LCD-SCROLL 48]`.
Write timings now use `[TTY WRITE V48 bytes=... ticks=...]`, retaining the
first-eight/every-500 rate. Deferred Enter scrolling is included in the next
write's rendering time, so compare the full visible interaction as well as
the per-write ticks. No new raw/timed-input or long-duration claim is made.

Hardware pending; no automatic flash. Fill the LCD and repeat ls/mm/write/sys/
ipc. Expect one final scrolling repaint rather than an echo repaint followed
by an output repaint; check blank Enter, wrapped erase and typing again after
a command. If a command has not produced output yet, Enter updates logical
state but its pixel scroll remains pending. Continued CLOCK progress and
visual correctness must be confirmed on image 48.

## MEM RAM-disk device service — image 49

[x] Image-48 scrolling hardware result: the user confirms the scrolling looks
correct. Excerpts in `docs/hardware/lcd-scroll-v48.log` show repeated ls,
successful MM/SYS/TTY exchanges, an 18-byte write and a later heartbeat at tick
1928. Scrolling write samples are 143–190 ticks; no exception appears in the
supplied capture. This confirms the bounded visual/service regression, not
long-duration or raw/timed terminal validation; IRQ 5000 is absent.

MINIX reference: `kernel/driver.c:driver_task/do_rdwt` and
`kernel/memory.c:m_schedule`. CP32 reuses MEM (-4), RAM_DEV (0), byte-offset
DEV_READ/DEV_WRITE and TASK_REPLY with byte count/error status. The existing
64 KiB internal-SRAM disk replaces the reference memory geometry; no hardware
register assumptions or image-loader changes are required.

[x] Add `src/kernel/memory.c`: a receive/dispatch/reply task accepting FS
requests, validating the complete mapped buffer, copying through a bounded
64-byte buffer, and returning short transfers at EOF. OPEN/CLOSE validate the
RAM minor; other memory minors and unsupported operations are rejected. The
reply saves PROC_NR before writing its aliased reply fields. The task ignores
non-FS/stale hardware messages as MINIX does, and panics on IPC transport errors.

[x] Activate only MEM's formerly stopped descriptor and point it at mem_task.
The remaining placeholders stay stopped; MEM blocks in RECEIVE when idle.
Shell disk performs five real SENDREC operations: save last sector, write a
pattern, read/compare, restore, and verify restoration. Failed/partial test I/O
still attempts restoration; restoration failures are reported as errors.
The backing disk is neither reset nor formatted when MEM starts.

[x] All 21 host scripts pass. The MEM tests execute production service/client
functions against the real RAM disk and modeled IPC/mapping: 64-byte chunks,
sector crossings, 1 KiB blocks, EOF/short I/O, invalid requests, mapping failure,
caller policy, aliased replies and IPC errors. One hundred full diagnostic
cycles preserve the disk checksum; failure injection checks restoration after
read/write errors and partial writes. Existing scheduler/IPC tests now exercise
MEM exchanges in both arrival orders and confirm its boot activation.

[x] Clean Xtensa build without warnings, ELF/image layout, sections/segments
and whitespace checks pass. `_iram_end=0x40374368`,
`_iram_ext_end=0x403814AC`, `_stack_top=0x3FCCEF10`. New service/client code is
in extended internal IRAM; two static sector buffers add 1024 bytes of BSS.
See `docs/ramdisk-ipc.md` for the request/reply and error contract.

Identity: `[FEATURE RAM-IPC 49]1`, `[TEST RAM-IPC 49]`. Normal device requests
emit `[RAM V49 op=... result=... n=...]` for the first eight and every 5000th
request. Shell success is `[RAM IPC V49 result=0]`. Existing subsystem marker
versions are unchanged.

Hardware pending; no automatic flash. Run disk repeatedly, expecting zero
status and 512-byte service transfers, then write/ls/mm/sys/ipc with accepted
scrolling and CLOCK progress through IRQ 5000. This is the storage-device
layer, not a filesystem: ls still prints diagnostic entries, and the private
RAM format marker is not a MINIX superblock. SCATTERED_IO, filesystem mounting,
inodes, directories and a real FS server remain to be implemented.

## Read-only MINIX superblock — image 50

[x] Analyze image-49 evidence: normal boot, TTY/SYS/MM/IPC success and heartbeat
1608, with no exception in the supplied capture. Excerpts are recorded in
`docs/hardware/ram-ipc-v49.log`. No disk command or RAM trace was supplied;
MEM hardware verification and the longer IRQ-5000 regression remain pending.

[x] Compare the current port against MINIX and write `minix.port-status.md`.
The kernel/service bring-up is ahead of the historical summary; a real FS,
full MM and user binaries remain missing. SYS fork/exec frame semantics need
audit before lifecycle activation; do_fork currently clears a1 (Xtensa SP).

[x] Add `minix-super.c:cp32_minix_super_read` and read-only shell fsinfo.
Reference: MINIX `fs/super.c:read_super`, `super.h`, `type.h:d2_inode`.
Decode the serialized header explicitly rather than copying a compiler-layout
structure on Xtensa. Preserve byte-offset MEM IPC, no disk mutation, bounded
geometry and unchanged output on failure. Recognize original V2 in both byte
orders and distinguish V1 unsupported, absent magic, invalid geometry and I/O
failure. This is recognition, not mounting or filesystem integrity validation.

[x] All 22 host test scripts pass, including geometry/endian/error cases and
the real shell/MEM adapter with a checksum-preserved disk. Clean Xtensa build
has no warnings; size, segments, sections and whitespace checks pass.
`_iram_end=0x40374368`, `_iram_ext_end=0x4038186c`, `_stack_top=0x3fccf380`.
The new parser and shell handler reside in extended IRAM.

Identity: `[FEATURE MINIX-SUPER 50]1`, `[TEST MINIX-SUPER 50]` immediately
before idle. Unchanged MEM/TTY/SYS trace versions remain intentional.
Hardware pending; no flash performed. Run fsinfo (blank disk: No MINIX
filesystem), disk repeatedly (RAM IPC V49 result=0), then ordinary command
and display regressions with CLOCK progress. See docs/minix-superblock.md.

[x] Subsequent image-50 hardware capture confirms disk save/write/read/restore/
verify transfers of 512 bytes and result=0. fsinfo reads 24 bytes through MEM
and correctly reports No MINIX filesystem on the blank disk. MM/IPC/SYS/ls
and TTY replies continue; SYS uptime reaches 2147. Evidence is preserved in
docs/hardware/minix-super-v50.log. This validates the basic device path and
absent-superblock case, not valid filesystem recognition or repeated/soak I/O.

## MINIX root-directory reads — image 51

[x] Generate a deterministic original MINIX V2 demo image and provision the
volatile RAM disk before tasks start. The 63-zone filesystem leaves physical
block 63 outside its geometry for the existing disk diagnostic. It contains
root, boot directory and README; bitmap padding and inode link counts are
verified. No on-device persistent storage is modified.

[x] Add minix-dir.c root inode decoding and directory iteration through the
MEM reader. Reference: MINIX inode.c:new_icopy, read.c:read_map and
path.c:search_dir. Xtensa uses explicit disk-byte decoding and bounded stack
buffers. Preserve read-only access, validated zone bounds, endian bitmap
semantics and no published handle on failure. Seven direct zones and ASCII
names are supported; indirect directories and unsupported names fail explicitly.

[x] Replace diagnostic ls strings with actual entries, including dot/dot-dot.
No full mount, subdirectory traversal or file-content API is claimed.

[x] All 23 host scripts pass. New tests cover both byte orders, map/inode/zone
errors, deleted/full-length entries, scaled zones, zone crossings and every
read failure. Real backend plus shell/MEM adapter integration preserves the
whole disk across ten directory/disk cycles. Clean build is warning-free;
size/segments/sections checked. `_iram_end=0x4037438c`,
`_iram_ext_end=0x40381d9c`, `_stack_top=0x3fcd18d0`. Runtime additions are in
extended IRAM; the generated initialized prefix adds 8253 bytes of rodata.

Identity: `[FEATURE MINIX-DIR 51]1`, `[TEST MINIX-DIR 51]` immediately before
idle. Existing MEM diagnostics retain V49. Hardware pending; no flash performed.
Run fsinfo (32 inodes, 63 zones), ls (., .., boot, README), disk (result=0),
and repeat with ordinary services and extended CLOCK progress.

## LCD period glyph — image 52

[x] Image-51 hardware validates the generated superblock (32 inodes, 63 zones),
two root listings, and one preserved-sector disk diagnostic. Heartbeats reach
tick 2619. Evidence: docs/hardware/minix-dir-v51.log. The reported LCD period
defect is rendering-specific: USB correctly shows dot and dot-dot.

[x] Define the period in display.c:glyph_scaled as a 2x2 baseline dot. It
previously used a pseudo-pattern fallback. MINIX console.c uses an adapter
font; CP32 rasterizes characters itself on the LCD. Preserve the input byte,
15x10 opaque cell, cursor advance and final-layout scroll batching. Extend
the existing font command with a dots sample and pixel-model coverage of
single/double periods and erasure.

[x] All 23 host scripts and warning-free clean build pass. ELF size, segments
and sections checked: `_iram_end=0x403743b0`, `_iram_ext_end=0x40381da8`,
`_stack_top=0x3fcd18f0`. Whitespace checks pass.

Identity: `[FEATURE LCD-DOT 52]1`, `[TEST LCD-DOT 52]` immediately before idle.
Hardware visual validation pending; no flash performed. Run ls and font,
then erase a typed period and repeat ls until scrolling occurs. Filesystem
code and image contents are unchanged from image 51.

## Root file reads — image 53

[x] Record image-52 hardware evidence: real listing, generated superblock,
disk result=0, MM/SYS/IPC success and heartbeat 3530. The font command ran;
serial output does not establish the LCD period's visual correctness.

[x] Add root-name lookup and direct-zone file reads. MINIX reference:
path.c:search_dir, inode.c:new_icopy, read.c:rw_chunk/read_map. Xtensa decodes
disk bytes explicitly. Preserve mapped MEM IPC, read-only storage, bounded
buffers, EOF and sparse-hole semantics. Validate the full handle before
publishing; stage each device read before advancing position or copying data.

[x] Add cat for a root name, optionally preceded by one slash, with distinct
missing/directory/name errors. Text rendering uses existing TTY batching.
No full pathname resolver, mount, permission or file-descriptor API is claimed.

[x] All 23 host scripts and warning-free clean build pass. Host tests cover
endian/maps/geometry, empty/sparse files, root lookup, zone crossings, EOF and
short/error reads. Ten production cat/list/disk cycles through the real RAM
backend and modeled MEM IPC preserve the checksum. ELF sections/segments
checked: `_iram_end=0x403743b0`, `_iram_ext_end=0x4038242c`,
`_stack_top=0x3fcd1fe0`. New file functions and cat reside in extended IRAM.

Identity: `[FEATURE MINIX-READ 53]1`, `[TEST MINIX-READ 53]` immediately before
idle. Hardware pending; no flash performed. See docs/minix-file-read.md for
expected text and regression commands. Existing subsystem traces keep their
versions; the generated filesystem bytes remain unchanged.

## Keyboard Shift event polarity — image 54

[x] Record image-53 hardware log in docs/hardware/minix-read-v53.log. cat README
prints the actual demo content; cat aaa reports File not found. IRQ 5000 has
unknown=0 and clock-msgs=805; a later heartbeat reaches tick 5867. Directory
target errors and repeated file/disk cycles were not captured. The user reports
Aa ineffective, Fn apparently changing case and uppercase staying active.

[x] Correct tty.c:cp32_cardputer_key to interpret bit 7 as press, following
M5Stack's TCA8418 reader. MINIX keyboard.c:make_break requires edge-sensitive
modifiers, but PC break-bit polarity is opposite. Preserve character mapping,
TTY ownership and bounded polling; clear modifiers on release, emit text only
on press, reject events outside the 7x8 matrix and retain Ctrl-letter behavior
with Shift held. Fn and Aa coordinates match the vendor map and remain distinct.

[x] Correct synthetic events in host tests and cover repeated a/A/a, release
ordering, Fn isolation, punctuation, Ctrl-Shift and invalid coordinates.
All 23 scripts and warning-free clean build pass. ELF sections/segments checked:
`_iram_end=0x403743b0`, `_iram_ext_end=0x40382414`, `_stack_top=0x3fcd1fd0`.

Identity: `[FEATURE KBD-SHIFT 54]1`, `[TEST KBD-SHIFT 54]` immediately before
idle. Hardware pending; no flash performed. Follow docs/keyboard-shift.md.

## Uppercase A glyph — image 55

[x] Record supplied image-54 capture in docs/hardware/kbd-shift-v54.log.
It includes interleaved compiler output, so do not treat it as an uninterrupted
serial session. Visible results include disk success, cat boot and cat dot
directory errors, listings, MM/SYS, IRQ 5000 and heartbeat 6288. The user
reports uppercase A displayed as C; repeated Shift cycles were not documented.

[x] Correct display.c:glyph_scaled: uppercase A had exactly the C bitmap.
Use an apex, two stems and crossbar. MINIX console.c relies on adapter glyphs;
CP32 supplies LCD bitmaps. Preserve the character byte, keyboard behavior,
opaque cell geometry, cursor and batch scrolling. Add independent raster
expectations for A/a/C/c and a case sample to the existing font command.

[x] All 23 host scripts and warning-free clean build pass. ELF size/segments/
sections checked: `_iram_end=0x403743b0`, `_iram_ext_end=0x40382420`,
`_stack_top=0x3fcd1fe0`. Identity: `[FEATURE LCD-A 55]1`, `[TEST LCD-A 55]`
immediately before idle. Hardware visual check pending; no flash performed.

## Subdirectory paths — image 56

[x] Record image-55 hardware evidence and explicit user acceptance: LCD good
and all known issues fixed. Full supplied capture: docs/hardware/lcd-a-v55.log.
It confirms README, disk/MM/SYS/IPC/TTY, IRQ 5000 unknown=0 and heartbeat 5165.
This is bounded acceptance, not exhaustive keyboard/terminal-mode testing.

[x] Generalize directory-inode opening and add iterative path resolution.
Reference: MINIX path.c:last_dir/advance/search_dir. Xtensa retains explicit
endian disk decoding; every component uses MEM reads and validated directory
zones. Preserve bounded stack use, no recursion, no writes and unchanged
output handles on failure. Support repeated slashes and actual dot/dot-dot
entries; relative paths start at root. Enforce 14-byte components, 255-byte
paths and directory-only intermediate/trailing-slash targets.

[x] Add ls path and nested cat paths. The demo adds boot/README as a hard link
to inode 3 and updates its link count, without extra zones or prefix growth.
The final physical block remains outside filesystem geometry for disk.

[x] All 23 host scripts and warning-free clean build pass. Tests include both
byte orders, path boundaries/errors, corrupted intermediate directories,
short/error reads throughout traversal and ten checksum-preserving production
nested-cat/list/disk cycles through the real backend and modeled MEM IPC.
ELF size/sections/segments checked: `_iram_end=0x403743b0`,
`_iram_ext_end=0x40382690`, `_stack_top=0x3fcd2270`. New path helpers use
extended IRAM. No additional dynamic or static filesystem buffers are added.

Identity: `[FEATURE MINIX-PATH 56]1`, `[TEST MINIX-PATH 56]` immediately
before idle. Hardware pending; no flash performed. See docs/minix-paths.md.
Indirect zones, permissions, symlinks, cwd and a true FS server remain future.

## Reference-aligned filesystem layout — image 57

[x] Record image-56 hardware evidence in docs/hardware/minix-path-v56.log:
root and boot directory listing, nested/root README reads and heartbeat 2872.
No dot-dot/error-path or IRQ-5000 result appears in this capture.

[x] Match MINIX source responsibilities and filenames in src/fs: super.c
(geometry/bitmaps), inode.c (decode/validate inode state), path.c (directory
scan/traversal), open.c (open orchestration), read.c (data/EOF/sparse holes),
utility.c (disk endian conversion). Add matching fs.h/const.h/type.h/super.h/
inode.h/file.h/proto.h. Remove old kernel/minix-dir and minix-super files.
CP32-prefixed functions preserve their subset contract rather than pretending
to expose full MINIX server/syscall APIs. Device and boot-image glue stay in
kernel; explicit byte decoding and extended-IRAM placement remain invariants.

[x] Build FS objects under build/fs, with header dependencies. Move FS tests
to tests/fs and update the kernel MEM integration links. The source-layout
test verifies matching reference names. Add a book/source map and README guide;
historical notes retain image-era filenames with a pointer to the new map.

[x] All 23 host scripts and warning-free clean Xtensa build pass. ELF size,
sections and segments checked: `_iram_end=0x403743b0`,
`_iram_ext_end=0x4038266c`, `_stack_top=0x3fcd2240`. All FS functions and their
conversion helpers remain in extended IRAM. Disk fixture and command behavior
are unchanged; no new static filesystem state or hosted dependency is added.

Identity: `[FEATURE FS-LAYOUT 57]1`, `[TEST FS-LAYOUT 57]` immediately before
idle. Hardware pending; no flash performed. Regress ls boot, nested/root cat,
disk and other services after flashing. Future portable FS work should use
the matching MINIX source file when possible, per the user's book preference.

## Reference-aligned MM layout — image 58

[x] Record image-57 listing regression in docs/hardware/fs-layout-v57.log.
Root and boot directories list correctly; ls /boot/README yields error 8,
the expected not-a-directory status. Heartbeat reaches 3087. No MM request
or IRQ-5000 check was included in that capture.

[x] Split kernel/mm.c into mm/main.c (receive/dispatch/reply, source validation
and removable trace) and mm/alloc.c (allocation/free/ownership/coalescing and
private block table). Add mm/mm.h and mm/proto.h matching reference filenames.
Preserve CP32 helper names, protocol, static allocator state and extended IRAM.
The reference MM uses an independent process and hole list; CP32 remains a
kernel-linked bounded allocator/service, without fork/exec/signal machinery.

[x] Move numap and mem_copy unchanged into kernel/system.c, where MINIX places
process-address translation. Preserve mapped range/overflow checks and the
Xtensa flat-SRAM task-stack exception. MEM RAM-device code stays in kernel/
memory.c; early heap geometry also stays in the kernel. Remove kernel/mm.c.

[x] Build MM objects under build/mm to avoid main.o collisions. Move the MM
service/allocator test to tests/mm/test_main.py and point mapping checks at
kernel/system.c. Extend reference-layout tests and the book/source guide.
All 23 host scripts, warning-free clean build, whitespace and ELF checks pass:
`_iram_end=0x403743b0`, `_iram_ext_end=0x4038267c`, `_stack_top=0x3fcd2250`.
MM, numap and mem_copy remain in extended IRAM. No allocator/protocol or
on-disk behavior change is introduced.

Identity: `[FEATURE MM-LAYOUT 58]1`, `[TEST MM-LAYOUT 58]` immediately before
idle. Hardware pending; no flash performed. Repeat mm and filesystem/device/
service commands with continued clock progress after flashing.

## Reference-aligned library layout — image 59

[x] Compare remaining source responsibilities against minix-2.0.0. Move
memcpy/memset/strcpy/strcmp/strtol unchanged from kernel/klib.c into matching
lib/ansi filenames. Keep CP32 delay and hardware/bring-up files in place.
Preserve the freestanding call0 ABI, implementations and section attributes.
MINIX's full libc semantics are not claimed; existing strtol gaps are recorded
in the refreshed minix.port-status.md. See docs/minix-source-layout.md.

[x] Update separate library object paths and relocate runtime tests under
tests/lib/ansi. Clean build has no warnings; all 23 host scripts pass.
ELF sections/segments and image-layout check pass: _iram_end=0x403743b0,
_iram_ext_end=0x4038267c, _stack_top=0x3fcd2250.

[x] Record supplied image-58 hardware results: mm alloc-release-result=0,
cat boot/README prints the expected two lines, disk result=0, heartbeat 1369.
No repeated MM cycle or IRQ-5000 result is present in that capture.

Identity: [FEATURE LIB-LAYOUT 59]1 and [TEST LIB-LAYOUT 59] immediately before
idle. Hardware pending; no flash performed. Recheck mm, cat boot/README,
ls boot, disk and sys, with normal keyboard/LCD behavior and clock progress.

## MINIX V2 single-indirect regular-file reads — image 60

[x] Record image-59 hardware acceptance in docs/hardware/lib-layout-v59.log:
SYS/MM/IPC succeed, root and /boot list correctly, /boot/README content is
correct, TTY WRITE returns 18, IRQ 5000 has unknown=0. No disk command was
included in this capture.

[x] Implement single-indirect regular-file mapping in fs/read.c, following
MINIX read_map/rd_indir: seven direct zones plus 256 four-byte V2 entries in
one 1024-byte indirect block, independent of zone scaling. Inode open stores
validated geometry and checks the indirect root's bounds/allocation/aliasing.
Read mapping explicitly decodes either endian order, checks referenced data
zones and allocation, and preserves sparse holes. CP32 uses four-byte metadata
reads and a 64-byte staged data transfer instead of MINIX's block cache; this
preserves bounded stack use, freestanding ABI and error atomicity. Directories
remain direct-only; double-indirect regular files return unsupported.

[x] Host tests cover direct/indirect boundaries, last entry 255, both byte
orders, 1/2 KiB zones, missing indirect trees and entries, EOF, corrupt and
unallocated pointers, metadata/data short reads, unchanged buffers/positions,
and unchanged handles on open failures. All 23 scripts pass. Clean build is
warning-free; ELF/image layout checks pass. _iram_end=0x403743b0,
_iram_ext_end=0x40382870, _stack_top=0x3fcd2450. Mapping/read code stays in
extended IRAM. Previous uncommitted image-59 layout work is preserved.

Identity: [FEATURE MINIX-INDIRECT 60]1 and [TEST MINIX-INDIRECT 60] immediately
before idle. Hardware validation pending; no flash performed. Existing mm,
sys, ipc, ls /boot, cat /boot/README, disk and write commands are regression
checks only: the boot fixture has no indirect file. An on-device indirect-file
fixture is still needed to validate the new path through real MEM IPC.

## On-device single-indirect fixture — image 61

[x] Record supplied image-60 log in docs/hardware/minix-indirect-v60.log.
Root/nested README reads, boot listing, disk/MM/SYS/IPC/TTY calls succeed;
IRQ 5000 reports unknown=0 and heartbeat reaches 5332. This validates the
existing direct-file regression, not the new single-indirect path.

[x] Extend the production boot image with boot/INDIRECT (inode 4). Seven
1024-byte direct zones 9..15 contain 224 labeled lines; indirect block 16
points to data zone 17 containing the final INDIRECT READ OK line. File size
7185 bytes. Inode/zone maps and boot directory size are updated, root README
and links preserved, final scratch block 63 remains outside filesystem zones.
The generator retains its minimal fixture option for existing hostile parser
tests; its normal CLI, used by firmware and MEM integration, emits the larger
fixture. No runtime test hook or new shell command is introduced.

[x] Verify the production fixture through actual shell cat, filesystem, MEM
handler and backing storage with modeled IPC. Check every direct-zone line,
indirect tail, allocation metadata, and unchanged disk checksum. All 23 host
scripts pass; clean cross-build has no warnings and ELF/image layout checks
pass. _iram_end=0x403743b0, _iram_ext_end=0x40382870,
_stack_top=0x3fcd4820. Initialized fixture prefix is 17425 bytes (formerly
8253); task stacks remain inside DRAM. Earlier uncommitted work preserved.

Identity: [FEATURE INDIRECT-DEMO 61]1 and [TEST INDIRECT-DEMO 61] immediately
before idle. Hardware pending; no flash performed. Run ls boot, then
cat boot/INDIRECT: it prints 224 numbered direct-zone lines and must end with
INDIRECT READ OK. Then run disk, cat README and mm. The long output exercises
scrolling and scheduling as well as the first single-indirect data read.

## Read-only file seeking and tail — image 62

[x] Record image-61 hardware in docs/hardware/indirect-demo-v61.log. The full
INDIRECT fixture reaches INDIRECT READ OK, IRQ 5000 has unknown=0, subsequent
disk/MM checks return zero, and heartbeat reaches 6336. Single-indirect reads
are now hardware-validated for this fixture (not all malformed/endian cases).

[x] Add cp32_minix_file_seek to fs/open.c, matching MINIX do_lseek's file
position responsibility. Support start/current/end, signed 32-bit offsets,
beyond-EOF seeking, and unchanged state on invalid origin/negative position/
overflow. CP32 retains caller-owned handles rather than claiming descriptor
syscalls; target range is enforced on 64-bit host tests too. No cache/read-ahead
state exists to invalidate, and no disk I/O occurs during seeking.

[x] Add tail filename to the CP32 command client: last ten lines using 64-byte
backward windows, filling reads that stop at zone boundaries. Reuse cat's
existing rendering/error behavior. A trailing newline does not add an empty
line; unterminated final lines count. This is an ordinary command, not a manual
kernel diagnostic hook. Existing cat behavior remains tested.

[x] Clean build, all 23 host scripts, whitespace and ELF/image-layout checks
pass without warnings. Tests cover offset arithmetic, unchanged failure state,
EOF and indirect readback; shell/MEM integration checks the expected ten-line
indirect tail and empty/short/unterminated text. _iram_end=0x403743b0,
_iram_ext_end=0x40382b04, _stack_top=0x3fcd4ad0. Earlier uncommitted work kept.

Identity: [FEATURE MINIX-SEEK 62]1 and [TEST MINIX-SEEK 62] immediately before
idle. Hardware pending; no flash performed. Run tail boot/INDIRECT: expect
Direct zone 7, line 24 through line 32 then INDIRECT READ OK. Run tail README,
cat README, disk and mm to regress shorter reads and other services.

## Double-indirect regular files — image 63

[x] Record supplied image-62 hardware evidence: tail boot/INDIRECT shows
zone 7 lines 24–32 then INDIRECT READ OK; tail README and MM succeed. A
subsequent capture runs disk (correcting earlier dist typo): five 512-byte
transfers and RAM IPC result=0. Neither capture includes IRQ 5000.

[x] Extend fs/read.c mapping using MINIX read_map's excess/256 and excess%256
indices; inode.c decodes/validates the double root at inode byte 56. Handle
missing root/child/data as sparse holes. Validate allocation, geometry and
root/parent/direct aliases before following pointers. Four-byte explicit
endian decoding and staged 64-byte data reads preserve bounded Xtensa stack
use and unchanged caller buffer/position on failure. Regular files now cover
7+256+65536 zones; directories remain direct-only. This is read-only mapping,
not a complete filesystem integrity checker or descriptor service.

[x] Add boot/DOUBLE (inode 5): sparse logical prefix of 263 KiB, double root
18, child table 19, data zone 20. Last ten lines are Double indirect line 02
through 10, then DOUBLE INDIRECT READ OK. tail exercises the new tree without
printing/scanning the sparse prefix. Zone/inode maps and boot listing updated;
reserved scratch block remains outside the filesystem. No runtime probe added.

[x] All 23 host scripts and warning-free clean build pass. Tests cover both
byte orders and 1/2 KiB zones; single/double boundary, child-table transition,
last addressable entry, sparse trees, corrupt/unallocated roots/children/data,
short reads, unchanged failure state, and fixture tail via shell/MEM/backend.
ELF/image layout checks pass: _iram_end=0x403743b0,
_iram_ext_end=0x40382c4c, _stack_top=0x3fcd5910.

Identity: [FEATURE MINIX-DOUBLE 63]1, [TEST MINIX-DOUBLE 63] immediately before
idle. Hardware pending; no flash performed. Run tail boot/DOUBLE, then
tail boot/INDIRECT, disk and mm. Avoid cat boot/DOUBLE for routine validation:
it would render the large sparse prefix as question marks.
