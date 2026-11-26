# CP32 port plan

## Implementation order:

1. Process table & scheduling:	complete the implementation tasks and process can fully run as part of the cp32 kernel like a Minix v2 OS.

| Priority 1 feature | Image 207 firmware, first updater run passed |
|---|---|
| Independent application execution | Every live process owns private text/data/stack backing; final exception return installs the selected image. Fixed virtual addresses are implemented through software residency, without memory protection. |
| Preemptive application scheduling | Timer round-robin selects independent runnable siblings/descendants; FS pins defer conflicting image switches while services continue. |
| General process lifecycle | MM coordinates launch, fork, staged exec, exit, zombies and wait. FS serializes descriptor/cache mutations through MM-to-FS requests. |
| General process trees | Independently live siblings and descendants; PID 1 INIT adopts live and exited orphans and collects their statuses. |
| Blocking and wakeup | Independent live-child waits and terminal-read continuations; coalesced MM timer notifications retry I/O, deliverable signals interrupt waits/reads with EINTR. |
| Signal delivery | PID/group delivery with credentials, handlers, exact MM-owned signal return, masks/pending sets and asynchronous delivery at safe return boundaries. |
| Concurrent validation | Production host coverage plus `hello --t` CPU progress, private memory, exhaustion/reuse, signals, orphan adoption, concurrent I/O and exec. |

All requested priority-1 implementation paths are present. Do not mark this item
`[x]` until the current image passes updater, normal firmware, repeated runs and a cold
restart on Cardputer. The physical image-copy scheduler is single-core; memory
protection, a separate user shell, background job control and broader POSIX APIs
are outside this priority-1 milestone.

2. System calls: implement the Minix v2 system calls on the CP32 so procress can use the kernel

3. System‑task / kernel services: Implement the kernel tasks like the Minix v2

4. File System: Implement the CP32 File System like Minix v2 in the user space, so it can be a full feature FS for the CP32 OS.

## Implementation logs

Write down here the progress of the `Implementation order` above.

### Priority 1 — image195 PROC-STATE

Parent PID and heap bounds now belong to each kernel process entry. Fork clones
heap bounds and assigns the parent PID; cooperative swaps no longer copy this
state through saved images. Exec commits new bounds only after staging succeeds.
Release clears the record before slot reuse. MINIX2 keeps corresponding state
in mproc; CP32 uses the existing kernel table as an incremental ownership step.
The broker still coordinates lifecycle; MM-owned creation/termination,
independent application images and preemptive scheduling remain pending.

hello --t now checks parent/child heap independence, switching, failed exec,
successful exec and slot reuse twice; the old capacity test is host-only.
Expect two dots, Process state OK over USB and [TEST ALL PASS]. Update apps,
then repeat on normal firmware and after a cold restart. Hardware195 pending.

[x] Image194 updater hardware acceptance: Process slots OK, [TEST ALL PASS],
Hello exit=0. Capture: docs/hardware/proc-reap-v194-updater-pass.log.

Validation: all68 host scripts and five clean builds pass (SD, updater, RAM,
GPIO-SPI, GPIO-SPI updater). Production tests cover independent records,
failed clone, exchange, exec preservation/reset and cleared slot reuse;
the focused hello test covers parent/child/exec paths and failure reporting.
ELF sections/segments checked: normal _iram_end=0x403743e0,
_iram_ext_end=0x4038c334, _runtime_stack_end=0x3fce76f0 (2320-byte margin).
Updater/RAM/GPIO/GPIO-updater margins:23168/7008/3264/24112 bytes.
Existing assembly field offsets are preserved; heap/stack reservations are
unchanged. hello.elf is2788 bytes. Host fsck and diff checks pass.
No hardware flashed; image195 hardware acceptance remains pending.

[x] Image195 updater hardware acceptance: apps, sync, Process state OK,
[TEST ALL PASS], Hello exit=0. See docs/hardware/proc-state-v195-updater-pass.log.
Normal/cold-boot and LCD appearance are not established by this capture.

### Priority 1 — image196 PROC-SIGOWN

