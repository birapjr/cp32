# CP32 implementation plan

## Current state — 2026-09-11

The production user exception entry was tightened after review: the user
frame now saves all interrupted call0 registers before reading `EXCCAUSE`,
preserving the interrupted `a5` across non-level-1 traps. The normal image
build and image-layout check pass. Hardware validation of the guarded image
then passed through IRQ 112: `CTX V65` returned `EBADCALL` (`-102`), user-rfe
count remained zero, handoff rejects remained zero, and `CTX V46` preserved
aligned `sp`/`a1`, `a15`, and `rel=1`.

The completed one-shot V1–V64 bring-up transcript is now excluded from the
image; only compact live IRQ/context diagnostics remain enabled. The normal
image layout check passes with 10,372 bytes of IRAM margin.
The obsolete `[IMG V8]` startup banner was also removed; the rebuilt image
has 10,384 bytes of IRAM margin.
The remaining obsolete timer/startup banners were removed; the rebuilt image
has 10,496 bytes of IRAM margin.
Added `[CTX V82 user-frame-save-ready]` immediately before the gated user
probe entry for the next hardware run; no hardware result is claimed yet.
Normal-image hardware validation then passed through IRQ 96: memory, vectors,
stack, IPC/MM, lock, invalid-syscall, and live context checks passed, with
`a1==sp`, `a15ok=1`, `rel=1`, and no exception. V82 was correctly absent
because `CP32_ENABLE_USER_PROBE` was disabled.
The dedicated `test-user-probe` image also builds and passes the layout check;
its IRAM margin is 10,184 bytes and awaits hardware validation of V82.
The guarded BOTH/SENDREC reply-probe image also builds and passes the layout
check, with 9,580 bytes of IRAM margin; it is ready for the next hardware run.
Added the function/destination contract guard: `ANY` is accepted only for
RECEIVE, while SEND and BOTH require a concrete destination. V92 marks the
accepted contract; the reply-probe image builds and passes layout validation
with 10,588 bytes of IRAM margin.
Hardware validation passed through IRQ 128 with V92 following the frame,
cause, owner, message, and destination markers; process 3 remained selected
and no exception occurred.
The high-frequency `CTX V78` scheduler trace was removed after probe
validation; the normal image now has 10,744 bytes of IRAM margin.
Removed the redundant high-frequency IPC V18/V24/V79/V80/V81 trace output;
the normal image now has 11,308 bytes of IRAM margin.
Removed redundant user-dispatch V67 entry/result messages; the user-probe
image now has 11,072 bytes of IRAM margin.
Removed the obsolete V68/V70–V77 probe trace family; the user-probe image now
has 12,068 bytes of IRAM margin, with V82/V83 retained.
Latest hardware output validated initialization, IPC/MM, syscall rejection,
lock nesting, V83 C-boundary entry, aligned context restoration, and periodic
IRQs through IRQ 112 with no exception. V82 was not observed in this capture,
so its hardware validation remains unconfirmed.
Hardware validation of that image passed through IRQ 80 with V82/V83 present,
the reduced diagnostic output, valid context invariants, and no exception.
The subsequent user-probe run reached V82 and V83 and remained stable through
IRQ 96 with no exception; `a1==sp`, `a15ok=1`, and `rel=1` held. In this
probe image `t1/t2=0` and `f=0` are expected because process 1 is the probe.
The cleaned image then remained stable through IRQ 160 with V82/V83 present,
reduced IPC output, valid frame alignment, and no exception.
Added the production trap-cause gate: only the documented illegal-instruction
cause (`EXCCAUSE=0`) reaches syscall dispatch; other user exception causes now
fail closed. V84 marks the accepted cause in the probe image.
Added the guarded non-syscall-cause probe and V85 marker; the rejected-cause
path now has explicit coverage before production return handling is enabled.
The latest live probe reached V82/V84/V83 and remained stable through IRQ 96,
but did not emit V85 because `cp32_user_trap_probe()` is no longer called by
the live path after the one-shot diagnostic block was disabled. V85 remains
build-only until that probe is wired into the active test path.
The invalid-cause probe is now wired into the active `test-user-probe` boot
sequence; the image passes layout validation with 11,652 bytes of IRAM margin
and awaits hardware validation of V85.
Hardware validation completed through IRQ 96: V82, V84, V83, and V85 appeared
in order, confirming accepted-cause dispatch and rejected-cause fail-closed
behavior with stable context/IRQ operation and no exception.
Added pre-dispatch destination validation and V89 for invalid destinations;
the user-probe image builds and passes layout validation with 11,384 bytes of
IRAM margin. Hardware validation is pending.
Synchronized the owner saved PC after trap-PC advancement, including early
rejection paths, and added V97 for that scheduler-visible contract. The
user-probe image builds and passes layout validation with 10,776 bytes of IRAM
margin; hardware validation is pending.
Hardware validation passed through IRQ 112 with V84, V83, V87, V86, V94, V97,
V88, V89, and V95 observed in order; owner-PC synchronization and rejection
return remained stable with no exception.
Added V90 for accepted destinations; the guarded BOTH/SENDREC reply-probe
image builds and passes layout validation with 10,760 bytes of IRAM margin.
Added V93 to mark syscall result propagation into both the trap frame and
owner register state; the reply-probe image builds and passes layout
validation with 10,544 bytes of IRAM margin.
Syscall return PC advancement is now unconditional for accepted user syscalls;
V94 marks the three-byte trap instruction skip. The reply-probe image builds
and passes layout validation with 10,508 bytes of IRAM margin.
Moved return-PC advancement before `sys_call` so blocked-frame snapshots retain
the post-trap PC and do not re-execute the syscall on wake. V94 remains the
hardware marker; the reply-probe image builds with 10,500 bytes of IRAM margin.
Early invalid-destination returns now record `E_BAD_DEST` in the user frame
before restoration, with V95 marking that error-result path. The user-probe
image builds and passes layout validation with 10,984 bytes of IRAM margin.
Hardware validation passed through IRQ 80: V82, V84, V83, V87, V85, V86, V88,
V89, and V95 appeared in order; the rejection result was recorded and no
exception occurred.
Moved return-PC advancement ahead of all syscall argument validation, so
invalid pointer/destination calls also resume past the trap instruction. V94
marks this path; the user-probe image builds with 10,828 bytes of IRAM margin.
Hardware validation passed through IRQ 80 with V94 preceding V96/V85/V88/V89/V95;
rejected syscalls advanced past the trap and returned recorded errors without
an exception.
Extended early error-result propagation to invalid message pointers, with V96
marking `EFAULT` recorded in the frame. The user-probe image builds and passes
layout validation with 10,856 bytes of IRAM margin; hardware validation is
pending.
Hardware validation passed through IRQ 80: V96, V85, V88, V89, and V95 all
appeared in the active rejection probe, confirming pointer, cause, destination,
and error-result handling with no exception.
Hardware validation passed through IRQ 80 with V94 present after V93;
return-PC advancement, process-3 handoff, context, and IRQ operation remained
stable with no exception.
The follow-up run confirmed the corrected order V94 before V93, then remained
stable through IRQ 64 with process 3 selected and no exception.
Hardware validation passed through IRQ 144 with V93 present after V92;
the result-recording path and process-3 handoff remained stable with valid
context invariants and no exception.
Added V91 for range-valid but free destination rejection; the reply-probe
image builds and passes layout validation with 10,660 bytes of IRAM margin.
Hardware validation of the valid-destination reply probe passed through IRQ
64: V82, V84, V83, V87, V85, V86, V88, and V90 appeared, process 3 was
selected, and no exception occurred. V91 was correctly absent.
The latest invalid-destination probe validated V82, V84, V83, V87, V85, V86,
V88, and V89 through IRQ 64 with no exception. V90 was correctly absent from
this rejection-only image and remains reserved for the reply probe.
The latest probe run observed V84, V83, V87, V86, V88, and V89, then remained
stable through IRQ 80 with no exception. V89 safely rejected the invalid
destination after mapped-message validation; V82/V85 were not present in this
capture.
Added unconditional user-trap owner/state validation and V86 for the accepted
owner contract; the probe image builds and passes layout validation with
11,616 bytes of IRAM margin. Hardware validation is pending.
Strengthened the user-frame contract to enforce `a1 == sp` and nonzero `a15`,
with V87 marking the validated shape. The probe image builds and passes layout
validation with 11,552 bytes of IRAM margin; hardware validation is pending.
Hardware validation completed through IRQ 80: V82, V84, V83, V87, V85, and V86
appeared in order, confirming frame shape, cause, and owner validation with
stable context/IRQ operation and no exception.
Added pre-dispatch user message-pointer validation and V88; the active probe
now supplies a mapped message buffer before exercising its invalid destination
path. The probe image builds and passes layout validation with 11,460 bytes of
IRAM margin; hardware validation is pending.
Hardware validation completed through IRQ 64: V88 appeared after the frame,
cause, and owner markers, confirming mapped message-pointer validation; context
and IRQ operation remained stable with no exception.
Hardware validation completed through IRQ 112: V82, V84, V83, V85, and V86
appeared in order, with owner validation, cause rejection, context, and IRQ
operation stable and no exception.
Hardware validation passed through IRQ 112 with V82, V84, and V83 appearing in
order, followed by stable context/IRQ diagnostics and no exception.
Added one-shot `[CTX V83 user-frame-c-boundary]` at the C trap boundary. The
user-probe image builds and passes layout validation with 10,136 bytes of IRAM
margin; hardware validation is pending.
Hardware validation of the user-probe image passed through IRQ 192: V82/V83
were reached, the corrected frame crossed the C boundary, and the immediate
probe return completed without exception. `a1==sp`, `a15ok=1`, and `rel=1`
remained valid. The zero stress counters are expected because process 1 is
occupied by the terminal user-probe entry in this image.

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

