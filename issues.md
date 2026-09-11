# CP32 Port – Current Issues and Handoff

Latest cleaned-log hardware run passed through IRQ 176 (`t1=3234`, `t2=3157`);
IPC V18 counted `n=1..8`, and LOCK/CTX remained healthy. IPC V19 will report
the final blocked-call census before handoff work.

## LOCK V2 build-only / LOCK V1 hardware baseline

Added `lock_save()` / `restore_lock(saved_ps)` using Xtensa `rsil` and PS
restore. IPC held-queue mutations, `lock_mini_send()`, and `unhold()` now
restore the caller's original interrupt level instead of unconditionally
enabling interrupts. Clean build, image generation, ELF symbols, and
disassembly passed; the existing libgcc ABI warning remains. Hardware
validation is pending, including nested/previous-mask behavior and regression
coverage for LOCK/CTX/IPC markers. The first hardware run reported
`[LOCK V2 pass=0 saved=1 nested=1 restored=1]` and halted in the diagnostic
check. `LOCK V3` showed `psb=pso=394528`, nested `psn=394543` (level 15),
and `psr=394543`; the diagnostic sampled `psr` before the outer restore.
`LOCK V4` samples after the outer restore. Hardware then passed
`[LOCK V4 pass=1 saved=1 nested=1 restored=1]`, with IRQ 160 at
`t1=2940`, `t2=2869`; CTX/IPC/MM markers remained healthy and no exception
was reported.

## SYS V7 — syscall dispatcher integration

`_send()`, `_receive()`, and `_sendrec()` now route through `sys_call()`.
Hardware passed all existing IPC/MM checks and showed `[SYS V7]` invalid-call
rejection (`-102`), with LOCK V4 and CTX V45 continuing through IRQ 160
(`t1=2939`, `t2=2870`) without an exception. The dispatcher still returns
after setting blocked flags; true suspended execution remains the next task.

Follow-up hardware output confirmed `[SYS V7 f=...]` consistently across the
IPC validation, `[LOCK V4 pass=1]`, and CTX/IRQ stability through IRQ 160
(`t1=2940`, `t2=2870`) with no exception.

The next kernel change adds `[IPC V18 blocked-state pass=1 flags=...]` from
`sys_call()` to verify blocked state through the dispatcher before context
handoff is enabled.

Hardware confirmed IPC V18 blocked transitions for flags `4`, `8`, and `12`;
the supplied output remained healthy through IRQ 112. The next marker adds a
monotonic `n=` count so a future handoff can correlate each blocked syscall
with scheduler/context activity.

Debug-log cleanup removed repetitive transport details while retaining
versioned regression markers, blocked-state counts, MM error markers, LOCK/CTX
state, IRQ counters, and panic diagnostics. Build passed; hardware regression
after cleanup remains pending.

## LOCK V1 / CTX V45

V17 passed on hardware through IRQ 144 (2645/2575). lock previously overwrote
the complete PS with 1 before rsil, clearing unrelated status bits. It now
uses rsil 15 and rsync; unlock uses rsil 0 and rsync. The pre-timer test checks
mask levels and preservation of other PS bits, then restores the original PS.
Clean build, segments and disassembly passed, with existing libgcc ABI warning.
Hardware passed: `[LOCK V1 pass=1]` and `[CTX V45]` appeared. IRQ 176 reached
with t1=3234, t2=3156; sampled frames retained a15ok=1, spok=1, rel=1.
IRQ samples showed r=1 inside the handler and f=1; no panic or exception was
reported. All emitted IPC regression checks passed. This API does not restore
a caller's previous interrupt mask; nesting-safe IPC locks remain future work.

Session paused at the user's request with documentation updates only. Next:
implement saved-PS critical sections, then actual IPC suspension/resumption
and a task-owned message exchange. Current wrappers only change blocked flags
and return; simulated boot tests do not validate suspended execution. Correct
scheduler process-number/index classification before claiming real MINIX
quantum behavior. The clock task message loop is not yet active.

Latest markers: LOCK V1, CTX V45, IPC V17, MM V12, PANIC V1. Keep markers
updated on subsequent code changes. Real nested interrupts, concurrent task
IPC, and terminal panic behavior remain unvalidated; the libgcc ABI warning
is unresolved. This summary supersedes historical pending/validation claims.

## IPC V17 / MM V12 — translated interrupt messages

V16 passed on hardware through IRQ 128 (2351/2289). Interrupt delivery and
pending receive previously wrote directly through virtual pointers. Both now
translate the full message range and copy the HARDWARE/HARD_INT header to
the physical buffer. Translation failure retains pending/blocked state.
V17 tests both paths with a synthetic nonidentity virtual mapping and restores
the mapping afterward. Hardware passed, including the latest LOCK V1/CTX V45
run; the original translation test used CTX V44.

## IPC V16 / MM V11 / PANIC V1 — V15 failure

V15 failed: bootstrap S maps virtual 0..16383 to stack memory, so pointer 1
was legitimately translated and queued. The boot test now clears inherited
maps before installing its buffer maps. numap's bounds logic is unchanged.
Panic previously returned; it now masks interrupts preserving other PS fields,
prints PANIC V1, accepts null strings and loops forever. IRQ progress after
the V15 panic is not a successful buffer-validation result. Hardware pending.

## IPC V15 / MM V10 — buffer validation

V14 passed on hardware through IRQ 144 (2645/2577). IPC primitives now reject
unmapped message buffers before setting blocked flags or inserting queue links.
numap validates process numbers before table lookup, rejects empty/wrapping
ranges, and checks segment offsets/physical addresses using wide arithmetic.
V15 tests unmapped send/receive buffers, wrapped ranges and an invalid process
number without queue mutation. Clean build/ELF segments passed with existing
ABI warning. Hardware pending; CTX V44 is unchanged.

## IPC V14 — send deadlock correction

V13 passed on hardware through IRQ 176 (3234/3158). The existing deadlock
walk tested SENDING before caller identity, missing a runnable caller closing
the cycle. V14 matches MINIX's identity-first order, bounds traversal, and
validates send links. The new boot test checks ELOCKED without queue mutation
and then drains the original message. Hardware pending; suspended IPC is
still outstanding. CTX V44 remains unchanged.

## IPC V13 — held notification replay

V12 passed on hardware, with counter progress through IRQ 192 (3528/3449).
V13 simulates k_reenter=2 before timer enable to check duplicate notification
coalescing, deferred replay and eventual delivery to a waiting receiver.
The test restores the original nesting count. unhold clears the removed
entry's p_nextheld. Hardware pending; physical nesting and concurrent task
IPC are still unvalidated. CTX V44 remains unchanged.