Group identity, signal masks, pending bits and default/ignore dispositions now
belong to the process record, not saved-image metadata. Fork inherits group,
mask and dispositions with no pending signals. Switching selects the existing
record; exec preserves it. Exit clears the reusable slot while retaining the
child's group in its wait result. MINIX2 mproc/forkexit provides the reference;
CP32 keeps flat fixed-address images and serialized foreground execution.

hello --t runs the focused --owner test plus MM (two dots). Two cycles check
inheritance, independent pending/group state, switching, exec and reuse.
Expect Signal ownership OK, [TEST ALL PASS], exit0. Install apps with updater,
then test normal firmware and a cold restart. Image196 hardware pending.

To close the broader process-lifecycle milestone:
- Give filesystem ownership, live-wait continuations and fault state
  persistent process ownership independent of the foreground broker stack.
- Move lifecycle request coordination into MM/process services, including
  creation rollback, exec commit, termination and collection.
- Support independent children and orphan adoption, then validate cleanup and
  wakeups across those lifetimes. Preemptive scheduling and full signal handlers
  remain separate unfinished priority-1 work.

Image196 validation: all69 host scripts and five clean builds pass (SD,
updater, RAM, GPIO-SPI, GPIO-SPI updater). New production tests cover persistent
signal ownership, fork inheritance, separate pending/group state, handoff,
retained exit group and20 cleanup/reuse cycles. Actual hello parent/child/exec
paths and lost-pending failure reporting pass. Exec tests verify preserved
signal/group fields on both commit and rollback.

The initial 32-bit fields failed the normal SD 2KiB headroom regression. Bounded
16-bit fields (PID<=30000, signals1..15) and direct internal call0 calls in fork
and process helpers restore the budget. These helpers already use -Os; their
calls stay in linked internal IRAM and pass linker displacement checks. ELF
sections/segments and helper disassembly were inspected. Normal _iram_end is
0x403743e0, _iram_ext_end0x4038c2a4, _runtime_stack_end0x3fce77b0:2128 bytes
static margin. Updater/RAM/GPIO/GPIO-updater margins:22864/6704/3072/23808 bytes.
Existing assembly field offsets, heap and stack reservations are unchanged.
Host fsck reports clean; diff checks pass. Hardware196 remains pending.

[x] Image196 updater hardware acceptance: Signal ownership OK, [TEST ALL PASS],
Hello exit=0. Capture: docs/hardware/proc-sigown-v196-updater-pass.log.

### Priority 1 — image197 PROC-IMAGE

Saved image allocations are owned by proc.p_shadow (MM click base), with zero
for the resident process. Fork commits the parent's ownership after allocation
and descriptor cloning; handoff transfers ownership under the scheduler lock;
restore clears ownership and releases the backing exactly once. Family metadata
no longer duplicates backing addresses. Kernel clone/publish/release reject
saved-image processes. This adapts MINIX's p_shadow/mkshadow model to CP32's full
text/data/stack copies; ESP32 SHADOWING translation remains disabled.

hello --t now runs --image plus MM, with two dots. Two cycles exercise nested
parent/child/grandchild images, repeated swaps, private stack/heap/globals and
exec of the deepest child. Expect Image ownership OK, ALL PASS and exit0.
Install through updater/apps/sync, then test normal firmware and a cold boot.
Image197 hardware pending; broad process lifecycle is not yet complete.

Remaining to close process lifecycle: process-owned filesystem/live-wait/fault
state; MM/service coordination for creation, exec and termination; independent
children and orphan adoption, with cleanup/wakeup validation. MM allocation
requests are still broker-owned. Independent/preemptive scheduling and full
signal handlers remain separate unfinished priority-1 work.

