
## Current handoff — 2026-09-10, IPC V11 / MM V9

- [x] V10 hardware: gateway/rejection pass=1; IRQ 208 reached with
      t1=3823, t2=3736, no exception in supplied output.
- [x] Add SENDREC request/reply state test: SENDING|RECEIVING becomes
      RECEIVING when the request is accepted, then runnable after reply.
      Clear the consumed sender link. This is a simulated boot-time exchange.
- [ ] Hardware: expect `[IPC V11 sendrec pass=1]` plus previous passing tests.
- [x] V11 clean build and ELF segment inspection passed; existing libgcc ABI
      mismatch warning remains.

- [x] V9 hardware checks passed in both orderings; IRQ 160 reached with
      t1=2940, t2=2871 and rel=1, no exception in the supplied output.
- [x] V10 fixes lock_mini_send to forward its explicit arguments, matching
      the MINIX reference. Reject null IPC buffers, invalid endpoints and
      sends to self before altering blocked flags/queues.
- [x] Clean V10 build passed; existing libgcc ABI warning persists.
- [ ] Hardware: expect `[IPC V10 gateway pass=1]` and
      `[IPC V10 rejection pass=1]` after the retained V9 regression tests.

- [x] Restore MINIX CopyMess sender identity in both delivery paths, using
      the translated destination address. Payload no longer overwrites headers.
- [x] Add receiver-first and sender-first checks for return values, blocked
      flags, cleared flags, source identity, type, payload, and sender queue drain.
      Preserve the original proc_ptr after the simulated exchange; account
      for buffer offsets when calculating mapped clicks.
- [x] Clean build and ELF section/segment checks passed (existing libgcc ABI
      mismatch warning remains).
- [ ] Hardware: expect both `[IPC V9 receiver-first pass=1]` and
      `[IPC V9 sender-first pass=1]`, then advancing t1/t2 counters.
- [ ] Implement actual suspended IPC calls and a task-owned exchange.
      Current wrappers return after setting blocked flags; these boot tests
      validate IPC state transitions, not suspended execution.

Historical correction: V8 printed `/0`, not `0/0`; it did not print the
receive return code or demonstrate suspension. Earlier V43 logs cannot be
dated relative to wrapper edits from unchanged markers alone. Process numbers
1/2 were incorrectly classified as tasks by the existing NR_TASKS comparison;
the clock-task/quantum lifecycle remains unfinished.

## Project identity

CP32 is a work-in-progress, bare-metal, Unix-like operating-system port for the M5Stack Cardputer Adv, built around the Espressif ESP32-S3FN8 (Xtensa LX7, dual core). The kernel is based on the MINIX 2.0 architecture and source style, adapted incrementally for the ESP32-S3 rather than running on a PC BIOS, 8259 PIC, or 8253 PIT.

The repository is currently kernel-focused. The long-term goal described by the project is a kernel, shell, and applications, but the current tree does not yet contain those user-space components.

## Reference

Use the folder minix-2.0.0 as port reference and copy code from the refence folder. When copy not possible port the code to ESP32-S3. Keep the as close as possible folder and file structure.

## AI Code Agent rules to follow

Before modifying kernel/assembly code:

1. Identify the MINIX v2 original behavior. Use folder minix-2.0.0 as reference.
2. Identify the ESP32-S3 architectural difference.
3. Identify the CP32 invariant being preserved.
4. Make the smallest testable change.
5. Build with the existing Makefile.
6. Inspect ELF sections/symbols when relevant.
7. Never assume hardware behavior without documentation or a
   hardware validation result.
8. Never replace bare-metal code with ESP-IDF unless explicitly requested.
9. Update issues.md with hardware validation results.
10. Update plan.md with tasks in progress, so next sessions can pick-up where was stopped
10. Update plan.md with all completed task and validated on hardware.
11. General Testing Procedure – For every change:
11.1. Modify the code.
11.2. `make clean && make` in `src/`.
11.3. fix compilation errors
11.4. jump back to 11.2. and repeat

This incremental plan keeps each change small, build‑test‑validate cycles, and provides clear console output to aid human and agent debugging.

## Task 1 - review corrent code with refence minix source
- [x] 1. check what was implemented and found critical derivations that could break the port in the future
- [x] 2. write the plan for the fixes to the issues foound above Task 1.1 as new tasks in this files

---

## Task 2 - Fix Critical Derivations (Post-Review)

The following tasks address critical architectural gaps identified during the review against MINIX 2.0.0.

### 2.1 Process Table Initialization
**Goal**: Ensure every process has a valid initial state.
**Change**: In `main.c`, implement the initialization loop to set `p_reg` (PC, SP) and `p_map` for all tasks and processes based on the MINIX reference.
**Verification**: Build and verify that `proc_ptr` points to a valid register frame.
- [x] Implemented initialization loop for `p_reg` and `p_map`.
- [x] Fixed Null Pointer crash by initializing `pproc_addr` and ready queues (`rdy_head`/`rdy_tail`) before use.
- [x] Verified stable boot and scheduling on hardware.

