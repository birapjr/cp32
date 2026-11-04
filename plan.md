# CP32 port plan

## Implementation order:

1. Process table & scheduling:	complete the implementation tasks and process can fully run as part of the cp32 kernel like a Minix v2 OS.

2. System calls: implement the Minix v2 system calls on the CP32 so procress can use the kernel

3. System‑task / kernel services: Implement the kernel tasks like the Minix v2

4. File System: Implement the CP32 File System like Minix v2 in the user space, so it can be a full feature FS for the CP32 OS.

## Implementation logs

Write down here the progress of the `Implementation order` above.

### Priority 1: process-table lifecycle — image 168 PROC-SLOTS

Implemented reservation, validated runnable publication and safe foreground
exit/reap through kernel process-table helpers. The launcher selects a free user
slot instead of assuming endpoint 3; failed image/argument/filesystem setup
releases the reservation. Reserved NO_MAP slots cannot run. Kernel/server slots
are excluded; duplicate PIDs, queued/current processes and invalid contexts/maps
are rejected. Application services and IPC use the selected endpoint.

[x] Image168 foreground launch/reap and exec hardware acceptance: PID100,
PID101, successful exec, failed-exec recovery, then PID104 returned normally.
See docs/hardware/proc-slots-v168-pass.log. Alternate-slot/exhaustion checks
remain host-tested only.
Priority 1 still needs multiple independent application images, general
parent/child fork/wait/exit semantics and concurrent-process scheduling tests.
The current one-foreground-image restriction is preserved. Priorities 2–4
remain in the requested order, after priority 1.

[x] Image 167 shell metadata timestamp/restart hardware check: mkdir, create,
link, chmod, directory enumeration and sync preserved the reported inode and
directory timestamps across reboot. See docs/hardware/fs-meta-time-v167-pass.log.
This does not establish hardware acceptance of all application API paths.

Validation: all 52 host test scripts pass, including production launch and
slot lifecycle tests for alternate slots, exhaustion, duplicate PIDs, rejected
contexts/maps, queue/current-owner rejection, alias cleanup and failed FS setup.
Clean builds passed for normal SD, updater, RAM, GPIO-SPI and GPIO-SPI updater.
Normal ELF: _iram_end=0x4037439c, _iram_ext_end=0x4038c51c,
_runtime_stack_end=0x3fce7560 (2720 bytes static SRAM margin). Every variant
retains at least 2048 bytes margin; helpers use at most 32-byte stack frames.
ELF section/segment checks, make fsck (clean) and git diff --check pass.
Markers: [FEATURE PROC-SLOTS 168], [TEST PROC-SLOTS 168], ROOT/SD V168.
No board/card was flashed. ABI25 and on-card applications remain compatible.


### Priority 1: process inspection — image169 PROC-INFO

Added kernel-only bounded process snapshots and a `ps` shell command. Each row
copies endpoint, PID, flags, running state, tick accounting and a bounded name
under the scheduler lock. Printing happens after unlocking. Free slots are
omitted; kernel roles receive descriptive fallback names. No register/pointer
state is exported, no process-structure layout or scheduler policy is changed.
Rows are individually coherent, not a simultaneous snapshot of the whole table.

Hardware acceptance pending: `ps`, `hello --pid`, `ps`, `hello --exec`, `ps`.
Exited applications must disappear; the prompt, keyboard and blinking cursor
must remain responsive. Tick counts are the existing 60Hz accounting, not wall
time or percentages. Priority 1 remains incomplete: concurrent images and
general fork/wait still precede priorities 2–4.

Validation complete: all 53 host scripts pass, including snapshot bounds,
free-slot omission, copied-row stability, lock restoration, name sanitization
and ps state formatting. Clean builds pass for normal SD, updater, RAM, GPIO
SPI and GPIO SPI updater. Normal ELF: _iram_end=0x4037439c,
_iram_ext_end=0x4038c708, _runtime_stack_end=0x3fce77d0; static margin2096 bytes.
All variants retain >=2048 bytes static margin without shrinking heap/stacks.
Snapshot and ps stack frames are32 and64 bytes. ELF sections/segments and
compiled stack checks pass; make fsck reports clean. No hardware was flashed.


