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

## G0 standalone console and USB development restart — image 74

[x] Compare MINIX 2.0 cstart and console putk: startup prepares main and the
local console does not depend on a host. CP32 retains the same startup,
process/stack initialization, Cardputer TTY and shell paths for both modes.
ESP32-S3-specific selection happens before the first diagnostic print.

[x] Implement the boot gate accepting either a fresh USB serial EP1 IN token or
three consecutive low GPIO0 samples. Configure only GPIO0 as a pulled-up
input; discard loader-era IN status. An empty FIFO or USB SOF alone cannot
select development mode. G0 selects a latched standalone mode in which
USB diagnostics return without touching/waiting on the FIFO. Development
output pays at most one bounded wait on a stalled FIFO and resumes when it
drains. Hardware USB reset/flashing controls and USB pins stay unchanged.

[x] Implement idle-path monitoring for a new USB serial reader in standalone mode.
After G0 is released, write RTC OPTIONS0 SW_SYS_RST with interrupts locked:
the ROM reloads the image and CP32 reinitializes state from its entry point.
This is a system reset, not a call to main. Register definitions are based on
Espressif ESP-IDF v5.5.3 ESP32-S3 usb_serial_jtag_reg.h, gpio_reg.h,
io_mux_reg.h, rtc_cntl_reg.h and rtc_cntl_ll_reset_system; no IDF runtime/API
is linked. Detection means host serial endpoint reads, not cable voltage.

[x] Clean cross-build, all 25 host test scripts, image-layout and whitespace
checks pass. New production-serial.c MMIO simulation covers stale events,
FIFO-ready-without-reader, debounce, simultaneous USB/G0, watchdog service,
10,000 standalone logging iterations, timeout suppression/recovery, G0-held
reset deferral, and reset requests. Idle tests require polling without
changing process ownership. ELF sections/segments and reset disassembly
checked: _iram_end=0x40374314, _iram_ext_end=0x403842ec,
_stack_top=0x3fcda920. Boot gate and console poll live in extended IRAM;
reset writes 0x80000000 to 0x60008000 after lock().

Identity: [BOOT V74 console=usb], [FEATURE GO-CONSOLE 74]1 and
[TEST GO-CONSOLE 74] immediately before idle. Flashed with make flash and
verified by esptool hash. Initial USB capture showed a live heartbeat
(ticks=1106, heartbeat=1); it missed early boot output and does not validate
the new reset path. Subsequent user feedback confirms G0 startup/reset works
as expected. The supplied USB capture identifies [FEATURE GO-CONSOLE 74]1
and [TEST GO-CONSOLE 74], valid data/BSS sentinels and aligned stack, all CORE
checks equal to 1, keyboard initialization, repeated ls output, and
[MM IPC V44 alloc-release-result=0]. This is user-reported G0/reset validation
plus serial evidence of USB startup and shell operation; the capture alone
does not identify the reset cause. Held-G0 reset deferral, charge-only behavior
and the cat readme/sys test commands remain host-tested or undocumented on
hardware, not independently confirmed by this log.

Hardware procedure: disconnect USB, power-cycle, press/release G0 after
power-on, run ls, cat readme and sys on the keyboard, then reconnect USB and
open a reader. Expect a full fresh boot with image-74 markers, a reset shell
and fresh uptime. A held G0 must delay the software reset until release.

## Read-only directory descriptors — image 75

[x] Compare MINIX fs/open.c:common_open, fs/read.c:read_write and libc
_opendir/_readdir. MINIX permits read-only directory opens and shares byte
offsets through filp references. CP32 now supports directories in its bounded
single-client descriptor table alongside regular files. No new user syscall
ABI or full libc DIR stream is claimed. Xtensa still uses explicit endian
byte decoding, bounded stack objects and internal extended IRAM.

[x] Reuse validated directory inode/mapping state for descriptor read, seek,
fstat and dup lifetime. cp32_fd_readdir shares the byte offset, rejects
unaligned positions and regular files, skips deleted entries and preserves
offsets/outputs on errors. Raw directory reads reject missing indirect data
zones instead of returning sparse zeros. Existing single-indirect directory
and console-safe filename limits remain. The standalone file-handle API
keeps its regular-file-only contract.

[x] Route normal shell ls through open/readdir/close. cat, tail and cmp use
a regular-file check and close rejected descriptors. Existing command output
is preserved; no manual probe or extra diagnostic path was introduced.

[x] Clean cross-build, all 25 host scripts, ELF/image and whitespace checks
pass. New tests exercise both byte orders, shared and independent directory
offsets, fstat, raw reads and unaligned iterator rejection, seek/EOF, deleted
entries, malformed entries, short I/O, single-indirect reads and hole rejection,
close/reuse, plus repeated shell ls/cat/tail/cmp success/error cleanup.
_iram_end=0x40374314, _iram_ext_end=0x40384648, _stack_top=0x3fcdaca0.
cp32_fd_open=0x40381cdc and cp32_fd_readdir=0x40382050 are in extended IRAM,
as are the shell ls and regular-file open helper, with literals/callees
resolved to internal SRAM. The low IRAM endpoint is unchanged from image 74.

[x] User-flashed image 75 hardware capture confirms markers
[FEATURE MINIX-DIRFD 75]1 and [TEST MINIX-DIRFD 75], valid sentinels/aligned
stack, all CORE checks equal to 1, root/boot listings, cat readme, cd boot
followed by another listing, double-indirect tail, MM alloc/release result=0
and RAM disk result=0. Selected serial evidence is saved in
docs/hardware/minix-dirfd-v75.log. No exception appears in the supplied log.

[ ] Remaining image-75 hardware coverage: ls boot/large, directory error
cases, cmp and prolonged descriptor reuse are not shown in this capture.
They remain host-tested. G0/USB restart was not retested in this capture.

## MINIX descriptor controls / fcntl — image 76

[x] Port MINIX misc.c:do_fcntl / filedes.c:get_fd(start) semantics for
F_DUPFD, F_GETFD, F_SETFD, F_GETFL and F_SETFL. Constants match MINIX 2.0
include/fcntl.h with CP32 prefixes. Table mutation stays in CP32 filedes.c;
misc.c:cp32_fd_dup now uses F_DUPFD with minimum 0, as MINIX libc dup does.
The normal tail command reaches this path through its owned scan duplicate.
No manual probe or new shell command is needed.

[x] Descriptor flags are separate from shared open-description status flags.
F_DUPFD selects the lowest free descriptor at/above the supplied minimum and
never replaces an occupied descriptor. Duplicates share offsets/status and
start with CLOEXEC clear; dup2 self preserves flags, replacement clears them.
Open/close/reuse reset descriptor flags, and a new description starts O_RDONLY.
F_SETFL accepts only APPEND/NONBLOCK, preserving read-only access. These flags
are metadata for the currently supported regular files/directories; no write,
pipe, blocking-device or executable-loader functionality is claimed. CLOEXEC
is stored only: no exec lifecycle exists to consume it. Record-lock requests
explicitly report unsupported; unknown commands and bad arguments are rejected.

[x] Preserve the architecture-independent MINIX distinction between per-fd
and per-filp state using bounded static arrays, without changing existing
file/message/process layouts. Code and literals remain in internal SRAM.
Clean warning-free cross-build, all 25 host scripts and ELF/image/whitespace
checks pass. Tests cover minimum descriptor bounds and occupied slots,
high-bound/full-table exhaustion, shared vs independent flags, ignored flag
bits, dup/dup2 self and replacement, shared seek offsets, close/reuse over
12 cycles for regular files and directories, and invalid request/fd precedence.
Existing shell/MEM tests exercise tail through the new production F_DUPFD path.
_iram_end=0x40374314, _iram_ext_end=0x40384778, _stack_top=0x3fcdae30.
cp32_fd_fcntl=0x4038229c, cp32_fd_dup=0x40381c50, and the tail caller are in
extended IRAM. The low IRAM endpoint is unchanged.

[x] User-flashed image-76 serial capture confirms markers
[FEATURE MINIX-FCNTL 76]1 and [TEST MINIX-FCNTL 76], valid data/BSS sentinels,
aligned stack and all CORE checks equal to 1. tail boot/double prints the
expected last ten lines ending DOUBLE INDIRECT READ OK through the production
F_DUPFD path. ls boot/large lists dot, dot-dot and readme through the indirect
directory path. RAM disk and MM alloc/release both report result=0. No exception
appears in the supplied capture. Selected evidence is saved in
docs/hardware/minix-fcntl-v76.log.

[ ] Remaining hardware coverage: repeated tail/descriptor reuse, other tail
paths, cmp and flag operations beyond the normal F_DUPFD path are not shown
in this capture; their coverage remains host-side. No exec or record-locking
support is claimed. Flashing remains reserved for the user.

## MINIX directory-stream library — image 77

[x] Port the responsibilities of MINIX lib/posix/_opendir.c, _readdir.c,
_closedir.c, _rewinddir.c and lib/other/_seekdir.c, telldir.c into matching
CP32 library directories. cp32_opendir/readdir/closedir own the descriptor;
seekdir/telldir/rewinddir manage its logical byte position. Opening validates
the descriptor with fstat, sets CLOEXEC and NONBLOCK through fcntl, and closes
on every failure before publishing the stream. The flags do not add an exec
lifecycle or blocking-device support.

[x] Adapt MINIX's malloc-backed buffered DIR to a caller-owned, explicitly
initialized CP32 stream with no read-ahead allocation. Valid telldir positions
are aligned byte offsets; invalid/overflowing seeks preserve the position.
The supported MINIX V2 fixed 14-byte names still use FS endian/allocation/name
validation. FLEX directories and the POSIX DIR/errno/user-syscall ABI remain
outside this change. A stream must not be copied or its owned fd manipulated
externally. Error/EOF leaves the caller's entry unchanged; successful close
invalidates the stream, so repeated close cannot close a reused descriptor.

[x] Move normal shell ls onto opendir/readdir/closedir. Directory flags now
run through the production fcntl path as well as host tests. Preserve listing
text, failure reporting and all prior filesystem/keyboard/USB functionality.
Add library sources and header dependencies to the existing bare-metal Makefile;
no runtime migration, process/frame layout change or diagnostic probe.

[x] Clean warning-free cross-build, all 26 host test scripts, ELF/image and
whitespace checks pass. The new tests compile the production library and FS,
cover both byte orders, two independent streams, flags, full-table exhaustion,
failed/repeated opens, double-close with fd reuse, EOF/output preservation,
seek/tell/rewind including invalid offsets, and indirect entries after deleted
slots. Inject short I/O at every disk read in opendir (including fstat) and
prove all eight descriptors remain available afterward. Existing shell/MEM
regressions test production ls and error cleanup on the new stream path.
_iram_end=0x40374314, _iram_ext_end=0x403849fc, _stack_top=0x3fcdb0d0.
All six library routines and shell ls are in extended IRAM with internal-SRAM
literals/callees; low IRAM remains unchanged. opendir=0x403846e0,
readdir=0x403847d8, closedir=0x40384854.

[x] User-flashed image-77 hardware capture saved verbatim in
docs/hardware/minix-dirstream-v77.log. Markers [FEATURE MINIX-DIRSTREAM 77]1
and [TEST MINIX-DIRSTREAM 77] identify the image. Data/BSS sentinels and stack
alignment pass, and all CORE checks report 1. Root ls and ls boot/large return
the expected entries through the stream library. ls readme reports the expected
directory error 8. Subsequent tail boot/double prints the last ten lines ending
DOUBLE INDIRECT READ OK; disk and MM checks both return 0. IRQ count reaches
5000 with unknown=0 and clock-msgs=713. No exception appears in this capture.