### 2.2 Restore Round-Robin Scheduling
**Goal**: Implement fair multi-tasking.
**Change**: In `proc.c`, update `sched()` to rotate the `USER_Q` (move current process to tail) before calling `pick_proc()`.
**Verification**: Flash and verify multiple user processes are interleaved.
- [x] Implemented round-robin rotation in `sched()` (moves current user process to tail of its queue).
- [x] Verified stable boot and scheduling on hardware.

### 2.3 Implement Interrupt Held Queue
**Goal**: Prevent race conditions during critical sections.
**Change**: In `proc.c`, implement `p_int_held` and `held_head/tail` logic in `interrupt()` and implement the `unhold()` function.
**Verification**: Stress test with high-frequency interrupts during context switches.
- [x] Implemented `interrupt(task)` to hold interrupts during `switching`.
- [x] Implemented `unhold()` to re-trigger held interrupts.
- [x] Fixed compilation errors in `proc.c` related to `usbj_print`.
- [x] Verified basic functionality; stress testing deferred to hardware validation.

### 2.4 Refactor Clock-Scheduler Path
**Goal**: Align with MINIX high-level scheduling architecture.
**Change**: Modify `clock.c` and `irq.S` so the ISR calls `interrupt(CLOCK)` instead of `schedule()` directly. Let `do_clocktick` manage the quantum.
**Verification**: Verify that `lock_sched()` is called only when the quantum expires.
- [x] Updated `cp32_timer_irq_dispatch` in `clock.c` to call `clock_handler()`.
- [x] Verified behavior on hardware: `clock_handler` triggers `do_clocktick` via `interrupt(CLOCK)`.
- [x] Validated on hardware.

### 2.5 Structural Alignment of `struct proc`
**Goal**: Support future MM and Signal features.
**Change**: Audit `proc.h` and add missing state variables (e.g., signal pending counts, shadow pointers) found in the MINIX 2.0.0 reference.
**Verification**: Compilation check with all kernel modules.
- [x] Audited `proc.h` against MINIX 2.0.0.
- [x] Added `p_shadow` to `struct proc` for MM support.
- [x] Verified compilation.

---

## Task 3 - Core Kernel IPC & Process Lifecycle
**Goal**: Move from stub IPC to a functioning MINIX-style message passing system.

### 3.1 Implement `mini_send` and `mini_rec`
**Goal**: Enable basic inter-process communication.
**Change**: Port the logic from `minix-2.0.0/src/kernel/proc.c` to `src/kernel/proc.c`, implementing the blocking/unblocking logic and queue management.
**Verification**: Create two dummy tasks that send/receive messages and verify they block/unblock correctly.

### 3.2 Implement `sys_call` Trap Handler
**Goal**: Allow user-space (future) to request kernel services.
**Change**: Implement the entry point in `proc.c` that dispatches `SEND`, `RECEIVE`, and `BOTH` based on the trap frame.
**Verification**: Trigger a manual system call from `main.c` to verify the dispatch path.

### 3.3 Process Context Switching (The "Big Jump")
**Goal**: Implement actual register saving/restoring.
**Change**: Update `mpx32.S` and `irq.S` to save the current process state into `p_reg` and load the next process state from `p_reg` during `switch_to`.
**Verification**: Verify that two tasks can actually swap execution and maintain their own stack/PC.

---

## Task 4 - Memory Management (MM) Foundation
**Goal**: Implement the basic memory protection and allocation logic.

### 4.1 Port `mm/` subsystem
**Goal**: Establish the Memory Manager task.
**Change**: Copy and adapt `minix-2.0.0/src/kernel/mm.c` (and associated headers).
**Verification**: Verify the MM task can initialize the system memory map.

### 4.2 Implement `mem_copy` and `mem_move`
**Goal**: Safe memory transfer between processes.
**Change**: Implement the kernel-level copy routines that respect the `p_map` boundaries.
**Verification**: Test copying data between two different process memory segments.

---

## Task 5 - Device Driver Framework & Cardputer Hardware
**Goal**: Establish the driver model and integrate Cardputer Adv specific peripherals.

### 5.1 Port `driver.c` logic
**Goal**: Implement the unified driver interface.
**Change**: Implement the `drv_` prefix functions for device interaction.
**Verification**: Register the USB Serial, Keyboard, and Display as system devices.

### 5.2 Keyboard Driver & Input Path
**Goal**: Allow hardware-based command entry.
**Change**: Implement the I2C/GPIO driver for the Cardputer Adv keyboard. Map keypresses to kernel input events or TTY characters.
**Verification**: Press a key on the Cardputer and see the character appear in the kernel console.

### 5.3 Display Driver & Output Path
**Goal**: Provide visual feedback on the device screen.
**Change**: Implement the driver for the Cardputer Adv display (SPI/I2C). Create a basic `printk`-like output for the screen.
**Verification**: Print "CP32 Kernel Booted" on the physical display.