The current probe image now exercises a complete guarded user BOTH/SENDREC
request/reply path. Process 2 queues a request to process 3, process 3 receives
it, the blocked process 2 is completed and made runnable, and both processes
continue scheduling without an idle handoff or exception. The normal image
also builds and passes the image-layout check. The production blocked-return
path remains guarded until the probe logic is reduced to the real process and
address-space setup.

Latest validated reply-probe markers:

- `[IPC V81 send-queued sender=2 dest=3]`
- `[IPC V79 receive-delivery sender=2 flags=8 receiver=3 rflags=0]`
- `[CTX V67 user-dispatch probe-ok]` after reply delivery
- stable `CTX V46` and IRQ output through at least IRQ 240
- no idle selection (`nr=4294967289`) or CP32 exception after reply wake

Committed as `c3b6da2` (`cp32: complete reply probe handoff`).

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
- [x] Added the shared runtime user-frame validator used by V59; the user
      exception vector remains terminal until trap restore is implemented.
- [x] Added a guarded C-side user-trap dispatch boundary; it validates owner,
      cause, and frame shape; the enabled path now decodes the call0 frame and
      routes SEND/RECEIVE/BOTH through `sys_call` while the gate remains off.
- [x] Added `[CTX V60 trap-boundary-guard pass=1]` to verify invalid trap
      inputs fail closed before syscall dispatch.