[ ] Remaining hardware coverage: repeated listings/descriptor exhaustion,
ls missing and seek/tell/rewind are not exercised in this capture. They remain
host-tested only. Passing commands after ls readme show continued operation,
not proof of leak-free prolonged reuse. Flashing remains the user's responsibility.

## Range-safe numeric conversion — image 78

[x] Compare MINIX lib/ansi/strtol.c and lib/other/errno.c. Complete the CP32
strtol range/error behavior and add its already-declared strtoul counterpart.
Both use a shared unsigned cutoff/remainder parser for bases 2..36 and base-0
inference. Preserve ASCII whitespace/sign handling, stop at the first invalid
digit, continue consuming digits after overflow, saturate at the appropriate
signed/unsigned limit and report ERANGE. Invalid bases report EINVAL. No-digit
conversion leaves endptr at the original input. A hexadecimal prefix requires
a following hex digit; bare 0x consumes only its valid leading zero. This
intentionally avoids the reference implementation's permissive bare-prefix
edge behavior and signed-overflow assumptions.

[x] Preserve ESP32-S3/MINIX 32-bit long limits using the project limits header,
including during 64-bit host tests. Negative unsigned conversions wrap at
ULONG_MAX; LONG_MIN is returned without overflowing signed negation. Successful
conversion preserves errno. Add the freestanding errno storage declared by the
existing header. This is shared kernel-library state for current boot-time
callers, not per-process/TLS errno or a completed user libc ABI. No hosted
ctype, allocation, IDF API or hardware behavior is introduced.

[x] Harden the existing kernel env_parse path: clear errno before conversion
and reject conversion errors before publishing a parameter, even when its
allowed maximum is LONG_MAX. Check errno as nonzero because library errno
values are positive while kernel error constants use MINIX's negative convention.
The environment-parser tests now link CP32's real strtol instead of the host's.
No manual runtime probe or shell command was added.

[x] Clean warning-free cross-build, all 26 host scripts, ELF/image and
whitespace checks pass. Conversion tests run with undefined-behavior sanitizer
and cover signed/unsigned boundaries in all bases 2..36, over/underflow,
leading zeros, long overflow strings, whitespace/sign/no-digit cases, invalid
bases, incomplete hex prefixes, endptr and stale errno. Kernel tests prove
boundary values work and overflow cannot be accepted or overwrite the result.
_iram_end=0x403741e4, _iram_ext_end=0x40384c78, _stack_top=0x3fcdb350.
strtol=0x4038494c, strtoul=0x403849a8 and their helpers are in extended IRAM;
errno is in BSS at 0x3fcb3344. Low IRAM shrinks because strtol moves into the
ordinary-runtime region; all literals/callees remain internal SRAM.

[x] User-flashed image-78 hardware regression passes in the supplied capture.
[FEATURE MINIX-STRTOL 78]1 and [TEST MINIX-STRTOL 78] identify the image.
Data/BSS sentinels and stack alignment pass; all CORE checks report 1.
ls boot/large returns the expected entries, tail boot/double prints the last
ten lines ending DOUBLE INDIRECT READ OK, and disk/MM both return result=0.
No exception appears. CLOCK_TASK text is split by interrupt diagnostics;
subsequent CLOCK receipt markers show continued operation. Selected evidence
is saved in docs/hardware/minix-strtol-v78.log.

Numeric conversion boundaries and environment error handling remain host-tested
only; these commands validate the image regression, not those conversion paths.
Flashing remains reserved for the user.

## MINIX tail line/byte counts — image 79

[x] Port regular-file count semantics from MINIX commands/simple/tail.c:
-n count selects lines, -c count selects bytes; unsigned/negative counts
select the end and +N selects the start (1-based, historical +0 equals +1).
Unsigned/negative zero selects EOF. The existing bare filename defaults to
the last ten lines. Support -- before an option-like filename. Reject malformed
counts, unsupported options and signed-long overflow without opening a file.
Follow mode, standard input, obsolete syntax and a separate user executable
remain outside this kernel-linked command implementation.

[x] Use the production strtol from image 78, checking errno without depending
on the kernel/library error-sign convention. Handle LONG_MIN magnitude without
signed overflow. Use bounded 64-byte forward/backward reads and direct seeks
for byte counts; clamp byte positions at EOF. Preserve the fstat/dup/fcntl and
shared-offset scan path, descriptor cleanup, and normal output formatting.
Xtensa freestanding constraints require no stdio stream, dynamic line buffer,
large stack object or hosted utility runtime. Existing no-option behavior is
unchanged, including final-newline handling.

[x] Clean warning-free cross-build, all 26 host scripts, ELF/image and
whitespace checks pass. Shell/MEM tests link the actual target-width CP32
strtol/errno and exercise signed line/byte counts, +0/+N, zero, counts beyond
EOF, LONG_MIN/LONG_MAX, malformed/overflow counts, --, whitespace, default
behavior, missing-file/directory errors, and unterminated lines spanning
64-byte windows. Repeated invalid options and fault injection at every read
in the forward-line path are followed by successful commands to check cleanup.
_iram_end=0x403741e4, _iram_ext_end=0x40384f90, _stack_top=0x3fcdb690.
Tail parsing/scanning and strtol remain in extended IRAM; the low IRAM
endpoint is unchanged. No diagnostic-only trigger or probe was introduced.

[x] User-flashed image 79 confirms tail -n 1 boot/double, tail -n +2
readme, tail -c 3 boot/double and tail -n 0 readme return the expected
output. Startup and CORE checks pass, with no exception in the capture.
Full evidence: docs/hardware/minix-tail-count-v79.log. Overflow rejection
and the remaining count boundaries are host-tested only. The user reports
an incorrect LCD plus glyph; serial input and +2 parsing work correctly.

## LCD plus glyph — image 80

[x] Add the missing + bitmap to glyph_scaled; previously it used the
fallback pattern. MINIX console.c out_char stores printable character codes
for the PC display font; CP32 rasterizes characters on the SPI LCD instead.
Preserve the character byte, cell size, cursor movement and batched painting.
The existing font command now includes a plus sample.

[x] Add an independent pixel-model regression for the cross, black margins
and row gap. Clean warning-free build and all 26 host test scripts pass;
ELF sections and segments inspected. _iram_end=0x40374208,
_iram_ext_end=0x40384f9c, _stack_top=0x3fcdb690.

[x] User-flashed image 80 verified on 2026-09-28: user confirms the LCD
plus is correct; font prints all samples and tail -n +2 readme prints the
expected second line. Startup and all CORE checks pass; no exception appears.
Selected evidence: docs/hardware/lcd-plus-v80.log.

## MINIX head — image 81

[x] Port first-line selection from minix-2.0.0/src/commands/simple/head.c:
default ten lines and historical -N positive decimal count. Also accept -n N
and -- for consistency with the shell's tail interface. Reject missing,
zero, signed, malformed and overflowing counts before opening files.
Only one regular file is supported; stdin, multiple-file headers and a
standalone user executable remain unported.

[x] Reuse descriptor ownership and the cat/tail display path with a head
mode. Stop output at the requested newline or EOF; never issue another read
after reaching the limit. Existing 64-byte reads may read ahead within the
last chunk, but the command closes its private descriptor. Preserve bounded
stack use, error cleanup and existing text sanitization/CRLF formatting.
Unlike MINIX's stdio implementation, Xtensa uses freestanding kernel-linked
helpers, without FILE streams, allocation or a hosted runtime.

[x] Clean warning-free cross-build and all 26 host test scripts pass. Tests
cover default ten-line cutoff, both count syntaxes, whitespace/--, long
lines across 64-byte reads, unterminated and empty files, counts beyond EOF,
invalid/overflow counts, directory/path errors, repeated calls and injected
read failures followed by successful commands. Existing cat/tail tests pass.
ELF sections/segments inspected: _iram_end=0x40374208,
_iram_ext_end=0x403851fc, _stack_top=0x3fcdb920;
cp32_shell_head=0x40378ed8 in extended IRAM. Low IRAM endpoint unchanged.

[x] User-flashed image 81 passes head readme, head -1 readme and
head -n 2 readme with expected output. Startup/CORE checks pass; IRQ count
5000 reports unknown=0 and clock-msgs=777, with no exception in the capture.
Full evidence: docs/hardware/minix-head-v81.log. Default ten-line cutoff
on longer files, invalid options and error cleanup remain host-tested only.

## MINIX wc — image 82

[x] Adapt minix-2.0.0/src/commands/simple/wc.c to the existing shell:
wc [-lwc] [--] filename. Default output is lines, words, bytes and pathname;
combined/repeated flags select columns in that fixed order. Compact spacing
suits the LCD. One regular file only; stdin, multiple-file totals, separate
option groups and a standalone user executable are not implemented.

[x] Use bounded 64-byte descriptor reads, retaining word state between
chunks. Count raw bytes before display sanitization, ASCII whitespace word
boundaries, and LF lines. Intentional corrections to the MINIX 2 reference:
count a final word without trailing whitespace, and do not count form feed
as a line. All counters are unsigned 32-bit and bounded by file size.
Xtensa uses no hosted stdio, dynamic allocation or large automatic buffer.
Close the descriptor on all read outcomes; publish no partial counts on error.

[x] Clean warning-free build and all 26 host scripts pass, including tests
for option combinations/order, repeated flags, --, empty files, missing paths,
directories, malformed options, long words spanning chunks, unterminated
words, all six whitespace bytes, NUL/high bytes and injected read failures
followed by successful commands. ELF sections/segments inspected:
_iram_end=0x40374208, _iram_ext_end=0x40385598, _stack_top=0x3fcdbd00.
cp32_shell_wc=0x40378c9c is in extended IRAM; low IRAM endpoint unchanged.

[x] User image-82 capture confirms wc readme = 2 8 61, -l = 2,
-wc = 8 61, boot/double = 11 lines and boot/indirect = 225 lines. ls boot
also succeeds. IRQ 5000 reports unknown=0, clock-msgs=825; no exception.
Full capture: docs/hardware/minix-wc-v82.log. Error cases remain host-tested.

## Application execution foundation — image 83

[x] Audit application prerequisites against MINIX mm/exec.c and crtso.s.
Record the ordered loader/runtime/lifecycle work in docs/application-execution.md.
MM has allocation/release only; commands still execute inside the kernel.

[x] Add src/mm/exec.c stack staging helper: argc, argv NULL, envp NULL,
32-bit little-endian relocated string pointers, 16-byte aligned SP, up to
32 combined vector entries and a 4096-byte staging region. Bounded scans,
address-overflow checks and a validation pass precede any writes. Trusted
MM-owned vectors only, with documented non-aliasing requirements. This adapts
MINIX stack pointer patching to Xtensa; it neither changes process state nor
loads executable code. No diagnostic trigger or fake application was added.

[x] New tests/mm host regression covers layout, relocation, empty vectors,
alignment, malformed/null/oversized inputs, address wrap, unchanged buffers
on error and writes restricted to the selected stack region. All 27 scripts
and a clean cross-build pass. ELF sections/segments checked: _iram_end=0x40374208,
_iram_ext_end=0x40385870, _stack_top=0x3fcdbfd0;
cp32_exec_stack=0x40384f80 in extended IRAM.

[ ] Execution integration and hardware validation remain pending. Identity
[FEATURE EXEC-STACK 83]1 / [TEST EXEC-STACK 83] distinguishes this build.
There is no new application command yet; boot and existing shell commands
can provide regression evidence only. The stack builder is host-tested,
not exercised by production execution. Flashing remains with the user.

## Hello executable contract — image 84