### 5.4 Full TTY Line Discipline
**Goal**: Implement the MINIX TTY layer for the Cardputer.
**Change**: Connect `tty.c` to both the serial driver and the keyboard driver.
**Verification**: Type commands on the keyboard and see them echoed on the display and serial console.

---

# Implementation Plan

## Goal: Fix recurring kernel crash at `printk` during interrupt return

### Completed
- [x] Fix `irq_level1` save path (correctly save `a0-a15`).
- [x] Fix `irq_level1` restore path (correctly restore `a1-a15`).
- [x] Initialize `a15` to non-null value in `main.c` for all processes.
- [x] Add safety fallback in `irq.S` to set `a15 = a1` if `a15` is `NULL`.
- [x] Verify scheduler basic context switch to IDLE loop.

### Next Steps
- [x] **Analyze fault disassembly**: `0x40375A9E` is `delay()`'s `s32i` through the call0 frame pointer `a15`, not `printk`.
- [x] **Fix IRQ call0 boundary**: Removed diagnostic C calls from the raw level-1 handler and preserved the interrupted SP before allocating the IRQ frame.
- [x] **Fix `a15` validation**: The restore path now compares the restored frame pointer against zero instead of comparing it with `a0`.
- [x] **Hardware validation**: Hardware run completed 27 periodic IRQs with `r=0 c=27 f=27 s=27 e=0`; no exception or stalled counter.

## Next task: 3.1 — complete MINIX IPC send/receive

- [x] Port the core MINIX queue behavior: process-number mapping, deadlock
      detection, immediate delivery, sender queueing, and receiver queueing.
- [x] Correct the bring-up test to pass MINIX process numbers (`p_nr`) rather
      than raw `proc[]` indexes.
- [x] Correct the IPC test memory maps to cover the actual message buffers;
      the previous fake mappings caused the observed `EFAULT` result.
- [x] Correct the test segment virtual base to the actual buffer address;
      `mem_vir=0` still rejected real stack pointers during `numap()`.
- [x] Correct `numap()` click-to-byte bounds conversion and page-align the
      test segment bases so IPC buffer offsets translate correctly.
- [x] Add `[BOOT V3]` stage markers around timer enable and IPC/MM validation
      to distinguish the flashed image and detect early-output loss.
- [x] Bump boot and IPC/MM diagnostics to `[BOOT V4]`, `[IPC V4]`, and
      `[MM V4]` for unambiguous hardware image tracking.
- [x] Fix `mini_send()` to accept valid negative MINIX task numbers using
      `isokprocn()` instead of rejecting every task destination.
- [x] Bump the IPC/MM validation marker to V5 for this fix.
- [x] Add compact `[MM E]` translation diagnostics for the remaining IPC
      bounds failure; use the next hardware result to complete Task 3.1.
- [x] Fix blocking order so a runnable process is removed from its ready queue
      before its `SENDING` or `RECEIVING` flag is set.
- [x] Propagate `mem_copy` failures instead of reporting successful delivery.
- [ ] Add a deterministic two-process send/receive test that verifies delivery,
      unblocking, and message contents.
- [ ] Keep the timer IRQ frame marker active while validating IPC.
- [ ] Hardware validation: confirm the corrected test reports delivery and
      cleared sender/receiver flags.

### Task 3.1 hardware result

- [x] Hardware IPC/MM validation passed: `send=0`, `receive=0`, `flags=0`,
      message text `Hello IPC!`, and `mem_copy len=36`.
- [x] Hardware timer regression remained clean during the same run; no
      exception was reported through 176 IRQs.

## Next task: 3.2 — syscall trap dispatch

- [x] Audit and constrain `sys_call` to MINIX SEND/RECEIVE/BOTH function codes;
      reject invalid functions and null dispatcher inputs.
- [x] Add compact `[SYS V6]` result diagnostics and an invalid-function test.
- [ ] Connect the dispatcher to the Xtensa user trap entry when the user frame
      semantics and connect it to the Xtensa trap entry when the user frame is
      available.

### Task 3.2 hardware result

- [x] Hardware validation passed: invalid syscall returned `EBADCALL` (`-102`,
      displayed as `4294967194`), with IPC/MM and timer diagnostics still clean.

## Next task: 3.3 — Xtensa process context switching

- [ ] Define the C/assembly process-frame offsets as one shared contract.
- [ ] Save and restore the interrupted PC, PS, SP, and registers without
      corrupting the call0 frame pointer.
- [ ] Add a compact `[CTX V7]` marker around the first controlled switch.
- [x] Instrument the active C switch boundary with `[CTX V7 p=<nr> sp=<sp>]`
      and reject a null switch target; full assembly handoff remains pending.
- [x] Add early `[IMG V8]` and pre-IRQ markers before interrupt enable so the
      flashed image can be identified even when post-enable output interleaves.
