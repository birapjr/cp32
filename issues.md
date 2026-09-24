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
## Image 55 — uppercase A duplicated C

The active glyph_scaled uppercase table used identical rows for A and C.
This confirms the reported LCD defect independently of keyboard input.
Image 55 gives A an apex, stems and crossbar and adds case: Aa Cc to font.
Pixel tests assert independent bitmaps for all four characters while retaining
cell geometry, lowercase forms, cursor and scrolling behavior.

All 23 host scripts and warning-free clean build pass. ELF size/segments/
sections checked: `_iram_end=0x403743b0`, `_iram_ext_end=0x40382420`,
`_stack_top=0x3fcd1fe0`. Markers: `[FEATURE LCD-A 55]1`, `[TEST LCD-A 55]`.
Hardware pending; no flash performed. Check font and README in ls on LCD.

The supplied image-54 log is preserved in docs/hardware/kbd-shift-v54.log;
it contains interleaved build text. Visible serial results include cat boot
and cat dot directory errors, disk success, MM/SYS, IRQ 5000 with unknown=0,
and heartbeat 6288. This is not explicit validation of repeated Aa/Fn cycles.
## Image 56 — subdirectory paths after LCD acceptance

The user explicitly accepts image 55's LCD and reports all known issues fixed.
docs/hardware/lcd-a-v55.log preserves the supplied capture: README reads,
disk/MM/SYS/IPC/TTY success, IRQ 5000 unknown=0 and heartbeat 5165. This closes
the reported visual/modifier issues within the user's acceptance, without
claiming exhaustive terminal-mode or lost-key-event validation.

Image 56 adds validated directory-inode opening and iterative pathname
traversal for cat and ls. Relative paths start at root; dot/dot-dot, repeated
slashes and directory-only trailing slashes are supported. Every intermediate
inode must be a directory; names are limited to 14 bytes and paths to 255
(the shell line remains 63 bytes). Invalid paths never publish a partial handle.
The demo adds boot/README as a hard link to the existing README inode, updating
its link count. Scratch space and storage size are unchanged.

All 23 host scripts and warning-free clean build pass. Tests cover both byte
orders, bounds, missing/non-directory components, corrupt intermediate metadata,
every short/error read, fixture links and unchanged disk checksums through
nested cat/list/disk integration. ELF checked: `_iram_end=0x403743b0`,
`_iram_ext_end=0x40382690`, `_stack_top=0x3fcd2270`. Runtime helpers use extended
IRAM. Markers: `[FEATURE MINIX-PATH 56]1`, `[TEST MINIX-PATH 56]`.
Hardware pending; no flash performed. Procedure: docs/minix-paths.md.
Indirect zones, symlinks, permissions, cwd and FS descriptors remain unimplemented.
## Image 57 — MINIX reference source layout

Image-56 capture confirms ls boot and nested/root README reads through tick
2872; excerpts are in docs/hardware/minix-path-v56.log. At the user's request
for following the MINIX 2 book, portable FS code is moved from combined kernel
helpers into src/fs/super.c, inode.c, path.c, open.c, read.c and utility.c,
with reference-matching header names. The former combined sources/headers are
removed. docs/minix-source-layout.md maps CP32 entry points to MINIX functions.

This is a source-organization change, not a complete MINIX FS server. Existing
helper APIs, on-disk ABI, bounded read-only behavior, fixture and command syntax
are preserved. The inode decode portion of file-open is now in inode.c; open.c
orchestrates lookup/open. Byte conversion is shared in utility.c. Kernel RAM
devices, fixture loader and temporary shell stay in their appropriate existing
locations. Tests now live in tests/fs; MEM integration remains in tests/kernel.

All 23 host scripts and warning-free clean build pass. The Makefile puts FS
objects in build/fs and tracks FS headers. ELF size/sections/segments checked:
`_iram_end=0x403743b0`, `_iram_ext_end=0x4038266c`, `_stack_top=0x3fcd2240`.
FS routines remain in extended IRAM. Markers: `[FEATURE FS-LAYOUT 57]1`,
`[TEST FS-LAYOUT 57]`. Hardware pending; no flash performed. Recheck nested
listing/cat and disk because addresses changed despite preserved behavior.
## Image 58 — reference-aligned memory manager