## IPC V12 / CTX V44

V11 passed on hardware through IRQ 144 (t1=2646, t2=2576). Interrupt now
addresses the requested negative task number, coalesces pending notifications,
and delivers HARDWARE/HARD_INT to an eligible receiver without replacing
proc_ptr or bill_ptr. Receive consumes pending notifications. Held entries
replay from timer dispatch; CP32 nesting uses task=0, outer IRQ=1.
Boot checks use SYN_ALRM_TASK (not the reserved IDLE slot). Clean build passed
with existing ABI warning; hardware and nested/concurrent paths are pending.
Actual suspended IPC and the clock task message loop remain incomplete.

## IPC V11 — request/reply state coverage

V10 passed gateway and rejection checks on hardware, with continued counter
progress through IRQ 208 (t1=3823, t2=3736). V11 exercises _sendrec, checking
that request acceptance clears only SENDING and reply delivery clears
RECEIVING. It also checks reply source, type and payload; the consumed sender
link is now cleared. Hardware validation is pending. The test simulates callers
before IRQ enable and does not prove suspended wrapper execution.
V11 clean build and ELF segment inspection passed; existing libgcc ABI
mismatch warning remains.

## IPC V10 — 2026-09-10

V9 hardware validation passed both ordering checks; IRQ progress reached 160
with t1=2940 and t2=2871 and no exception. This proves the tested message/state
transitions, not task suspension inside IPC.

Fixed lock_mini_send: it previously ignored all arguments and invoked send
with a null message. It now calls mini_send(caller_ptr, dest, m_ptr), as in
MINIX. V10 tests this with proc_ptr deliberately different from the sender.
Null buffers, invalid send/receive endpoints and self-send now return errors
before queue/flag mutations. A separate rejection marker checks those cases.
Clean build passed with the existing libgcc ABI warning; hardware pending.

## 2026-09-10 — IPC V9 / MM V9 (build verified, hardware pending)

Both message-copy paths now assign the actual sender to m_source, matching
MINIX CopyMess. The write uses the translated receiver address. The boot test
uses m3_ca1 for text and an intentionally forged source, checks both delivery
orderings, and restores proc_ptr afterward. Mapping lengths include buffer
offsets. Expected results: receiver-first pass=1 and sender-first pass=1.
Clean build, ELF sections and segments passed; libgcc ABI mismatch warning
persists. No assembly changes in this step.

Correction to historical reports below: V8 showed `/0`, message delivery and
cleared flags, not two printed return values or suspended IPC execution.
The wrappers still return while callers have blocked flags. Real task-owned
IPC needs a protected context-switch boundary before clock_task can run.
The existing ready/unready and billing tests confuse process numbers with
table indexes (1/2 are not negative kernel task numbers). V43 demonstrated
counter progress under that temporary policy, not full MINIX scheduling.

Work-in-progress MINIX 2.0 port to the M5Stack Cardputer Adv / ESP32-S3
Xtensa LX7. Tested path:

`CP32` → `start()` → `main()` → diagnostics → periodic SYSTIMER probe → idle.

The scheduler, context switching, message passing, and normal MINIX clock
handler are not active.

## Validated on hardware

- ESP image-loader-owned `.data`/`.rodata`/IRAM placement works. Do not add
  software LMA copy loops without changing and revalidating the image format.
- `.data`/`.bss` sentinels pass.
- Vectors are resident at `0x40370000`, size `0x400`.
- Call0 stack is within linker bounds and 16-byte aligned.
- Process reverse mapping passes: `41` slots.
- 4 KiB click accounting reports about `32–33` usable clicks.
- Stack guard remains intact during idle.
- SYSTIMER UNIT0 advances.
- TARGET0 maps to CPU interrupt `2`, Xtensa level 1.
- Periodic TARGET0 interrupts enter/return safely and advance regularly.
- `k_reenter` is balanced (`r=0`).
- The guarded ISR-to-C bridge receives a non-null aligned frame inside the
  kernel stack (`c=f=s`).
## Validated on hardware
 
 - ESP image-loader-owned `.data`/`.rodata`/IRAM placement works. Do not add
   software LMA copy loops without changing and revalidating the image format.
 - `.data`/`.bss` sentinels pass.
 - Vectors are resident at `0x40370000`, size `0x400`.
 - Call0 stack is within linker bounds and 16-byte aligned.
 - Process reverse mapping passes: `41` slots.
 - 4 KiB click accounting reports about `32–33` usable clicks.
 - Stack guard remains intact during idle.
 - SYSTIMER UNIT0 advances.
 - TARGET0 maps to CPU interrupt `2`, Xtensa level 1.
 - Periodic TARGET0 interrupts enter/return safely and advance regularly.
 - `k_reenter` is balanced (`r=0`).
 - The guarded ISR-to-C bridge receives a non-null aligned frame inside the
   kernel stack (`c=f=s`).
 - The real clock-handler gate remains disabled (`e=0`).
 - Minimal scheduler with dummy task, timer-driven scheduling, and IPC stubs verified on hardware. Marker: CP32-IRQ-FRAME-64-FINAL.


Typical diagnostic:

```text
timer probe build: CP32-IRQ-FRAME-64-ABI-4
timer reentry baseline: 0 (expected 0)
.27[r=0 c=0 f=0 s=0 e=0].54[r=0 c=0 f=0 s=0 e=0]
```

Always verify the unique marker before interpreting a flashed image.

## Interrupt/frame contract

- CPU interrupt 2 is level 1 and uses `EXCCAUSE=0x04`.
- Kernel and user exception dispatchers forward only cause `0x04` to
  `irq_level1`; other causes remain terminal diagnostics.
- Level-1 entry returns with `rfe`, not `rfi 1`.
- `irq_level1` saves `a0` in `EXCSAVE1`, saves `a2–a15` in a 64-byte,
  16-byte-aligned frame, acknowledges TARGET0, updates diagnostics and
  `k_reenter`, then returns.
- `irq_frame.h` defines the C frame view; `irq_const.h` defines assembly-safe
  frame size and offsets.
- The active ISR remains assembly-only apart from a limited disabled bridge
  smoke test.

Diagnostic fields: `r` = re-entry count; `c` = bridge calls; `f` = aligned
frames; `s` = frames inside stack bounds; `e` = real clock-handler gate.

## Do not regress

- Do not restore the software relocation loops; they caused serial garbage.
- Do not treat CPU interrupt 2 as level 2.
- Do not use `rfi 1` for this exception-vector entry.
- Do not enable `clock_handler` before context-switch support exists.
- Do not include full `kernel/const.h` from assembly; use `irq_const.h`.