Image197 validation: all70 host scripts and five clean builds pass (SD,
updater, RAM, GPIO-SPI, GPIO-SPI updater). Production tests cover rollback,
process-owned backing, nested exchanges, parked clone/publish/release rejection,
restoration and20 cleanup cycles. Actual hello tests cover nested child exec,
parent exchange/reuse, stale ABI, allocation/wait failures and memory corruption.
ELF sections/segments inspected; normal _iram_end=0x403743e0,
_iram_ext_end=0x4038c2f4, _runtime_stack_end=0x3fce77f0 (2064-byte margin).
Updater/RAM/GPIO/GPIO-updater margins:23168/7008/3008/24112 bytes.
Heap/stack reservations and assembly field offsets are unchanged. Shared
ownership-failure diagnostics keep normal headroom above the required2KiB.
hello.elf is2532 bytes. Host fsck is clean; git diff --check passes.
No hardware flashed; image197 hardware acceptance remains pending.

[x] Image197 updater hardware acceptance: Image ownership OK, [TEST ALL PASS],
Hello exit=0. Capture: docs/hardware/proc-image-v197-updater-pass.log.

### Priority 1 — image198 PROC-ZOMBIE

Exited children now retain real P_ZOMBIE process slots with PID, parent, group
and encoded status until wait collects them. Image/FD resources are released
on exit; failed status copyout retains the zombie. The eight-result parent
arrays and logical slot reservations are removed: the actual process table
bounds live plus exited children. Collection releases a slot without charging
child CPU usage twice. Zombies cannot send/receive IPC or enter ready queues;
ps identifies them as ZOM. MINIX2 forkexit.c HANGING/cleanup is the reference.

hello --t runs --zombie plus MM (two dots). It retains more than eight children,
exhausts process slots, checks invalid-pointer recovery, selectively collects
and reuses capacity, drains the results and forks again. Expect Zombie wait OK,
ALL PASS and exit0. Install updater/apps/sync; test normal and cold boots.
Image198 hardware pending. Retained exit ownership is implemented; this does
not yet provide INIT adoption or independently scheduled live-child waits.

Remaining lifecycle work: filesystem and fault ownership; persistent general
live-wait/wakeup state; MM/process-service orchestration of fork/exec/exit;
independent children and orphan adoption, with cleanup/wakeup validation.
The existing policy of discarding an exiting parent's uncollected results is
preserved until adoption is implemented. Preemption and full signal handlers
remain additional priority-1 work.

Image198 validation: all71 host scripts and five clean builds pass (SD,
updater, RAM, GPIO-SPI, GPIO-SPI updater). Production tests retain16 children,
exhaust actual slots, reject sibling collection, preserve EFAULT results,
collect normal/signal status and reuse slots without duplicate CPU accounting
or resource leaks across10 cycles. IPC tests reject zombie send/receive and
ready insertion. Historical nested/fault/signal/exec regressions still pass;
actual hello tests cover capacity, child exit, collection and failure reporting.

ELF sections/segments inspected. Normal _iram_end=0x403743e4,
_iram_ext_end=0x4038c368, _runtime_stack_end=0x3fce7800:2048-byte static margin.
Updater/RAM/GPIO/GPIO-updater margins:23312/7152/2992/24256 bytes. Parent/status
packing and shared lifecycle-failure strings preserve the required headroom;
heap/stack reservations and assembly-visible offsets remain unchanged.
hello.elf is2376 bytes. Host fsck is clean; diff checks pass.
No hardware flashed; image198 hardware acceptance remains pending.

[x] Image198 updater hardware acceptance: supplied capture shows the application
update followed by Zombie wait OK, [TEST ALL PASS] and Hello exit=0. Archived at
docs/hardware/proc-zombie-v198-updater-pass.log. Normal-firmware/cold-restart
acceptance and LCD appearance remain unconfirmed. This validates the focused
exited-child test, not completion of priority1 or the broader lifecycle milestone.

### Image199 PROC-FAULT

MINIX2 kernel/exception.c associates user exceptions with the affected process
through cause_sig. CP32 retains its terminal-fault policy for supported Xtensa
application exceptions, but replaces global fault metadata with process-owned
signal/cause/PC. After committing the exit frame, the terminal process no longer
needs heap bounds; a union reuses that storage without changing struct size or
assembly offsets. p_exitstatus holds the pending fault signal until broker
consumption, then later the encoded zombie status. Normal exits preserve heap
state; fault consumption is scoped to the exiting process. Clone/publication
reject terminal records. No allocation or diagnostic output occurs in the
exception classifier; the normal broker emits bounded CTX V199 diagnostics.