The image-57 capture confirms root and boot listing plus the expected error 8
for ls /boot/README (a regular file), with heartbeat 3087. Evidence:
docs/hardware/fs-layout-v57.log. It contains no MM request or IRQ-5000 result.

At the user's request, MM now follows MINIX directory/file names: src/mm/main.c
holds the service loop/dispatch, alloc.c holds allocation/free/coalescing and
ownership, with mm.h/proto.h. The old kernel/mm.c is removed. numap/mem_copy
move into kernel/system.c to match MINIX's address-translation boundary;
kernel/memory.c remains the separate RAM-device driver. Semantics and ABI are
preserved; CP32 is still kernel-linked and does not yet implement full MM
fork/exec/signals. See docs/minix-source-layout.md for the book mapping.

The Makefile builds build/mm objects separately and tracks MM headers. Tests
move to tests/mm/test_main.py, retaining allocation/ownership/coalescing,
200 receive/reply cycles and mapping checks against kernel/system.c. All 23
host scripts and warning-free clean build pass. ELF size/sections/segments
checked: `_iram_end=0x403743b0`, `_iram_ext_end=0x4038267c`,
`_stack_top=0x3fcd2250`. MM and mapping/copy routines remain in extended IRAM.
Markers: `[FEATURE MM-LAYOUT 58]1`, `[TEST MM-LAYOUT 58]`. Hardware pending;
no flashing performed. Validate repeated mm and nested cat/list/disk/services.

## Reference-aligned library layout — image 59

[x] Compare remaining source responsibilities against minix-2.0.0. Move
memcpy/memset/strcpy/strcmp/strtol unchanged from kernel/klib.c into matching
lib/ansi filenames. Keep CP32 delay and hardware/bring-up files in place.
Preserve the freestanding call0 ABI, implementations and section attributes.
MINIX's full libc semantics are not claimed; existing strtol gaps are recorded
in the refreshed minix.port-status.md. See docs/minix-source-layout.md.

[x] Update separate library object paths and relocate runtime tests under
tests/lib/ansi. Clean build has no warnings; all 23 host scripts pass.
ELF sections/segments and image-layout check pass: _iram_end=0x403743b0,
_iram_ext_end=0x4038267c, _stack_top=0x3fcd2250.

[x] Record supplied image-58 hardware results: mm alloc-release-result=0,
cat boot/README prints the expected two lines, disk result=0, heartbeat 1369.
No repeated MM cycle or IRQ-5000 result is present in that capture.

Identity: [FEATURE LIB-LAYOUT 59]1 and [TEST LIB-LAYOUT 59] immediately before
idle. Hardware pending; no flash performed. Recheck mm, cat boot/README,
ls boot, disk and sys, with normal keyboard/LCD behavior and clock progress.

## MINIX V2 single-indirect regular-file reads — image 60

[x] Record image-59 hardware acceptance in docs/hardware/lib-layout-v59.log:
SYS/MM/IPC succeed, root and /boot list correctly, /boot/README content is
correct, TTY WRITE returns 18, IRQ 5000 has unknown=0. No disk command was
included in this capture.

[x] Implement single-indirect regular-file mapping in fs/read.c, following
MINIX read_map/rd_indir: seven direct zones plus 256 four-byte V2 entries in
one 1024-byte indirect block, independent of zone scaling. Inode open stores
validated geometry and checks the indirect root's bounds/allocation/aliasing.
Read mapping explicitly decodes either endian order, checks referenced data
zones and allocation, and preserves sparse holes. CP32 uses four-byte metadata
reads and a 64-byte staged data transfer instead of MINIX's block cache; this
preserves bounded stack use, freestanding ABI and error atomicity. Directories
remain direct-only; double-indirect regular files return unsupported.

[x] Host tests cover direct/indirect boundaries, last entry 255, both byte
orders, 1/2 KiB zones, missing indirect trees and entries, EOF, corrupt and
unallocated pointers, metadata/data short reads, unchanged buffers/positions,
and unchanged handles on open failures. All 23 scripts pass. Clean build is
warning-free; ELF/image layout checks pass. _iram_end=0x403743b0,
_iram_ext_end=0x40382870, _stack_top=0x3fcd2450. Mapping/read code stays in
extended IRAM. Previous uncommitted image-59 layout work is preserved.