- [x] Added `[CTX V61 trap-dispatch-pending pass=1]` to verify valid frames
      reach the guarded boundary and remain intentionally undispatched.
- [x] Added an explicit disabled user-trap gate and `[CTX V62
      user-trap-gate pass=1]`; it will remain off until `irq_user` constructs
      and validates a real frame.
- [x] Added `[CTX V63 user-blocked-return-guard]` so a blocked user syscall
      cannot accidentally execute `rfe`; scheduler handoff is still pending.
- [x] Added the gated user-frame scheduler-handoff contract and `[CTX V64
      user-handoff-contract]`; selection and frame copy remain disabled until
      a targeted hardware probe.
- [x] Wired `irq_user` to attempt the guarded handoff on blocked returns;
      immediate returns still use the validated frame restore, and rejected
      handoffs fall back to the terminal diagnostic path.
- [x] Added `[CTX V65 enabled-trap-probe]` to exercise the enabled C trap
      boundary with a complete frame while leaving the production gate off.
- [x] Added an isolated `cp32_user_probe_entry` containing a real Xtensa
      exception instruction (`ill`) for hardware vector-entry validation; it
      is not selected by boot until the hardware experiment is enabled.
- [x] Added explicit `CP32_ENABLE_USER_PROBE` build-time activation, keeping
      the normal image unchanged while making the real probe selectable.
