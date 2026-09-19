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

Hardware result: the user manually flashed image 31 and supplied
`docs/hardware/systimer-v31.log`. The reported crash does not recur in this
capture. Timer counts reach 500, 1000 and 1500; sampled rearming clears RAW/ST,
CPU INTENABLE remains `0x00000004`, and return checks report `frame=1`.
FS enters at tick 5 and resumes at tick 11 with PC `0x4037D285`, SP/a15
`0x3FCD6DF0`, and code return address a0 `0x4037D241`.

Keyboard characters form the `ipc` command while the timer continues. The
queued marker and result `0` appear, with an idle heartbeat interleaved inside
the result line. That result is still the local fallback, not real TTY IPC.
No `ls` run or extended stress result is present. This validates the bounded
crash-fix/timer acceptance run; special-register preservation and blocking
task IPC remain separate work. No automatic flashing was performed.

## Image 32 — integer special-register preservation

MINIX restores each selected process's execution state at restart. On Xtensa,
general registers alone omit SAR (used by variable shifts) and LBEG/LEND/
LCOUNT (hardware loops). C interrupt dispatch can overwrite these registers
while the interrupted process still needs their previous values.

The process and syscall frames now append these four registers at offsets
76/80/84/88 (92 bytes total). IRQ temporary frames use offsets 64..79 without
changing their 80-byte stack allocation. Entry captures the values before C;
the return epilogue restores the selected values while EXCM is still set.
Fresh descriptors zero the new state and C frame-copy paths preserve it.
This follows the SAR/loop save and restore pattern in Espressif's local
ESP-IDF v5.5.3 `components/xtensa/xtensa_context.S`; CP32 remains bare-metal.

All 12 host scripts pass. The assembly interpreter tests distinct outgoing
and selected special-register values, C clobbering, gate-off return, syscall
return, and zero-initialized entry. Layout assertions, clean Xtensa build,
and ELF/image layout checks pass. This is build/contract validation only.

Manual hardware image: `[FEATURE CONTEXT-SPECIAL 32]1` and
`[TEST CONTEXT-SPECIAL 32]`. Check continued FS resumption, keyboard/shell
operation and IRQs through 1500; V32 FS-return records include SAR/LCOUNT.
The subsequent hardware result is recorded below; no automatic flash was performed. Floating-
point/MAC/other extension state and real blocking task IPC remain unvalidated.


Image-32 hardware result: the manually flashed image ran through IRQ 1500
without an exception in `docs/hardware/context-special-v32.log`. FS return
at tick 11 reports SAR `0x15`, LCOUNT `0`, and `frame=1`; timer samples clear
RAW/ST after rearm and retain CPU INTENABLE `0x4`. Keyboard input forms
`ipc` and the existing fallback returns `0`. Idle heartbeats split the result
line; this is interleaved diagnostic output, not proof of a real IPC reply.
The capture passes the bounded regression check, but does not exercise an
active nonzero loop count or establish exhaustive special-register coverage.
Next work is the real TTY IPC suspension/request/reply path.


## Image 33 — first real TTY IPC exchange confirmed on hardware

The former `ipc` success came from shared-slot shell/clock fallbacks. Those
fallbacks are removed. `_send`, `_receive` and `_sendrec` now use SYSCALL and
resume through a saved task frame. The queued BOTH bug is fixed: accepting a
request clears SENDING only; the client remains RECEIVING until the reply.
TTY receives and handles DEV_IOCTL/TCGETS and replies through actual IPC.
The shell checks reply source, type and process before printing status.
CLOCK/SYS remain stopped; MM remains cooperative pending separate activation.

Markers: `[FEATURE TTY-IPC 33]1`, `[TEST TTY-IPC 33]`, first/per-5000-operation
`[IPC V33 op=... n=... owner=... blocked=... next=... frame=...]`, and
`[TTY IPC V33 reply-result=0]` on successful shell exchanges.

All 13 host test scripts pass, including production IPC/scheduler tests for
both message arrival orders and assembly SYSCALL immediate/blocked returns.
Clean build and image layout pass; ELF `_iram_end=0x4037432C`,
`_iram_ext_end=0x40380390`, `_stack_top=0x3FCCD500`.
The user manually flashed image 33. `docs/hardware/tty-ipc-v33.log` confirms
one real exchange: TTY RECEIVE blocks, FS BOTH blocks, TTY SEND replies, and
the resumed shell reports 0. Heartbeats split the result line; it is not
a crash. IPC return frames pass, and IRQs continue through 500 and 1000
without an exception. Repeated `ipc`, post-reply keyboard/`ls`, and IRQ 1500
remain pending before CLOCK/MM activation.
This validates the first task request/reply path, not full terminal input IPC,
CLOCK/MM service activation, nested interrupts or optional extension context.


## CLOCK wake starvation — image 34 observed, image 35 fix pending hardware