- [x] Add `[IMG V9]` timer-bridge gating so the first IRQ cannot switch
      `proc_ptr` away from `main()` before bring-up validation completes.
- [ ] Hardware validation: confirm post-enable boot, IPC, and `[CTX V7]`
      output now remain reachable with the bridge gated.
- [x] Hardware validation passed with `[IMG V9]`: IPC/MM, syscall validation,
      `[CTX V7]`, and timer progress through 224 IRQs completed without an
      exception; final counters remained consistent.

### Next context-switch step

- [ ] Replace the diagnostic C-only switch boundary with a controlled assembly
      handoff using the shared `struct stackframe_s` offsets.
- [ ] Validate one process switch at a time and retain compact versioned
      `[CTX V8]` diagnostics.
- [x] Centralize process-frame offsets and add a compile-time 76-byte layout
      assertion; active switch diagnostics now report `[CTX V8]` and `a15ok`.
- [x] Hardware validation passed: `[CTX V8 p=... sp=... a15ok=1]` appeared;
      IPC/MM, syscall, and timer IRQ checks remained clean through 144 IRQs.
- [ ] Prepare the first gated assembly context handoff test; do not enable it
      in the normal IRQ path until its save/restore frame is independently
      verified.
- [x] Add non-mutating assembly probe `cp32_context_probe_pc()` and extend the
      marker to `[CTX V8 ... pc=...]`; the real register handoff remains gated.
- [x] Hardware validation passed: `[CTX V8 ... pc=1077348660]` reported a
      valid target PC with `a15ok=1`; IPC/MM, syscall, and timer checks stayed
      clean through 112 IRQs.
- [ ] Extend the non-mutating probe to read target SP/PS before enabling any
      live register restore.
- [x] Extend the probe with assembly SP/PS readers and version the marker to
      `[CTX V9]`; live register restoration remains gated.
- [ ] Hardware-validate target PC/SP/PS and stack alignment.
- [x] Hardware validation passed: `[CTX V9 p=... sp=1070284720 a15ok=1
      ps=256 pc=1077348660]`; IPC/MM and syscall checks passed and timer
      validation remained clean through 192 IRQs.
- [ ] Run the first isolated register-restore experiment using a dedicated
      gate and `[CTX V10]` marker; keep normal IRQ return unchanged.
- [x] Add the gated V10 register contract check for saved `a0`, `a1`, `a15`,
      target PC, and 16-byte SP alignment; live restoration remains disabled.
- [x] Fix the V10 finding: initialize `p_reg.a[1]` from the aligned `p_reg.sp`
      for every process, and bump the active marker to `[CTX V11]`.
- [ ] Hardware-validate `a1 == sp` before enabling live register restoration.
- [x] Hardware validation passed: `[CTX V11 ... a1=1070284736 sp=1070284736
      spok=1]`; PC/PS and `a15` remained valid, with IPC/MM and timer checks
      clean through 192 IRQs.
- [ ] Implement the first gated live restore of only `a1`/`a15`; retain the
      existing full IRQ restore as the default fallback and add `[CTX V12]`.
- [x] Add the explicit disabled V12 restore gate and `[CTX V12]` marker;
      normal IRQ restoration is unchanged while the gate remains zero.
- [ ] Hardware-validate the V12 gate marker before enabling the experiment.
- [x] Hardware validation passed for the disabled V12 gate: `[CTX V11 ...
      gate=0]` and `[CTX V12]` startup markers appeared; IPC/MM, syscall, and
      timer checks remained clean through 144 IRQs.
- [x] Correct the context-line version label to `[CTX V12]` so all V12
      diagnostics identify the same image stage.
- [x] Hardware validation passed for V12: `gate=0`, `a1==sp`, `a15ok=1`, valid
      PC/PS, IPC/MM and syscall checks passed, and timer progress remained
      clean through 256 IRQs.
- [x] V13: enable the guarded `a1`/`a15` restore for one controlled
      return, with an immediate fallback gate and versioned diagnostics.
- [ ] Hardware-validate V13 with `gate=1`; revert immediately if any exception,
      stack corruption, or timer regression appears.
- [x] Hardware validation passed: V13 `gate=1` ran through 320 IRQs with no
      exception; IPC/MM, syscall, PC/SP/PS, and `a15` checks remained valid.
- [ ] Wire the gate into a genuinely distinct reduced `a1`/`a15` restore path;
      current V13 intentionally preserves the complete safe restore behavior.
- [x] Wire the gate into distinct assembly fallback/live labels and bump the
      marker to `[CTX V14]`; both paths retain identical proven loads pending
      hardware validation.
- [ ] Hardware-validate V14 before reducing the live path further.
- [x] Hardware validation passed: V14 `gate=1` selected the new assembly live
      branch with no exception through 163 IRQs; `a1==sp`, `a15ok=1`, IPC/MM,
      syscall, and timer checks remained valid.
- [ ] Reduce the V14 live branch to the minimum safe frame-pointer restore,
      retain fallback recovery, and bump diagnostics to `[CTX V15]`.
