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


## SYS task activation — image 39 pending hardware

Image-38 evidence (`docs/hardware/quiet-systimer-v38.log`) confirms MM/TTY
success, ls completion, CLOCK count 587 at IRQ 5000 and quieter diagnostics
without an exception. Image 39 enables the SYS descriptor and its real
receive/dispatch/reply loop. Shell sys queries its saved SP and uptime via
SYS_GETSP/SYS_TIMES. Those handlers reject free process slots; time sampling
preserves the interrupt mask. Receive/send failures are now explicit panics.

Markers: `[FEATURE SYS-IPC 39]1`, `[TEST SYS-IPC 39]`,
`[SYS V39 received=... resumed=1]`, `[SYS IPC V39 result=0 uptime=...]`.
All 16 host test scripts, clean build and ELF/image checks pass, with
`_iram_end=0x403741B4`, `_iram_ext_end=0x40380AC8`, `_stack_top=0x3FCCDE30`.
Hardware confirmation awaits manual flashing, repeated sys queries and
mm/ipc/ls with continued CLOCK progress. Other existing SYS handlers and
full CPU-accounting correctness remain unvalidated.


## SYS hardware pass; console latency fix in image 40

`docs/hardware/sys-ipc-v39.log` confirms four SYS replies with increasing
uptime (255/614/966/1495), MM/TTY success and CLOCK count 1175 at IRQ 10000.
No exception appears through heartbeat tick 11732. ls completes slowly,
with thousands of ticks between lines.

Image 40 fixes the unused/broken LCD batch path: scrolling marks a pending
repaint, nested writes coalesce to one outer-command redraw, and the final
cursor is preserved. Bottom-row wrapping now scrolls the text buffer instead
of clamping/overwriting; spaces erase old glyphs. SPI transport is unchanged.
Markers: `[FEATURE CONSOLE-BATCH 40]1`, `[TEST CONSOLE-BATCH 40]`.
All 17 host scripts and the clean ELF/image build pass. Pixel-model testing
shows identical final batched/unbatched output with 7 scroll redraws reduced
to 1. Symbols: `_iram_end=0x40374224`, `_iram_ext_end=0x40380AD0`,
`_stack_top=0x3FCCDE50`. Manual LCD appearance/performance validation remains
pending; test full-screen ls followed by sys/mm/ipc and CLOCK progress.


## Scroll black-pass delay — image 41 removes it in software

User confirms image 40's one-pass scrolling but reports slow full-screen
black cleanup. `docs/hardware/console-batch-v40.log` confirms repeated ls,
successful MM/SYS/TTY and CLOCK progress at IRQ 5000 without an exception.
Image 41 adds a 192-byte painted-character shadow and updates only changed
opaque cells, including spaces, at batch flush. Scrolling no longer issues a
full-screen clear. Explicit/boot clear remains and resets both text shadows.
Markers: `[FEATURE LCD-CELLS 41]1`, `[TEST LCD-CELLS 41]`.
All 17 host scripts and clean ELF/image checks pass; pixel-model tests verify
zero scroll clears and equality to a complete reference rasterization.
`_iram_end=0x40374264`, `_iram_ext_end=0x40380AD0`, `_stack_top=0x3FCCDF00`.
Manual display verification remains pending; software SPI still limits pixel
transfer speed. No claim of a hardware single-command fill is made.


## Hyphen appearance unresolved — image 43 adds direct comparison

User accepted image-41 scrolling but reports '-' still incorrect on image 42.
The pasted image-42 log shows SYS/MM/TTY success and ls, with no hyphen input
trace; it cannot establish the visual cause. Image 43 centers the hyphen,
adds explicit underscore/slash/colon glyphs, and introduces shell font with
labelled '- _ / =' samples. Slash previously used a placeholder even in the
MM alloc/release message; this may be a separate observed symbol issue.

Markers: `[FEATURE LCD-PUNCT 43]1`, `[TEST LCD-PUNCT 43]`.
All 17 host scripts and clean build/image checks pass. Pixel tests verify
hyphen/underscore/slash shapes; the real keyboard decoder emits '-' or '_'
according to Shift state. Keyboard arrays now accommodate their terminators.
`_iram_end=0x403742F8`, `_iram_ext_end=0x40380B18`, `_stack_top=0x3FCCDF90`.
Hardware punctuation correctness remains pending. Run font and compare with
typed punctuation; an LCD photo and USB code would distinguish any remaining
rendering issue from an input mismatch. No automatic flashing performed.