[x] Image169 hardware: ps returns before/after hello PID and successful exec;
exited application rows are absent. Capture: docs/hardware/proc-info-v169-pass.log.
The capture exposes incorrect CPU accounting: almost all sampled execution is
charged to HARDWARE. This is not acceptance of the reported counter accuracy.

### Priority 1: CPU accounting — image170 PROC-TIME

Corrected the inherited MINIX interrupt-depth assumption: MINIX outer clock
entry uses zero; CP32 irq.S increments zero to one before C dispatch. Depth1
now charges the saved interrupted owner, depth>1 charges HARDWARE. Capture and
billing occur before TTY wakeup can select another process. Idle ticks do not
add system time to the billable process. Lost-tick batching remains unchanged.
No process layout, scheduling policy or memory reservations changed.

Hardware validation pending: ps, wait a few seconds, ps, hello --pid, ps.
Expect idle/task counters to advance; HARDWARE should no longer receive almost
every outer timer tick. Very short tasks may have zero samples. Concurrent
images and general fork/wait remain outstanding under priority1.

Validation: all54 host test scripts pass. New production accounting tests
cover saved-owner selection despite a different scheduler pointer, outer versus
nested IRQ depth, idle exclusion, separate task/user billing, null fallback,
and batched ticks; integration checks enforce accounting before TTY wakeup.
Clean normal SD, updater, RAM, GPIO-SPI and GPIO-SPI updater builds pass.
Normal ELF boundaries: _iram_end=0x4037439c, _iram_ext_end=0x4038c71c,
_runtime_stack_end=0x3fce77f0 (2064 bytes static margin). All variants retain
at least2048 bytes margin. ELF section/segment and compiled stack checks pass;
make fsck reports clean. No hardware was flashed; image170 hardware validation
remains pending.


[x] Image170 hardware accounting check passed: sampled idle, TTY, disk and
clock time advances while HARDWARE remains zero. Foreground PID100 exits
normally. Capture: docs/hardware/proc-time-v170-pass.log.

### Priority 1: child usage on reap — image171 PROC-REAP

MINIX2 kernel/system.c do_xit adds a child's user/system and descendant totals
to its parent before releasing the descriptor. CP32 now does this for the
existing foreground shell broker. NO_MAP cancellation contributes nothing;
rejected/double releases cannot charge again. Billing returns to FS/shell
rather than the stopped LOW_USER boot placeholder. ps snapshots include the
existing child counters and print a children U/S line when either is nonzero.
No parent-tree, general fork/wait, background execution or new syscall is
claimed. These remain outstanding under priority1, before priorities2–4.

Application callbacks and the small C context initializer now use -Os to
retain internal SRAM headroom without reducing heap or stacks. Assembly and
process layout are unchanged. ABI25 and the SD applications remain compatible.
Hardware acceptance pending: ps, hello --pid, ps, hello --exec, ps. Child totals
should persist and be nondecreasing; short programs can finish between 60Hz
samples and contribute zero. No application row should remain after exit.

Image171 build validation: clean builds pass for normal SD, updater, RAM,
GPIO-SPI and GPIO-SPI updater. Explicit noinline boundaries retain measurable
exec-replacement and application-file dispatch stack frames with -Os. Compiled
stack checks pass for all five variants. Normal ELF: _iram_end=0x4037439c,
_iram_ext_end=0x4038c758, _runtime_stack_end=0x3fce7800 (2048 bytes margin).
All variants retain >=2048 bytes static margin without shrinking heap/stacks.
ELF section/segment inspection passes and make fsck reports clean. Markers:
[FEATURE PROC-REAP 171], [TEST PROC-REAP 171], ROOT/SD V171. No hardware flashed.

Final image171 host validation: all54 scripts pass. Lifecycle tests cover
repeated reaping with child/descendant totals, NO_MAP cancellation without
charging, rejected/double release without additional charging, and billing
transfer to the shell. Snapshot/ps tests verify child-total copying and display.
Hardware acceptance remains pending.


### Priority 1: kernel fork preparation — image172 PROC-FORK