## Remaining implementation queue

1. Complete task/process initialization and ready-queue invariants. `pick_proc`,
   `ready`, and `unready` have isolated implementations; `sched` and the
   runnable task set remain incomplete.
2. Define the real process/context frame in `proc.p_reg`, including Xtensa
   PC, SP, PS, `a0`, and required saved state. Implement safe conversion from
   interrupt frame to process frame and post-`rfe` process selection.
3. Enable `clock_handler` only after item 2. It updates accounting, pending
   ticks, alarms, quantum state, and may request rescheduling.
4. Implement `interrupt`, `sys_call`, `mini_send`, `mini_rec`, and remaining
   message/queue operations.
5. Decode reset reason `0x0000F041`; reduce watchdog writes; populate
   `k_environ` if needed; replace magic processor value `32`; revalidate flat
   DRAM/click accounting and remove stale initialization variables.

## Verification

```sh
cd src
make clean && make
make flash
```

## 2026-09-09 build validation

- The temporary level-1 IRQ-frame contract was corrected to match the
  assembly's 80-byte allocation: saved `a0`, interrupted `a1`, `a2-a15`, and
  reserved padding. Compile-time size and `a15` offset checks now protect the
  C/assembly boundary.
- `make clean && make`, `make headers`, `make segments`, and `make nm` passed.
- ELF placement remained unchanged: vectors at `0x40370000`, executable
  segment in IRAM, and data/bss/heap/stack in DRAM.
- Hardware validation is pending. The clock-to-scheduler bridge remains
  disabled; no claim about live context switching is made from this build.
- The process-frame contract now has compile-time checks for all assembly-used
  offsets: `a[0]`, `a[1]`, `a[15]`, `pc`, `psw`, and `sp` in the 76-byte frame.
- Added a separate disabled `cp32_context_handoff_gate` for the future live
  scheduler experiment; the normal validated image does not invoke `sched()`
  from the timer IRQ.
- The next image enables only `cp32_context_handoff_gate` and identifies itself
  with `[CTX V17]`; `clock_handler` remains disabled. Hardware validation is
  required before treating this as a working process handoff.
- V17 partial hardware result: the IRQ path selected multiple saved frames and
  returned to each target's diagnostic idle loop without an immediate fault.
  The trace stopped before the periodic IRQ counters resumed; timer progress,
  queue integrity, and true task resumption remain unvalidated. All targets
  currently use the same diagnostic idle PC, so this is not yet proof of
  independent task execution.
- V18 reduces diagnostic volume without changing handoff behavior: scheduler
  lines are suppressed, context lines are rate-limited, and idle entry prints
  once. Hardware validation of the quieter image is pending.
- V18 handoff hardware validation failed: after initial frame selection,
  `[CTX V18]` reported `rel=0` with `a1` offset from `sp`, `ps=16`, changing
  stack values, and `f=2`. This indicates the live handoff is saving/restoring
  the temporary IRQ frame as a process stack rather than preserving the
  interrupted process SP. The handoff gate is disabled again in V19; do not
  re-enable it until the save/restore boundary is corrected.
- V20 fixes the identified save-side bug: `p_reg.sp` now receives the
  interrupted `a1` from the IRQ frame, not the temporary frame pointer. The
  handoff remains disabled pending safe-path hardware validation.
- V20 safe-path hardware validation passed through 208 IRQs. V21 enables only
  the handoff gate; the clock bridge remains disabled. Hardware validation is
  required before declaring process switching functional.
- V21 partial hardware validation passed: live handoff held `rel=1` and
  `a15==sp` through 112 IRQs with ongoing timer progress and no exception.
  The scheduler repeatedly selected process 2; fair rotation and independent
  task execution are still not proven.
- V22 requeues every runnable non-idle task/server as well as user processes,
  correcting the starvation identified in V21. Handoff remains enabled and
  hardware validation is pending.
- V22 hardware validation passed through 96 IRQs: process/task selection
  rotated across multiple entries, `rel=1` and `a15ok=1` stayed valid, and no
  exception occurred. Independent task counters and normal clock lifecycle
  remain unvalidated.
- V23 adds separate counter loops and PCs for processes 1 and 2. Hardware
  validation is pending; the clock bridge remains disabled.
- V23 hardware validation passed through 160 IRQs: process/task frames rotated,
  `rel=1` and `a15ok=1` stayed valid, and processes 1 and 2 showed distinct
  resumed PCs without an exception. Counter values are not yet emitted, so
  loop progress still needs direct confirmation.
- V24 adds compact `t=` values to `[CTX V24]` for processes 1 and 2 so the
  dedicated loop counters can be verified directly on hardware.
- V24 hardware validation passed through 176 IRQs: task 1 advanced `t=37`
  to `615`, task 2 advanced `t=0` to `579`, and live frame integrity stayed
  valid with `rel=1` and no exception.
- V25 isolates the handoff experiment to processes 1 and 2 after IPC
  validation and before IRQ enable. Hardware validation is pending.
- V25 hardware validation failed: process 2 advanced from `t=74` to `2097`,
  while process 1 stayed at `t=0`; frame/stack counters also diverged. V26
  disables handoff again. The next investigation is per-process stack and
  saved-frame ownership.
- V27 adds pre-IRQ process 1/2 stack-pointer and separation diagnostics while
  keeping handoff disabled.
- V27 hardware validation passed: `p1=1070325744`, `p2=1070329840`,
  separation `4096`; safe IRQ validation remained clean through 256 IRQs.
  Initial stack overlap is ruled out; live saved-frame ownership remains the
  handoff issue.
- V28 fixes the identified ownership sequence: handoff starts with the current
  idle/main frame selected, and the explicit `schedule()` call is skipped so
  process 1 is not overwritten before its first execution.
- V28 hardware validation narrowed the remaining bug: process 1 was selected
  first but never advanced, while process 2 monopolized later selections;
  frame integrity stayed valid. V29 disables handoff pending ready-queue flag
  diagnostics.
- V30 explicitly initializes process 1/2 stress frames with `ps=0x100` after
  V29 showed process 1 being restored with `ps=0`. Handoff is re-enabled for
  the controlled test; hardware validation is pending.
- V31 moves the stress PS override after the generic initializer; V30 showed
  the earlier override was immediately overwritten.
- V31 hardware validation still showed process 1 stalled at `t=0` while
  process 2 advanced to `t=2941`, despite `ps=256` and valid frames. V32
  disables handoff pending process-1 execution/queue diagnostics.
- V33 resets `current_proc` and `proc_ptr` to idle after the two-task queue is
  prepared, preventing stale bootstrap ownership during the first handoff.
  Hardware validation is pending.