[x] Adapt MINIX mm/exec.c read_header responsibility to fixed Xtensa ELF32.
Add MM image validator with exact-read callback and unchanged output on failure.
Reject wrong architecture/format, malformed program headers, invalid flags,
size/address/offset overflow, overlapping file payloads and invalid entry.
No target memory is written and no process is made runnable.

[x] Define one application slot using the existing D/IRAM mapping: RX code
403d8000..403dc000 (data alias 3fce8000..3fcec000), data/BSS
3fcec000..3fcef000, stack 3fcef000..3fcf0000. Kernel linker now asserts
_stack_top <= 3fce8000. Runtime alias copying/execution remain unverified.

[x] Add separate make hello target, call0 crt0 and hello C artifact. Entry
uses the prior argc/argv/envp stack and a versioned trusted write/exit callback
table. No kernel bridge or application launch exists yet. hello.elf has no
undefined symbols; entry 403d8000, RX file/memory 156 bytes, RW file 32 bytes,
RW memory 36 bytes (4 bytes BSS). Cross-disassembly confirms call0 entry flow.

[x] Clean kernel/application build and all 28 test scripts pass. Validator
runs against the actual hello ELF plus malformed/truncated/overflowing inputs
and injected reader failures; host hello verifies greeting and initial BSS.
ELF section/segment checks: _iram_end=40374208, _iram_ext_end=40385c80,
_stack_top=3fcdc3e0; cp32_image_read=40385268 in extended IRAM.

[ ] Hardware execution pending. Identity [FEATURE EXEC-IMAGE 84]1 and
[TEST EXEC-IMAGE 84] immediately before idle. No new shell command; existing
commands validate kernel regression only. Images 83/84 have no supplied
hardware execution evidence. Built but not flashed. Next integrate segment
copy/zeroing, package hello, and implement process startup/write/exit lifecycle.

## Segment staging and runtime stack reservation — image 85

[x] User image-84 capture confirms boot, all CORE checks, ls and ls boot.
hello reports unknown, as expected before command/lifecycle integration.
No exception appears. This is kernel regression evidence, not application
execution evidence; the image validator was not invoked on hardware.

[x] Implement cp32_image_load as the MINIX exec load_seg/BSS-zeroing stage.
Validate the full metadata first, then clear and fill inactive 16K code and
12K data buffers using bounded 64-byte reads. A payload read failure clears
both regions; output metadata publishes only on success. Caller must own an
inactive slot and stable, nonaliasing source. No target addresses, instruction
synchronization or runnable process state are changed by this helper.

[x] Correct image-84's incomplete kernel/application overlap assertion.
main initializes task/server stacks beyond the 32K boot stack, through
_stack_bottom + 0x13000 (LOW_USER=2). Reserve those bytes in .task_stacks,
keep the boot SP unchanged, and assert _runtime_stack_end <= 0x3fce8000.
Configuration guards require reservation review if process counts increase.
The application slot has never been written by the production path.

[x] Clean kernel/hello builds and all 28 host scripts pass. Real hello ELF
and synthetic images are staged; verify payload bytes, zero BSS/tails,
guard bytes, unchanged output on failure, each header/payload read failure,
clearing after partial copy and successful reload. ELF inspection:
_iram_end=40374208, _iram_ext_end=40385df4, _stack_top=3fcdc550,
_runtime_stack_end=3fce7550, cp32_image_load=403855fc. Remaining gap to
application code's data alias: 0xab0 bytes. Packaging must account for this
small margin; the linker now rejects overlap with runtime stacks.

[ ] Hardware validation pending for image 85, identified by
[FEATURE EXEC-LOAD 85]1 and [TEST EXEC-LOAD 85]. Built but not flashed.
No hello command yet: next implement filesystem packaging, inactive process
ownership, instruction synchronization, write/exit bridges and return to shell.
The load helper is host-tested only, not called by the production MM loop.

## Packaged hello file — image 86

[x] Package actual cross-linked hello.elf at /boot/hello, inode 7, mode
0100555, direct zones beginning at 30. Retain existing filesystem entries,
indirect fixtures and reserved final diagnostic sector. Reject empty or
larger-than-seven-zone payloads. The default firmware make target now builds
hello before generating the disk, avoiding stale or absent application bytes.

[x] Link a compact stripped ELF with four-byte segment alignment (-n/-s),
validated by the same production image parser. Current file size is 916 bytes;
entry remains 403d8000, with 156 code bytes, 32 initialized data and 4 BSS.
Use hello.map for symbols; kernel debug information remains enabled.

[x] Encode boot disk as packed nonzero runs plus offset/length records.
Provisioning resets the RAM disk then reconstructs runs. This preserves the
exact disk bytes while removing zero gaps from kernel rodata. No heap size
reduction or application-slot movement. MINIX directory/inode allocation
rules follow the existing original V2 image generator.

[x] Clean build and all 29 host scripts pass. New integration test exercises
actual generated provisioning, normal FS descriptor reads/stat, byte-for-byte
ELF comparison and image loading/BSS zeroing. Sparse round trips cover no
application, one-byte and seven-zone payloads, plus oversized rejection.
Real ELF integration requires the cross-built artifact; firmware build creates
it automatically. _iram_end=40374208, _iram_ext_end=40385e44,
_stack_top=3fcd7c40, _runtime_stack_end=3fce2c40. Kernel-to-application
margin is now 0x53c0 bytes (21440), without reducing the heap.

[ ] Hardware pending: [FEATURE HELLO-FILE 86]1 / [TEST HELLO-FILE 86].
Built but not flashed. Run ls boot (now includes hello), stat boot/hello
(916 bytes, regular inode 7), wc -c boot/hello (916 boot/hello), then disk
and mm. hello is still not a launch command. Remaining next step: process
ownership/context installation, runtime instruction alias copy/synchronization,
write/exit bridge and shell wait/resume. No application execution is claimed.

## First foreground hello execution — image 87

[x] Image-86 hardware confirms /boot/hello listing, inode 7 size 916 and
wc byte count 916. Startup/CORE checks pass; no exception appears. Selected
evidence: docs/hardware/hello-file-v86.log.

[x] Wire hello shell command to load /boot/hello through descriptors, copy
segments into the reserved SRAM slot, zero stack, and build argc/argv/envp
using only its top 512 bytes. Preserve >3K for C, IPC and interrupt frames.
Use memw/isync before publishing the initial call0 context. Initialize all
registers/bookkeeping, PC, aligned SP/a1/a15, PS=0x100 and service pointer.
Application endpoint LOW_USER+1 is dedicated and must be free before launch.

[x] Trusted write/exit callbacks use SENDREC to the waiting FS shell. FS
bounds output to 256 bytes in the application data/stack region, formats it
through normal serial/TTY output, and replies. Exit leaves the child blocked
waiting for a reply; FS verifies that state, reaps the descriptor without
resuming it, resets its billing target and returns to the prompt. Single
foreground application only, no protected user ABI, general exec, fork, or
application-fault containment. The next launch reloads and zeroes the slot.

[x] Reference behavior: MINIX mm/exec.c load_seg/initial stack publication
and exit/parent-wait responsibility adapted to the existing CP32 scheduler
and IPC. ESP32-S3 SRAM aliases follow existing linker policy, corroborated by
Espressif soc/esp32s3 memory_layout.c (same-order I/D buses). No ESP-IDF runtime
introduced. Actual instruction execution/synchronization still needs hardware.

[x] Clean build and all 30 test scripts pass. New production lifecycle model
covers fresh-context publication, 20 launches/reaps, bounded output and invalid
requests, load/stack failures, parent ownership and billing reset. ELF/loader,
BSS/argument tests and existing scheduler/IPC suites also pass. Model stubs
hardware boundaries and IPC delivery; it does not execute Xtensa instructions.
_iram_end=40374208, _iram_ext_end=403863c0, _runtime_stack_end=3fce3280;
cp32_application_run=40378048; services remain in internal SRAM.

[x] Image 87 hardware: two hello launches print the greeting and exit=0,
with the shell accepting subsequent commands; disk and MM return result=0.
Startup and all CORE checks pass. This confirms the first trusted application
load/start/write/exit path and repeated launch on this hardware. Selected
capture: docs/hardware/hello-run-v87.log. The user reports the missing LCD !
glyph; serial output is correct. Fault isolation/general exec remain absent.

## LCD exclamation glyph — image 88

[x] Add ! to glyph_scaled and the existing font command. MINIX console.c
out_char passes printable character codes to the PC font; CP32 must render
them in software on the LCD. Preserve character bytes, text-cell size,
cursor/batching behavior and application output. Only the missing bitmap
previously fell through to the fallback pattern.

[x] Pixel-model regression checks the vertical stem, separated dot, margins
and row gap. Clean warning-free build, all 30 test scripts and ELF layout
checks pass. _iram_end=4037422c, _iram_ext_end=403863cc,
_stack_top=3fcd8290, _runtime_stack_end=3fce3290.

[ ] LCD appearance pending: [FEATURE LCD-BANG 88]1 and [TEST LCD-BANG 88]
immediately before idle. Built but not flashed. Run font or hello and check !.

## Application arguments — image 89

[x] Image-88 capture shows font samples, cd boot and successful hello/exit=0.
Full capture: docs/hardware/lcd-bang-v88.log. LCD ! appearance is not provable
from serial alone; no explicit visual confirmation was supplied.

[x] Pass shell arguments through the production application API and existing
MINIX-style argc/argv/envp stack builder. Replace the hardcoded one-element
argv with caller-owned trusted vectors, copied before runnable publication.
Preserve Xtensa crt0's register/stack contract and the 512-byte argument budget.
Shell accepts up to eight whitespace-separated arguments within its existing
63-byte line limit. No quoting/expansion. hello prints each received argument
via the same bounded IPC write service; plain hello preserves its greeting.

[x] Clean build and all 30 test scripts pass, including new shell parsing
coverage for spaces/tabs, no args, maximum/too-many args, overlong input,
load error/descriptor cleanup. Lifecycle tests verify passed vector contents;
hello tests verify complete argument output. Prior stack relocation and ELF
provisioning/loading tests continue to pass. hello.elf is now 1184 bytes.
ELF endpoints: _iram_end=4037422c, _iram_ext_end=40386564,
_runtime_stack_end=3fce3590, all below application reservations.

[ ] Hardware pending: [FEATURE HELLO-ARGV 89]1 / [TEST HELLO-ARGV 89].
Built but not flashed. Try hello one two: greeting, arg: one, arg: two,
Hello exit=0. Then hello with no arguments and disk/mm regression checks.

## Pathname application launch — image 90

[x] Latest user image-89 capture confirms hello and hello um dois: correct
argument output, exit=0 and resumed shell. This supersedes the prior image-89
hardware-pending note; maximum arguments and failure paths remain host-tested.

[x] Add run path [up to 8 arguments] using cwd-relative/absolute resolution.
Share tokenization and loading with the existing hello shortcut. Follow MINIX
mm/exec.c's path/permission/header ordering while retaining the current trusted
single-slot service: reject nonregular files through open_file, require at least
one execute bit, then validate/load ELF. No UID/GID credential checks or PATH
search yet. Close descriptors on stat, permission and load failure. Derive the
process display name from argv[0]'s basename; preserve argument bytes and signed
exit status. No new privilege or concurrent execution claim.

[x] Package a separately linked /boot/echo at inode 8, zones 37..43. It uses
the same call0 startup/linker contract, emits arguments separated by spaces and
a final newline, and checks BSS on each launch. This is the basic behavior of
MINIX commands/simple/echo.c; -n and buffered stdio are not implemented. Correct
the stale shared ABI comment now that its write/exit bridge is operational.

