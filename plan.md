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

User confirms image-43 punctuation is correct; `docs/hardware/lcd-punct-v43.log`
records '-' as 0x2D, '_' as 0x5F and CLOCK progress to IRQ 5000. Image 44
connects TTY DEV_WRITE to the LCD and adds shell write for a real byte-count
reply. All 18 host scripts and the clean build pass. Next: manually flash,
run write repeatedly and expect result=18 expected=18 plus LCD text, then
sys/mm/ipc/ls with continued CLOCK progress. Device-backed DEV_READ and
full shell routing through TTY remain later work.

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