- V34 aligns process 1/2 initial PS to `0`, matching the process-2 state that
  successfully executes. Hardware validation is pending.
- V35 adds a bounded `[Q V1]` scheduler trace for current flags and ready-link
  ownership to diagnose why process 1 disappears after its first selection.
- V35 hardware trace showed `cur=1`/`cur=2` alternation with both flags clear;
  process 1 was selected but its later context reports were rate-limited. V36
  removes the temporary queue trace.

For every hardware test, record the marker and `r/c/f/s/e`. A regression is
an exception, stalled counter, nonzero `r` after idle, `c/f/s` mismatch, or
unexpected `e=1`.

# Known Issues

## Kernel Crash: EXCCAUSE 0x1D / EXCVADDR 0x0
- **Symptom**: The system crashes with a LoadStore alignment or null pointer exception (`EXCCAUSE: 0x1D`, `EXCVADDR: 0x00000000`) shortly after the first periodic interrupt returns.
- **Location**: `EPC1: 0x40375A9E` is the `s32i` in `delay()`, storing through the call0 frame pointer `a15`; it is not in `printk`.
- **Cause**: The raw level-1 handler called `usbj_print_u32` before establishing a call0 frame and again immediately before `rfi`, corrupting interrupted call0 state. It also saved the post-allocation temporary frame address as the interrupted `a1`.
- **Current State**: Removed both unsafe debug calls, preserve the pre-frame SP, and corrected the `a15 == 0` fallback. Hardware validation passed: 27 periodic IRQs completed with `r=0 c=27 f=27 s=27 e=0`.

## IPC bring-up status
- The hardware run reached the IPC/MM validation and exercised `mini_send`
  and `mini_rec` without an exception.
- This is not yet full MINIX IPC validation: both calls currently exercise the
  blocking stubs/partial queue path, and the diagnostic process numbers show
  unsigned representations of negative task numbers. Task 3.1 is next.
- Task 3.1 core fixes are now implemented: MINIX process-number lookup,
  deadlock-cycle rejection, correct ready-queue removal, and `mem_copy` error
  propagation. The rebuilt image passes compilation; hardware IPC validation
  and message-content verification remain pending.
- The IPC diagnostic now uses `p_nr` consistently and emits one compact
  `[IPC B]sender->destination` marker when a sender blocks. Future changes
  should retain similarly small progress markers while bring-up is active.
- Hardware validation passed: `send=0`, `receive=0`, `flags=0`, text
  `Hello IPC!`, and `mem_copy len=36`. Task 3.1 is complete; Task 3.2 is next.
- Task 3.2 hardware validation passed: invalid syscall returned `EBADCALL`
  (`-102`), printed as `4294967194`; timer IRQ progress remained stable through
  160 ticks. Task 3.3 is next.
- V9 hardware validation passed: `[IMG V9]`, IPC/MM, `[SYS V6]`, and `[CTX V7]`
  all appeared; timer progress reached 224 IRQs with no exception. The
  transient counter skew at one sample self-corrected on the next report.
- V8 context-layout validation passed: `[CTX V8 ... a15ok=1]` appeared and the
  timer path remained clean through 144 IRQs. The real assembly handoff is
  still gated and is the next context-switch step.
- V8 assembly PC probe validated on hardware: `[CTX V8 ... pc=1077348660]`
  appeared, with IPC/MM and timer checks clean through 112 IRQs. SP/PS probing
  is the next gated step.
- V9 SP/PS probe validated on hardware: `sp=1070284720`, `ps=256`,
  `pc=1077348660`, `a15ok=1`; timer checks remained clean through 192 IRQs.
  The live register-restore experiment remains gated.
- V11 hardware validation passed: `a1` matched `sp` (`1070284736`), with valid
  PC/PS and `a15`; IPC/MM and timer diagnostics remained clean through 192
  IRQs. Next is the gated live `a1`/`a15` restore experiment.
- V12 hardware validation passed: `gate=0`, `a1==sp`, `a15ok=1`, valid PC/PS,
  IPC/MM and syscall checks, and clean timer progress through 256 IRQs. V13
  will be the first guarded live restore experiment.
- Post-split restore validation passed: V12 stayed at `gate=0`, IPC/MM and
  syscall checks passed, and timer progress reached 434 IRQs with no exception.
- V13 hardware validation passed with `gate=1`: no exception through 320 IRQs;
  IPC/MM and syscall checks remained valid. The gate currently preserves the
  complete restore behavior; a distinct reduced restore path is still needed.
- V14 hardware validation passed: the gated assembly branch ran through 163
  IRQs without exception, with `a1==sp`, `a15ok=1`, and all existing IPC/MM,
  syscall, and timer checks valid. The next step is reducing the live branch.
- V15 hardware validation passed: reduced live `a15` restore with `gate=1`
  remained stable through 136 IRQs; `a1==sp`, `a15ok=1`, IPC/MM, syscall, PC,
  and PS checks remained valid. V16 is next.
- V16 hardware validation passed: `rel=1` confirmed `a1==sp`; `a15ok=1`, IPC/MM,
  syscall, and timer diagnostics remained stable through 240 IRQs. Further
  register reduction is unsafe; the next work is real scheduler handoff.
- V16 extended hardware run passed: `[CTX V16 ... gate=1 rel=1]` appeared;
  IPC/MM and invalid-syscall checks passed, and timer diagnostics remained
  clean through 368 IRQs with `c=f=s` and `e=0`. The transient `r=1` samples
  occurred inside the level-1 handler and returned to `r=0` after return.
- V36 hardware validation kept the live handoff stable through the supplied
  run, with `rel=1`, valid `a15`, and no exception. The V35 queue trace showed
  process 1/process 2 selection alternation, but rate-limited context lines
  did not independently prove both loop counters advanced.
- V37 adds `t1`/`t2` to the existing 16-IRQ summary while retaining reduced
  context output. Hardware validation passed through IRQ 272: `t1` advanced
  from `292` to `5005`, `t2` from `253` to `4766`, with `rel=1`, `f=1`, and
  no exception. The context trace still repeatedly samples process 2, so it
  is not useful as a fairness measure now that the independent counters prove
  both loops execute.
- V38 reduces context sampling from every 8th switch to every 32nd switch;
  the first switch remains visible and the V37 counter summary is unchanged.
  Hardware validation passed through IRQ 256: `t1=4711`, `t2=4611`,
  `rel=1`, `f=1`, and no exception. The next work is scheduler lifecycle,
  not further IRQ-frame diagnostics.