[x] Clean kernel/application build and all 30 host scripts pass. Shell models
cover both path forms, the hello shortcut, count limits, execute-bit denial,
missing/stat/load failures, descriptor cleanup/retry and signed status. Lifecycle
model alternates process names across 20 launches. Production FS integration
loads both real ELF payloads into the same host buffers and verifies exact file
bytes, mode, segment contents and cleared BSS/tails. Packaging tests cover both
seven-zone slots and retain the diagnostic sector. Echo host tests cover empty
args, literal arguments, write failure and repeated entry without BSS reset.

_iram_end=4037422c, _iram_ext_end=40386744, _runtime_stack_end=3fce3cc0;
hello.elf=1184 bytes, echo.elf=1108 bytes. The fixed application reservation is
unchanged. Documentation-only review estimates remain a snapshot of image 89.

[ ] Hardware pending: [FEATURE APP-PATH 90]1 / [TEST APP-PATH 90] immediately
before idle. Built but not flashed. From / run:
run /boot/hello one two; run /boot/echo one two (one two, Application exit=0);
cd boot; run echo again; run missing (Cannot open executable);
run readme (File is not executable); run hello recovered; hello; disk; mm.
These validate sequential programs, cwd lookup, permission rejection and recovery.
Malformed-image tests remain host-only; no test-only executable is added to disk.

## Shared Xtensa exec context — image 91

[x] Image-90 hardware confirms run /boot/hello one two, run /boot/echo one two,
and run echo again after cd boot, each exiting with status 0. IRQ 5000 reports
unknown=0 and clock-msgs=756. Full capture: docs/hardware/app-path-v90.log.
Missing/nonexecutable rejection and malformed ELF recovery remain host-only.

[x] Add kernel/context.c:cp32_exec_frame and use it from application launch
and legacy SYS_EXEC. MINIX exec resets machine context for the replacement
image; Xtensa requires a1/a15 consistent with SP, call0 a2 argument/result,
initial RFE PS=0x100, and cleared SAR/loop/general state. Reject null PC or
zero/misaligned SP without mutation. Placement and map policy stay with caller;
the helper does not change runnable state or confer memory protection.

[x] SYS_EXEC accepts MM requests only and requires an existing receiver,
rejecting free/sending/runnable targets before frame mutation. Clear obsolete
blocked-frame fields and message pointer, reset alarm and bound name copying;
publish ready only after initialization. Retain trace-stop policy. Correct
SYS_FORK child result from a1 (stack pointer) to a2 (call0 result). This does
not finish fork bookkeeping/map cloning or establish a general MM exec API.

[x] Clean build, all 31 scripts and ELF section/segment checks pass. New
production-handler tests check register initialization, unchanged-on-invalid
input, caller/target rejection, stale receive-frame cleanup, name termination,
ready publication, tracing and preserved fork stack/a2 result. The lifecycle
model uses the actual shared initializer across repeated launches.
_iram_end=4037422c, _iram_ext_end=4038682c, _runtime_stack_end=3fce3dd0;
cp32_exec_frame=40378000 in extended IRAM. No linker region or assembly/frame
layout change. No runtime test trigger or diagnostic probe added.

[ ] Hardware pending: [FEATURE EXEC-CONTEXT 91]1 / [TEST EXEC-CONTEXT 91].
Built but not flashed. Run hello, run /boot/echo one two, then hello again,
disk and mm. This validates the shared initializer through the working launcher;
legacy SYS_EXEC/FORK remain host-tested only and are not exposed as shell calls.


## APP-EXIT 92 — explicit termination and exit status (2026-09-29)

Image 91 hardware result supplied by the user: CORE checks pass;
`hello one two`, `run /boot/echo one two`, and `hello` exit successfully;
`disk` and `mm` report result=0. This validates the shared frame initializer
through the foreground launcher, not legacy SYS_EXEC/FORK.

MINIX reference: src/mm/forkexit.c do_mm_exit/mm_exit suppresses the reply to
an exiting child and retains an eight-bit exit status. CP32 keeps its existing
trusted call0 callback/SENDREC bridge and fixed slot; the parent now reports
status & 255, directly rather than as a POSIX wait word. The child remains
blocked and is reaped before another image may reuse its slot.

`hello --exit N` invokes the exit callback directly, without a greeting or
return through main. Decimal 0..65535 is accepted; malformed/out-of-range
arguments print usage and return 2. Ordinary hello arguments are unchanged.
Host tests exercise explicit exit via a nonreturning modeled callback, parser
boundaries, unsigned/signed status normalization and repeated production reaping.

Hardware pending: [FEATURE APP-EXIT 92]1 / [TEST APP-EXIT 92].
Try hello --exit 7, hello --exit 263 (both report 7), hello --exit 65535
(reports 255), hello --exit 65536 (usage and 2), then hello, run /boot/echo ok,
disk and mm. No flashing performed. Fault recovery, cancellation, general MM
exit/wait, signals and resource ownership remain unfinished.

Build validation: clean cross-build and all 31 host scripts pass, without
compiler warnings. ELF section/segment checks pass: _iram_end=4037422c,
_iram_ext_end=4038682c, _runtime_stack_end=3fce4000, below app alias 3fce8000.


## APP-ENV 93 — initial application environment (2026-09-29)

Image 92 hardware passed explicit exits 7, 263 -> 7, 65535 -> 255 and
invalid 65536 -> usage/2, followed by hello, echo and disk/MM success.
Full capture: docs/hardware/app-exit-v92.log.

MINIX src/mm/exec.c installs arguments and environment in the new process
stack. CP32 now supplies HOME=/, PATH=/boot and USER=root through its existing
bounded stack builder. Xtensa crt0 already computes envp after argv's NULL
and passes it as the third call0 C argument. Strings and vector entries are
copied into application SRAM on every launch, rather than exposing pointers
to the kernel's environment. The 512-byte startup budget and 16-byte stack
alignment are preserved; remaining stack space is unchanged.

hello --env prints the received environment. This is a fixed bootstrap policy,
not mutable shell state, permission enforcement, PATH command search or full
execve inheritance. Applications remain trusted and share address space.
Tests cover loader environment selection, vector pointers/terminators, all
nine permitted arguments, fresh copies after mutation, populated/empty envp,
and existing exit/relaunch behavior.

Hardware pending: [FEATURE APP-ENV 93]1 / [TEST APP-ENV 93]. Try hello --env,
run /boot/hello --env, hello one two, hello --exit 7, hello --env, disk and mm.
Expected environment: HOME=/, PATH=/boot, USER=root, followed by exit=0.
Flashing remains with the user.

Validation: clean build and all 31 host scripts pass without compiler warnings.
ELF section/segment checks pass: _iram_end=4037422c, _iram_ext_end=40386830,
_runtime_stack_end=3fce41e0, below the application alias at 3fce8000.


## SHELL-EXEC 94 — direct external commands (2026-09-29)

Image 93 hardware confirms hello --env and run /boot/hello --env receive
HOME=/, PATH=/boot, USER=root, including relaunch after exit 7. Arguments and
disk/MM checks pass. Full capture: docs/hardware/app-env-v93.log.

MINIX reference: commands/ash/exec.c shellexec searches PATH for names without
slashes and executes slash-containing names directly. CP32 now dispatches
unmatched shell commands through its proven bounded foreground loader. Builtins
retain priority; the fixed bootstrap PATH has one directory, /boot. Explicit
run still resolves its argument against cwd. No environment mutation, general
PATH list parsing, quoting, pipes or separate user shell is implied.

The same ELF validation, execute-mode check, argument limits, blocking wait,
exit and reap path are reused. No architecture/frame/assembly change: this is
shell-side selection of the existing trusted call0 application path. Tests
cover bare-name lookup from another cwd, relative/absolute slash paths,
missing/nonexecutable errors and descriptor closure, alongside prior regressions.

Hardware pending: [FEATURE SHELL-EXEC 94]1 / [TEST SHELL-EXEC 94].
Try echo one two, /boot/hello --env, cd boot, ./echo again, cd /, nosuch,
echo recovered, hello --exit 7, disk and mm. No flashing performed.

Validation: clean build, all 31 host scripts and ELF section/segment checks
pass, with no compiler warnings. _iram_end=4037422c, _iram_ext_end=4038687c,
_runtime_stack_end=3fce4240 remain within their reserved regions.


## SHELL-ARGV 95 — quoted application arguments (2026-09-29)

Image 94 hardware passed echo, /boot/hello --env, ./echo after cd, unknown
command recovery, exit 7 and disk/MM checks. Capture: docs/hardware/shell-exec-v94.log.

MINIX reference: commands/ash/parser.c quote handling preserves quoted word
boundaries and removes quoting syntax. CP32 implements bounded in-place word
decoding for application launch only: single/double quotes, empty words,
adjacent quoted/unquoted fragments and backslash escapes. In double quotes,
backslash is removed before quote, backslash, dollar or backtick; otherwise it
is literal. No expansions or command operators are added. Other builtins keep
their existing parsers. Input remains 63 characters and at most eight app args.

Invariant: decoding never grows its buffer, rejects syntax/argument overflow
before opening a file, and passes ordinary strings through the existing SRAM
stack-copy and call0 startup path. No kernel frame or hardware changes.
Host tests cover spaces, empty strings, concatenation, escapes, malformed input,
argument exhaustion and rejection without a file open or process launch.

Hardware pending: [FEATURE SHELL-ARGV 95]1 / [TEST SHELL-ARGV 95]. Try
hello "one two" "", echo one\ two, hello 'unfinished, echo recovered,
hello --env, disk and mm. Flashing remains with the user.

Validation: clean build, all 31 host scripts and ELF section/segment checks
pass. _iram_end=4037422c, _iram_ext_end=403869e0,
_runtime_stack_end=3fce43c0 remain within reserved regions.


## LCD-QUOTES 96 — quote and backslash glyphs (2026-09-29)

Image 95 serial output confirms quoted/empty arguments, escaped space,
unclosed-quote rejection, recovery, environment and disk/MM checks. User
reports double quote, apostrophe and backslash rendered incorrectly on LCD.
Capture: docs/hardware/shell-argv-v95.log.

Cause: glyph_scaled had no explicit glyphs for these characters and used
its placeholder pattern fallback. Add top-aligned single/double quote strokes
and a backslash diagonal opposite the existing slash. Extend font samples.
Pixel-model tests check all pixels, spacing, row gap and cursor advancement.

This is Cardputer-specific rendering, not a change to MINIX character/TTY
semantics. Preserve byte values, keyboard mapping, shell parsing, 15x11 cell
layout and existing SPI transport; no new hardware behavior is assumed.

Hardware pending: [FEATURE LCD-QUOTES 96]1 / [TEST LCD-QUOTES 96]. Run font
and visually inspect quotes and backslash, then hello "one two" "" and
echo one\ two. Build/pixel evidence does not establish physical LCD success.
Flashing remains with the user.

Validation: clean build, all 31 host scripts including glyph pixel tests,
and ELF section/segment checks pass without compiler warnings.
_iram_end=40374298, _iram_ext_end=403869f8, _runtime_stack_end=3fce43f0.


## APP-PID 97 — process identity and short executable reads (2026-09-29)

User confirms image 96 font looks correct on LCD. Serial capture confirms
quoted/empty arguments, escapes and recovery after malformed input:
docs/hardware/lcd-quotes-v96.log.

MINIX mm/forkexit.c allocates PIDs separately from process slots, wraps at
30000 and avoids live identities. CP32 now allocates 100..30000, skips live
process PIDs, and publishes the chosen identity with the new frame under the
scheduler lock. The reserved range avoids bootstrap process identities.
There are no process groups yet; this remains one trusted foreground slot,
not general fork, parent bookkeeping or waitpid.