SYS_FORK now accepts only MM and calls a locked kernel clone helper. Both
endpoints must be application slots; the parent must be receiving from MM with
a message buffer and saved context, the child must be free and detached, and
the new PID must be positive and unused. All rejection checks precede writes.
The child inherits the context, maps and virtual receive-buffer address, but
gets a zero call0 return value and fresh accounting, alarm, signal, IRQ and
queue-link state. The parent remains unchanged. NO_MAP|RECEIVING keeps the
child blocked for MM map installation and completion of its receive.

Reference: MINIX2 kernel/system.c do_fork. Xtensa differs in return register
a2 and the lack of virtual-address relocation/MMU isolation. This is the kernel
preparation boundary, not working application fork(): independent image storage,
MM fork/wait policy and per-child FS state remain necessary. Existing fixed
application images cannot simply execute this cloned map concurrently.

No process layout or application ABI change. Ordinary memory/environment
helpers in misc.c now use -Os; hardware timing loops and IRQ assembly are
unchanged. Shared PID validation and shorter panic labels retain SRAM space.
Images171 and172 remain hardware-unverified at the user's request to continue
without hardware. Later regression: ps, hello --pid, hello --exec, ps.
The new SYS_FORK branch is host-tested only; those commands do not exercise it.

Image172 validation: all54 host scripts pass; strengthened clone-state tests
also pass independently. Tests cover MM authorization/lock restoration, invalid
parent/endpoint/PID, duplicate PID, occupied/queued destinations, unchanged
parent and rejected target, child call0 return, blocked publication state and
clearing inherited alarm/signal/IRQ/queue/accounting ownership. Five clean build
variants pass, as do compiled stack budgets, ELF sections/segments, fsck and
git diff --check. Normal ELF: _iram_end=0x4037439c, _iram_ext_end=0x4038c78c,
_runtime_stack_end=0x3fce7800 (2048 bytes static margin); all variants retain
at least2048 bytes. Clone helper stack frame48 bytes. Markers:
[FEATURE PROC-FORK 172], [TEST PROC-FORK 172], ROOT/SD V172.
No hardware was flashed; no hardware clone/child-execution validation claimed.


### Priority 1: application fork/wait — image173 APP-FORK, ABI26

Implemented one outstanding child-first fork across the application ABI, FS
broker, kernel clone, MM image backing and wait-status collection. The fixed
Xtensa image region cannot host independent simultaneous absolute-address
programs: save all32KiB (text alias, data, heap and stack), clone context/FD state,
run only the child, then restore the parent image and reply with its child's PID.
The child receives0. Parent stays blocked until child exit, including after
child exec. Private FS cwd/mask/flags and shared file offsets are inherited;
parent cwd protection/rebasing includes its suspended record. Child usage is
accumulated in its actual parent before normal outer reap bills the shell.

waitpid supports a specific child or -1, status or NULL, and WNOHANG. Encoded
exit status is collected once; invalid pointers/options or wrong PID preserve
the record. Nested/uncollected-child forks return EAGAIN, MM exhaustion ENOMEM.
Resource failures unwind FD duplicates and backing memory without changing the
parent image. The one pending result is dropped if the parent exits without wait.
This is not general concurrent fork: no nested trees, signals, process groups,
background jobs or application fault recovery. MM owns the backing allocation;
the existing serialized FS broker coordinates lifecycle. MINIX2 forkexit.c
provides private image copy, shared-description inheritance and wait encoding;
CP32 adds child-first backing because the Xtensa setup lacks virtual remapping.

Added hello --fork and --fork-exec, ABI wrappers, production lifecycle tests
with fault/reuse coverage, FS suspended-cwd checks and compiled fork stack paths.
SRAM stays within existing heap/stack reservations by size-optimizing the pure
SD protocol and directory wrappers. Transport timing loops are unchanged. Hello
uses LTO with memcpy retained to keep all acceptance paths inside its16KiB text
slot. ABI26 hello must be updated using the on-device updater before testing.

Cumulative hardware instructions: docs/hardware/pending-tests.md. Hardware
acceptance remains pending for images171–173; no board or card was flashed.