Identity: [FEATURE MINIX-INDIRECT 60]1 and [TEST MINIX-INDIRECT 60] immediately
before idle. Hardware validation pending; no flash performed. Existing mm,
sys, ipc, ls /boot, cat /boot/README, disk and write commands are regression
checks only: the boot fixture has no indirect file. An on-device indirect-file
fixture is still needed to validate the new path through real MEM IPC.

## On-device single-indirect fixture — image 61

[x] Record supplied image-60 log in docs/hardware/minix-indirect-v60.log.
Root/nested README reads, boot listing, disk/MM/SYS/IPC/TTY calls succeed;
IRQ 5000 reports unknown=0 and heartbeat reaches 5332. This validates the
existing direct-file regression, not the new single-indirect path.

[x] Extend the production boot image with boot/INDIRECT (inode 4). Seven
1024-byte direct zones 9..15 contain 224 labeled lines; indirect block 16
points to data zone 17 containing the final INDIRECT READ OK line. File size
7185 bytes. Inode/zone maps and boot directory size are updated, root README
and links preserved, final scratch block 63 remains outside filesystem zones.
The generator retains its minimal fixture option for existing hostile parser
tests; its normal CLI, used by firmware and MEM integration, emits the larger
fixture. No runtime test hook or new shell command is introduced.

[x] Verify the production fixture through actual shell cat, filesystem, MEM
handler and backing storage with modeled IPC. Check every direct-zone line,
indirect tail, allocation metadata, and unchanged disk checksum. All 23 host
scripts pass; clean cross-build has no warnings and ELF/image layout checks
pass. _iram_end=0x403743b0, _iram_ext_end=0x40382870,
_stack_top=0x3fcd4820. Initialized fixture prefix is 17425 bytes (formerly
8253); task stacks remain inside DRAM. Earlier uncommitted work preserved.

Identity: [FEATURE INDIRECT-DEMO 61]1 and [TEST INDIRECT-DEMO 61] immediately
before idle. Hardware pending; no flash performed. Run ls boot, then
cat boot/INDIRECT: it prints 224 numbered direct-zone lines and must end with
INDIRECT READ OK. Then run disk, cat README and mm. The long output exercises
scrolling and scheduling as well as the first single-indirect data read.

## Read-only file seeking and tail — image 62

[x] Record image-61 hardware in docs/hardware/indirect-demo-v61.log. The full
INDIRECT fixture reaches INDIRECT READ OK, IRQ 5000 has unknown=0, subsequent
disk/MM checks return zero, and heartbeat reaches 6336. Single-indirect reads
are now hardware-validated for this fixture (not all malformed/endian cases).

[x] Add cp32_minix_file_seek to fs/open.c, matching MINIX do_lseek's file
position responsibility. Support start/current/end, signed 32-bit offsets,
beyond-EOF seeking, and unchanged state on invalid origin/negative position/
overflow. CP32 retains caller-owned handles rather than claiming descriptor
syscalls; target range is enforced on 64-bit host tests too. No cache/read-ahead
state exists to invalidate, and no disk I/O occurs during seeking.

[x] Add tail filename to the CP32 command client: last ten lines using 64-byte
backward windows, filling reads that stop at zone boundaries. Reuse cat's
existing rendering/error behavior. A trailing newline does not add an empty
line; unterminated final lines count. This is an ordinary command, not a manual
kernel diagnostic hook. Existing cat behavior remains tested.

[x] Clean build, all 23 host scripts, whitespace and ELF/image-layout checks
pass without warnings. Tests cover offset arithmetic, unchanged failure state,
EOF and indirect readback; shell/MEM integration checks the expected ten-line
indirect tail and empty/short/unterminated text. _iram_end=0x403743b0,
_iram_ext_end=0x40382b04, _stack_top=0x3fcd4ad0. Earlier uncommitted work kept.

Identity: [FEATURE MINIX-SEEK 62]1 and [TEST MINIX-SEEK 62] immediately before
idle. Hardware pending; no flash performed. Run tail boot/INDIRECT: expect
Direct zone 7, line 24 through line 32 then INDIRECT READ OK. Run tail README,
cat README, disk and mm to regress shorter reads and other services.

## Double-indirect regular files — image 63

[x] Record supplied image-62 hardware evidence: tail boot/INDIRECT shows
zone 7 lines 24–32 then INDIRECT READ OK; tail README and MM succeed. A
subsequent capture runs disk (correcting earlier dist typo): five 512-byte
transfers and RAM IPC result=0. Neither capture includes IRQ 5000.