- The held-interrupt replay path now retains its MINIX queue/replay behavior
  without printing one multi-line debug record per replay; hardware validation
  passed through IRQ 208 with both task counters advancing (`t1=3827`,
  `t2=3740`), `rel=1`, `f=1`, and no exception.
- V39 aligns `pick_proc()` with MINIX billing semantics: task/server picks
  preserve the prior user billing target, while user and idle picks update
  `bill_ptr`. Hardware validation is pending.
- V40 enabled the existing clock-handler bridge for the first lifecycle test;
  hardware validation passed through IRQ 256 with `t1=4705`, `t2=4605`,
  `rel=1`, `f=1`, and no exception. The process handoff gate remained
  separate.
- V41 removes the direct per-IRQ scheduler call when the clock bridge is
  active, allowing `clock_handler()` and its quantum accounting to control
  rescheduling. Hardware validation is pending.
- V41 hardware validation showed a policy regression: `t1` advanced to `9948`
  while `t2` remained `0`. Both stress entries are kernel tasks, so the MINIX
  user-quantum condition did not request a switch. V42 adds explicit rotation
  when multiple kernel tasks are ready; hardware validation is pending.
- V42 hardware validation showed `t1=34` while `t2` advanced to `9910`: the
  two-entry queue guard stopped scheduling after task 2 was selected and only
  task 1 remained queued. V43 rotates whenever a task is queued, allowing the
  current task to be requeued by `sched()`.
- V43 hardware validation passed through IRQ 192: `t1=3528`, `t2=3447`,
  `rel=1`, `f=1`, and no exception. Kernel-task rotation is now validated;
  the next risk is entering a real clock-task lifecycle rather than the
  diagnostic counter loops.
- The existing `clock_task()` still assumes blocking `receive()` and `send()`
  semantics. Do not assign the CLOCK task slot to that entry until the
  partial CP32 IPC path can block, wake, and return messages safely.
- The kernel-side `_send()` and `_receive()` wrappers previously called the
  syslib macro names and could recurse. They now dispatch directly to
  `mini_send()`/`mini_rec()`, with `_sendrec()` added; build validation is
  pending hardware blocking/wakeup validation. The supplied V43 log predates
  this change and still reports `[IPC V5][MM V5]`.
- The V6 run confirmed the rebuilt image but the test still invoked
  `mini_send()`/`mini_rec()` directly. V7 changes the test to exercise the
  kernel-facing wrappers with explicit simulated callers.
- V7 hardware validation passed: wrapper IPC returned `0/0`, delivered
  `Hello IPC!`, and timer/context checks remained clean through IRQ 208.
  V8 changes the test to receiver-first ordering for blocking/wakeup coverage.
- V8 hardware validation passed: receiver-first ordering produced the expected
  `mem_copy`, delivery, `0/0`, `Hello IPC!`, and cleared flags; timer/context
  checks remained clean through IRQ 144. The next IPC gap is task-owned
  message-loop execution.
- 2026-09-11 build-only: reordered `irq_user` frame construction so all
  interrupted call0 registers, including `a5`, are saved before reading
  `EXCCAUSE`. `make clean && make` and the image-layout check passed. No
  hardware validation has been performed for this change yet.
- 2026-09-11 hardware validation: guarded trap/scheduler image passed through
  IRQ 112. `[CTX V65]` returned `4294967194` (`EBADCALL=-102`), with
  `[CTX V66]` user-rfe count `0`, `[CTX V64]` handoff rejects `0`, and
  `[CTX V46]` showing aligned `sp`/`a1`, `a15ok=1`, and `rel=1`; IRQ samples
  reported `r=1`, `f=1`, and no exception.
- 2026-09-11 cleanup: excluded the completed one-shot V1–V64 diagnostic
  transcript from `main.c`. Build and image-layout validation pass; IRAM
  margin increased to 10,372 bytes. The compact periodic IRQ/context checks
  remain enabled.
- 2026-09-11 cleanup: removed the obsolete `[IMG V8] CP32 diagnostic image`
  startup banner. Build and image-layout validation pass; IRAM margin is now
  10,384 bytes.
- 2026-09-11 cleanup: removed the obsolete SYSTIMER/BOOT/image startup
  banners and timer probe build label. Build and image-layout validation pass;
  IRAM margin is now 10,496 bytes.
- 2026-09-11 marker: added `[CTX V82 user-frame-save-ready]` before the gated
  user probe entry. Hardware validation is pending.
- 2026-09-11 hardware validation: normal image passed through IRQ 96 with
  memory/vector/stack, IPC/MM, lock, invalid-syscall, and context checks
  passing. `a1==sp`, `a15ok=1`, and `rel=1` remained valid; no exception was
  observed. V82 was absent as expected because the user probe was disabled.
- 2026-09-11 build-only: `make test-user-probe` passed the image-layout check
  with 10,184 bytes of IRAM margin. The V82 probe marker awaits hardware
  validation.
- 2026-09-11 build-only: `make test-both-reply-probe` passed the image-layout
  check with 9,580 bytes of IRAM margin. The guarded reply path and V82 marker
  await hardware validation.
- 2026-09-11 marker/build-only: added one-shot `[CTX V83 user-frame-c-boundary]`
  after structural validation at the C trap boundary. `make test-user-probe`
  and image-layout validation pass with 10,136 bytes of IRAM margin.
- 2026-09-11 hardware validation: user-probe image reached V82/V83 and ran
  cleanly through IRQ 192 with no exception. The corrected frame reached the
  C boundary; `a1==sp`, `a15ok=1`, and `rel=1` remained valid. `t1/t2=0` is
  expected for this image because process 1 is the terminal user probe.
- 2026-09-11 cleanup: removed the high-frequency `CTX V78` scheduler trace.
  Normal build and image-layout validation pass with 10,744 bytes of IRAM
  margin; compact V46/IRQ diagnostics remain enabled.
- 2026-09-11 cleanup: removed redundant high-frequency IPC V18/V24/V79/V80/V81
  trace output. Normal build and image-layout validation pass with 11,308
  bytes of IRAM margin; one-shot IPC pass/fail markers remain.
- 2026-09-11 cleanup: removed redundant user-dispatch V67 entry/result
  messages. `make test-user-probe` and image-layout validation pass with
  11,072 bytes of IRAM margin; V83 remains as the C-boundary marker.
- 2026-09-11 cleanup: removed obsolete V68/V70–V77 user-probe trace output.
  `make test-user-probe` and image-layout validation pass with 12,068 bytes
  of IRAM margin; V82/V83 remain enabled.