Image173 final validation: all55 host scripts pass, including production fork
helpers, descriptor clone/rollback, private image restoration after simulated
child code/data/stack replacement, wait bounds/status/error preservation,
20 fork/reap cycles, ABI version/result/errno routing and suspended-cwd
remove/rename protection. Five clean build variants pass (normal SD, updater,
RAM, GPIO-SPI and GPIO-SPI updater). Compiled fork/exec/FS stack budgets pass.
Normal ELF: _iram_end=0x4037439c, _iram_ext_end=0x4038c734,
_runtime_stack_end=0x3fce77e0 (2080 bytes static margin). Every variant retains
at least2048 bytes; the RAM variant retains2368. Hello text is16250/16384 bytes.
ELF section/segment checks, make fsck (clean) and git diff --check pass.
No hardware was flashed. Actual Xtensa fork continuation and child-exec
restoration remain hardware acceptance items in docs/hardware/pending-tests.md.

The additional production-broker integration harness passes: launch parent,
reply0 to the cloned child, consume child exit, restore parent and reply with
child PID, collect encoded wait status, then reap the parent and verify shell
usage plus complete descriptor/MM cleanup. Physical RAM and IPC delivery are
modeled; actual Xtensa continuation remains a hardware check.


### Image174 — call0 compiler shifts / startup freeze fix (hardware pending)