## Punctuation confirmed; image 44 adds real DEV_WRITE

User confirms image-43 characters are correct; saved evidence is
`docs/hardware/lcd-punct-v43.log`. TTY output was still a no-op backend despite
working ioctl IPC. Image 44 connects console DEV_WRITE to mapped, bounded
buffer copies, existing termios output processing, batched LCD output and a
real consumed-byte reply. Shell write exercises it with 18 input bytes.
Markers: `[FEATURE TTY-WRITE 44]1`, `[TEST TTY-WRITE 44]`,
`[TTY WRITE V44 result=18 expected=18]`. All 18 host scripts and clean
ELF/image checks pass; hardware confirmation is pending. Symbols:
`_iram_end=0x40374310`, `_iram_ext_end=0x40380DD0`, `_stack_top=0x3FCCE280`.
Device input, full shell TTY routing and complete terminal control sequences
remain incomplete. LCD batch flush may occur after the byte-count reply when
the shell owns an outer command batch. No automatic flashing performed.

## TTY input duplicate copies — image 45, build-tested

The previous in_transfer directly wrote each input byte, advanced the virtual
address and count, then copied the same bytes again via the legacy 64-byte
buffer. Even a one-byte request could write a second byte beyond its requested
range and report twice the consumed count. It also bypassed canonical semantics
for FS and wrote an unrelated shell-global byte.

Image `[FEATURE TTY-READ-COPY 45]1` / `[TEST TTY-READ-COPY 45]` restores the
MINIX buffered accounting contract with CP32 numap translation: each byte is
copied and counted once, and invalid destinations fail before queue removal.
Canonical and EOF behavior now applies to FS as to other clients. Existing
shell input uses its separate direct reader, so device-backed input remains
unfinished. In particular, its raw keyboard queue does not produce the full
IN_EOT/IN_EOF line-discipline representation; connect in_process and task-owned
wakeups before enabling DEV_READ for the shell.

All 19 host scripts and clean Xtensa ELF/bin build pass. New tests execute the
production transfer with nonidentity mapping and destination guards, covering
chunk boundaries, ring wrap, canonical/EOF, partial reads and mapping failure.
Sections/segments and image layout pass: `_iram_end=0x40374310`,
`_iram_ext_end=0x40380DD4`, `_stack_top=0x3FCCE270`.
Hardware status: pending; no automatic flash or serial-console result.
Manual regression should exercise write (18-byte reply and LCD text),
sys/mm/ipc/ls and continued CLOCK progress. That regression alone will not
validate the repaired input path until a real DEV_READ backend is connected.


## Image 45 hardware regression — LCD and TTY write confirmed

Evidence: `docs/hardware/tty-read-copy-v45.log`; the user confirms everything
appears as expected on the LCD. Both `[FEATURE TTY-READ-COPY 45]1` and
`[TEST TTY-READ-COPY 45]` identify the image. Unchanged subsystem diagnostics
retain V44 labels; these do not identify a different flashed image.

[x] One real TTY DEV_WRITE exchange reports `result=18 expected=18` and the
user confirms correct LCD output. This validates the first hardware write
exchange for the backend introduced in image 44, running in image 45.

[x] Subsequent MM allocate/release returns 0, SYS returns 0 with uptime 1461,
TTY ioctl IPC returns 0, and ls prints capacity 65536/formatted=0. CLOCK
receives its initial two HARD_INT messages at ticks 14 and 33; kernel
heartbeats advance through ticks 1596 and 3266. No exception appears in the
capture, and initial selected-frame checks report frame=1.

Remaining: the capture has one write, not repeated-write stress, and ends
before the IRQ-5000 sample, so it does not show a later CLOCK message count.
The shell still reads through its direct helper; this regression does not
exercise the repaired DEV_READ transfer on hardware. Connect line-discipline
input, TTY wakeups and SUSPEND/REVIVE handling before claiming that feature
complete. No code or image change was made when recording this evidence.