- 2026-09-11 hardware validation: output passed initialization, IPC/MM,
  syscall rejection, lock, V83 C-boundary, context, and IRQ checks through
  IRQ 112 with no exception. V82 was absent from this capture despite being
  present in source; do not mark V82 hardware validation complete yet.
- 2026-09-11 hardware validation: cleaned user-probe image passed through IRQ
  80 with V82/V83 appearing once, IPC/MM and lock checks passing, valid
  context invariants, and no exception.
- 2026-09-11 hardware validation: user-probe image reached `[CTX V82]` and
  `[CTX V83]` and ran cleanly through IRQ 96. Frame alignment, `a15ok=1`, and
  `rel=1` remained valid with no exception. `t1/t2=0` and `f=0` are expected
  because process 1 is occupied by the user probe.
- 2026-09-11 hardware validation: cleaned user-probe image remained stable
  through IRQ 160 with V82/V83 present, reduced IPC output, valid frame
  alignment, and no exception.
- 2026-09-11 build-only: added the user trap-cause gate and V84 accepted-cause
  marker. `make test-user-probe` and image-layout validation pass with 12,016
  bytes of IRAM margin; hardware validation is pending.
- 2026-09-11 hardware validation: V82, V84, and V83 appeared in the expected
  order; IPC/MM and lock checks passed, context invariants remained valid, and
  periodic IRQ delivery was stable through IRQ 112 with no exception.
- 2026-09-11 build-only: added the guarded invalid-cause probe and
  `[CTX V85 trap-cause-reject pass=1]`. `make test-user-probe` and image-layout
  validation pass with 12,016 bytes of IRAM margin; hardware validation is
  pending.
- 2026-09-11 hardware validation: live user probe reached V82/V84/V83 and ran
  cleanly through IRQ 96 with no exception. V85 was absent because its helper
  is not currently invoked by the active live path; do not mark V85 hardware
  validation complete.
- 2026-09-11 build-only: wired the invalid-cause probe into the active user
  probe sequence. `make test-user-probe` and image-layout validation pass with
  11,652 bytes of IRAM margin; V85 hardware validation is pending.
- 2026-09-11 hardware validation: V82, V84, V83, and V85 appeared in order
  through IRQ 96. Accepted-cause dispatch and rejected-cause handling passed;
  context/IRQ checks remained stable with no exception.
- 2026-09-11 build-only: user-trap owner/state validation is now enforced for
  probe and production paths, with `[CTX V86 user-owner-validated]`. The probe
  image passes layout validation with 11,616 bytes of IRAM margin; hardware
  validation is pending.
- 2026-09-11 build-only: strengthened user-frame validation with `a1 == sp`
  and nonzero `a15`, adding `[CTX V87 user-frame-shape-validated]`.
  `make test-user-probe` and image-layout validation pass with 11,552 bytes of
  IRAM margin; hardware validation is pending.
- 2026-09-11 hardware validation: V82, V84, V83, V87, V85, and V86 appeared in
  order through IRQ 80. Frame shape, cause, and owner validation passed with
  stable context/IRQ operation and no exception.
- 2026-09-11 build-only: added pre-dispatch message-pointer validation and
  `[CTX V88 user-message-validated]`; the active invalid-destination probe now
  uses a mapped message buffer. `make test-user-probe` and image-layout
  validation pass with 11,460 bytes of IRAM margin.
- 2026-09-11 hardware validation: V88 appeared after the frame/cause/owner
  markers through IRQ 64, confirming mapped message-pointer validation.
  IPC/MM, syscall, lock, context, and IRQ checks passed with no exception.
- 2026-09-11 hardware validation: V82, V84, V83, V85, and V86 appeared in
  order through IRQ 112. Owner validation, cause rejection, context, and IRQ
  checks passed with no exception.
- 2026-09-11 build-only: added pre-dispatch destination validation and
  `[CTX V89 user-destination-reject]`. `make test-user-probe` and image-layout
  validation pass with 11,384 bytes of IRAM margin; hardware validation is
  pending.
- 2026-09-11 build-only: synchronized the owner saved PC after trap-PC
  advancement, including early rejection paths, and added
  `[CTX V97 owner-return-pc-synchronized]`. `make test-user-probe` and
  image-layout validation pass with 10,776 bytes of IRAM margin.
- 2026-09-11 hardware validation: V84, V83, V87, V86, V94, V97, V88, V89, and
  V95 appeared in order through IRQ 112. Owner-PC synchronization and
  rejection return passed with stable context/IRQ operation and no exception.
- 2026-09-11 build-only: added `[CTX V90 user-destination-validated]` for the
  accepted destination path. `make test-both-reply-probe` and image-layout
  validation pass with 10,760 bytes of IRAM margin; hardware validation is
  pending.
- 2026-09-11 build-only: added `[CTX V93 syscall-result-recorded]` after
  recording the syscall result in the trap frame and owner register state.
  `make test-both-reply-probe` and image-layout validation pass with 10,544
  bytes of IRAM margin; hardware validation is pending.
- 2026-09-11 build-only: made syscall return-PC advancement unconditional and
  added `[CTX V94 syscall-return-pc-advanced]`. `make test-both-reply-probe`
  and image-layout validation pass with 10,508 bytes of IRAM margin.
- 2026-09-11 build-only: moved return-PC advancement before `sys_call` so
  blocked-frame snapshots retain the post-trap PC. `make test-both-reply-probe`
  and image-layout validation pass with 10,500 bytes of IRAM margin.
- 2026-09-11 build-only: early invalid-destination returns now write
  `E_BAD_DEST` into the user frame before restore and emit V95. `make
  test-user-probe` and image-layout validation pass with 10,984 bytes of IRAM
  margin; hardware validation is pending.
- 2026-09-11 hardware validation: rejection probe reached V82, V84, V83, V87,
  V85, V86, V88, V89, and V95 through IRQ 80. `E_BAD_DEST` was recorded in
  the user frame before restore; context/IRQ checks passed with no exception.
- 2026-09-11 build-only: moved syscall return-PC advancement before argument
  validation so rejected calls cannot re-execute the trap. `make
  test-user-probe` and image-layout validation pass with 10,828 bytes of IRAM
  margin; V94 remains the marker.
- 2026-09-11 hardware validation: V94 preceded V96, V85, V88, V89, and V95
  through IRQ 80. Rejected syscalls advanced past the trap and returned their
  recorded errors; context/IRQ checks passed with no exception.
- 2026-09-11 build-only: extended early error-result propagation to invalid
  message pointers and added `[CTX V96 pointer-reject-result-recorded]`.
  `make test-user-probe` and image-layout validation pass with 10,856 bytes of
  IRAM margin; hardware validation is pending.