- [x] Reduce the live V15 branch to restore only `a15`; `a1` is restored from
      the dedicated SP field immediately afterward, while fallback retains
      both loads.
- [ ] Hardware-validate V15 with gate enabled.
- [x] Hardware validation passed: V15 reduced live `a15` restore with `gate=1`
      ran through 136 IRQs without exception; `a1==sp`, `a15ok=1`, IPC/MM,
      syscall, PC, and PS checks remained valid.
- [ ] V16: isolate the next nonessential register restore while preserving the
      fallback path and add a versioned diagnostic marker.
- [x] V16 audit: no remaining general register is nonessential for a correct
      call0 return; added `a1==sp` integrity reporting as `rel=1` and retained
      the full fallback restore.
- [ ] Hardware-validate V16 register integrity before any further reduction.
- [x] Hardware validation passed: V16 reported `rel=1`, `a1==sp`, `a15ok=1`,
      and stable IPC/MM, syscall, and timer diagnostics through 240 IRQs.
- [x] Hardware validation extended: `[CTX V16 ... gate=1 rel=1]` appeared;
      IPC/MM and invalid-syscall checks passed, and timer diagnostics remained
      clean through 368 IRQs with `c=f=s` and `e=0`. The sampled `r=1` values
      occur while the level-1 handler is active and return to `r=0` afterward.
- [ ] Next architectural task: connect validated process frames to a real
      scheduler handoff without dropping required call0 registers.
- [x] Split the IRQ restore sequence into general-register and dedicated
      `a1`/`a15` stages, preserving the default behavior while creating the
      controlled V13 insertion point.
- [x] Hardware validation passed after the split: V12 marker remained at
      `gate=0`, IPC/MM and syscall checks passed, and timer diagnostics reached
      434 IRQs without an exception.
- [ ] Add a compact `[SYS V]` diagnostic for the dispatch result.

---

# Authoritative continuation plan — CP32 on M5Stack Cardputer Adv

This section supersedes the older high-level Task 3–5 checklist for new
work. The entries above are retained as the historical implementation log;
their unchecked probe items must not be reopened when a later hardware result
already records them as passed. The current baseline is: the image reaches
`main()`, the SYSTIMER IRQ frame is stable, IPC/MM and invalid syscall checks
pass on hardware, and the V16 reduced restore experiment is stable. CP32 is
still not a usable MINIX system.

## Phase 0 — keep the baseline reproducible

- [ ] Record the exact current image marker, toolchain version, ELF section
      layout, flash command, serial device, and Cardputer board revision.
- [ ] Separate diagnostic-only code from production paths with named build
      gates; keep the clock-to-scheduler bridge disabled until Phase 1 passes.
- [ ] For every phase: make the smallest change, run `make clean && make` in
      `src/`, inspect `make headers`, `make segments`, and `make nm`, then flash
      and record the marker plus `r/c/f/s/e` in `issues.md`.

## Phase 1 — finish the kernel scheduler and context handoff

This is the immediate missing step. Do not enable the normal clock handler or
claim process execution until all of these are complete.

- [x] Define the temporary level-1 IRQ-frame contract in `irq_frame.h` and
      `irq_const.h`: 80 bytes, saved a0/a1/a2-a15 offsets, and compile-time
      size/offset checks. This is distinct from the 76-byte process frame;
      no scheduler handoff behavior changed.
- [x] Define and assert the 76-byte Xtensa process-frame contract in
      `proc.h`: `a[0]..a[15]`, `pc`, `psw`, and `sp` offsets are compile-time
      checked against the constants consumed by `irq.S` and `mpx32.S`.
- [ ] Consolidate the C and assembly declarations so the process-frame
      offsets are generated from one authoritative definition.
- [x] Consolidated process-frame size and offsets in `irq_const.h`; `proc.h`
      now imports and asserts the shared constants used by the assembly
      probes and restore path.
- [ ] Add a dedicated saved-frame field for the interrupted PC, PS, SP, `a0`,
      `a1`, `a15`, and all registers required by the call0 ABI. Do not use a
      magic DRAM address as a frame pointer.
- [ ] Implement `sched()` as MINIX-style quantum/priority selection, including
      queue removal, requeueing, `bill_ptr`, and idle fallback.
- [ ] Implement a gated assembly `switch_to(old, new)` that saves the current
      frame and restores the selected process without returning through the
      diagnostic C boundary.
- [x] Added an independent `cp32_context_handoff_gate`; the timer dispatch
      cannot invoke scheduler handoff unless this gate is explicitly enabled,
      and the existing validated clock bridge remains separately controlled.
- [x] Enable the handoff gate for the first isolated hardware experiment;
      clock-handler bridge remained disabled and the image marker was `[CTX V18]`.
- [x] Reverted the handoff gate for V19 after hardware showed `a1 != sp`
      (`rel=0`) and changing saved stack/frame values after the first target
      switch. The validated IRQ-only path is restored.