## Image 46 — device-backed TTY input, awaiting hardware

`[FEATURE TTY-READ 46]1` and `[TEST TTY-READ 46]` identify the new image.
The shell now sends DEV_READ, accepts an immediate reply or waits for REVIVE
after SUSPEND, and validates reply source/type/process/count. TTY polls at most
eight controller events per device-read callback and feeds in_process; timer
interrupts only flag the line and post a coalesced TTY notification after init.
No I2C or LCD operation was added to the ISR.

Two source issues had to be resolved to activate this path: positional c_cc
defaults used MINIX's old indexes rather than CP32's header indexes, and
independent TTY echo could preempt a direct shell LCD transfer. Defaults now
use named indexes. All shell output uses DEV_WRITE, buffered into bounded
command batches, so TTY owns runtime LCD transactions and echo. The existing
boot display setup runs before task handoff. LCD backspace now crosses wrapped
rows, allowing MINIX BS-space-BS erase to update the preceding row.

All 20 host scripts pass, including production canonical/raw input, echo,
Ctrl-U/Ctrl-D, immediate/suspended replies, malformed/failed I/O, output bounds,
and IPC REVIVE arriving before or after the client starts RECEIVE. A pending
TTY poll does not complete a blocked SEND in the IPC tests. Clean build,
ELF/image layout and section/segment checks pass without compiler warnings:
`_iram_end=0x40374334`, `_iram_ext_end=0x40380E50`, `_stack_top=0x3FCCE420`.

Hardware status: pending. Test lx, Backspace, s, Enter first; expect ls and
`[TTY READ V46 reply=3 n=1]`. Then exercise wrapped erase, Ctrl-U, empty Ctrl-D,
repeated write/sys/mm/ipc/ls, typing during output, and continued CLOCK messages
through IRQ 5000. Unchanged subsystem markers still say V44; the feature/test
markers identify image 46. No automatic flash was performed.

This build changes scheduling load by waking TTY periodically and moves the
interactive shell onto suspended reads. Host passes cannot certify silicon
timing, keyboard FIFO behavior or long-duration progress. Raw/timed terminal
modes and full cancellation/signal behavior remain unvalidated on hardware.

## Image 46 latency regression — image 47 removes idle-task contention

The user's image-46 capture (excerpts in `docs/hardware/tty-read-v46.log`)
confirms working Backspace, a 3-byte TTY read, successful MM/SYS/TTY requests,
ls and CLOCK count 673 at IRQ 5000. Display writes are reported very slow.
The write command spans heartbeats at 7081 and 8285 before returning 18 bytes:
at least 1204 ticks occur inside that observed interval; the full latency is
not timestamped. No exception appears in the supplied capture.

Boot made four placeholder task slots and LOW_USER runnable despite assigning
them the idle loop. The class-rotating scheduler gives TTY only a fraction of
the task-class share, whereas FS previously rendered from the server class.
The IRQ-5000 sample selects -8, one of those placeholders. A production
scheduler model reproduces 100/1200 TTY selections with the old workload and
600/1200 when placeholders are stopped, with CLOCK continuously runnable.

Image 47 uses P_STOP for those reserved boot descriptors. Their frames/maps
remain initialized, the implemented services remain runnable, and IDLE is the
fallback when there is no useful work. IRQ/frame handling, class fairness,
TTY polling and software SPI are unchanged. There are no new hardware-register
assumptions in this fix.

Identity: `[FEATURE TTY-LATENCY 47]1`, `[TEST TTY-LATENCY 47]`.
`[TTY WRITE V47 bytes=... ticks=...]` reports task-side rendering elapsed ticks
for the first eight writes and every 500th, including descheduling and excluding
request queue wait. All 20 host scripts, clean Xtensa build and ELF/image checks
pass: `_iram_end=0x4037433C`, `_iram_ext_end=0x40380F70`,
`_stack_top=0x3FCCE570`. Hardware latency remains unverified; no flash performed.

Next capture: repeated write/ls, typing and Backspace, timing markers, service
queries and IRQ 5000 with advancing clock-msgs. Idle heartbeats are no longer
expected alongside every busy write. The scheduler model cannot establish the
actual speedup or prove that all remaining display latency is resolved.

