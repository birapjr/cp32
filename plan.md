## Plan Update – Step 1 Completed

- Added a minimal `schedule()` stub in `src/kernel/proc.c` that prints a clear debug message.
- Updated `src/kernel/main.c` to call `schedule()` each idle‑loop tick.
- Adjusted debug message format for clarity.
- Verified build, flash, and console output now show:
  ```
  timer probe build: CP32-IRQ-FRAME-64-SCHED-2
  last change: SCHED-2 (Scheduled called)
  ```
- Hardware validation matches plan expectations.

---


This plan documents the incremental steps to evolve the CP32 kernel from a diagnostic boot‑loop to a minimal working scheduler, ensuring that each change is verified on actual Cardputer hardware.  Each step contains:

* **Goal** – What feature will be added or fixed.
* **Change** – Where the change occurs in the codebase.
* **Test on Hardware** – How to run the build, flash and verify success.
* **Debug Log** – A `usbj_print(" … \r\n");` message to emit on the serial console, matching the format used in the existing diagnostics.

All steps follow the rules in `agent.md`: smallest testable change, build with `src/Makefile` and validate output.

---

## 1. Add a One‑Shot Scheduler Hook
**Goal** – After the diagnostic idle loop in `main.c`, invoke a simple `schedule()` stub that will run once and return.

**Change** – Add a call to `schedule()` after the idle loop, and implement a minimal stub in `proc.c` that logs a message.

**Test on Hardware** –
1. `make clean && make` in `src/`.
2. `make flash`.
3. Observe USB serial console; expect the message from `schedule()`.

**Debug Log** – In `proc.c` add:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-SCHED-1\r\n");
```
In `main.c` after idle loop:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-SCHED-2\r\n");
schedule();
``` 

---

## 2. Implement Basic Task Structure
**Goal** – Define a `struct proc` entry for a dummy task that sleeps for a few ticks then prints.

**Change** – In `proc.c` create an array of one `struct proc` with `state = TASK_READY`. Add logic in `pick_proc()` to return this task.

**Test on Hardware** – Build and flash; the console should show the diagnostic + the dummy task message.

**Debug Log** – Inside task entry point:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-SCHED-3\r\n");
```

---

## 3. Wire Timer Interrupt to Scheduler
**Goal** – Make the SYSTIMER interrupt (`TARGET0`) invoke the scheduler instead of entering `rfe`.

**Change** – In `irq.S`, replace the existing probe frame to call a new C function `sched_tick()`; in `clk.c` implement `sched_tick()` to call `schedule()`.

**Test on Hardware** – Rebuild, flash; every ~16 ms the console should output the tick log followed by the scheduler debug logs.

**Debug Log** – In `clk.c`:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-TICK-1\r\n");
```

---

## 4. Implement Context Switch Skeleton
**Goal** – Add minimal `switch_to()` that saves/restores registers and updates `current` index.

**Change** – In `proc.c` add `switch_to(struct proc *next)` that updates a global `current_proc` pointer. Adjust `schedule()` to call `switch_to()`.

**Test on Hardware** – Build and flash; verify sequential logs: tick → schedule → task message → tick → …

**Debug Log** – In `switch_to()`:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-SWITCH-1\r\n");
```

---

## 5. Finalise IPC Stubs
**Goal** – Provide working `send()` and `receive()` stubs that simply return OK.

**Change** – In `proc.c` implement `int send(...) { return OK; }` and `int receive(...) { return OK; }` with debug prints.

**Test on Hardware** – Compile and flash; ensure no crashes and see prints for send/receive invocations.

**Debug Log** – In each stub:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-IPC-1\r\n");
```

---

## 6. Clean Up and Documentation
**Goal** – Update `issues.md` markers, tidy comments, and ensure the Makefile remains unchanged.

**Change** – Append markers like `CP32-IRQ-FRAME-64-FINAL` and describe the current status.

**Test on Hardware** – No additional test; confirm previous steps still pass.

**Debug Log** – Final marker:
```c
usbj_print("timer probe build: CP32-IRQ-FRAME-64-FINAL\r\n");
```

---

**General Testing Procedure** – For every change:
1. Modify the code.
2. `make clean && make` in `src/`.
3. `make flash`.
4. Open the serial console at 115200 baud, e.g., `screen /dev/cu.usbmodem2101 115200`.
5. Verify the expected `usbj_print` messages appear in order.

This incremental plan keeps each change small, build‑test‑validate cycles, and provides clear console output to aid human and agent debugging.