- [x] Added the `make -C src test-user-probe` hardware-test target so probe
      activation cannot be accidentally omitted by an ordinary build.
- [x] Initialized the probe task with a kernel-mode frame; ESP32-S3 has no
      hardware `PS.UM`, so syscall isolation uses the software gate.
- [x] Replaced unavailable `PS.UM` usage on ESP32-S3 with a kernel-mode
      software syscall gate routed through the same validated user frame.
- [x] Add hardware markers for blocked-frame save, wake, restore, and resumed
- [x] Add hardware counters/markers for blocked-frame save, wake, and restore
      eligibility; the actual resumed syscall return remains gated.
- [x] Added `[CTX V69 resumed-syscall-return]` at the shared wake completion
      point to record blocked-frame result restoration and resumed-return
      eligibility.
- [x] Verified the blocked RECEIVE wake through the mapped message-copy path;
      `[IPC V24 real-send-wake]` now precedes the cleared receiver flags and
      stable post-wake IRQ/context stream.
- [x] Shared blocked-message delivery between normal `mini_send` and the
      mapped probe, preserving copy, frame completion, and ready-queue rules.
- [x] Added the guarded BOTH/SENDREC reply probe, including a real request
      queue, receiver delivery, blocked-frame completion, reply wake, and
      resumed scheduling.
- [x] Stabilized reply-probe process identities by restoring canonical
      `pproc_addr` mappings and clearing stale ready-queue entries after stress
      setup.
- [x] Added reply-path diagnostics V70/V72/V73/V74/V75/V76/V77/V78/V79/V80/V81
      to distinguish trap dispatch, queue insertion, receive delivery, handoff,
      and scheduler state.
- [x] Verified both the reply-probe image and the normal image compile with
      the image-layout check passing.

## Remaining kernel work

- [ ] Complete production Xtensa user exception/trap entry and separate
      user-frame layout from the level-1 interrupt frame.
- [ ] Generalize the validated probe path into production SEND, RECEIVE, and
      BOTH syscall routing with real process/address-space ownership and
      privilege/cause validation.
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

- 2026-09-11 build-only: added `[CTX V98 blocked-frame-pc-validated]` to
  verify that a blocked syscall snapshots the post-trap return PC. The
  reply-probe image passes layout validation with 10,120 bytes of IRAM margin;
  hardware validation is pending.
- 2026-09-11 build-only: restored the owner PC after the synthetic user-trap
  probe so the initial user handoff cannot jump into the middle of the probe
  instruction stream. `make test-both-reply-probe` passes layout validation
  with 10,104 bytes of IRAM margin.
- 2026-09-11 hardware validation: blocked RECEIVE probe reached V98, V84,
  V83, V87, V86, V94, V97, V88, V90, V92, and V93, then remained stable
  through IRQ 192 with no exception. Timer readings remained consistent.
- 2026-09-11 build-only: added `[CTX V99 blocked-handoff-ready]` for the
  fully validated blocked-owner scheduler-handoff predicate; the return gate
  remains fail-closed pending hardware validation.
- 2026-09-11 build-only: corrected V100 to observe the real user-probe path,
  not only the synthetic scheduler test. `make test-blocked-probe` passes
  layout validation with 9,932 bytes of IRAM margin.
- 2026-09-11 build-only: corrected the blocked-handoff predicate to require a
  valid, matching saved frame and enabled its gate only for the dedicated
  blocked probe. `make test-blocked-probe` passes layout validation with 9,948
  bytes of IRAM margin.
- 2026-09-11 build-only: added a bounded retry that rejects a stale blocked
  owner selected from a legacy ready queue before exception return. `make
  test-blocked-probe` passes layout validation with 9,916 bytes of IRAM margin.
- 2026-09-11 build-only: explicitly unlinked the blocked owner from all ready
  queues before scheduler handoff. `make test-blocked-probe` passes layout
  validation with 9,908 bytes of IRAM margin.
- 2026-09-11 build-only: added a blocked-probe-only process-2 replacement
  fallback when the scheduler reports the blocked owner. `make
  test-blocked-probe` passes layout validation with 9,876 bytes of IRAM margin.