## Image 47 faster; image 48 coalesces the double scroll repaint

User confirms improved speed on image 47 but reports two screen cycles for a
scroll. Excerpts in `docs/hardware/tty-latency-v47.log` show successful MM/SYS/
TTY/ls/write, and later write timings of 205–211 ticks. The separate prompt
costs one tick. The capture has no exception, but no IRQ-5000 sample either.

Two source behaviors caused avoidable transfer work: Enter echo immediately
painted its scroll before the command response, and begin-batch did not defer
ordinary glyphs until a later scroll occurred. Thus even a batched write could
paint characters that would subsequently move or leave the visible screen.

Image 48 computes the whole batch's text/cursor layout in memory, including
all required scrolling, and then paints final changed cells once. Enter echo
keeps its logical newline pending for the next output/character echo; starting
a batch preserves pending layout. No open batch spans IPC, and normal writes
still flush at completion. Explicit clear discards pending repaint state.
The tradeoff is deliberate: Enter alone does not visually scroll until the
next output or ordinary echo. The shell always writes a response or prompt.

Identity: `[FEATURE LCD-SCROLL 48]1`, `[TEST LCD-SCROLL 48]` and write timing
`[TTY WRITE V48 bytes=... ticks=...]`. All 20 scripts, clean Xtensa build,
image-layout and section/segment checks pass without warnings. Pixel tests
establish final-image equivalence and at most one paint per changed cell,
including nested batches and more than a screen of output. One modeled
Enter-plus-command workload drops from 344 to 178 cell writes. Symbols:
`_iram_end=0x40374354`, `_iram_ext_end=0x40380F78`, `_stack_top=0x3FCCE570`.

Hardware status: pending; no flash performed. Repeat commands on a full LCD,
check one visible scroll pass, wrapped erase and prompt placement, and capture
timings with continued CLOCK progress. Timing now includes the deferred Enter
scroll inside the next write; compare complete interaction time, not just a
single V47/V48 write marker. Software SPI transport remains unchanged.

## Image 48 scrolling accepted; image 49 adds MEM device IPC

The user confirms image-48 scrolling looks correct. Evidence excerpts in
`docs/hardware/lcd-scroll-v48.log` include repeated ls, MM/SYS/TTY success,
an 18-byte write and heartbeat tick 1928. No exception appears in the supplied
capture. That bounded acceptance does not include IRQ 5000 or full terminal
mode validation.

The next missing storage prerequisite was a device message service: the RAM
disk previously had only directly callable helpers. Image 49 adds memory.c's
MEM task using the existing MINIX RAM_DEV/DEV_READ/DEV_WRITE/TASK_REPLY ABI.
Only FS requests are serviced. Buffer mapping precedes data mutation, transfers
use 64-byte chunks, and EOF yields zero/short counts. Unknown minors and
unsupported operations are rejected; raw memory devices are not exposed.
MEM retains its existing mapped descriptor/stack and now blocks on RECEIVE
instead of remaining stopped. Other placeholder tasks remain stopped.

The disk command exercises five real exchanges while preserving the last
sector on success. It attempts restoration after test-I/O failure and reports
any restoration/verification failure. No format/reset is added to startup.
This command is a device check, not file I/O, and must be revisited once a
filesystem owns that sector. The ABI and limitations are in docs/ramdisk-ipc.md.

Markers: `[FEATURE RAM-IPC 49]1`, `[TEST RAM-IPC 49]`, normal-path
`[RAM V49 op=... result=... n=...]`, and `[RAM IPC V49 result=0]` on successful
shell completion. All 21 host scripts, clean Xtensa build and ELF/image checks
pass. Tests include the real backend, translated buffer bounds, 1 KiB blocks,
EOF, caller/operation checks, reply field aliases, transport errors, 100
checksum-preserving diagnostic cycles and injected partial-write/restore
failures. Existing production scheduler/IPC tests include MEM exchanges.
`_iram_end=0x40374368`, `_iram_ext_end=0x403814AC`, `_stack_top=0x3FCCEF10`.