hello --t now runs --fault-owner plus MM (two dots): illegal/null-load child
faults, parent heap preservation and normal child reuse. Historical zombie tests
remain host fixtures. Handshake199 requires updating hello; service ABI36 is
unchanged. FEATURE/TEST PROC-FAULT199, SD/ROOT V199 and UPDATER199 identify it.
Hardware acceptance remains pending; no firmware was flashed.

Remaining to close the broader lifecycle milestone: persistent per-process
filesystem ownership and live-wait/wakeup continuations; moving creation,
execution and termination orchestration from the foreground FS broker into
MM/process services; independent child lifetime and INIT orphan adoption.
Broader priority1 also still needs independent images, timer-driven application
preemption and general asynchronous signal handlers/return/I/O interruption.

Image199 validation: all72 host scripts and five clean builds pass (SD,
updater, RAM, GPIO-SPI, GPIO-SPI updater). Tests cover independent fault records,
rejected transitions, parent heap preservation, slot reuse and existing nested
fault/exec/signal regressions. Fault classification uses128/512 stack bytes;
exit IPC uses528/3072 in every variant. ELF sections/segments inspected;
normal _iram_end=0x403743e4, _iram_ext_end=0x4038c354,
_runtime_stack_end=0x3fce77e0 (2080-byte margin). Updater/RAM/GPIO/GPIO-updater
margins:23552/7392/3024/24496 bytes. Heap/stack reservations are unchanged.
hello.elf is2160 bytes. Host fsck reports MINIX V2 clean. Hardware199 pending.

[x] Image199 updater hardware acceptance: apps and sync succeeded, both expected
SIGILL/SIGSEGV children terminated, Fault ownership OK, [TEST ALL PASS] and
Hello exit=0. Capture: docs/hardware/proc-fault-v199-updater-pass.log.
Normal/cold/LCD acceptance is not established by this USB capture.

### Image200 PROC-CONTEXT — persistent filesystem and wait records

MINIX2 FS misc.c do_fork gives each process a distinct fproc and shares open
file descriptions by reference; MM mproc retains wait/lifecycle state. CP32
now binds each application to a stable FS record and lifecycle continuation.
Fork clones descriptors once; image handoff selects those records rather than
copying descriptor arrays, namespace state or wait messages into global slots.
Ancestor signal traversal uses the process-owned continuation. Nested cleanup
releases child resources once and keeps the parent's record at its original
address. Failed clones roll back descriptor references without changing the
parent. Successful root startup binds after FS cloning, so cloning cannot
clear the continuation pointer.

ESP32-S3 still uses the fixed execution window and cooperative image exchange.
The existing task-stack-guard pointer slot is a union: tasks retain the guard
field; application entries use p_files. No assembly offsets or process-table
size change. A lifecycle pointer in the FS record identifies the stable
continuation in the fork allocation; the root has permanent static storage.
Backing allocations still have foreground-family lifetimes. This is persistent
state ownership, NOT independent process scheduling or MM-owned orchestration.

hello --t runs --fs-owner plus MM (two dots). The child changes cwd/umask,
reads an inherited descriptor, yields, and closes it; the parent verifies its
own namespace and shared offset, waits, then reads and closes its own handle.
Two rounds check slot reuse. Expect FS ownership OK, [TEST ALL PASS], exit0.
Handshake200 requires updating hello; ABI36 is unchanged. Image199 fault and
image198 zombie tests remain host fixtures. Hardware200 is pending.

Priority1 remains incomplete. Remaining implementation: independently runnable
images and timer-driven application preemption; MM/process-service dispatch for
fork/exec/exit; independent siblings, parent exit and INIT orphan adoption;
general blocking/wakeup and interruption of I/O; user signal handlers and
sigreturn/asynchronous delivery; concurrent fairness/exhaustion/leak validation.
Persistent continuations alone do not establish general live-child wakeups.