- [x] Corrected the context marker to `[CTX V19]` and removed the remaining
      per-selection `[SCHED]` diagnostic output.
- [x] Corrected `irq.S` to save `p_reg.sp` from the interrupted `a1` captured
      in the IRQ frame rather than from the temporary IRQ-frame address.
- [ ] Hardware-validate V20 on the safe IRQ-only path before re-enabling the
      handoff gate.
- [x] Safe-path V20 hardware validation passed through 208 IRQs with
      `a1==sp`, `rel=1`, `a15ok=1`, and `c=f=s`; enabled only the handoff gate
      for V21 while keeping the clock bridge disabled.
- [ ] Hardware-validate V21 for persistent `a1==sp`, stable IRQ progress, and
      independent task resumption; disable the gate on any regression.
- [x] V21 hardware validation partially passed: live handoff remained stable
      through 112 IRQs with `rel=1`, `a15ok=1`, and uninterrupted timer
      progress. The scheduler repeatedly selected process 2, so fair queue
      rotation and independent task execution remain unvalidated.
- [ ] Fix scheduler selection/queue rotation so the handoff experiment can
      demonstrate more than one runnable process without corrupting frames.
- [x] Fixed scheduler requeueing to include runnable tasks and servers, not
      only user processes; otherwise the task queue was consumed once and
      process 2 monopolized subsequent handoffs. Versioned as `[CTX V22]`.
- [ ] Hardware-validate V22 queue rotation and frame integrity.
- [x] V22 hardware validation passed through 96 IRQs: runnable entries
      rotated across processes/tasks (`p=2,1,0,-2,-3,-4,-5` and back),
      `rel=1` and `a15ok=1` remained stable, and no exception occurred.
- [ ] Add dedicated task counters/PCs to prove independent resumption;
      current targets still use bring-up frames and diagnostic code.
- [x] Added distinct counter loops and entry PCs for processes 1 and 2;
      versioned as `[CTX V23]` while retaining the handoff and clock gates.
- [ ] Hardware-validate V23 counters and alternating task PCs.
- [x] V23 hardware validation passed through 160 IRQs: runnable frames
      rotated across the set, `rel=1` and `a15ok=1` remained stable, and
      processes 1 and 2 resumed at distinct PCs (`pc` values differed) with
      no exception.
- [ ] Add compact counter values to the diagnostic marker to prove both
      dedicated loops continue making progress after preemption.
- [x] Added V24 compact `t=` counter output for processes 1 and 2; other
      process context lines remain unchanged.
- [ ] Hardware-validate increasing counters for both dedicated loops.
- [x] V24 hardware validation passed through 176 IRQs: process 1 counter
      advanced from `37` to `615` and process 2 from `0` to `579` while
      frames rotated and `rel=1`/`a15ok=1` remained valid.
- [ ] Add a bounded two-task-only stress run before enabling the normal clock
      lifecycle.
- [x] Added a two-task stress setup that completes IPC before IRQ enable,
      clears all ready queues, and seeds only processes 1 and 2; marker is
      `[CTX V25]`.
- [ ] Hardware-validate two-task-only rotation and counters.
- [x] V25 stress result failed: process 2 advanced its counter, but process 1
      remained at `t=0`; stack-frame accounting also diverged (`f` and `s`),
      so the handoff gate was disabled again for V26.
- [ ] Fix per-process task-stack initialization and saved-frame ownership
      before attempting another live handoff.
- [x] Added V27 pre-IRQ stack ownership diagnostics for process 1/2 SP values
      and their separation; handoff remains disabled.
- [ ] Hardware-validate V27 stack diagnostics before the next handoff attempt.
- [x] V27 hardware validation passed: process 1/2 stacks were distinct and
      separated by exactly `4096` bytes; the handoff-disabled IRQ path stayed
      clean through 256 IRQs with `c=f=s`.
- [ ] Investigate live saved-frame ownership; initial process-stack overlap is
      ruled out by V27.
- [x] Found and fixed the first-handoff ownership bug: `schedule()` was
      selecting process 1 while `main()` was still executing, causing the
      first IRQ to overwrite process 1's entry frame. V28 leaves the current
      idle frame selected and lets the first gated IRQ choose process 1.
- [ ] Hardware-validate V28 task counters and frame integrity.
- [x] V28 handoff hardware result: first selection of process 1 was correct,
      but process 2 then monopolized the two-task queue (`t=111` to `2516`)
      while process 1 stayed at `t=0`; `rel=1` and `f=1` remained stable.
      Disabled handoff again for V29.
- [ ] Diagnose ready-queue flags/link ownership after the first process switch.
- [x] V29 diagnosis found process 1 restored with `ps=0`; explicitly set the
      two stress tasks to `ps=0x100` and enabled the isolated handoff for V30.
- [ ] Hardware-validate V30 task rotation and both counters.
- [x] V30 found the explicit `ps=0x100` assignment was overwritten by the
      later generic PS initialization; moved the stress-task override after
      that assignment and versioned the retry as V31.