- 2026-09-11 hardware validation: active rejection probe reached V96, V85, V88,
  V89, and V95 through IRQ 80. Invalid pointer, cause, and destination results
  were recorded safely; no exception occurred.
- 2026-09-11 hardware validation: V94 appeared after V93 and the reply probe
  remained stable through IRQ 80. Return-PC advancement, process-3 handoff,
  context, and IRQ checks passed with no exception.
- 2026-09-11 hardware validation: follow-up output showed V94 before V93, as
  expected after moving PC advancement before `sys_call`; process 3 remained
  selected and the system stayed clean through IRQ 64 with no exception.
- 2026-09-11 hardware validation: valid BOTH/SENDREC reply probe reached V93
  after V92 and remained stable through IRQ 144. Result recording, process-3
  handoff, context, and IRQ checks passed with no exception.
- 2026-09-11 build-only: added the SEND/RECEIVE/BOTH destination contract
  guard and `[CTX V92 user-call-contract-validated]`. The reply-probe image
  passes layout validation with 10,588 bytes of IRAM margin; hardware
  validation is pending.
- 2026-09-11 hardware validation: valid BOTH/SENDREC reply probe reached V92
  after the frame/cause/owner/message/destination markers and remained stable
  through IRQ 128. Process 3 stayed selected and no exception occurred.
- 2026-09-11 build-only: added free-process destination rejection and
  `[CTX V91 user-free-destination-reject]`. `make test-both-reply-probe` and
  image-layout validation pass with 10,660 bytes of IRAM margin.
- 2026-09-11 hardware validation: valid-destination reply probe reached V82,
  V84, V83, V87, V85, V86, V88, and V90 through IRQ 64; process 3 was
  selected and no exception occurred. V91 was absent as expected.
- 2026-09-11 hardware validation: invalid-destination probe reached V82, V84,
  V83, V87, V85, V86, V88, and V89, then remained stable through IRQ 64 with
  no exception. V90 was absent as expected because this image exercises only
  destination rejection.
- 2026-09-11 hardware validation: V84, V83, V87, V86, V88, and V89 appeared;
  V89 rejected the invalid destination after message validation. Context and
  IRQ checks remained stable through IRQ 80 with no exception. V82/V85 were
  absent from this capture and remain unconfirmed for this run.
- 2026-09-11 build-only: added `[CTX V98 blocked-frame-pc-validated]` to
  verify that a blocked syscall snapshots the post-trap return PC. The
  reply-probe image passes layout validation with 10,120 bytes of IRAM margin;
  hardware validation is pending.
- 2026-09-11 build-only: restored the owner PC after the synthetic user-trap
  probe to prevent the initial user handoff from entering mid-instruction in
  the probe entry. `make test-both-reply-probe` passes layout validation with
  10,104 bytes of IRAM margin.
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
- 2026-09-11 build-only: added the dedicated `test-blocked-send-probe`
  target and SEND probe entry for the next IPC lifecycle validation. Image
  layout passes with 9,920 bytes of IRAM margin.
- 2026-09-11 build-only: extended the guarded user/blocked handoff gate to
  the SEND probe. `make test-blocked-send-probe` passes layout validation with
  9,900 bytes of IRAM margin.
- 2026-09-11 build-only: routed blocked SEND through the same direct validated
  replacement-frame path and suppressed its premature live scheduler call.
  `make test-blocked-send-probe` passes layout validation with 9,892 bytes of
  IRAM margin.
- 2026-09-11 hardware validation: blocked SEND reached V99, V102, V103, and
  V101; the replacement process 2 resumed and V46 reported process 2 through
  IRQ 160 with advancing timer values and no exception.
- 2026-09-11 build-only: configured process 2 as the waiting receiver and
  reversed the timer wake roles for the dedicated blocked-SEND probe. `make
  test-blocked-send-probe` passes layout validation with 9,860 bytes of IRAM
  margin.
- 2026-09-11 build-only: moved blocked-SEND receiver setup to the final
  pre-entry stage so generic initialization cannot clear `RECEIVING`. `make
  test-blocked-send-probe` passes layout validation with 9,828 bytes of IRAM
  margin.
- 2026-09-11 build-only: corrected SEND probe ordering so process 1 blocks
  before process 2 enters RECEIVE during the timer wake. `make
  test-blocked-send-probe` passes layout validation with 9,828 bytes of IRAM
  margin.
- 2026-09-11 build-only: enabled the timer wake hook for blocked SEND so the
  receiver-side completion path can run at tick 16. `make
  test-blocked-send-probe` passes layout validation with 9,156 bytes of IRAM
  margin.
- 2026-09-11 hardware validation: blocked SEND reached V99/V102/V103/V101,
  then V105/V104/V107/V108/V106 passed. Process 2 resumed and timer values
  advanced through IRQ 160 with no exception.
- 2026-09-11 build-only: routed blocked-SEND wake completion through
  `mini_rec()` so the queued sender, rather than only the receiver, receives
  the saved result and wake transition. `make test-blocked-send-probe` passes
  layout validation with 9,132 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V109 send-wake-owner-complete pass=1]`
  to validate blocked sender flags and frame completion after `mini_rec()`.
  `make test-blocked-send-probe` passes layout validation with 9,068 bytes of
  IRAM margin.
- 2026-09-11 hardware validation: blocked SEND wake completed with V110
  `frame=0`, V109 pass, and V112 final `frame=0 flags=0`; process 2 remained
  stable through IRQ 160 with advancing timer values.
- 2026-09-11 build-only: added the dedicated `test-blocked-sendrec-probe`
  target and BOTH/SENDREC probe entry. `make test-blocked-sendrec-probe`
  passes layout validation with 8,788 bytes of IRAM margin.
- 2026-09-11 hardware validation: blocked SENDREC completed the handoff and
  wake markers through V112; sender frame cleared, process 2 resumed, and
  execution remained stable through IRQ 128 with advancing timer values.
- 2026-09-11 build-only: removed the unused superseded blocked-frame restore
  helper; explicit saved-frame checks remain in the active handoff path.
  `make test-blocked-sendrec-probe` passes layout validation with 8,792 bytes
  of IRAM margin.
- 2026-09-11 build-only: added `[CTX V100 blocked-handoff-mask]` to identify
  the first unmet blocked-handoff predicate during the guarded probe.
  `make test-blocked-probe` passes layout validation with 9,932 bytes of IRAM
  margin.
- 2026-09-11 build-only: added `[CTX V120 user-process-identity-ready pass=1]`
  to validate process 1's canonical identity and non-free slot before user
  entry. `make clean && make test-user-probe` passes layout validation with
  9,176 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V121 user-process-table-map-ready
  pass=1]` to verify the canonical process-table slot resolves to the same
  process validated by V120. `make clean && make test-user-probe` passes image
  layout validation with 9,108 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V122 user-entry-handoff-ready pass=1]`
  to verify the user trap gate is armed after all process setup checks and
  before initial user entry. `make clean && make test-user-probe` passes image
  layout validation with 9,020 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V123 user-entry-contract-ready pass=1]`
  to verify the initial PC targets the probe entry and the assembly handoff
  routine is linked. `make clean && make test-user-probe` passes image layout
  validation with 8,924 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V124 user-entry-mode-ready pass=1]` to
  verify probe mode activation and entry alignment before assembly handoff.
  `make clean && make test-user-probe` passes image layout validation with
  8,828 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V125 user-trap-preflight-returned pass=1]`
  after the guarded trap preflight succeeds and before initial user entry.
  `make clean && make test-user-probe` passes image layout validation with
  8,812 bytes of IRAM margin.