Hardware remains pending; no flash performed. Run disk repeatedly and confirm
zero status, then check write/ls/mm/sys/ipc, LCD scrolling and CLOCK progress
through IRQ 5000. Filesystem structures, vectored block I/O and actual FS/user
services remain incomplete. The existing CP32 format marker is not a MINIX
filesystem and ls remains diagnostic output.

## Image 49 bounded stability result; image 50 superblock recognition

The supplied image-49 capture shows successful write/SYS/MM/IPC/ls commands
and heartbeat tick 1608, without an exception. The user reports kernel stable.
Excerpts: docs/hardware/ram-ipc-v49.log. It contains no disk command, RAM
service trace or IRQ 5000, so it does not establish MEM device correctness or
long-duration stability.

Image 50 adds read-only fsinfo through MEM DEV_READ. Original MINIX V2 headers
in both byte orders are decoded without struct-layout assumptions; capacity,
metadata overlap, bitmap sizes and shift bounds are validated. Errors do not
publish partial geometry. The blank RAM disk should report No MINIX filesystem.
No mount, format, inode traversal or file API is added, and ls remains a
diagnostic. Details: docs/minix-superblock.md and minix.port-status.md.

All 22 host scripts and the clean Xtensa build pass; no compiler warnings.
ELF size/segments/sections checked: `_iram_end=0x40374368`,
`_iram_ext_end=0x4038186c`, `_stack_top=0x3fccf380`. Parser and shell handler
are in extended IRAM. Markers: `[FEATURE MINIX-SUPER 50]1` and
`[TEST MINIX-SUPER 50]`. Existing service traces retain their versions.
Hardware pending; no flashing performed. Run fsinfo and disk, then the
accepted command/scrolling regression and extended CLOCK progress.

Review finding for future process lifecycle work: system.c:do_fork assigns
zero to p_reg.a[1] as a child return value, but a1 is Xtensa's stack pointer.
Do not enable fork on that assumption. Audit do_exec's complete saved frame
at the same time. This is outside the active storage-recognition path and is
not claimed as fixed by image 50.

## Image 50 hardware result — basic storage path accepted

The subsequent user capture identifies MINIX-SUPER 50. disk completes all five
512-byte transfers and reports `[RAM IPC V49 result=0]`, covering save,
pattern write/read, restoration and verification. fsinfo then reads 24 bytes
through MEM and prints No MINIX filesystem, as expected for the blank disk.
MM/IPC/SYS/ls and TTY output continue successfully, with SYS uptime 2147 and
no exception in the capture. Excerpts: docs/hardware/minix-super-v50.log.

Basic MEM I/O and blank-superblock recognition are now hardware-confirmed.
Repeated disk cycles, valid-image recognition, malformed-image behavior on
hardware and IRQ-5000/long-duration progress remain outside this evidence.
No firmware change or marker increment is needed for this result recording.

## Image 51 — real MINIX root-directory listing

Image 50 hardware established the basic MEM diagnostic and blank-superblock
path. Image 51 now provisions a deterministic volatile MINIX V2 boot image,
generated by tools/make_minix_demo.py. Its declared 63 zones exclude the final
physical block used by disk. ls reads actual root inode/map/directory records
through MEM IPC; hardcoded entries are removed. Expected entries: ., ..,
boot, README. fsinfo now reports inodes=32 zones=63 and Not mounted.

The read-only implementation supports seven direct zones and ASCII names.
It rejects unsupported larger directories, bad maps/types/sizes/zone pointers
and short I/O. It is not a mount, full consistency check or file-content API.
Details and next hardware procedure: docs/minix-directory.md.

All 23 host scripts and clean Xtensa build pass without warnings. Tests cover
disk endian conversion including 16-bit bitmap chunks, geometry and malformed
records, read failures and unchanged disks across ten real-backend listing/
diagnostic cycles using the shell/MEM adapter. ELF layout checked:
`_iram_end=0x4037438c`, `_iram_ext_end=0x40381d9c`, `_stack_top=0x3fcd18d0`.
The generated prefix occupies 8253 bytes of rodata; the RAM disk retains its
existing BSS allocation. New runtime code is in extended IRAM.