Image200 validation: all74 host scripts and five clean builds pass (SD,
updater, RAM, GPIO-SPI, GPIO-SPI updater). The new context regression verifies
stable per-process FS/continuation addresses through nested handoffs, rollback
and slot reuse using production helpers with undefined-behavior sanitization.
The actual hello test covers parent/child branches and error reporting; prior
fault, zombie, signal, exec and five-level fork regressions remain passing.
ELF sections/segments inspected; normal _iram_end=0x403743e4,
_iram_ext_end=0x4038c338, _runtime_stack_end=0x3fce77e0:2080-byte margin.
Updater/RAM/GPIO/GPIO-updater margins:23280/7136/3024/24224 bytes.
Heap/stack reservations are unchanged. hello.elf is2452 bytes. Host fsck reports
MINIX V2 clean. No firmware flashed; image200 hardware acceptance is pending.

### Image201 — FS ownership test prerequisite and diagnostic

Image200 updater hardware reached the shell, installed apps and synced, then
hello --t returned232 while opening /readme. No fork occurred and no kernel
exception was reported. The capture does not contain errno, so missing media
content versus other open failures cannot be distinguished. Image200's new
ownership behavior has not yet passed hardware acceptance.

The focused test now reads /boot/hello, the executable the shell just loaded,
instead of relying on optional /readme content. No disk mutation is added.
If opening still fails, the removable application diagnostic prints
[FS V201 open errno] followed by the numeric errno over USB during --t, before
returning232. The LCD retains the existing dots/final-result behavior.
The actual application host regression verifies the selected path and errno2
output. Firmware/handshake201 requires updating hello through apps; ABI36 is
unchanged. Hardware201 remains pending.

### Image202 — apps restores missing /readme

The user confirmed /readme was deliberately deleted, explaining image200's
exit232. The optional apps command now creates a missing /readme with short
CP32 filesystem documentation and mode0644 after updating executables. Existing
regular files are preserved without writes, including custom contents and hard
links; a non-regular /readme or disk failure stops the updater with its existing
FS error report. The bounded create path verifies writes before publication.
Normal firmware still excludes updater payloads and this maintenance helper.

The FS ownership test continues to read /boot/hello, so deleting documentation
does not invalidate a process test. Open failure diagnostics now identify V202.
Install updater202, run apps (expect /readme installed), sync, cat /readme, and
hello --t. A second apps run must report /readme unchanged. No reformat is needed.
Hardware202 acceptance remains pending.

Image202 validation: all74 host scripts and five clean builds pass. Updater
regressions cover create-only restoration, verified contents/mode, unchanged
custom files and hard links, non-regular-path rejection and write failure.
ELF sections/segments inspected; normal static headroom remains2080 bytes.
Host fsck is clean; diff checks pass. Hardware202 remains pending.

[x] Image202 updater hardware acceptance: apps recreated /readme, cat printed
its expected contents, and hello --t reported FS ownership OK, [TEST ALL PASS]
and Hello exit=0. Capture: docs/hardware/proc-context-v202-updater-pass.log.
This verifies the focused filesystem/continuation test on updater firmware;
normal/cold-start and LCD appearance are not established by the serial capture.
It does not validate independent scheduling or the other remaining priority1
features. Those items remain pending.

### Image203 APP-BOUNDARY — exception stack and image ownership boundaries

MINIX kernel save/restart switches to a kernel stack; the CP32 level1 IRQ,
syscall and terminal-application-fault C paths now use a linker-named16KiB
exception window in the existing lower boot-stack reservation. Entry never
stores through the interrupted application SP. Task stacks start at the named
upper boundary, preserving all reservations and process offsets. EXCM excludes
normal level1 nesting; illegal nested entry is terminal. Bounded production
CTX V203 diagnostics report actual C SP: kinds1/2 task IRQ/IPC and4/8 app IRQ/IPC.