Application ABI v2 appends getpid after the unchanged write/exit table offsets.
The callback validates the active child and obtains its PID through SENDREC;
the parent responds from the live child metadata. hello --pid prints it.
Bundled hello/echo accept v1/v2; old binaries that require exactly version 1
must be rebuilt for the v2 service table. crt0 offsets and call0 ABI unchanged.

The larger hello exposed short reads at filesystem block boundaries. Fix the
shell's executable reader to accumulate positive reads, rejecting premature
EOF/errors. The filesystem-to-ELF integration test now extracts this actual
production reader rather than maintaining a duplicate. ELF bounds and failed
load cleanup are preserved.

Host tests cover collision avoidance, repeated fresh identities, wrap at
30000, getpid responses in the production launch loop, hello PID formatting,
and actual multi-block ELF loading. Hardware pending: [FEATURE APP-PID 97]1 /
[TEST APP-PID 97]. Try hello --pid twice (100,101 after a fresh boot),
echo ok, hello --pid (103), hello --env, hello --exit 7, disk and mm.
Other successful application launches also consume PIDs. No flashing performed.

Validation: clean build, all 31 host test scripts and ELF section/segment
checks pass without compiler warnings. _iram_end=40374298,
_iram_ext_end=40386b64, _runtime_stack_end=3fce47d0 remain in reserved regions.


## APP-PARENT 98 — bootstrap parent identity (2026-09-29)

Image 97 hardware confirms PID 100,101, echo using 102, then PID 103,
exit 7 and disk/MM success. Capture: docs/hardware/app-pid-v97.log.

MINIX mm/getset.c GETPID returns process PID plus the parent's PID from its
MM parent relationship. CP32's shell is still the kernel-linked FS process;
assign it bootstrap PID 1, leaving IPC endpoint numbers unchanged. Capture
its positive PID in the foreground launch activation before loading. That
activation survives the wait and is discarded on return/reap, avoiding stale
parent identity on the reused child slot. This is not an MM parent table,
init process, reparenting, orphan handling or general waitpid.

ABI v3 appends getppid after getpid, preserving prior field offsets and crt0.
The callback checks active child ownership and uses SENDREC; its response is
the captured parent PID. hello --ppid displays the value. Bundled applications
accept v1..v3; previously compiled versions with strict version checks require
rebuilding. No process structure/assembly/register-frame layout changes.

Host checks cover getppid replies across repeated child slots, application
formatting, legacy PID/exit services and filesystem loading. Hardware pending:
[FEATURE APP-PARENT 98]1 / [TEST APP-PARENT 98]. After boot run hello --pid,
hello --ppid, echo ok, hello --ppid, hello --pid, disk and mm. Expected first
PID 100, PPID always 1 and final PID 104. No flashing performed.

Validation: clean build and all 31 host scripts pass without compiler
warnings. ELF section/segment checks pass: _iram_end=403742ac,
_iram_ext_end=40386be4, _runtime_stack_end=3fce49a0.


## APP-HEAP 99 — bounded per-application sbrk (2026-09-29)

Image 98 hardware confirms PPID=1, successful application execution and disk/MM
checks. PIDs 101 and 105 are correct: initial hello --pip consumed PID 100.
Capture: docs/hardware/app-parent-v98.log.

MINIX mm/break.c validates data growth against stack space. CP32 uses its
fixed 12 KiB data region: heap begins at the 16-byte-aligned end of ELF data/BSS
and may grow up to CP32_APP_STACK, never into the separate 4 KiB stack. ABI v4
appends sbrk(int); returns the old break or (void *)-1, with sbrk(0) querying it.
The callback checks active-child ownership and requests changes through IPC.
Each launch gets a fresh break; failed changes leave it unchanged. Positive
growth is zeroed, including after shrinking. Signed-minimum negative increments
and oversized positive increments are handled without overflow.

No changes to flat memory protection or proc/frame layouts: applications remain
trusted and the entire reserved data window is mapped. This is not malloc/free,
general MM brk syscall dispatch, dynamic address-space allocation or fork.
Bundled apps accept ABI 1..4; older strict-version binaries need rebuilding.

hello --heap checks growth, zeroes, writes, oversized request rejection,
shrink, below-floor rejection and zeroed regrowth. Host tests exercise break
boundaries, INT_MIN, unchanged-on-failure, zeroing extent, repeated launch
reset and application behavior. Hardware pending: [FEATURE APP-HEAP 99]1 /
[TEST APP-HEAP 99]. Try hello --heap twice, hello --pid, hello --ppid,
hello --env, echo ok, disk and mm. Expect Heap grow/shrink OK and exit=0.
No flashing performed.

Validation: clean build and all 31 host test scripts pass without compiler
warnings. ELF section/segment checks pass: _iram_end=403742ac,
_iram_ext_end=40386d38, _runtime_stack_end=3fce4e90, below app alias 3fce8000.


## APP-MALLOC 100 — application allocation library (2026-09-29)

Image 99 hardware confirms two Heap grow/shrink OK results, PID/PPID,
environment, echo and disk/MM checks. Capture: docs/hardware/app-heap-v99.log.

MINIX lib/ansi/malloc.c grows through sbrk, tracks slots and merges free slots.
CP32's small bootstrap adaptation lives in apps/lib/heap.c: initialize once
with cp32_heap_init(services), then cp32_malloc(unsigned)/cp32_free(void *).
It uses first-fit, 16-byte payload alignment, split blocks and adjacent free
block coalescing. Metadata and payload live in application SRAM and reset on
image reload. Zero size, overflow and exhaustion return NULL; free(NULL) is
harmless. Reused bytes are unspecified. No kernel ABI bump or process/frame
change; the library uses the hardware-tested ABI-v4 sbrk callback.

The allocator requires exclusive ownership of sbrk after initialization and
is single-threaded. Freed blocks remain reusable rather than shrinking the
break; process-slot reload reclaims the whole region. No calloc/realloc,
errno, standard libc symbol replacement or protected heap is claimed.

hello --malloc exercises allocation, writes, preservation of a neighboring
live block, free/reuse, merge and oversized request rejection. Host tests
also cover splitting, alignment, exhaustion, interleaved frees and large
allocation from merged space without further growth, under UBSan.

Hardware pending: [FEATURE APP-MALLOC 100]1 / [TEST APP-MALLOC 100]. Run
hello --malloc twice, hello --heap, hello --pid, hello --ppid, echo ok,
disk and mm. Expect Malloc/free OK and exit=0. No flashing performed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386d38,
_runtime_stack_end=3fce57c0 remain below the reserved application alias.


## APP-REALLOC 101 — calloc/realloc (2026-09-30)

Image 100 hardware confirms repeated Malloc/free OK, heap, identity, echo
and disk/MM checks. Capture: docs/hardware/app-malloc-v100.log.

MINIX lib/ansi/malloc.c realloc preserves contents, allocates for NULL and
frees for zero size; calloc.c clears allocated bytes. Add cp32_calloc and
cp32_realloc to the application library. Multiplication overflow is rejected;
calloc clears reused memory. Realloc keeps capacity when shrinking and uses
allocate/copy/free when growing. Failure leaves the original allocation and
contents intact. No in-place growth optimization or errno yet. ABI v4, SRAM
bounds and kernel interfaces stay unchanged.

hello --alloc exercises zeroing, growth, prefix preservation, oversized
failure, shrink, zero-size free and reused zeroed memory. Host allocator tests
cover multiplication overflow, exhaustion preservation, adjacent live data,
NULL allocation and zero-size behavior. Applications now compile with -Os:
unoptimized hello exceeded the existing 7168-byte RAM fixture capacity. Kernel
remains -O0; both generated optimized ELF files are validated/loaded by tests.

Hardware pending: [FEATURE APP-REALLOC 101]1 / [TEST APP-REALLOC 101]. Try
hello --alloc twice, hello --malloc, hello --heap, hello --pid, hello --ppid,
echo ok, disk and mm. Expect Calloc/realloc OK and exit=0. Flashing remains
with the user; optimized application execution needs hardware confirmation.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. hello.elf=4400 bytes, echo.elf=1012 bytes.
_iram_end=403742ac, _iram_ext_end=40386d38, _runtime_stack_end=3fce5240.


## APP-RESIZE 102 — allocator resizing in place (2026-09-30)

Image 101 hardware confirms repeated calloc/realloc checks, malloc/free,
sbrk, identity, echo and disk/MM checks with size-optimized applications.
Capture: docs/hardware/app-realloc-v101.log.

MINIX lib/ansi/malloc.c realloc consumes adjacent free space and splits excess
space into a free tail. Add these behaviors to cp32_realloc: retain address on
growth when the next free block satisfies the full request, and split useful
space on shrink. Freed tails coalesce through the existing free implementation.
If the neighbor is insufficient, leave it intact while trying the existing
allocate/copy/free fallback, preserving the original allocation on failure.
No kernel ABI, frame, mapping or heap-boundary change.

Host tests check original contents, live neighbor data, unchanged heap break
and tail reuse. hello --resize performs the same visible application check.
Hardware pending: [FEATURE APP-RESIZE 102]1 / [TEST APP-RESIZE 102]. Try
hello --resize twice, hello --alloc, hello --malloc, hello --heap, echo ok,
disk and mm. Expect Resize in place OK and exit=0. Not flashed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386d38,
_runtime_stack_end=3fce5520 remain within reserved regions.


## APP-TRIM 103 — return free heap tail (2026-09-30)

Image 102 hardware confirms two Resize in place OK results, calloc/realloc,
heap, echo and disk/MM checks. hello -malloc was an ordinary argument (single
dash), not a malloc test. Capture: docs/hardware/app-resize-v102.log.

MINIX malloc maintains a top-of-heap boundary via brk; CP32 adds an explicit
cp32_heap_trim extension over the existing bounded sbrk service. It returns
a free last block, including its header, verifies the current break and
unlinks metadata only after shrink succeeds. Live last blocks are untouched;
failed shrink retains the list and contents. Return value is bytes released,
zero if nothing can be released, or -1 on failure. free still retains blocks
until trim is requested. The fixed address reservation is not released to
other processes; only this application's logical break shrinks.

No kernel ABI, map or frame changes. Host tests cover failed shrink, full
trim, live-prefix preservation and allocation after trim. hello --trim checks
the same behavior via real IPC/sbrk on hardware.

Hardware pending: [FEATURE APP-TRIM 103]1 / [TEST APP-TRIM 103]. Run hello
--trim twice, hello --resize, hello --alloc, hello --malloc, echo ok, disk and
mm. Expect Heap trim OK and exit=0. Not flashed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386d38,
_runtime_stack_end=3fce57f0 remain within reserved regions.


## APP-TRIM 103 hardware confirmation (2026-09-30)

User capture docs/hardware/app-trim-v103.log confirms two Heap trim OK runs,
Resize in place OK, Calloc/realloc OK, Malloc/free OK and echo, all exit=0.
Disk and MM report result=0; CORE checks pass and IRQ count 5000 reports
unknown=0. This validates the planned image-103 hardware sequence. It does
not establish general fork/wait, fault isolation or per-process FS support.
Documentation-only update; firmware and image marker unchanged. No flashing.


## APP-READ 104 — foreground application console input (2026-09-30)

MINIX fs/device.c handles DEV_READ with immediate replies or SUSPEND/REVIVE.
CP32 now bridges an application read callback through the foreground parent
and the existing TTY protocol. ABI v5 appends read(char *,unsigned), max 64
bytes; the parent validates the full application buffer before servicing it.
Zero-length reads return zero without touching TTY. Input uses FS stack
staging because the TTY request carries the FS endpoint/map, then copies to
the blocked child's buffer. Flush pending application output before waiting.
No process layout or assembly changes. This is canonical console input only,
not file descriptors, arbitrary file reads or a general FS server.