- 2026-09-11 build-only: assigned the replacement process a non-trapping
  terminal user entry so the blocked handoff cannot re-enter the illegal-
  instruction probe. `make test-blocked-probe` passes layout validation with
  9,852 bytes of IRAM margin.
- 2026-09-11 build-only: made the replacement process-2 selection explicit in
  the blocked probe after scheduler evaluation. `make test-blocked-probe`
  passes layout validation with 9,860 bytes of IRAM margin.
- 2026-09-11 build-only: added V101 to dump the selected handoff frame's
  process number, PC, SP, and a15 immediately before exception return.
  `make test-blocked-probe` passes layout validation with 9,732 bytes of IRAM
  margin.
- 2026-09-11 build-only: isolated the blocked probe from the failing live
  `sched`/`switch_to` path by selecting process 2's validated frame directly;
  production scheduling remains unchanged. `make test-blocked-probe` passes
  layout validation with 9,592 bytes of IRAM margin.
- 2026-09-11 build-only: prevented `sys_call` from scheduling before the
  guarded blocked-probe handoff. `make test-blocked-probe` passes layout
  validation with 9,604 bytes of IRAM margin.
- 2026-09-11 hardware validation: V99 passed; V102 showed distinct owner and
  replacement pointers, and V101 showed process 2's valid PC/SP/a15 frame.
  Execution remained stable through IRQ 176 with no exception. V46 continues
  to identify the interrupted owner during this diagnostic handoff.
- 2026-09-11 build-only: aligned blocked-probe context ownership with the
  restored process-2 frame for subsequent V46 diagnostics. `make
  test-blocked-probe` passes layout validation with 9,588 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V103 user-rfe-frame-ready pass=1]` at
  the validated replacement-frame return boundary. `make test-blocked-probe`
  passes layout validation with 9,568 bytes of IRAM margin.
- 2026-09-11 hardware validation: V102, V103, and V101 confirmed the process-2
  replacement frame at the pre-rfe boundary; execution remained stable through
  IRQ 128 with no exception.
- 2026-09-11 build-only: synchronized the probe's saved IRQ owner with the
  process-2 replacement frame for post-rfe timer diagnostics. `make
  test-blocked-probe` passes layout validation with 9,564 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V104 blocked-wake-result-slot pass=1]`
  to validate wake completion's saved `a2` result slot. `make
  test-blocked-probe` passes layout validation with 9,496 bytes of IRAM margin.
- 2026-09-11 build-only: added V106 after the wake attempt to report delivery
  result, receiver flags, and wake count. `make test-blocked-probe` passes
  layout validation with 9,288 bytes of IRAM margin.
- 2026-09-11 build-only: reset the one-shot V104 flag at user-probe start so
  the hardware wake completion is reported independently of earlier wake
  tests. `make test-blocked-probe` passes layout validation with 9,280 bytes
  of IRAM margin.
- 2026-09-11 hardware validation: V105 observed the blocked receiver, V104
  confirmed the saved `a2` wake result, and V106 confirmed successful wake with
  flags cleared. The replacement frame remained stable through IRQ 160.
- 2026-09-11 build-only: added `[CTX V107 wake-owner-released pass=1]` for
  post-wake runnable-state and blocked-owner release validation. `make
  test-blocked-probe` passes layout validation with 9,204 bytes of IRAM margin.
- 2026-09-11 hardware validation: V104, V106, and V107 passed; the blocked
  receiver's result slot was restored, flags cleared, and blocked-owner state
  released. Execution remained stable through IRQ 160 with no exception.
- 2026-09-11 build-only: added `[CTX V108 wake-message-source-validated
  pass=1]` to validate the sender identity copied into the awakened message.
  `make test-blocked-probe` passes layout validation with 9,144 bytes of IRAM
  margin.
- 2026-09-11 hardware validation: V108 passed; the awakened message source
  matched the synthetic sender, with V104/V106/V107 also passing. Execution
  remained stable through IRQ 128 with no exception.
- 2026-09-11 build-only: added `[CTX V100 blocked-handoff-mask]` to identify
  the first unmet blocked-handoff predicate during the guarded probe.
  `make test-blocked-probe` passes layout validation with 9,932 bytes of IRAM
  margin.