[x] Extend fs/read.c mapping using MINIX read_map's excess/256 and excess%256
indices; inode.c decodes/validates the double root at inode byte 56. Handle
missing root/child/data as sparse holes. Validate allocation, geometry and
root/parent/direct aliases before following pointers. Four-byte explicit
endian decoding and staged 64-byte data reads preserve bounded Xtensa stack
use and unchanged caller buffer/position on failure. Regular files now cover
7+256+65536 zones; directories remain direct-only. This is read-only mapping,
not a complete filesystem integrity checker or descriptor service.

[x] Add boot/DOUBLE (inode 5): sparse logical prefix of 263 KiB, double root
18, child table 19, data zone 20. Last ten lines are Double indirect line 02
through 10, then DOUBLE INDIRECT READ OK. tail exercises the new tree without
printing/scanning the sparse prefix. Zone/inode maps and boot listing updated;
reserved scratch block remains outside the filesystem. No runtime probe added.

[x] All 23 host scripts and warning-free clean build pass. Tests cover both
byte orders and 1/2 KiB zones; single/double boundary, child-table transition,
last addressable entry, sparse trees, corrupt/unallocated roots/children/data,
short reads, unchanged failure state, and fixture tail via shell/MEM/backend.
ELF/image layout checks pass: _iram_end=0x403743b0,
_iram_ext_end=0x40382c4c, _stack_top=0x3fcd5910.

Identity: [FEATURE MINIX-DOUBLE 63]1, [TEST MINIX-DOUBLE 63] immediately before
idle. Hardware pending; no flash performed. Run tail boot/DOUBLE, then
tail boot/INDIRECT, disk and mm. Avoid cat boot/DOUBLE for routine validation:
it would render the large sparse prefix as question marks.

## Inode metadata / stat — image 64

[x] Save supplied image-63 hardware capture in docs/hardware/minix-double-v63.log.
Both single/double indirect tails match, disk and MM return zero, and IRQ 5000
reports unknown=0. This confirms the fixture paths, not every host edge case.

[x] Add fs/stadir.c:cp32_minix_stat, matching MINIX stadir.c:do_stat/stat_inode.
Resolve paths, check inode allocation, decode mode/links/uid/gid/logical size
and atime/mtime/ctime explicitly in either byte order. Publish only on success.
Support regular files and directories; special types remain unsupported. Unlike
a read, metadata inspection need not map data zones. No permission enforcement,
descriptor fstat, device metadata or timestamp updates are claimed. CP32 keeps
caller-owned results and byte decoding for the freestanding Xtensa environment.

[x] Add stat pathname to the temporary shell, printing type/inode/size/links,
uid/gid/mtime (decimal). Sparse files report logical size. Add the translation
unit to firmware, parser tests, integration build and reference-layout checks.
All 23 host scripts and warning-free clean build pass. Tests cover endian
metadata, nonzero owners/timestamps, root/nested paths, sparse logical size,
malformed inodes, every short-I/O stage and unchanged output on failure. Real
shell/MEM-backend tests verify README and boot metadata output. ELF/image
checks pass: _iram_end=0x403743b0, _iram_ext_end=0x40382f60,
_stack_top=0x3fcd5ca0. Prior working-tree changes preserved.

Identity: [FEATURE MINIX-STAT 64]1, [TEST MINIX-STAT 64] immediately before idle.
Hardware pending; no flash performed. Run stat README (inode 3 size 61 links 2),
stat boot (directory inode 2 size 80 links 2), stat boot/DOUBLE (inode 5 size
269576 links 1), then tail boot/DOUBLE, disk and mm. Fixture uid/gid/mtime are 0.

## Working directory — image 65

[x] Record image-64 hardware in docs/hardware/minix-stat-v64.log: stat boot,
README, DOUBLE and INDIRECT report expected metadata; disk/MM pass and IRQ
5000 has unknown=0. No new fault appears in the supplied capture.

[x] Add cp32_minix_abspath in fs/path.c and cp32_minix_chdir in fs/stadir.c,
following MINIX path traversal and do_chdir/change responsibilities. Join
relative paths without prematurely collapsing components: README/.. must
fail as not-a-directory. Validate target directory, canonicalize for pwd,
verify canonical/original inode identity, then publish cwd. Invalid paths,
path overflow and I/O errors preserve cwd. Absolute paths ignore cwd; root
parents remain at root. Bounded buffers preserve freestanding execution.