hello --read prints a prompt, reads up to 64 bytes and writes the result.
Bundled apps accept ABI versions 1..5; strict older binaries need rebuilding.
Host checks cover count/range rejection, valid read replies, FS staging,
short read copying, error propagation, prompt flush ordering and application
output. Existing TTY tests cover suspend/revive and canonical editing.

Hardware pending: [FEATURE APP-READ 104]1 / [TEST APP-READ 104]. Run hello
--read, type test then Enter; expect Read: test and exit=0. Repeat with an
empty line and a line edited using Backspace. Then hello --alloc, echo ok,
disk and mm. No flashing performed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce5a60 remain within reserved regions.


## APP-STDIO 105 — standard-stream wrappers (2026-09-30)

Image 104 confirms normal and empty console reads and return to the shell;
user explicitly confirms Backspace editing. Allocator, echo and disk/MM checks
pass. Capture: docs/hardware/app-read-v104.log.

MINIX lib/posix/_read.c and _write.c provide descriptor-based byte-count APIs.
CP32's bootstrap io library now exposes cp32_io_init, cp32_read and cp32_write
for standard stream numbers only: 0 reads TTY, 1/2 write the same TTY.
Read returns at most 64 bytes, preserving canonical short reads. Write splits
requests into callback-sized chunks, collects short writes, stops on zero
progress, and returns transferred bytes if a later callback fails. Invalid
streams, null nonempty buffers, oversized requests or invalid callback counts
are rejected. Zero-length valid requests are no-ops. ABI remains v5.

This is not general FS descriptors, errno, FILE buffering, printf or a full
POSIX syscall implementation. hello --io prompts, reads through fd 0 and
writes through 1/2, then verifies wrong-direction stream rejection. Host tests
cover 600-byte output, partial/error/zero transfers, EOF and count bounds.

Hardware pending: [FEATURE APP-STDIO 105]1 / [TEST APP-STDIO 105]. Run
hello --io, type a line then Enter; expect Read: followed by that line and
exit=0. Repeat with an empty line and Backspace editing. Then hello --alloc,
echo ok, disk and mm. No flashing performed.

Validation: clean build, all 33 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce5e30 remain within reserved regions.


## APP-LINE 106 — bounded line input (2026-09-30)

Image 105 hardware confirms --io input, empty lines, allocator and device
regressions; user confirms Backspace editing. Capture: docs/hardware/app-stdio-v105.log.
README now summarizes the implemented application runtime and validated status,
while retaining build, debug, flash, standalone startup and shell instructions.

MINIX lib/stdio/fgets.c bounds input, retains newline and NUL-terminates.
CP32 adds cp32_readline over standard input, returning fragment length, zero
at EOF or -1 on error; partial error output remains terminated. Capacity must
be at least two. Byte-at-a-time reads avoid consuming the following line;
excess characters remain in the existing TTY queue. No FILE buffering or full
fgets ABI is claimed. Existing ABI v5 and kernel services remain unchanged.

hello --line consumes fragments through a 16-byte buffer until newline/EOF.
Host tests cover long-line fragmentation, subsequent lines, empty lines, EOF,
partial error, tiny buffers and the application loop. Hardware pending:
[FEATURE APP-LINE 106]1 / [TEST APP-LINE 106]. Run hello --line, enter a line
longer than 15 characters, then repeat with empty input and Backspace editing.
Then echo ok, hello --alloc, disk and mm. No flashing performed.

Validation: clean build, all 33 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce6030 remain within reserved regions.


## APP-CAT 107 — standalone standard-input utility (2026-09-30)

Image 106 confirms line-reading and allocator, echo, disk/MM, IPC and SYS
regressions. Capture: docs/hardware/app-line-v106.log.

MINIX commands/simple/cat.c copies standard input when no filenames are given.
Add independently linked apps/cat/cat.c using the shared cp32_read/cp32_write
library. Copy until EOF, fail on I/O errors, reject unsupported file arguments
with usage/exit=2. The kernel's cat filename builtin remains unchanged.
This is the stdin subset, not the complete MINIX utility or file API.

Package inode 9 at zones 44..50, preserving the other application extents and
reserved disk diagnostic sector. Reuse the fixed ELF slot, call0 crt0 and ABI
v5. make cat builds it independently. Host tests cover multi-read input,
short writes, EOF, read/write failures and unsupported arguments. The real
filesystem-to-ELF test now loads all three programs sequentially.

Hardware pending: [FEATURE APP-CAT 107]1 / [TEST APP-CAT 107]. Run ls boot,
run /boot/cat, enter two lines, then Ctrl-D on an empty line. Expect copied
lines and Application exit=0. Repeat for slot reuse, then hello --alloc,
echo ok, disk and mm. EOF after typed text may require a second Ctrl-D.
No flashing performed.

Validation: clean build, all 34 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce67c0 remain within reserved regions.


## APP-REDIRECT 108 — application stdin from files (2026-10-01)

Image 107 confirms repeated standalone cat input/EOF, allocator and device
regressions. Capture: docs/hardware/app-cat-v107.log.

MINIX commands/ash/redir.c NFROM opens input read-only and restores/closes
redirected descriptors. CP32 adds one trailing unquoted < path to application
launch only. The scanner respects quote/escape state; the filename is decoded
with the bounded word parser. Paths resolve against shell cwd. Missing,
multiple or extra operands are rejected before launch. Quoted '<' is literal.

The parent opens the file after validating the executable, services app reads
through the existing FS staging buffer, and closes/resets its input descriptor
on normal exit or load failure. Subsequent launches again use TTY. The shell
itself does not lose its keyboard input. No ABI/frame/map changes. This is
single foreground redirection, not per-process FS tables, output redirection,
pipes or application open/close. Other builtins retain existing parsers.

Host tests cover parser quoting/errors, redirected read routing, successful
launch and failed-load cleanup, plus existing TTY and filesystem tests.
Hardware pending: [FEATURE APP-REDIRECT 108]1 / [TEST APP-REDIRECT 108]. Try
run /boot/cat < readme, run /boot/cat < boot/readme, run /boot/cat < nosuch,
then run /boot/cat (type a line and Ctrl-D), hello --alloc, disk and mm.
Not flashed.

Validation: clean build, all 34 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40387074,
_runtime_stack_end=3fce69f0 remain within reserved regions.


## LCD-LESS 109 — input-redirection glyph (2026-10-01)

Image 108 confirms redirected file reads, missing-input rejection, subsequent
keyboard input/EOF, allocator and device checks. Capture:
docs/hardware/app-redirect-v108.log. User reports missing LCD < glyph.

The scaled renderer fell back to its placeholder pattern for <. Add an
explicit left-pointing chevron and a font sample. Pixel-model tests check
the complete cell, margins, row gap and cursor movement. This is a Cardputer
font change, not MINIX TTY byte handling: preserve ASCII, keyboard mapping,
redirection parsing, cell size and SPI behavior.

Hardware pending: [FEATURE LCD-LESS 109]1 / [TEST LCD-LESS 109]. Run font
and inspect <, then run /boot/cat < readme. No flashing performed.

Validation: clean build, all 34 host scripts (including the glyph pixel test)
and ELF section/segment checks pass without compiler warnings.
_iram_end=403742d0, _iram_ext_end=40387080, _runtime_stack_end=3fce6a00.


## FONT-COMPACT 110 — compact font sample (2026-10-01)

Image 109 user log confirms font output and successful redirected cat. User
reports the labeled multi-line sample scrolls before every glyph can be seen.
Replace font's labels and individual lines with one space-separated sequence,
retaining punctuation, repeated dots and uppercase/lowercase samples. The LCD
wraps it naturally across a few rows. No glyph, TTY, parser or hardware change;
this is CP32 diagnostic presentation rather than a MINIX behavior change.

Hardware pending: [FEATURE FONT-COMPACT 110]1 / [TEST FONT-COMPACT 110].
Run font and confirm the entire sample remains visible. Not flashed.

Validation: clean build, all 34 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742d0, _iram_ext_end=40387008,
_runtime_stack_end=3fce6920.


## LCD-SYMBOLS 111 — additional punctuation (2026-10-01)

User confirms image 110 compact font display looks good. Add explicit glyphs
for > { } ( ) | ?, and improve existing square brackets with clearer caps.
Keep all samples space-separated in font. Pixel tests independently specify
each requested shape and verify its complete cell, margins and row gap.
This changes Cardputer glyph presentation only: ASCII values, TTY behavior,
cell geometry and SPI transport remain unchanged; no MINIX protocol change.

Hardware pending: [FEATURE LCD-SYMBOLS 111]1 / [TEST LCD-SYMBOLS 111]. Run font
and inspect > [ ] { } ( ) | ?. No flashing performed.

Validation: clean build, all 34 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403743d4, _iram_ext_end=40387008,
_runtime_stack_end=3fce6930.


## APP-WC 112 — standalone word count (2026-10-07)

User confirms image 111 looks good. Retain expanded font samples.
MINIX commands/simple/wc.c counts newline bytes, whitespace-delimited words
and bytes with -l/-w/-c selection. Add a standalone /boot/wc implementing the
stdin subset with combined/separate options and fixed lines/words/bytes output
order. Reuse shared I/O callbacks and shell-owned input redirection. Preserve
word state across reads, count the final non-newline word, reject unsupported
file operands and fail on I/O/counter overflow. No new kernel ABI or layout.

Package inode 10 in zones 51..57; keep existing fixtures and diagnostic block
63 intact. make wc builds its own ELF. Tests cover byte-at-a-time input,
short output, empty/whitespace-only input, option selection and error paths.
Real filesystem-to-ELF tests load all four application images sequentially.

Hardware pending: [FEATURE APP-WC 112]1 / [TEST APP-WC 112]. Run
run /boot/wc < readme, run /boot/wc -l < readme, run /boot/wc -c < readme,
then run /boot/wc interactively (type one two, Enter, then Ctrl-D on an empty
line; expect 1 2 8). Existing builtin wc remains available for comparison.
Then hello --alloc, disk and mm. No flashing performed.

Validation: clean build, all 35 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403743d4, _iram_ext_end=40387008,
_runtime_stack_end=3fce7360 stays below app alias 3fce8000 (3232-byte margin).


## APP-ERRNO 113 — application I/O errors (2026-10-07)

Image 112 hardware confirms readme counts 2 8 61, -l=2, -c=61 and interactive
1 2 8. Capture: docs/hardware/app-wc-v112.log.

MINIX include/errno.h defines positive userspace EIO=5, EBADF=9, EFAULT=14,
EINVAL=22. Add application-local cp32_errno with scoped CP32_ constants to
avoid kernel sign conventions. Standard-stream wrappers distinguish invalid
stream direction, null nonempty buffer, initialization/count errors and
callback failure. Success/EOF leaves errno unchanged; partial-write failure
returns progress and records EIO. Opaque bootstrap callback errors map to EIO
rather than inventing underlying FS error identities. Heap errno and full libc
errno/syscall translation remain unfinished. Kernel ABI stays v5.

hello --errno checks the new behavior. Consolidate repeated hello option
matching without removing modes. The added code exceeded the fixed 7168-byte
fixture, so independently linked applications use -mno-longcalls alongside
-Os: all direct callees are within their validated 16 KiB text region. The
linker resolves local call0 relocations; service callbacks remain callx0.
Kernel compilation remains unchanged. This compiler change needs hardware
regression. Final hello ELF is 7156 bytes, only 12 bytes below its fixture
limit; further growth needs a deliberate packaging/runtime-size solution.