- [ ] Hardware-validate V31 task 1/task 2 progress.
- [x] V31 result: process 1 showed `ps=256` but remained at `t=0`, while
      process 2 advanced to `t=2941`; frame integrity stayed valid. Disabled
      handoff for V32; PS value alone is not the cause.
- [ ] Add process-1 execution/queue-state diagnostics before another handoff.
- [x] Made the two-task harness explicitly own the live frame with idle before
      enabling handoff, clearing stale `current_proc`/`proc_ptr` state; retry
      is versioned as V33.
- [ ] Hardware-validate V33 task 1/task 2 progress.
- [x] V33 confirmed stable frame restoration but process 1 remained at `t=0`;
      aligned both stress entries to initial `ps=0` for V34.
- [ ] Hardware-validate V34 process-1 progress and two-task rotation.
- [x] Added a rate-limited V35 queue-state trace around `sched()` to report
      current process, flags, and ready-link state; handoff remains enabled.
- [ ] Hardware-validate V35 queue-state trace and identify process-1 loss.
- [x] V35 queue trace showed clean alternation of `cur=1` and `cur=2` with
      `fl=0`; process 1 was not lost. Its later context lines were hidden by
      the global diagnostic rate limiter. Removed the temporary queue trace
      and versioned the cleaner handoff image as V36.
- [ ] Hardware-validate V36 with direct counter observations.
- [x] Hardware-validate V37 with direct `t1`/`t2` counter summaries at the
      existing 16-IRQ diagnostic cadence; both counters advanced through
      IRQ 272 with `rel=1`, `f=1`, and no exception.
- [x] Reduced the remaining rate-limited context lines to the V38 cadence after
      independent task progress was proven.
- [x] Hardware-validate V38's quieter context sampler; retain the V37 counter
      summary as the primary task-progress diagnostic. Both counters advanced
      through IRQ 256 with `rel=1`, `f=1`, and no exception.
- [ ] Begin the next scheduler-lifecycle step: replace the diagnostic stress
      loops with one real MINIX-style kernel task and validate its lifecycle.
- [x] Align `pick_proc()` billing with MINIX: task/server selection no longer
      overwrites `bill_ptr`; idle fallback explicitly bills the idle process.
- [ ] Hardware-validate V39 before enabling the normal clock-to-scheduler
      bridge or replacing the two-task stress setup.
- [ ] Hardware-validate V40 with the clock-to-scheduler bridge enabled; an
      exception or stalled timer requires reverting only this bridge gate.
- [ ] Hardware-validate V41 with scheduler handoff driven only by the clock
      quantum path; direct per-IRQ `sched()` fallback is now disabled when the
      clock bridge is active.
- [ ] Hardware-validate V42 after adding explicit runnable-kernel-task
      rotation; V41 showed task 1 monopolizing because user quantum accounting
      does not schedule kernel tasks.
- [ ] Hardware-validate V43 after allowing rotation with one queued task; V42
      showed task 2 monopolizing once only task 1 remained queued.
- [x] V43 hardware validation passed through IRQ 192: both kernel-task
      counters advanced with `rel=1`, `f=1`, and no exception.
- [ ] Replace the two diagnostic task loops with the first real clock-task
      lifecycle entry, including a valid task frame and message-loop boundary.
- [ ] Before activating `clock_task()`, complete its required blocking
      `receive()`/`send()` path; the current IPC implementation is not yet a
      safe message-loop boundary.
- [x] Replace recursive `_send()`/`_receive()` wrappers with direct
      `mini_send()`/`mini_rec()` dispatch and add the `_sendrec()` boundary.
- [ ] Hardware-validate blocking wrapper behavior before assigning the CLOCK
      task slot to `clock_task()`.
- [ ] Hardware-validate the V6 IPC/MM marker after the direct `_send()` /
      `_receive()` wrapper fix; the supplied V43 log predates that image.
- [ ] Hardware-validate V7, which routes the deterministic IPC test through
      `_send()` and `_receive()` rather than calling the primitives directly.
- [ ] Hardware-validate V8 with receiver-first ordering to prove blocking,
      sender wakeup, ready-queue reinsertion, and cleared flags.
- [x] V8 hardware validation passed: receiver-first IPC completed with
      `mem_copy`, delivery, `0/0`, `Hello IPC!`, and cleared flags; timer and
      context checks remained clean through IRQ 144.
- [ ] Move from the deterministic kernel IPC test to real task-owned message
      loops before activating `clock_task()`.
- [x] Reduced V18 handoff diagnostics: removed per-switch scheduler lines,
      rate-limited context reports, and made the idle banner one-shot.
- [ ] Hardware-validate V17; revert the gate immediately on exception, stack
      corruption, stalled IRQs, or queue inconsistency.
