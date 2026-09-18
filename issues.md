# SYSTIMER first-interrupt investigation

## Image 30 — 2026-09-17

Build identity: `[FEATURE SYSTIMER-IRQ 30]1`, `[TEST SYSTIMER-IRQ 30]`.
Hardware logs received on 2026-09-18 prove two consecutive timer deliveries
and successful rearming for image 30, followed by a crash after FS entry
(see image 31 below). Image 29's shell remained usable, but USB IRQ output
stopped after count 1. The local `ipc` result is a bring-up fallback, not proof
of TTY message delivery.

Build validation: all 11 host test scripts pass, including a stateful
SYSTIMER MMIO model and seven assembly register-flow scenarios. A clean
Xtensa build and image-layout check pass without compiler warnings. Section
and load-segment inspection confirms `_iram_end=0x40374264` is below the
low-IRAM limit `0x40378000`, `_iram_ext_end=0x4037FF14` is below `0x403E0000`,
and `_stack_top=0x3FCCCD50` is below `0x3FD00000`. These are build results,
not hardware results.

MINIX reference: `minix-2.0.0/src/kernel/clock.c:clock_handler()` acknowledges
the device, accounts ticks, and defers task work through `interrupt(CLOCK)`.
CP32 must preserve that division while replacing the PIT/PIC and x86 return
mechanism with SYSTIMER and Xtensa level-1 exception entry/return.

Corrections in this image:

- Read the CPU pending bitmap before a registered device handler clears its
  level source. Removed unconditional TARGET0 clear/accounting from generic
  level-1 assembly. Only the timer handler increments the timer IRQ counter.
- Restore every general register from the selected frame and write named PS
  before `rfe`. SR192 is DEPC, not an EPS1 register, and `rfi 1` is undefined.
  Keep EXCM set through the final restore so a fresh descriptor cannot allow
  an interrupt partway through its register loads.
- Clear stale UNIT0 VALUE_VALID when requesting UPDATE; use coherent
  low/high/low reads when task and ISR can both request snapshots. Share the
  one-shot rearm helper across boot, CLOCK init and IRQ handling. Correct the
  unused periodic-field mask to the documented 26 bits.

The existing TARGET0 offsets, work-enable bit 24, comparator-load strobe and
disable/load/enable ordering match the ESP32-S3 documentation. The previous
boot value `raw=0` was sampled before interrupts started; it did not establish
that no later raw event was generated.

Primary references:

- [ESP32-S3 technical reference manual](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf), System Timer register descriptions.
- [Espressif SYSTIMER registers](https://github.com/espressif/esp-idf/blob/v5.5.3/components/soc/esp32s3/register/soc/systimer_reg.h) and [counter/alarm implementation](https://github.com/espressif/esp-idf/blob/v5.5.3/components/hal/systimer_hal.c). These describe direct-register semantics; CP32 still uses its own bare-metal implementation.
- [Cadence Xtensa ISA reference](https://login.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf), RFE/RFI and special-register tables; [Espressif exception vectors](https://github.com/espressif/esp-idf/blob/v5.5.3/components/xtensa/xtensa_vectors.S).

Hardware acceptance:

1. Confirm both image-30 identity markers.
2. Observe `[SYSTIMER V30 n=1 before ...]`, `n=2`, `n=500`, `n=1000` and
   continued `[IRQ count=...]` output. Two IRQs alone do not prove sustained
   operation.
3. For each capture, compare the 52-bit `now` and actual `target` (high:low)
   before/after with the programmed `deadline`. The load crosses clock
   domains, so an immediate actual-target read may lag. CONF bit 24 must be
   enabled after rearm, peripheral `ena` bit 0 and CPU `ien` bit 2 enabled.
   RAW/ST should clear after acknowledgement; a later event may already be
   pending if serial output takes longer than a tick.
4. `ps` is the live handler status; EXCM there is expected. `psout` is the
   effective selected status after RFE: EXCM clear and INTLEVEL zero for the
   current task descriptors. `frame=1` checks frame/owner consistency.
5. Exercise `ls` and keyboard input while counts continue. Keep the existing
   `ipc` fallback separate from future real-IPC acceptance.

Remaining limitations: hardware timing and full task resumption are unproven;
the process frame does not yet preserve SAR/loop/coprocessor state, and nested
interrupt ownership needs separate validation. Do not activate real blocking
TTY/CLOCK/MM paths based only on a host-test pass.

## Image 31 — FS ownership repair, 2026-09-18

The user supplied two image-30 hardware logs. Both reach `[TTY user-entry]`
after successful timer samples 1 and 2, then halt:

```text
First log: EXCCAUSE=0x00000002 EXCVADDR=0x3FCD6D40 EPC1=0x3FCD6D40 PS=0x00000110
Next log:  EXCCAUSE=0x00000014 EXCVADDR=0x00000000 EPC1=0x00000001 PS=0x00000110
```

FS starts with SP `0x3FCD6D50`; the compiled console prologue subtracts 16,
making the first fault address its frame pointer. The differing invalid PCs
are consistent with corrupted execution state, but the log alone does not
identify the exact instruction that first corrupts it.

A concrete source defect was reproduced: `kernel_idle_loop()` is also the
entry for runnable LOW_USER, yet it directly restored a remembered FS frame
without changing `proc_ptr/current_proc`. The subsequent IRQ could save FS
registers under another task's identity. A second direct FS restore in C
`switch_to()` could abandon an uncaptured outgoing C frame.

Image 31 removes those two direct transfers. The idle loop stays on its
current stack and ordinary C scheduling only publishes selection; the
IRQ/syscall epilogue performs the physical restore. This follows MINIX
`mpx386.s:save/_restart`'s separation between capturing outgoing execution,
choosing a process, and restoring it at the return boundary. This change
does not complete the still-gated task-context IPC suspension contract.

`tests/test_idle_handoff.py` extracts and compiles the actual production C
function bodies with a modeled context-transfer boundary. The original code
fails for LOW_USER and IDLE (restore owner differs from FS) and for the C
selection path (unexpected direct restore). The fix passes five cases,
including FS suspended inside a nested call. All 12 host scripts and a clean
Xtensa build pass. ELF limits: low IRAM ends at `0x40374268`, extended IRAM at
`0x4037FFAC`, stack at `0x3FCCCE20`; image layout passes.

Identity: `[FEATURE SYSTIMER-IRQ 31]1`, `[TEST SYSTIMER-IRQ 31]`.
The first two FS IRQ returns emit `[CTX V31 FS-RETURN ...]` with PC, SP,
return register a0, frame pointer a15 and ownership validation. Timer
diagnostics retain their sample rate under the V31 label.

Image 31 is built, not hardware-validated or automatically flashed. The user
requested manual flashing. Check for sustained IRQ counts 500/1000, continued
shell operation, and no exception after repeated FS returns. If the crash
persists, use the two FS-return records and the new ELF to continue tracing;
do not treat the reproduced software defect as proof of hardware resolution.