Hardware pending: [FEATURE APP-ERRNO 113]1 / [TEST APP-ERRNO 113]. Run hello
--errno twice, hello --alloc, hello --io (enter a line), run /boot/wc < readme,
echo ok, disk and mm. Expect I/O errno OK and exit=0. Not flashed.

Validation: clean build, all 35 host scripts, ELF section/segment checks and
application call disassembly pass without compiler warnings.
_iram_end=403743d4, _iram_ext_end=40387008, _runtime_stack_end=3fce7540.


## APP-ZONES 114 — variable application file allocation (2026-10-07)

Hardware image 113 confirms repeated I/O errno checks, calloc/realloc, interactive
I/O, echo, disk and MM. Capture: docs/hardware/app-errno-v113.log. The wc command
in that capture was consumed by hello --io, so it is not a new wc regression.

MINIX V2 fs/const.h and read.c use seven direct zones followed by a table of
32-bit zone numbers. The image generator now uses this format for applications
and packs actual file sizes into shared zones 30..62, instead of reserving seven
blocks per executable. It accounts for indirect tables, updates allocation
bitmaps and rejects aggregate exhaustion. Block 63 stays reserved for the disk
diagnostic. ESP32 application addresses, call0 ABI, fixed executable slots and
read-only runtime filesystem are unchanged. This removes a packaging limit,
not the SRAM limit: embedded bytes and sparse-run records still consume SRAM.

Validation: clean build and all 35 host scripts pass. Tests cover the 7169-byte
boundary, exact zone capacity, aggregate overflow, bitmaps, following-file
placement and a padded valid ELF read through production FS and image loader.
ELF size/sections/segments pass; _iram_end=403743d4,
_iram_ext_end=40387008, _runtime_stack_end=3fce7550 (2736 bytes margin).

Hardware pending: [FEATURE APP-ZONES 114]1 / [TEST APP-ZONES 114]. Run hello
--errno, hello --alloc, echo ok, run /boot/wc < readme, run /boot/cat < readme,
disk and mm. Large-file loading is host-tested; current production hello still
fits direct zones. No flashing performed.


## APP-STREAM 115 — buffered application input (2026-10-07)

Image 114 hardware confirms errno/allocator checks, echo, redirected wc
(2 8 61), cat, disk and MM; capture: docs/hardware/app-zones-v114.log.

Add apps/lib/stream.c and stream.h: caller-owned 64-byte stdin buffering,
cp32_getc, cp32_fgets, cp32_feof, cp32_ferror and cp32_clearerr. MINIX 2
lib/stdio/fgetc.c and fgets.c establish unsigned-character results, retained
newline, bounded NUL termination, EOF after partial text and error distinction.
CP32 uses existing ABI-v5 read callbacks over IPC instead of a full FILE/syscall
runtime. EOF/error remain sticky until clearerr; clearing flags preserves unread
buffered bytes. Capacity below two is rejected. A valid stream pointer is
required. Do not mix buffered and raw reads from the same input source.

The standalone wc now uses buffered getc in its normal counting path. No kernel
ABI or process layout changes. Full stdio, file opening and output buffering
remain unfinished. Tests cover high-bit/NUL bytes, refill boundaries, line
fragments, final lines without newline, sticky EOF/error, recovery and partial
line failure; existing wc checks cover counting and read/write errors.

Validation: clean build and all 36 host scripts pass, including UBSan stream
checks; ELF sections/segments pass. _iram_end=403743d4,
_iram_ext_end=40387008, _runtime_stack_end=3fce7870. Only 1936 bytes remain
before the application text's DRAM alias; further embedded growth needs care.
Hardware pending: [FEATURE APP-STREAM 115]1 / [TEST APP-STREAM 115]. Test
run /boot/wc < readme (2 8 61), run /boot/wc -l < readme (2),
run /boot/wc -c < readme (61), then hello --errno, disk and mm.
Line helper behavior is host-tested; wc exercises character buffering on hardware.
Not flashed.


## APP-FLUSH 116 — buffered application output (2026-10-08)

Image 115 hardware confirms wc default/line/byte counts (2 8 61 / 2 / 61),
hello --errno, disk and MM. The capture also shows sustained timer activity
through a long idle interval; this is not concurrent-application stress proof.
Capture: docs/hardware/app-stream-v115.log.

MINIX 2 lib/stdio/fflush.c reports completion or a stream error when buffered
output cannot be written. CP32 adds apps/lib/output.c and output.h with a
caller-owned 64-byte stdout/stderr buffer, cp32_putc, cp32_flush, error inspection
and clearerr. It uses the existing ABI-v5 bounded IPC write wrapper, preserving
bare-metal call0 and current process ownership. A partial write removes only
the delivered prefix, retaining unsent bytes for retry after clearerr. A full
buffer is flushed before accepting the next character; failure leaves that
character unconsumed. Zero progress is an I/O error. Flush is explicit before
exit or blocking input; automatic exit cleanup, full FILE and general file
descriptors remain unimplemented. Do not interleave raw and buffered writes.

The standalone wc now buffers its result and flushes before returning; its
usage errors still use immediate stderr. Tests cover high-bit/NUL characters,
full buffers, empty flush, repeated short writes, partial failure and retry,
sticky errors, zero-progress writes, invalid streams and failure before
accepting the next character. Existing wc tests exercise normal counts and
input/output failure through the production application.

Validation: clean build, all 37 host scripts including UBSan checks, ELF
sections/segments and image layout pass without compiler warnings.
_iram_end=403743d4, _iram_ext_end=40387008, _runtime_stack_end=3fce7b60.
Only 1184 bytes remain before the application code DRAM alias. Embedded
application growth will need a memory-placement or packaging improvement soon.
Hardware pending: [FEATURE APP-FLUSH 116]1 / [TEST APP-FLUSH 116]. Run
run /boot/wc < readme, run /boot/wc -l < readme, run /boot/wc -c < readme,
hello --errno, disk and mm. Expected counts remain 2 8 61 / 2 / 61.
Write-failure injection remains host-tested. Not flashed.


## APP-FILES 117 — application file access and utility operands (2026-10-08)

Image 116 hardware passes default/selected wc counts, errno, disk and MM.
Capture: docs/hardware/app-flush-v116.log.

This feature spans the application ABI, IPC, foreground FS ownership, libc
wrappers and utilities. MINIX 2 fs/open.c do_open/do_close/do_lseek and
filedes.c establish per-client descriptors, independent open offsets, seek
validation and closing references. CP32 retains its single trusted fixed
application slot and adapts these rules to four owned handles (3..6), backed
by the existing read-only MINIX filesystem. Shell executable/redirection
handles are never exposed to the child. The parent handles requests while
waiting for that child and closes leftovers before launch and on normal exit.
This does not add concurrent FS clients, fork inheritance or fault cleanup.

ABI v6 appends one typed file request callback; earlier fields keep their
layout. Application callbacks validate process identity and use SENDREC to FS;
the parent checks operation, transfer/path length and complete application
memory span before dereferencing. Open names include a terminator and are
bounded to 255 bytes. Relative paths resolve from the waiting shell cwd.
Directories are rejected; a regular file needs read bits. This is the trusted
bootstrap permission policy, not full credential-based POSIX authorization.
File reads are staged through FS memory and capped at 64 bytes. Seek supports
signed 32-bit offsets and SET/CUR/END. Helpers translate private FS statuses
into positive MINIX errno in the application, preserving specific missing,
directory, descriptor, permission, exhaustion and argument errors.

Public wrappers: cp32_open(path), cp32_read(fd,buffer,count), cp32_lseek(fd,
offset,whence), cp32_close(fd). Buffered input can select a file descriptor;
reinitialize only when intentionally discarding unread buffered data.
Standalone cat copies sequential file operands or '-' stdin. Standalone wc
accepts -lwc, '--', filenames and '-' stdin, prints per-file names/counts and
aggregate totals, and continues after missing-file errors with exit=1.
The kernel builtins remain; use run /boot/cat and run /boot/wc to exercise apps.
hello --files checks independent offsets, seek/EOF, exhaustion, closed-handle
errors, missing/directory paths, and intentionally leaves three handles for
exit cleanup. Repeat it on hardware to prove subsequent launch reuse.

Memory: the first full implementation exceeded the fixed application boundary.
Coalescing embedded nonzero runs across zero gaps of at most four bytes avoids
redundant metadata; reconstruction remains byte-identical. Application linking
now uses --gc-sections so unreferenced library routines are omitted. No heap,
stack or application-slot reservation is reduced. Final _iram_end=403743d4,
_iram_ext_end=40387424, _runtime_stack_end=3fce6360, leaving 7328 bytes before
the application text data alias. Final hello is 5136 bytes; larger ELF loading
remains covered by the padded indirect-file integration test.

Validation: clean build, all 38 test scripts, ELF sections/segments and image
layout pass. Tests compile production FS and app wrappers/utilities together,
cover handle isolation/exhaustion/reuse, 40 leak/cleanup cycles, errno, cwd,
seek, single/double-indirect reads, request bounds, actual hello --files, cat/wc
multiple operands and missing-file recovery. Launch/reap tests check cleanup
on each of 20 launches. Sparse reconstruction and all four executable loading
checks pass. Hardware pending: [FEATURE APP-FILES 117]1 / [TEST APP-FILES 117].
Not flashed.

Hardware exercise:
hello --files (twice; File API OK and exit=0)
run /boot/cat readme boot/readme (file contents twice)
run /boot/wc readme boot/readme (2 8 61 per file, 4 16 122 total)
run /boot/wc -c readme boot/readme (61 per file, 122 total)
run /boot/cat missing readme (error, valid file still printed, exit=1)
cd boot; then run /boot/wc -c readme (61 readme), then cd /
run /boot/wc < readme (2 8 61), hello --errno, disk, mm
Enter each command separately; shell command chaining is not implemented.


## APP-DIRS 118 — application directory/metadata APIs and ls (2026-10-08)

Hardware image 117 confirms repeated hello --files, file operands and totals
in cat/wc, missing-file recovery, redirected stdin, disk and MM.
Capture: docs/hardware/app-files-v117.log.

MINIX 2 lib/posix/_fstat.c and fs/stadir.c obtain metadata from an open inode;
lib/posix/_readdir.c converts directory entries and skips deleted slots. CP32
preserves those portable semantics using its validated V2 directory decoder
and fixed ABI records, rather than exposing an in-kernel pointer or historical
host-sized struct stat. ABI v7 extends file operations without changing table
layout. cp32_opendir returns one of four owned fd handles, cp32_readdir returns
1/0/-1 for entry/EOF/error, cp32_fstat reads the opened inode, and cp32_stat
resolves a pathname without consuming a descriptor. cp32_close releases either
kind, and cp32_lseek(fd,0,0) rewinds directory iteration. Separate opens have
independent offsets; normal exit cleanup covers both kinds.

The parent validates exact record lengths and complete application address
spans before access. Path requests are bounded and terminated. Metadata and
entries are copied from aligned FS temporaries, allowing unaligned application
buffers; EOF/errors leave result records unchanged. stat/fstat return inode,
mode, link count, numeric uid/gid, size and three stored timestamps. Paths use
the launching shell cwd. Read-only directory opening requires read bits, while
path metadata lookup requires no extra descriptor or read-open permission.
This is still one trusted foreground client, not general FS credentials or a
protected multi-process server.

Add a fifth independent executable /boot/ls and package it in the MINIX image.
It lists current directory by default, supports -a (hidden entries), -l (mode,
links, UID, GID, byte size), -d (directory itself), '--', and multiple paths.
It reports a failed path and continues to later operands with exit=1. Listing
is streamed in disk order with bounded memory; sorting, recursion, dates and
owner names are not implemented. Bare ls remains the kernel builtin.
Application builds link the existing freestanding lib/ansi/memcpy.c for
compiler-generated aggregate copies; no hosted libc or ESP-IDF is introduced.