- 2026-09-11 build-only: added `[CTX V126 user-trap-probe-count pass=1]` to
  confirm the trap preflight executed its runtime probe path. `make clean &&
  make test-user-probe` passes image layout validation with 8,728 bytes of
  IRAM margin.
- 2026-09-11 build-only: added `[CTX V127 user-trap-owner-stable pass=1]` to
  verify the preflight leaves `proc_ptr` on canonical process 1 before the
  initial handoff. `make clean && make test-user-probe` passes image layout
  validation with 8,620 bytes of IRAM margin.
- 2026-09-11 build-only fix: restored canonical `proc_ptr` and `current_proc`
  after trap preflight before V127, correcting the observed owner panic.
  `make clean && make test-user-probe` passes image layout validation with
  8,600 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 build-only: added `[CTX V128 user-trap-current-owner-aligned
  pass=1]` to verify `current_proc` agrees with canonical `proc_ptr` before
  user entry. `make clean && make test-user-probe` passes image layout
  validation with 8,508 bytes of IRAM margin.
- 2026-09-11 hardware validation: V128 passed; `current_proc` and `proc_ptr`
  were aligned to process 1, with the user rejection probe stable through IRQ
  128 and no exception.
- 2026-09-11 build-only: added `[CTX V129 handoff-frame-copy pass=1]` after
  blocked handoff frame construction to validate copied PC, PSW, SP, and
  `a15` against the selected process. `make clean && make
  test-blocked-send-probe` passes image layout validation with 7,272 bytes of
  IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V129 passed on blocked SEND; process 2 was
  selected with a matching frame, wake completed with `frame=0`, and execution
  remained stable through IRQ 160.
- 2026-09-11 build-only: added `[CTX V130 handoff-owner-selected pass=1]`
  after installing the selected process as `proc_ptr`, `current_proc`, and
  saved IRQ owner. `make clean && make test-blocked-send-probe` passes image
  layout validation with 7,160 bytes of IRAM margin. Hardware validation is
  pending.
- 2026-09-11 build-only: added `[CTX V131 handoff-owner-runnable pass=1]` to
  verify the selected handoff owner has no blocking flags before return-frame
  use. `make clean && make test-blocked-send-probe` passes image layout
  validation with 7,096 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V131 passed on blocked SEND; the selected
  process 2 was runnable, wake completed with cleared frame state, and timer
  execution remained stable through IRQ 96.
- 2026-09-11 build-only: added `[CTX V132 handoff-entry-pc-aligned pass=1]`
  to require a nonzero, instruction-aligned replacement PC before `rfe`.
  `make clean && make test-blocked-sendrec-probe` passes image layout
  validation with 7,004 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V132 passed on SENDREC; process 2 resumed
  with the validated handoff and remained stable through IRQ 128.
- 2026-09-11 build-only: added `[CTX V133 handoff-entry-psw-ready pass=1]`
  to require the expected kernel-mode PSW (`0x10`) in the replacement frame.
  `make clean && make test-blocked-sendrec-probe` passes image layout
  validation with 6,932 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware correction: V133 rejected the incorrect `0x10` saved
  PSW assumption; the user frame contract initializes the saved PSW to `0`.
  V133 now validates that established value. `make clean && make
  test-blocked-sendrec-probe` passes image layout validation with 6,940 bytes
  of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: corrected V133 passed on SENDREC; process 2
  resumed and remained stable through IRQ 128.
- 2026-09-11 build-only: added `[CTX V134 handoff-entry-sp-aligned pass=1]`
  to require a nonzero, 16-byte-aligned replacement stack pointer. `make
  clean && make test-blocked-sendrec-probe` passes image layout validation
  with 6,856 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V134 passed on SENDREC; the replacement
  stack remained aligned and execution stayed stable through IRQ 96.
- 2026-09-11 build-only: added `[CTX V135 handoff-call0-registers-ready
  pass=1]` to validate the replacement frame's `a0` and `a1` call0 values.
  `make clean && make test-blocked-sendrec-probe` passes image layout
  validation with 6,772 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V135 passed on SENDREC; process 2 resumed
  and remained stable through IRQ 96.
- 2026-09-11 build-only: added `[CTX V136 handoff-a15-ready pass=1]` to
  require a nonzero saved `a15` in the replacement frame. `make clean && make
  test-blocked-sendrec-probe` passes image layout validation with 6,708 bytes
  of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V136 passed on SENDREC; process 2 resumed
  and remained stable through IRQ 96.
- 2026-09-11 build-only: added `[CTX V137 handoff-call-args-ready pass=1]`
  to validate the replacement frame's initial `a2–a4` call argument slots.
  `make clean && make test-blocked-sendrec-probe` passes image layout
  validation with 6,620 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V137 passed on SENDREC; process 2 remained
  stable through IRQ 160 with wake state cleared.
- 2026-09-11 build-only: added `[CTX V138 handoff-owner-number-ready pass=1]`
  to verify the selected process number resolves back to its canonical table
  object. `make clean && make test-blocked-sendrec-probe` passes image layout
  validation with 6,508 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V138 passed on SENDREC; process 2 resumed
  and remained stable through IRQ 96.
- 2026-09-11 build-only: added `[CTX V139 handoff-stack-map-ready pass=1]`
  to require a nonempty stack mapping for the selected replacement process.
  `make clean && make test-blocked-sendrec-probe` passes image layout
  validation with 6,432 bytes of IRAM margin. Hardware validation is pending.
- 2026-09-11 hardware validation: V139 passed on SENDREC; the replacement
  stack mapping was present, wake completed, and process 2 remained stable
  through IRQ 128.