- [ ] V17 partial hardware result: the IRQ path selected successive saved
      frames (`p=-5,-4,-3,-2,-1,0,1,2`) and returned to each target's
      diagnostic idle loop without an immediate exception. The trace ended
      before periodic IRQ counters resumed, so timer progress and queue
      integrity are not yet validated.
- [ ] Fix scheduler handoff test isolation: use dedicated task PCs/counters
      and prevent repeated ready-queue insertion before declaring V17 passed.
- [ ] Run two deterministic kernel tasks with separate stacks and counters;
      prove they alternate, resume at the correct PC, preserve registers, and
      never corrupt the IRQ frame.
- [ ] Enable the timer-to-scheduler bridge only after the isolated switch test
      passes; validate 60 Hz progress, re-entry balance, queue integrity, and
      idle fallback.

## Phase 2 — make traps and IPC real

- [ ] Complete Xtensa user exception/trap entry and document the user-frame
      layout separately from the level-1 interrupt frame.
- [ ] Route SEND, RECEIVE, and BOTH from the trap frame into `sys_call`, copy
      the result back to the caller frame, and reject invalid privilege/cause/
      function combinations.
- [ ] Replace the current bring-up IPC call sequence with a blocking two-task
      test: sender blocks, receiver wakes it, message contents and return values
      are checked, and the ready queues are checked after each transition.
- [ ] Implement `interrupt`, held-interrupt replay, locking boundaries, and
      nested-entry policy using the MINIX `proc.c` behavior as the reference.

## Phase 3 — activate the MINIX clock/task lifecycle

- [ ] Port the relevant MINIX `clock.c` behavior onto ESP32-S3 SYSTIMER:
      tick accounting, lost ticks, alarms, TTY timers, quantum expiration,
      and deferred rescheduling.
- [ ] Replace simplified process initialization with explicit task/server
      descriptors, names, stacks, initial frames, maps, and privilege state.
- [ ] Add a kernel panic/fatal path that preserves a short diagnostic marker
      and never silently loops after an invariant failure.
- [ ] Start one real kernel task through the scheduler, then add the system
      task, clock task, and TTY task one at a time. Validate each transition on
      hardware before adding the next.

## Phase 4 — establish the Cardputer hardware abstraction

Do not copy PC MINIX drivers directly. `driver.c`, BIOS access, 8259/PIT,
8250 UART, VGA/console, and ATA/ floppy drivers are references for interfaces
and state machines only; each device must be mapped to the Cardputer Adv
hardware after checking its board documentation and a measured register test.

- [ ] Add a small board configuration layer for the Cardputer Adv revision:
      GPIOs, I2C buses, SPI host, USB Serial/JTAG, display controller, keyboard
      controller, battery/power signals, and storage wiring.
- [ ] Implement the lowest-risk diagnostic drivers first: USB console, GPIO,
      I2C, SPI, and a monotonic delay/timeout facility. Keep all MMIO volatile
      and document register offsets and clock/reset requirements.
- [ ] Implement the keyboard input driver and interrupt/polling path; prove
      debouncing, modifiers, key repeat policy, and a raw scancode/event test.
- [ ] Implement the display driver with a framebuffer or bounded text console;
      prove reset, initialization, pixel/text output, and recovery after a
      malformed command.
- [ ] Connect keyboard and display to the MINIX TTY line discipline while
      retaining the USB console as the recovery/debug channel.

## Phase 5 — storage, filesystem, and process loading

- [ ] Choose and document the first persistent medium actually present on the
      board (internal flash partition, SD, or another validated device). Add a
      read-only block-device test before implementing writes.
- [ ] Port the MINIX block-device interface and cache only after the chosen
      medium has stable reads, bounds checks, and power-loss-safe error paths.
- [ ] Port the MINIX filesystem structures and essential operations in this
      order: superblock, inode lookup, directory read, open/close, read, then
      write/create/unlink. Add an image-based host test for every operation.
- [ ] Implement executable loading and user address-space setup for the CP32
      flat-memory model; validate bounds, alignment, permissions, stack setup,
      and initial user PC before launching any shell.

## Phase 6 — minimal user space and usable system

- [ ] Add the minimum syscall ABI and user library wrappers for TTY, process
      creation/exit/wait, memory growth, file I/O, and time.
- [ ] Boot one statically linked user program that prints a banner, reads a
      line, and exits; validate the complete user trap → kernel → return path.
- [ ] Implement a small shell with a bounded command parser and built-ins for
      diagnostics, memory, processes, and files.
- [ ] Add applications incrementally (test utilities first), then signals,
      pipes, redirection, and job control only after the base shell is stable.

## Definition of the first usable CP32 milestone

- [ ] Cold boot reaches a versioned banner on USB console and Cardputer display.
- [ ] Keyboard input is echoed through TTY; a shell command executes and exits.
- [ ] At least one file can be read from validated persistent storage.
- [ ] Two user processes can run, block on IPC or TTY, resume, and exit.
- [ ] A stress run covering timer ticks, context switches, IPC, display output,
      and repeated shell commands completes without exception or queue leak.