MINIX shadow-aware copies are adapted to CP32's full32KiB saved images. numap
and ESP32 umap resolve saved application data/stack into validated MM backing;
IPC payload plus trusted source-header copying holds the scheduler lock.
Invalid spans, zombie slots, nonidentity saved maps and unowned backings fail.
Raw FS/TTY pointers pin the resident window across validation and synchronous
I/O, including failure paths; image activation rejects a pinned window. Times
copyout and heap zeroing hold the lock; exec keeps the window pinned while
staging/committing. This protects today's serialized broker; a future ANY-source
broker must atomically select its caller before acquiring the window pin.

Each process now owns its backing allocation for its full lifetime. Root bind
allocates32KiB and can fail cleanly; each child allocation owns that child's
image, FS record and continuation. Handoff never transfers allocation ownership;
exit frees the child's allocation and root exit frees root backing. The new
cp32_image_activate boundary is used by production handoff/exit, and changes
image residency only: no allocation, FS selection, wait-state or ready-queue
changes. It requires the scheduler lock and a kernel stack. Initial root/child
images are snapshotted, and fresh FS metadata is explicitly cleared.

This adds32KiB root backing compared with image202, lowering the RAM-bounded
maximum fork depth. Synthetic deep-fork tests do not establish device capacity.
Heap and stack reservations are unchanged. Direct internal call0 calls for
application/system helpers keep static headroom above2KiB; linker placement
and call ranges were checked by the clean builds.

hello --t now runs --irq-stack, --fs-owner and MM (three dots). The first test
holds stack canaries during CPU work for at least12 ticks and checks a
repeatable integer sequence. Expect IRQ stack OK, FS ownership OK, ALL PASS,
exit0, plus app stack kinds4/8 ok=1 over USB. Install apps for handshake203;
service ABI36 is unchanged. Image203 hardware acceptance is pending.

Validation: all77 host scripts and five clean variants pass. Assembly modeling
covers register/special-state preservation, invalid SP, no application-stack
stores and destructive replacement of app memory during dispatch. Image tests
cover independent saved sender/receiver buffers at identical virtual addresses,
owned allocation identity, rollback, pin rejection and repeated cleanup.
ELF sections/segments and host fsck pass. Normal static margin2144 bytes;
updater/RAM/GPIO/GPIO-updater22448/6560/3088/23392. hello.elf3072 bytes.

Priority1 is not complete: independent runnable scheduling/preemption, MM-owned
lifecycle orchestration, independent children and INIT adoption, general wait/
wakeup and interruptible I/O, user signal handlers/sigreturn/asynchronous delivery,
and concurrent fairness/cleanup validation remain pending. These prerequisites
are integrated into current cooperative paths; no new preemptive policy is
silently enabled or claimed tested.

[x] Image203 updater hardware acceptance: application update and sync followed
by IRQ stack OK, FS ownership OK, application exception-stack kinds4/8 ok=1,
[TEST ALL PASS] and Hello exit=0. Capture:
docs/hardware/app-boundary-v203-updater-pass.log. Normal/cold boot and LCD
appearance are not established by this USB capture.

### Image204 APP-PROCESS — integrated priority-1 process runtime

MINIX2 mm/forkexit.c, mm/exec.c, mm/signal.c and kernel save/restart establish
MM lifecycle policy, independently runnable process contexts, orphan adoption,
interruptible waits and signal return. CP32 adapts those contracts to single-core
Xtensa call0 without hardware memory protection: a private32KiB backing plus
stable FS/lifecycle metadata belongs to every live process, and final exception
return selects the resident SRAM image from the kernel exception stack.
The assembly frame layout and heap/stack reservations are unchanged.

The timer schedules competing applications without yield. FS accepts requests
from any live application, atomically selects/pins its image and releases the
pin after copying/servicing borrowed buffers. Other application images cannot
replace that window during FS/device IPC. Saved-image IPC translates both
sender and receiver buffers under the scheduler lock. Invalid resident ownership
fails closed. Generic allocator changes are also locked against return-time
ownership validation. The obsolete cooperative fork broker is excluded from
production, while its historical regressions remain explicit host fixtures.