[x] Add cd/pwd to CP32 shell and route ls/cat/tail/stat through its working
directory. Bare ls uses current directory; bare cd returns to root. This is
single-client textual state, not MINIX per-process inode references, permission
checking, chroot, symlinks or mount-aware cwd. The read-only tree keeps paths
stable; those features require replacing/expanding this bounded API later.

[x] Clean build, all 23 host scripts, ELF/image and whitespace checks pass.
Tests cover relative/absolute and dot paths in both byte orders, failed
file-as-directory traversal, empty/oversized paths, every short-I/O stage,
unchanged cwd on errors, and actual shell/MEM relative stat/cat integration.
_iram_end=0x403743b0, _iram_ext_end=0x40383418, _stack_top=0x3fcd6280.
Previous uncommitted work is preserved.

Identity: [FEATURE MINIX-CWD 65]1, [TEST MINIX-CWD 65] immediately before idle.
Hardware pending; no flash performed. Run pwd (expect /), cd boot, pwd (expect
/boot), ls, stat DOUBLE, tail DOUBLE, cd README (must fail and retain /boot),
cd .., pwd (expect /), disk and mm.

## Previous-directory switching — image 66

[x] Save image-65 capture as docs/hardware/minix-cwd-v65.log. cd boot, pwd,
relative ls/stat/tail, cd / succeed; IRQ 5000 unknown=0 and heartbeat 9436.
The attempted cd - returns error 5 because image 65 resolves a literal hyphen.
No disk/MM command is present in this capture.

[x] Add previous-directory policy to cp32-shell.c:cp32_shell_cd. Successful
changes save the old path; cd - revalidates and switches to it, printing its
path. Before any successful change, report No previous directory. Stage the
new cwd so lookup and I/O failures preserve both current and previous paths.
Keep this CP32 command policy outside fs/stadir.c: MINIX change/do_chdir
validates directories, while previous-directory selection belongs to a shell.
No per-process environment/OLDPWD ABI is claimed. Existing FS and call0 ABI
remain unchanged; bounded path buffers only.

[x] All 23 host scripts and warning-free clean build pass. Integration tests
exercise first-use error, repeated toggling, regular-file rejection and failed
MEM read with both directory states preserved. ELF/image and whitespace
checks pass: _iram_end=0x403743b0, _iram_ext_end=0x403834d4,
_stack_top=0x3fcd6450. Earlier user/work-in-progress changes preserved.

Identity: [FEATURE CWD-PREV 66]1 and [TEST CWD-PREV 66] immediately before idle.
Hardware pending; no flash performed. Run cd boot, cd /, cd - (prints /boot),
pwd, tail DOUBLE, cd - (prints /), pwd, disk, mm. A failed cd README while in
/boot must leave current/previous directory state unchanged.

## Single-indirect directory mapping — image 67

[x] Record image-66 hardware in docs/hardware/cwd-prev-v66.log: cd - returns
from /boot to /, relative DOUBLE/README reads succeed, root listing/read works,
IRQ 5000 unknown=0 and heartbeat reaches 6608. No disk/MM command in capture.

[x] Extend directory inode decoding to seven direct zones plus 256 indirect
entries. Validate the indirect root and allocation before publishing a handle.
path.c directory scanning calls the shared fs/read.c:cp32_fs_read_map beyond
direct zones, corresponding to MINIX search_dir using read_map. Preserve
byte-order decoding, bounded Xtensa stack buffers, deleted-entry skipping,
inode/name checks and rejection of directory holes. Double-indirect directory
sizes still return unsupported; regular-file double mapping is unchanged.

[x] Add boot/LARGE, inode 6: seven direct zones 21..27, table 28 and data 29.
Dot entries are in zone 21, deleted entries fill intervening slots, and README
is the first indirect entry. Correct link accounting: boot links=3 size=96;
README links=3 (root, boot, LARGE). Root unchanged. Scratch block stays reserved.
This is a real on-disk fixture used by normal ls/cat/path operations.

[x] All 23 host scripts and warning-free clean build pass. Tests cover actual
shell/MEM listing and nested cat, both byte orders, corrupt/empty/unallocated
pointers, and metadata/data I/O failures preserving handles and entry outputs.
ELF/image/whitespace checks pass: _iram_end=0x403743b0,
_iram_ext_end=0x40383628, _stack_top=0x3fcd88b0. Prior changes preserved.