Image173 hardware stopped after keyboard initialization, entering TTY/MM/CLOCK
before SD-root and timer initialization completed. The exact updater ELF has
CALL0 calls to __ashldi3/__lshrdi3 but both libgcc routines contain ENTRY/RETW:
they use the incompatible windowed ABI. The SDSC capacity calculation (the
user's 2GB card path) invokes __ashldi3 during SD initialization; optimized
clock arithmetic also references these helpers. This is a concrete ABI defect;
confirmation that it resolves the reported freeze awaits hardware.

MINIX kernel/main.c completes process initialization before restart(). CP32
must likewise preserve the boot continuation until its deliberate handoff;
Xtensa register windows are not part of CP32's call0 context contract. Added
call0 leaf shifts in klib32.S, preserving the low/high a2:a3 result convention,
a0 return address and callee-saved registers. No scheduler/FS/fork behavior,
heap size or stack reservation was changed. The image build now rejects
windowed function entry prologues in the linked ELF, using function symbols to
avoid treating literal-pool data as instructions.

Regression models the production leaf instructions for all counts0..63 on
boundary and deterministic random 64-bit inputs. A cross-assembled negative
fixture verifies rejection of ENTRY/RETW and acceptance of a call0 leaf.
The checker also rejected the original image173 updater's __ashldi3.
Five clean builds (SD, updater, RAM, GPIO-SPI, GPIO-SPI updater) pass ABI/image
checks. ELF placement: normal _iram_end=0x4037439c,
_iram_ext_end=0x4038c734, _runtime_stack_end=0x3fce77f0, margin2064 bytes.
Updater margin7632, RAM2368, GPIO3152, GPIO updater8720. FS/fork stack checks
and fsck pass. No flashing performed; image174 hardware result is pending.
Use docs/hardware/pending-tests.md for cold-boot and cumulative fork acceptance.

Image174 final host result: make tests passes all56 scripts; both replacement
helpers inspected in the updater ELF use RET.N and no ENTRY/RETW. All five
clean builds completed successfully. Hardware remains pending.


[x] Image174 hardware acceptance (2026-11-02): updater installed hello and
synced; normal image reached SD root and all CORE checks, ran ls/disk, passed
hello --fork and hello --fork-exec (both exit0), then ps showed no application
rows, shell children1/132 and HARDWARE0/0. Captures:
docs/hardware/call0-shift-v174-updater-pass.log and
 docs/hardware/app-fork-v174-pass.log. This validates the call0 shift startup
fix and the bounded fork/child-exec continuation. Repeated cycles, other service
checks and MM-origin SYS_FORK authorization are not established by these logs.

### Priority1: multiple waitable child results — image175 PROC-WAIT

MINIX mm/forkexit.c retains HANGING child records until waitpid cleanup and
excludes their PIDs from allocation. CP32 now keeps eight bounded result records
for the one foreground parent. Child image/FD resources are released at exit;
PID/status remain until collected. Further child-first forks can run before
older results are collected. waitpid accepts a specific PID or any child (-1),
NULL/valid status and options0/WNOHANG; no ordering of any-child results is
promised. Errors preserve results. Collection frees a slot; parent exit clears
all results. PID allocation skips retained PIDs even at wraparound. Child calls
cannot collect the suspended parent's other results. Full table and nested
fork return EAGAIN before MM/FS staging, and failed staging leaves records intact.

Xtensa fixed-address images still require child-first execution: this does not
provide concurrent parent progress, nested forks, signals or process-group waits.
Priority1 remains incomplete and priorities2–4 have not been advanced.
ABI26 remains compatible. New hello --wait must be installed using the updater.
It checks eight exits before wait, exhaustion, selective collection, reuse,
any-child draining and uncollected-status cleanup when run twice. Kernel tests
cover failed copyout/invalid options, duplicate PID retention, sibling isolation,
reset and allocation accounting; production broker integration now executes two
children before collecting in reverse order. PID wrap tests reserve a retained
PID. Hardware acceptance of image175 remains pending.

To retain the existing memory reservations, pure strtol conversion now uses -Os;
hello disables optional branch-target alignment padding with -mno-target-align.
No heap, image region or stack size changed. Hello text16162/16384 bytes.
Five clean builds pass: SD, updater, RAM, GPIO-SPI and GPIO-SPI updater.
Normal ELF _iram_end=0x4037439c, _iram_ext_end=0x4038c6d8,
_runtime_stack_end=0x3fce77d0, static margin2096 bytes. Other margins:
updater7728, RAM2224, GPIO3184, GPIO updater8816. All56 host scripts pass;
the added hardware-client harness also passes. ABI/image checks, FS/fork stack
budgets, ELF sections/segments and fsck pass. No board/card flashed here.
Cumulative hardware instructions: docs/hardware/pending-tests.md.


### Image175 hardware result — 2026-11-04

Normal SD boot and all CORE checks pass. hello --wait fails twice with exit107
(the test's fork-failed code during its initial eight-child loop, not errno107).
The serial output does not report the failing iteration or errno; cause remains
undetermined. Multiple-result wait acceptance is therefore failing, not complete.
Subsequent hello --fork and hello --fork-exec both pass with exit0. ps shows no
application rows and shell children0/147; the shell remains operational.
Capture: docs/hardware/proc-wait-v175-fail.log. README and pending checklist now
distinguish the passing fork regressions from this outstanding hardware issue.
Documentation-only update; no firmware change or new build claimed.


### Image176 WAIT-INIT — fix hello --wait exit107

The image175 command dispatcher omitted cp32_io_init(s) for --wait. The actual
runtime therefore returned -1/ENOSYS from the first cp32_fork, before any kernel
request, and wait_test returned107. This was an application test initialization
defect, not evidence of wait-table allocation failure. Added initialization to
the dispatcher (same contract as --fork). No fork/wait lifecycle policy changed.
The only kernel change is the image176 diagnostic identity.

Replaced the isolated wait_test/mock-wrapper harness with actual app_main plus
production io.c/heap.c and modeled kernel service callbacks. The pre-fix command
was reproduced separately: exit107, zero kernel callbacks. Fixed command passes
all eight-result checks through the real wrappers. Updating /boot/hello on SD
is mandatory; firmware-only flashing leaves the defect in the old executable.
Hardware acceptance of image176 remains pending; see the cumulative checklist.

Image176 validation: all56 scripts pass; five clean builds pass ABI/image checks.
FS/fork stack budgets and fsck pass. Normal _iram_end=0x4037439c,
_iram_ext_end=0x4038c6d8, _runtime_stack_end=0x3fce77d0 (2096-byte margin).
Other static margins: updater7776, RAM2224, GPIO3184, GPIO updater8848.
Hello text16122/16384 bytes. Hardware retest pending; no flashing performed.


[x] Image176 hardware acceptance: normal SD root (100MiB), all CORE checks,
two consecutive hello --wait runs, hello --fork and hello --fork-exec pass.
All application tests exit0. Final ps shows no application rows, shell
children0/186 and HARDWARE0/0. Capture: docs/hardware/proc-wait-v176-pass.log.
This confirms the image175 initialization fix and bounded eight-result wait
sequence, including parent-exit cleanup between invocations. The supplied log
contains one boot; cold-restart repetition and unrelated pending checks are not
claimed complete. README and cumulative checklist updated; no firmware changes.
