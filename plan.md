
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
- [ ] Add a compact `[SYS V]` diagnostic for the dispatch result.
