
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