MM now owns launch, fork, exec, exit, zombie collection, heap and accounting
requests. Clone/close/exec-staging requests are serialized by FS. Exec uses a
separate temporary image; errors preserve the caller and successful staging
closes CLOEXEC files immediately before the guaranteed commit. Parent/child
returns no longer imply child-first execution. Live and zombie orphans move to
PID1 INIT; it waits and reaps them independently. The shell uses PID2 and resumes
after the foreground root and adopted descendants finish and INIT collects every
adopted zombie. The final reap completes the job even when no live process
remains to generate MM ticks. PID allocation also
reserves the root job identity until all descendants drain.

Blocked reads and child waits retain MM-owned continuations. Clock notifications
retry nonblocking terminal reads without retaining application pointers or
blocking FS for keyboard input. A coalesced MM notification cannot be hidden by
an always-nonempty application queue. Signal delivery and transition to a wait
are checked atomically, avoiding lost interrupts; interrupted operations return
EINTR through ABI37. TTY cursor blink follows MM read waits between polls.

Signal handlers, exact full-register signal return, pending masks, asynchronous
safe-boundary injection, fork/exec inheritance, PID/group selectors and MINIX
real/effective UID checks are implemented. Zombie probes retain credentials
without extending the process layout. MM owns the authoritative handler frame;
fork inside a handler inherits it. Caught signals defer during a handler;
SIGKILL/default termination can proceed. Exec resets caught handlers while
preserving ignores, masks and pending state. Hardware faults retain terminal
process cleanup; unsupported kernel/nested faults remain terminal.

hello --t runs --process, --signals, --orphan, --io-exec and the MM check. Five
LCD dots precede [TEST ALL PASS]; detailed logs stay on USB. Tests cover CPU
progress/private state, independent siblings, repeated capacity exhaustion and
reuse, interruptible reads/waits, handlers, adoption, concurrent buffers/cwd,
shared file offsets and failed/successful exec. hello.elf is5656 bytes. Update
apps for ABI37/handshake204. Markers: FEATURE/TEST APP-PROCESS204, SD/ROOT V204,
UPDATER204. No hardware flashed; image204 hardware acceptance is pending.

Validation: all82 host scripts pass, including production MM lifecycle/rollback,
image ownership, real timer ready-queue fairness/pinning, MM wakeup priority,
signal context/credentials and FS registry/staging tests. Legacy cooperative
fixtures are retained as historical regressions, not proof of the new runtime.
Five clean variants pass: SD, updater, RAM, GPIO-SPI and GPIO-SPI updater. ELF
sections, segments, frame offsets and targeted FS/MM/IRQ/INIT stack budgets are
checked; fsck reports MINIX V2: clean. Normal _iram_end=0x40374440,
_iram_ext_end=0x4038c0e0 and _runtime_stack_end=0x3fce7630 leave2512 bytes.
Updater/RAM/GPIO/GPIO-updater margins are20208/3888/3456/21152 bytes.
Targeted peak FS/MM/IRQ/INIT call-chain budgets are3952/912/480/112 bytes,
within their reserved stacks. git diff --check passes.

Priority1 implementation is integrated; closure remains hardware acceptance:
updater apps/sync, two hello --t runs, normal firmware repetition, cold restart,
and responsive ps/mm/disk plus hello --line/cursor. Do not mark priority1 [x]
from build/host results. Background shell jobs, separate user-space services,
full POSIX sigaction/job control and memory protection are outside this scoped
process milestone and are not claimed implemented.

### Image 205 APP-LAUNCH — first application launch

Image 204 updater hardware completed `apps` and `sync` on the 100 MiB SD
root. MM received launch request 2100 from FS. The log ended before the shell
printed the first suite case. [USB capture](docs/hardware/app-process-v204-launch-stall.log).