Validation: clean build and all 39 host scripts pass without compiler warnings.
New integration runs actual directory and metadata wrappers, production FS/IPC
request validation and ls over the demo disk, including boot/large's indirect
entry, deleted slots, independent offsets, rewind, closed/non-directory handles,
full handle tables, stat without spare fds, unchanged EOF/error results, short
record rejection, unaligned buffers, missing operands, default cwd, invalid
options, and output failure cleanup. All five real ELFs load through the FS
and image loader tests. ELF sections/segments and image layout pass.
_iram_end=403743d4, _iram_ext_end=403875ec, _runtime_stack_end=3fce6f40;
4288 bytes remain before the application text DRAM alias. ls.elf is 2616 bytes.

Hardware pending: [FEATURE APP-DIRS 118]1 / [TEST APP-DIRS 118]. Run:
run /boot/ls /boot
run /boot/ls -al /boot/large
run /boot/ls -l /readme
run /boot/ls -ld /boot
run /boot/ls missing /readme
hello --files
run /boot/wc readme boot/readme
disk
mm
For boot/large expect '.', '..', 'readme'; /readme long output is
-r--r--r-- 3 0 0 61 /readme. The missing-path command prints the valid operand
and exits 1; successful commands exit 0. Also try cd boot followed by
run /boot/ls and cd / as separate commands. Not flashed.

## Image 120: transactional foreground application exec

ABI v8 adds `cp32_execv(path, argv)` and `/boot/exec`. A running application
can replace its fixed-slot ELF while retaining its process slot, PID, parent
wait relationship and all four owned file/directory handles with their offsets.
Success starts at the replacement entry and never replies to the old SENDREC
buffer. The new context clears general/special registers and blocked-frame
bookkeeping, resets the application heap and BSS, and installs fresh argc/argv
and the fixed HOME/PATH/USER environment. Paths resolve against the shell cwd;
the process name is the executable basename.

MINIX reference: `minix-2.0.0/src/mm/exec.c` copies arguments before replacing
memory, installs a fresh context without allocating a new PID, and notifies FS
for close-on-exec processing. CP32 uses fixed Xtensa ELF addresses and its
existing internal SRAM instruction/data alias. The foreground FS coordinator
requests a temporary 32 KiB allocation from MM, loads and validates the complete
image and stack there, then copies into the blocked child's slot and publishes
the new context under the scheduler lock. It releases staging on both success
and failure. Unlike MINIX's early `new_mem` transition, no old image bytes are
changed before all payload reads succeed. No hardware mapping changes are made.

Missing/nonexecutable paths, malformed images, read errors and MM allocation
failure return errno through the existing file IPC callback; the old image,
registers, heap break and application handles remain intact. Loader descriptors
are separate from the application's four handles, so exec works with a full
application fd table. Arguments are bounded to nine entries and 256 string
bytes. This is trusted single-foreground execv, not general MM lifecycle
ownership, fork, execve environment inheritance or protected execution.
Close-on-exec flags are not exposed by this bootstrap application ABI.

Validation: `make clean && make`, all 40 `make tests` scripts, ELF section/segment
inspection and image-layout verification pass. No compiler warnings. The new
integration test runs production FS, ELF loading, stack construction and
replacement logic, modeling IPC and physical memory. It injects failures at
every disk-read boundary, checks exact rollback of image/process/heap state,
MM exhaustion, invalid executable/request/argument rejection, repeat staging
release and successful replacement with a full fd table and preserved offset.
All six generated application ELFs pass loader validation. Host tests do not
execute Xtensa replacement instructions.

ELF endpoints: `_iram_end=0x403743d4`, `_iram_ext_end=0x40387ba0`,
`_runtime_stack_end=0x3fce7c50`. The new helpers and literals are in extended
IRAM. Only 944 bytes remain before the application text's DRAM alias at
0x3fce8000; existing 128 KiB heap and stack reservations remain intact.

Hardware pending, not flashed: `[FEATURE APP-EXEC 120]1` and
`[TEST APP-EXEC 120]`. Run these commands separately:

```text
hello --exec-fail
hello --exec
run /boot/exec /boot/echo replaced successfully
run /boot/exec /missing
hello --files
run /boot/ls /boot
mm
disk
```

Expect `Exec failure recovery OK`, then `Exec PID and file OK`, both exit=0.
The exec-to-echo command prints `replaced successfully` and exits 0; missing
path prints `exec: cannot execute` and exits 1. Repeat `hello --exec` and check
that `mm` has no leaked staging allocation and the shell remains usable.
Invalid-image and injected read/allocation failures are host-verified only.

## Image 121: keep MM allocations inside the linker heap

Image 120 hardware FAILED during `hello --exec`, after `hello --exec-fail`
returned exit=0. User serial report: EXCCAUSE=0x1c, EXCVADDR=0,
EPC1=0x403d8002, PS=0x110. The expected instruction at that PC is `mov.n a6,a1`,
not a memory load; an intact image does not explain that fault there.

Root defect found in `mem_init`: `_heap_start=0x3fcb4c50` was rounded DOWN
to a 4 KiB click, yielding staging base 0x3fcb4000. The 3152 bytes before the
heap belong to kernel BSS, including part of the FS cache, its control fields,
and the MM allocation table at 0x3fcb4848. Clearing/loading the exec buffer
there overwrites these globals, and subsequent cache fills can overwrite
staged executable bytes. This is software memory overlap, not evidence of a
new Xtensa context or instruction-cache requirement. The earlier host exec
test modeled a separate staging array and therefore missed this integration
boundary; it also used 256-byte clicks instead of the target's 4096-byte clicks.

Fix: round the heap start UP and `_heap_end` DOWN to whole clicks, exposing
only the interior range. Empty/reversed/sub-click ranges expose zero pages.
Arithmetic rounds after division to avoid overflow at the address-space end.
The 128 KiB linker reservation is unchanged; this layout supplies 31 usable
4 KiB pages (124 KiB), enough for the 32 KiB exec staging allocation. MINIX's
whole-free-click allocation invariant is preserved without allocating BSS or
stack bytes. Correct the exec host model to use target-sized clicks.

Validation: clean ELF/bin build, all 41 host scripts and ELF sections/segments
and image-layout checks pass without compiler warnings. New regression runs
the production mem_init and allocator against image 120's exact addresses,
all 4096 alignment offsets, empty/reversed/address-limit ranges, whole-heap
exhaustion and release. The transactional exec regression still passes.
Image 121 endpoints: `_iram_end=0x403743d4`, `_iram_ext_end=0x40387bac`,
`_heap_start=0x3fcb4c60`, `_heap_end=0x3fcd4c60`,
`_runtime_stack_end=0x3fce7c60`; 928 bytes remain before the app reservation.
MM's usable interval is [0x3fcb5000, 0x3fcd4000).

Hardware retest pending, not flashed: `[FEATURE APP-EXEC 121]1` and
`[TEST APP-EXEC 121]`. Run `hello --exec-fail`, `hello --exec` twice,
`run /boot/exec /boot/echo replaced successfully`, then `mm` and `disk`.
Expect failure recovery, `Exec PID and file OK` with exit=0 on each successful
self-replacement, echo output and a usable shell without leaked MM staging.
Image 120 does not establish successful exec hardware validation.

## Image 122: drain stale keyboard input before publishing readiness

Image 121 hardware reported `[KBD init=1 stale=0 int=1 status=1 fifo=10]`
followed by five empty TTY lines and repeated `$ ` prompts without new typing.
`cardputer_keyboard_init` set `kbd_ready=0`, then called the public event reader
to flush the FIFO. That reader returns immediately while readiness is false,
so every queued event survived initialization. The ten queued events are
consistent with five Enter press/release pairs; the log does not identify
individual event bytes. Exec hardware validation is still pending.

Initialization now drains KEY_EVENT_A directly through the existing register
reader, masks the event count to its low four bits, and publishes readiness
only after successful drain and interrupt setup. I2C failures or a still-busy
FIFO after 32 discarded events leave the driver unready instead of releasing
stale input to TTY. The normal runtime reader and shell prompt behavior remain
unchanged; Enter and all other keys typed after initialization still work.
Interrupt setup also stops if clearing old status fails.

MINIX reference: src/kernel/keyboard.c kb_init consumes old scan input before
enabling the keyboard IRQ. CP32 uses the TCA8418 FIFO rather than PC keyboard
ports. Register behavior is from TI SCPS215G sections 8.6.2.3–4:
https://www.ti.com/lit/ds/symlink/tca8418.pdf (0x03 count bits 3:0; each 0x04
read pops one event). No new hardware timing assumptions or delays were added.

Validation: clean firmware build and ELF sections/segments/image-layout checks
pass. New production-init host regression covers FIFO depths 0–10, all count
read/event read failures, register write failures, upper count-register bits,
bounded draining and readiness gating. All 42 host test scripts pass.
ELF endpoints: _iram_end=0x403743d4, _iram_ext_end=0x40387bd0,
_runtime_stack_end=0x3fce7c90; 880 bytes remain before the application reservation.

Hardware pending, not flashed: `[FEATURE KBD-START 122]1`,
`[TEST KBD-START 122]`, and `[KBD V122 init=1 stale=... int=0 status=1 fifo=0]`
when no new keys arrive during the diagnostic. Reboot after queued key events,
then leave the keyboard untouched: expect a single prompt and no empty TTY
lines. Confirm newly typed characters and Enter work, then retry hello --exec.


## Image 122 hardware accepted; image 123 moves the root to SD

User capture docs/hardware/app-exec-v122.log confirms no stale startup input,
exec failure recovery, repeated PID/file-preserving self-replacement, exec to
echo, and successful MM/disk checks. The previous two regressions are resolved
on hardware. General multi-process lifecycle remains separate work.

Image 123 changes the default root backing store to SD SPI, using the existing
MINIX V2 FS and device IPC. Supported: SD v1/v2 SDSC and SDHC/SDXC command/address
formats, CRC-checked sector reads, one primary MINIX partition or raw V2 volume,
bounded waits and read-only diagnostics. Missing/bad media leaves the console
available. Root writes return EROFS. No card contents or firmware were flashed.
The RAM backend is retained as make STORAGE=ram in a separate build directory.

Static SRAM savings versus image 122 are 86,632 bytes net. SD heap reservation
increases from 128 to 192 KiB (188 KiB whole-page allocator); application and
stack reservations are unchanged. Remaining margin is 21,968 bytes. ELF:
_iram_end=403743e8, _iram_ext_end=403885f8, _heap_start=3fc9fa28,
_heap_end=3fccfa28, _runtime_stack_end=3fce2a30. No resident ramdisk/demo symbols.
Recovery RAM layout also passes, with _runtime_stack_end=3fce7cc0.

Validation: both clean builds, all 43 host scripts, ELF sections/segments and
image-layout checks pass without compiler warnings. New tests exercise SPI
initialization/addressing/CRC/errors, partition bounds and malformed media,
sector cache failure recovery, root/device readonly behavior and all six real
application ELF loads through the filesystem from the generated SD image.
Host tests model the card and GPIO is not yet hardware-validated.

Pending hardware: [FEATURE SD-ROOT 123]1 / [TEST SD-ROOT 123], ROOT result=0,
SD-backed directory/file/exec and no-card startup tests. See docs/sd-root.md for
exact media preparation, error codes, acceptance commands and references.
Writable MINIX allocation/sync/recovery and hardware SPI throughput are next.
