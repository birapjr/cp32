# CP32 Port – Current Issues and Handoff

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
- The real clock-handler gate remains disabled (`e=0`).

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

For every hardware test, record the marker and `r/c/f/s/e`. A regression is
an exception, stalled counter, nonzero `r` after idle, `c/f/s` mismatch, or
unexpected `e=1`.