Image 205 synchronizes the instruction window before MM publishes the newly
loaded root process. MM clock notifications track parked terminal reads and
pause while FS polls input. The first two launches emit USB stage markers.
After 600 timer interrupts without a new stage, the kernel can report the
interrupted PC and endpoint. Kernel panics print the reason before halting.
The image uses ABI 37 and the suite request code 204.
Hardware acceptance is open. [Cardputer procedure](docs/hardware/pending-tests.md).

All 82 host scripts pass. All five firmware configurations passed clean builds
and image-layout checks. The normal SD ELF places `_iram_end` at `0x40374764`,
`_iram_ext_end` at `0x4038c190` and `_runtime_stack_end` at `0x3fce77b0`
(2,128 bytes before the application window). The updater has 19,840 bytes of
margin. Disassembly shows both instruction barriers on initial publication
and on image exchange. The generated SD filesystem passes `make fsck`.

### Image 206 EXCM-LOOPS — first return to an application

The image 205 updater allocated backing at click `0x3FCAB`, cloned the files,
published PID 100 in slot 3 and panicked with `return image`. During exception
return, the generated Xtensa hardware loop examined only the FS stack record.
The later MM record owned the image. Image 206 uses software branch loops in
kernel-linked code and checks the linked instructions before generating the
firmware. The regression follows the first return before the FS launch reply,
with the FS stack allocated first on the updater's 4 KiB-page heap.

Hardware acceptance remains open. Boot `[TEST EXCM-LOOPS 206]` with the image
204 SD applications and run `hello --t`. [Hardware checklist](docs/hardware/pending-tests.md).

Validation used the real S3-compiled allocator on a generic Xtensa QEMU CPU
with 4 KiB pages. With the FS stack in record 0 and the application at click
`0x3FCAB` in record 1, the image 205 flags reproduced the false rejection
only with EXCM set. The image 206 flags accepted the MM image in either mode
and rejected a wrong owner and an oversized range.

All 82 host scripts passed. All five clean firmware variants passed ELF,
exception-loop and image-layout checks. The normal SD ELF has
`_iram_end=0x40374764`, `_iram_ext_end=0x4038c128` and
`_runtime_stack_end=0x3fce7740` (2,240 bytes below the application window).
The updater margin is 19,952 bytes. The generated SD image passed fsck and
all six updater application ELFs match image 205 byte for byte.

### Image 207 APP-SLICE — forward progress after fork

Image 206 returned from the first launch and completed the suite request with
status 204. The concurrent-process test then reached a fork. The timer sampled
slot 4 at the IPC return with FS and MM both waiting. [USB capture](docs/hardware/excm-loops-v206-process-stall.log).

MINIX rotates runnable users at a quantum boundary. On CP32, moving between
two runnable applications also copies 64 KiB through the shared execution
window. Image 207 retains the interrupted user at the head of its queue for
six charged ticks. Services remain eligible on every tick. The save/restore
loop uses aligned word copies and preserves the instruction barriers.

The host regressions exercise two runnable users taking a timer interrupt
immediately after each swap, both orderings of the fork request and distinct
parent/child replies. USB reports the first swap duration and the saved PC,
return address and pending signal mask for both slots if the first test stalls.
Priority 1 remains open pending repeated updater, normal and cold-boot runs.
[Image 207 hardware checklist](docs/hardware/pending-tests.md).

All 82 host scripts passed. With one timer period charged for each swap,
each sibling made progress in 250 of the 600 simulated periods. Reinstating
the old rotation failed this regression. The linked kernel executes 24,580
instructions per full 32 KiB copy in each of the five clean builds.
Normal SD has `_iram_end=0x40374918`, `_iram_ext_end=0x4038c110` and
`_runtime_stack_end=0x3fce7770`, leaving 2,192 bytes below the app window.
The updater leaves 19,904 bytes. The generated SD volume passed fsck. All six
updater application ELFs match image 206 byte for byte.

The first Cardputer updater run passed all five checks with `[TEST ALL PASS]`
and `Hello exit=0`. The suite's process table showed PID 1 waiting and no
remaining application or zombie rows. The repeat updater run, normal firmware
runs and cold restart remain open. [Image 207 USB capture](docs/hardware/app-slice-v207-updater-pass.log).