Identity: [FEATURE MINIX-DIRMAP 67]1, [TEST MINIX-DIRMAP 67] immediately before
idle. Hardware pending; no flash performed. Run ls boot/LARGE (., .., README),
cat boot/LARGE/README, cd boot/LARGE, pwd, cat README, cd /, disk and mm.
Repeated lookups scan deleted entries, so this fixture may be slower until a
filesystem block cache is implemented.

## Mask disabled pending interrupts before dispatch — image 68

[x] Record image-67 log in docs/hardware/minix-dirmap-v67.log. Large-directory
navigation/listing, parent traversal, disk/MM and cwd toggling work through
heartbeat 15749. IRQ 10000 has unknown=0; IRQ 15000 shows unknown=8265 and
raw pending=0x00018040. This is a new diagnostic failure, not clean IRQ soak.

[x] Identify architectural difference: MINIX mpx386.s receives individual
8259-delivered interrupt vectors; CP32 irq.S reads Xtensa's raw INTERRUPT
bitmap. Espressif core-isa.h defines timer mask 0x00018040 (CCOMPARE0/1/2 on
6/15/16); TRM chapter Interrupt Matrix documents INTENABLE masking. Startup
programs compare registers and leaves those lines disabled. Raw pending bits
can therefore exist without those lines being enabled interrupt causes.
Sources: https://github.com/espressif/esp-idf/blob/master/components/xtensa/esp32s3/include/xtensa/config/core-isa.h
https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf

[x] Mask IRQ dispatch input with INTENABLE after saving registers. Preserve
raw pending state, enabled sources, enabled-unregistered reporting, frame ABI,
owner selection and source-owned device acknowledgement. No timer reprogramming
or pending-bit clearing is added. IRQ status becomes [IRQ V68 count=...].

[x] Clean build and all 23 host scripts pass without warnings. Assembly model
executes both entry paths with masked compare bits, no enabled pending bits,
enabled unregistered bits and full bitmaps; verifies register restoration and
unchanged CPU mask/pending state. Existing handoff tests also inject compare
pending bits. Disassembly confirms rsr interrupt / rsr intenable / and before
call0 dispatch. ELF/image and whitespace checks pass: _iram_end=0x403743b8,
_iram_ext_end=0x40383628, _stack_top=0x3fcd88b0.

Identity: [FEATURE IRQ-MASK 68]1 and [TEST IRQ-MASK 68] immediately before idle.
Hardware pending; no flash performed. Run beyond tick 15000 with ls/cat/cd on
boot/LARGE, disk and mm. Expect unknown=0 in IRQ V68 reports while only the
registered SYSTIMER is enabled. Raw pending may still be 0x00018040: those bits
are deliberately neither cleared nor hidden. Actual hardware confirmation is
required before marking the long-run issue resolved.

## Clean filesystem block cache — image 69

[x] Record image-68 hardware in docs/hardware/irq-mask-v68.log. IRQ 15000 and
20000 retain unknown=0 despite raw pending=0x00018040 with ien=4; MM/disk,
SYS/IPC, cwd/listing and metadata calls succeed. This confirms the masked
pending-bit fix over the supplied longer run, not indefinite soak proof.

[x] Add fs/cache.c matching MINIX get_block/invalidate responsibilities:
four clean 1024-byte blocks, FIFO replacement and reader/capacity identity.
The single FS client wraps uncached MEM reads; cache misses remain actual IPC.
Bound parser requests to 64 bytes and stage output so cross-block failures
never partially publish bytes. Invalidate a victim before filling it; short
reads never publish valid cache state. Partial final device blocks are bounded.
CP32 uses static internal DRAM rather than MINIX's full device/hash/LRU/dirty
buffer pool; no write-back, concurrent callers or pinned buffers yet.

[x] Invalidate before all shell raw DEV_WRITE attempts, including failed or
partial writes and diagnostic restoration. Boot fixture initialization precedes
runtime cache use. Any future backing-device writer/reset path must likewise
invalidate before mutation. Host tests that mutate backing storage directly
explicitly invalidate it. Existing read-only filesystem semantics are preserved.