Markers: `[FEATURE MINIX-DIR 51]1`, `[TEST MINIX-DIR 51]`. No hardware result
yet and no flash performed. Confirm fsinfo/ls, repeat disk and listing, then
TTY/MM/SYS/IPC and extended clock regressions. Fork/exec frame issues remain
outside this storage step.
## Image 52 — missing LCD period glyph

The image-51 capture confirms a successful disk diagnostic, two real root
listings, valid superblock geometry and heartbeat tick 2619. Evidence:
docs/hardware/minix-dir-v51.log. The user reports incorrect LCD periods,
although USB correctly prints the directory names `.` and `..`.

Root cause: display.c:glyph_scaled had no period case and generated a fallback
pattern. Image 52 defines a 2x2 dot on the baseline. Cell dimensions, cursor
movement, opaque background painting and scrolling remain unchanged. The
font command now includes . and ..; host pixel tests verify both directory
rows and erasure. Markers: `[FEATURE LCD-DOT 52]1`, `[TEST LCD-DOT 52]`.
All 23 host scripts and warning-free clean build pass. ELF size, segments and
sections checked: `_iram_end=0x403743b0`, `_iram_ext_end=0x40381da8`,
`_stack_top=0x3fcd18f0`.
Hardware visual validation pending; no flashing performed. File reads and
mounting remain future work, and this capture does not establish IRQ-5000
or long-duration stability.
## Image 53 — read-only root files

The image-52 capture confirms ls/font/fsinfo/disk/MM/SYS/IPC execution and
heartbeat tick 3530 without an exception in the supplied log. It contains no
explicit LCD appearance confirmation or IRQ 5000. Evidence is preserved in
docs/hardware/lcd-dot-v52.log.

Image 53 implements root-name lookup and regular-file reads through MEM, exposed
by cat README and cat /README. The reader validates inode allocation/type/link
count/size and direct-zone ownership/bounds, returns zeros for sparse zones,
and handles EOF. Staged 64-byte reads preserve position and caller data on
device failure. cat distinguishes missing files and directory targets; it is
a text viewer, while the underlying reader returns exact bytes.

Seven direct zones and root names only are supported. There is no full path
resolver, permission enforcement, descriptor table, mount or writable file API.
The image and scratch reservation are unchanged. See docs/minix-file-read.md.

All 23 host scripts and the warning-free clean build pass. Tests cover endian,
EOF, sparse/empty/cross-zone files, invalid names/inodes/maps/zones, failure
atomicity, and ten checksum-preserving production cat/list/disk cycles through
the real backend and modeled MEM adapter. ELF checked: `_iram_end=0x403743b0`,
`_iram_ext_end=0x4038242c`, `_stack_top=0x3fcd1fe0`. New functions use extended
IRAM. Markers: `[FEATURE MINIX-READ 53]1`, `[TEST MINIX-READ 53]`.
Hardware remains pending; no flash performed. Validate cat success and errors,
repeat after disk, then regress directory, service, LCD and clock behavior.
## Image 54 — reversed keyboard event polarity

The image-53 hardware log confirms cat README and missing-file handling,
IRQ 5000 with unknown=0/clock-msgs=805, and heartbeat tick 5867. Full supplied
evidence: docs/hardware/minix-read-v53.log. The user reports Aa not behaving
as Shift, apparent uppercase via Fn, and inability to return to lowercase.

Confirmed defect: cp32_cardputer_key interpreted bit 7 as release, but the
official M5Stack TCA8418 reader interprets it as press. Modifiers were therefore
set on release and printable input emitted on release. Fn/Aa coordinates
already match the vendor map. Image 54 fixes polarity rather than swapping
keys, rejects invalid matrix positions and keeps Ctrl-Shift-letter usable.
No Fn function layer, caps lock or overflow recovery is claimed.

The host tests repeated the original polarity mistake and are corrected.
All 23 scripts and clean build pass without warnings, including repeated case
transitions, release order and Fn independence. ELF sections/segments checked:
`_iram_end=0x403743b0`, `_iram_ext_end=0x40382414`, `_stack_top=0x3fcd1fd0`.
Markers: `[FEATURE KBD-SHIFT 54]1`, `[TEST KBD-SHIFT 54]`. Hardware validation
pending; no flashing performed. References and procedure: docs/keyboard-shift.md.