`docs/hardware/clock-ipc-v34.log` shows CLOCK entering and its blocked frame
waking, but clock-msgs remains zero through IRQ 1000. A real TTY exchange
still returns 0 and the capture contains no exception. CLOCK activation
therefore failed its progress check despite continued timer delivery.

The legacy scheduler's fixed nine rotations to prefer TTY can continually
select the same tail when TTY sleeps and five tasks are runnable. A test of
production pick_proc/sched reproduced starvation of CLOCK (-3). Image 35
removes the preference and restores FIFO within each queue class.

This includes image 34's real CLOCK receive/dispatch loop, saved-frame
completion for HARD_INT, single boot-time SYSTIMER initialization, correct
CPU line 2 at stop, and task-safe quantum accounting. MM remains cooperative;
SYS remains stopped. IRQ/RFE retains ownership of context selection.

Markers: `[FEATURE CLOCK-FAIR 35]1`, `[TEST CLOCK-FAIR 35]`,
`[CLOCK V35 received=... owner=4294967293 resumed=1]`, periodic IRQ
`clock-msgs=...`, and `[TTY IPC V35 reply-result=0]`.
All 14 test scripts, clean build and ELF/image layout checks pass.
Symbols: `_iram_end=0x403741C0`, `_iram_ext_end=0x4038056C`,
`_stack_top=0x3FCCD770`. Hardware validation remains pending; user flashes
manually and captures increasing clock-msgs, IRQ 1500 and repeated IPC/ls.


## Image 35 hardware result — CLOCK starvation fix confirmed

Evidence: `docs/hardware/clock-fair-v35.log`. CLOCK receives HARD_INT from
HARDWARE at ticks 15 and 32 with resumed=1. Its dispatch count advances to
57 at IRQ 500 and 115 at IRQ 1000. One real TTY IPC exchange returns 0;
heartbeats continue through tick 1106 with no exception. The split reply
line is diagnostic interleaving. This confirms the scheduler fix restores
CLOCK progress. Repeated ipc, ls and IRQ 1500 are not present in this capture.
Next implementation work is separate MM receive/request/reply activation;
SYS remains stopped.


## MM activation — image 36 built, hardware confirmation pending

MM's old cooperative loop never reached receive and its descriptor had no
usable map for its stack message buffer. Image 36 enables receive/handle/reply
and supplies the same flat SRAM map used by the bring-up FS client. This is
the existing small allocate/release service, not a complete MINIX MM server.
Shell `mm` performs two real SENDREC calls to allocate one click and release
it, checking MM source and status. SYS remains stopped; CLOCK and TTY stay live.

Markers: `[FEATURE MM-IPC 36]1`, `[TEST MM-IPC 36]`,
`[MM V36 received=... op=... source=... resumed=1]` and
`[MM IPC V36 alloc-release-result=0]`.
All 15 test scripts pass, including 200 production MM service cycles,
allocator ownership/error/coalescing checks, stack mapping, and server-queue
IPC suspension/resumption. Clean build and ELF/image checks pass without
warnings: `_iram_end=0x403741C4`, `_iram_ext_end=0x403807E0`,
`_stack_top=0x3FCCDA90`.
User flashes manually; repeated mm/ipc/ls and increasing CLOCK messages through
IRQ 1500 remain to be verified. Host checks do not certify hardware behavior.


## Image 36 hardware pass; image 37 quieter heartbeat

`docs/hardware/mm-ipc-v36.log` confirms four successful MM allocate/release
exchanges with resumed=1, ls capacity 65536/formatted=0, and a later TTY reply
of 0. CLOCK dispatch reaches 1175 messages at IRQ 10000; no exception appears
through the last heartbeat at tick 10193. Shell rendering is slow and its
USB lines interleave with diagnostics; responsiveness remains a separate issue.

At the user's request, image 37 changes the shared idle heartbeat interval
from 20 to 400 loop iterations (20 times less frequent, roughly 30 seconds
at the observed rate). Markers: `[FEATURE QUIET-HEARTBEAT 37]1` and
`[TEST QUIET-HEARTBEAT 37]`. Other diagnostic sampling rates are unchanged.
Clean build, all 15 existing host test scripts and image layout pass; the
new heartbeat cadence awaits manual hardware validation.


## Images 37/38 — quieter runtime diagnostics

Image-37 evidence in `docs/hardware/quiet-heartbeat-v37.log` confirms the
heartbeat spacing (ticks 1698 and 3368), successful MM/TTY IPC, and CLOCK
progress to 411 messages at IRQ 3500 without an exception.

Image 38 reduces recurring SYSTIMER, IRQ status and RFE trace intervals from
500 to 5000, keeping initial startup samples. Heartbeat and IPC sampling
intervals are unchanged. Markers: `[FEATURE QUIET-SYSTIMER 38]1` and
`[TEST QUIET-SYSTIMER 38]`. All 15 existing host scripts, clean build and
ELF/image layout pass; manual hardware verification of the cadence is pending.