[x] Clean build, all 24 host scripts and ELF/image/whitespace checks pass.
New cache tests cover hits, eviction, crossing, short I/O, capacity edges,
reader replacement and explicit invalidation. Shell/MEM integration validates
raw successful/failed writes cannot retain stale blocks and listing LARGE
uses fewer than 40 device requests while traversing hundreds of slots.
_iram_end=0x403743b8, _iram_ext_end=0x40383920, _stack_top=0x3fcd9bd0.
Static cache blocks begin at 0x3fcb0798. Prior working-tree changes preserved.

Identity: [FEATURE MINIX-CACHE 69]1 and [TEST MINIX-CACHE 69] immediately before
idle. Hardware pending; no flash performed. Run ls boot/LARGE repeatedly,
cat boot/LARGE/README, cd boot/LARGE, pwd, cat README, cd /, disk, then repeat
ls boot/LARGE and cat boot/LARGE/README. Check tail boot/DOUBLE, mm, and IRQ
reports beyond tick 15000. Expect identical output with fewer MEM exchanges.

## Read-only file descriptors — image 70

[x] Save image-69 hardware in docs/hardware/minix-cache-v69.log. LARGE listing
and README reads work before and after disk; DOUBLE tail and MM succeed.
IRQ 15000 unknown=0 despite masked raw pending bits. READE typo correctly
reports File not found. This confirms the supplied cache regression sequence.

[x] Add fs/filedes.c following MINIX get_fd/get_filp responsibilities: eight
read-only slots, lowest-free allocation, publish only after successful open,
independent offsets, checked read/seek/close and reuse after close. Small
wrappers delegate pathname/open, read mapping and seek to existing files.
CP32 static DRAM replaces full MINIX per-process filp references for now;
no descriptor inheritance, dup, credentials, user syscall ABI or multiple
clients are claimed. Negative CP32 statuses remain distinct from syscall errno.

[x] Route normal shell cat/tail through the descriptor API, always closing
a successful open after EOF or read/seek failure. Tail obtains size through
seek-to-end and retains bounded backward scanning. No manual diagnostic probe.
Existing command syntax, cache invalidation and on-disk fixture stay unchanged.

[x] All 24 host scripts, warning-free clean build, ELF/image and whitespace
checks pass. Twenty fill/drain cycles verify failed opens do not consume slots,
independent offsets, exhaustion, reuse, invalid/double closes, failed-read
buffer/position preservation, failed-seek output preservation and indirect
reads. Repeated shell/MEM tests cover actual descriptor-backed cat/tail.
_iram_end=0x403743b8, _iram_ext_end=0x40383ae8, _stack_top=0x3fcda060.
Descriptor storage is internal DRAM at 0x3fcb0960. Prior edits preserved.

Identity: [FEATURE MINIX-FD 70]1, [TEST MINIX-FD 70] immediately before idle.
Hardware pending; no flash performed. Repeatedly run cat README, tail
boot/DOUBLE and cat boot/LARGE/README (more than eight commands total), then
cat missing, cat boot, disk, and cat README. Successful reads must continue
without descriptor exhaustion after both successful and failing commands.

## Shared open descriptions / duplication — image 71

[x] Save complete image-70 evidence in docs/hardware/minix-fd-v70.log. Two
boot sequences are present; the second has twelve consecutive successful cat
README calls, correct missing/directory errors, DOUBLE tail, disk result=0,
and another successful README read. IRQ 10000 has unknown=0 with raw masked
pending bits. Descriptor reuse is hardware-confirmed for this sequence.

[x] Separate descriptor slots from open descriptions in fs/filedes.c. Each
successful independent open owns a fresh description; aliases reference the
same offset via bounded reference counts. Close releases a description only
on the final reference. Add fs/misc.c duplication wrappers following MINIX
misc.c:do_dup, with dup2 source validation, self-target no-op and replacement.
No per-process tables, fork inheritance or user syscall interface yet. Static
internal DRAM and the existing read/seek ABI preserve CP32 task invariants.

[x] Tail scans through an owned duplicate, sharing its final position with
the original output descriptor and closing the duplicate even on scan error.
This exercises dup in production without a manual test hook. Add cmp file1
file2 to compare raw bytes via independent opens, with bounded buffers and
cleanup on every failure. Command parsing accepts two whitespace-separated
paths; quoting and filenames containing spaces are not supported by cmp yet.

[x] Clean build, all 24 host scripts, ELF/image and whitespace checks pass.
Host tests exercise shared/independent offsets, last-close lifetime, 20 reuse
cycles, full tables, dup2 replacement/self/alias cases and invalid descriptors.
Shell/MEM tests cover equal/different files, repeated second-open failures,
long equal files, I/O error recovery and descriptor-backed tail. ELF confirms
cp32_fd_dup/share are retained in extended IRAM; dup2 remains host-tested API
without a production caller. _iram_end=0x403743b8,
_iram_ext_end=0x4038400c, _stack_top=0x3fcda600. Earlier edits preserved.

Identity: [FEATURE MINIX-DUP 71]1, [TEST MINIX-DUP 71] immediately before idle.
Hardware pending; no flash performed. Run cmp README boot/LARGE/README
(identical), cmp README boot/INDIRECT (differ), cmp README missing (error 5),
then repeat tail boot/DOUBLE and cat README, disk and mm. Tail directly tests
shared-position duplicate lifetime; cmp tests independent simultaneous opens.

## Lowercase boot filenames — image 72

[x] Record image-71 hardware in docs/hardware/minix-dup-v71.log. Comparisons
report identical and different as expected, tail DOUBLE and README succeed,
disk/MM pass, IRQ 10000 unknown=0. The missing-file comparison used READMe
as its first operand, so it confirms case-sensitive failure, not specifically
second-open cleanup. Host tests cover second-open cleanup separately.

[x] Rename boot fixture entries README/INDIRECT/DOUBLE/LARGE to
readme/indirect/double/large at the user's request for easier keyboard input.
Update every hard-link name, parser and shell/MEM test path and listing.
Keep contents, inode numbers, links, byte lengths, geometry and read-only
behavior unchanged. Case-sensitive lookup is preserved (uppercase README
is now a negative test). Repository source filenames are unchanged.

[x] Warning-free clean build, all 24 host scripts, ELF/image and whitespace
checks pass. Memory endpoints are unchanged: _iram_end=0x403743b8,
_iram_ext_end=0x4038400c, _stack_top=0x3fcda600.

Identity: [FEATURE LOWER-NAMES 72]1 and [TEST LOWER-NAMES 72] immediately before
idle. Hardware pending; no flash performed. Run ls boot, cat readme,
cat boot/large/readme, tail boot/double, cmp readme boot/large/readme.
Use lowercase paths for all future hardware tests; historical logs retain
the spelling of the image actually tested.

## Descriptor-based metadata — image 73

[x] Record image-72 hardware in docs/hardware/lower-names-v72.log. Lowercase
large-directory listing, root/nested readme and double tail succeed; cmp
boot/readme boot/large/readme returns identical after an earlier cpm typo.
IRQ 10000 unknown=0. No disk/MM command is present in that capture.

[x] Store inode identity in each open description and add cp32_fd_fstat in
fs/stadir.c, following MINIX do_fstat/stat_inode. Path stat and descriptor
fstat share one byte-decoding helper. Descriptor metadata reads only allocation
and inode bytes using the saved reader/geometry/number, with no pathname lookup
or offset change. Duplicates retain identity after another reference closes.
Bad descriptors and metadata I/O failures preserve output and shared position.
The existing single-client, read-only and regular-file descriptor limits apply;
this does not add inode-cache lifetime/unlink semantics or a user syscall ABI.

[x] Tail obtains size through fstat instead of seeking solely to query EOF.
Its production duplicate/seek/read path remains intact. No manual probe added.
All 24 host scripts and warning-free clean build pass. New tests cover both
byte orders, path-stat parity, exact metadata-only reads, descriptor identity
after synthetic entry removal, shared lifetime, unchanged offsets/output on
short reads and invalid descriptors, and sparse logical size. Full shell/MEM
regressions exercise tail with fstat. ELF/image/whitespace checks pass:
_iram_end=0x403743b8, _iram_ext_end=0x403840e4, _stack_top=0x3fcda700.
cp32_fd_fstat is retained in extended IRAM at 0x40382248. Prior edits preserved.

Identity: [FEATURE MINIX-FSTAT 73]1 and [TEST MINIX-FSTAT 73] immediately before
idle. Hardware pending; no flash performed. Run tail readme, tail boot/indirect,
tail boot/double, cmp readme boot/large/readme, disk, tail boot/double and mm.
Expect existing output; the normal tail path now obtains metadata by descriptor.
