# CP32 port plan

## Implementation order:

1. Process table & scheduling:	complete the implementation tasks and process can fully run as part of the cp32 kernel like a Minix v2 OS.

Remaining feature	Current limitation
Independent application images	All applications execute at the same fixed addresses. Suspended parents are backed up in memory.
Concurrent application scheduling	Image182 implements cooperative exchange at either of two fork levels. Timer-driven application preemption and multiple runnable image pairs remain missing.
General process lifecycle management	Lifecycle coordination is still inside the single-application FS broker. It needs persistent per-process state and MM-managed creation, execution and termination.
Waiting for live children	Image181 hardware confirms blocking wait and WNOHANG for a cooperatively parked live child; image182 hardware confirms this for grandchildren. General independently scheduled child waits remain missing.
General process trees and orphan adoption	Fork nesting is limited to two levels, with eight retained results per parent. There is no INIT process adopting orphaned children.
General signal delivery	Fault termination works. Image183 hardware confirms bounded self/direct-child SIGKILL/SIGTERM. Image184 hardware confirms SIGTERM masks/pending delivery. Image185 hardware confirms default/ignore disposition. Image186 hardware confirms hello --t. Image187 extends independent default/ignore/mask/pending handling to signals1..15 (hardware pending). General asynchronous delivery, group targeting, handlers, signal return and blocked-I/O interruption remain missing.
Multi-process validation	Still needed: competing CPU-bound applications, independent blocking/wakeup, fairness, exhaustion, and repeated creation/termination without leaks.

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


### Priority1: nested child-first process trees — image177 PROC-TREE

MINIX mm/forkexit.c gives every child its own parent identity and private wait
state; fork can be called by a child. CP32 now supports two nested fork levels
(parent/child/grandchild) while preserving its fixed-address child-first model.
Each MM backing allocation saves the full32KiB image, filesystem record and
previous broker continuation/wait table. On exit, the immediate parent is
restored and receives the child PID. Each parent has eight private result slots;
PID allocation scans saved ancestor tables as well as the active table. A third
fork level returns EAGAIN before allocation. Allocation/FD/slot failures unwind
without damaging the suspended chain. Exiting a child discards its uncollected
already-exited descendant results; general INIT adoption is still missing.

FS saved records form a trusted ancestor chain. Directory removal/replacement
protect every ancestor cwd, and rename validates/rebases them all. Cloning an
active FS record clears its ancestor link. Nested exit/reap accounts descendant
CPU usage once to the immediate parent and ultimately the shell. Existing
child-first policy, no concurrent parent progress, no fault recovery and no
arbitrary-depth tree remain limitations. Priority1 is not complete.

Hardware command: hello --tree twice, then hello --wait, hello --fork,
hello --fork-exec, ps, mm, disk; repeat after cold restart. Update hello via the
updater first. Expect Fork tree OK and Hello exit=0 for each tree invocation.
ABI26 remains unchanged. Image177 hardware acceptance is pending.

Host validation covers two full image restores, private ancestor wait tables,
ancestor PID reservation, depth rejection, nested MM/FD/process-slot rollback,
parent-exit result cleanup, all-ancestor cwd protection, and production broker
reply routing with descendant accounting. Command-entry tests use real io.c
and exercise root, child and grandchild branches through modeled services.
All56 scripts pass. Five clean builds pass ABI/image checks, compiled FS/fork
stack budgets and fsck. Hello text16298/16384 bytes. Normal ELF:
_iram_end=0x4037439c, _iram_ext_end=0x4038c3a4,
_runtime_stack_end=0x3fce74a0 (2912-byte static margin). Other margins:
updater8384, RAM2064, GPIO3856, GPIO updater9328.

To retain heap/stack/image reservations, sdcard.c uses -Os (poll delays remain
CCOUNT-based), and FS compilation omits optional branch-target padding using
-mno-target-align, already used for hello. Peripheral accesses remain volatile
and transport deadlines remain bounded. No board/card flashed here. Record
actual hardware results separately in docs/hardware/pending-tests.md.


[x] Image177 hardware: updater Apps update OK and sync; normal boot passes
hello --tree twice, --wait, --fork, --fork-exec, ps, mm and disk. Cold restart
passes tree twice, wait, fork, ps, mm and disk; fork-exec was not entered in the
cold log. All entered application tests exit0. Shell child totals1/162 and1/78.
Captures: docs/hardware/proc-tree-v177-updater-pass.log, proc-tree-v177-pass.log,
proc-tree-v177-cold-pass.log. Nested fork continuation and repeatability accepted.

### Priority1: MINIX process groups — image178 PROC-GROUP / ABI27

Ported MINIX2 mm/getset.c GETPGRP/SETSID group semantics and forkexit.c group
selection in waitpid. Foreground launches inherit bootstrap shell group1;
setsid sets the caller's group to its PID unless already a group leader (EPERM).
Groups inherit on fork, survive exec, and restore independently with each
parent's saved continuation. Exit records retain group IDs. waitpid(0) selects
only the caller's group; pid<-1 selects that explicit group, -1 still any child.
No matching child returns ECHILD without consuming status. INT_MIN is rejected
before negation. PID allocation excludes retained group IDs as well as PIDs.

CP32 has no controlling-terminal/session object: signals, terminal job control,
setpgid and full POSIX sessions remain missing. Child-first scheduling, two-level
nesting and per-parent eight-result bounds remain. Priority1 remains incomplete.
The next acceptance uses the short hello --tree command: each child starts a new
group; its parent rejects a current-group wait then collects by explicit group.
Existing fork/wait regressions remain in the cumulative checklist. ABI27 requires
updating hello on SD; no hardware acceptance of image178 is claimed.

Image178 validation: five clean variants pass ABI/image checks, ELF placement,
compiled FS/fork stack budgets and fsck. Normal _iram_end=0x4037439c,
_iram_ext_end=0x4038c274, _runtime_stack_end=0x3fce7390 (3184-byte margin).
Other margins: updater8704, RAM2080, GPIO4128, GPIO updater9648. Hello text is
16382/16384 bytes; future acceptance additions will need further factoring or
another application. The optional target-alignment padding is disabled for
application/process/shell C objects to retain the original memory reservations.
Group lifecycle tests cover inheritance, nested creation, EPERM for leaders,
restored parent identity, current/explicit/any-child selection, missing groups,
invalid status pointer retention, INT_MIN and group1-vs-any distinction. ABI
wrapper and production broker tests pass. Hardware acceptance remains pending;
no firmware/card was flashed here.

Final image178 host validation: all56 scripts pass against the final builds.
No hardware result is claimed for the new process-group feature.


[x] Image178 hardware acceptance: two tree/group runs, wait, fork and fork-exec
all exit0; ps has no application rows and shell children0/169. SD root100MiB,
all CORE checks, mm0 and disk0. One boot supplied; no image178 cold restart
claimed. Capture: docs/hardware/proc-group-v178-pass.log.

### Priority1: process accounting API and smaller acceptance app — image179 PROC-TIMES

MINIX2 FS/time.c do_tims and kernel/system.c SYS_TIMES expose five accounting
values. CP32 now provides ABI28 cp32_times: a20-byte copied record of user,
system, child user/system and uptime ticks. The broker selects the actual caller,
locks the snapshot and validates the full writable-image range before copying.
Wrong size/descriptor returns EINVAL, invalid buffer EFAULT, old ABI ENOSYS.
Unsigned32-bit60Hz counters wrap modulo2^32. Child totals accrue on exit and
are not added again by wait; exec/fork accounting policy remains unchanged.
Xtensa flat addressing requires explicit copyout bounds; no MMU or concurrent
application execution is claimed. This completes the accounting exposure slice
of priority1; general concurrent process images, signals, fault recovery and
INIT adoption remain unfinished. Priorities2–4 have not been declared complete.

Removed retired filesystem/allocator/duplicate I/O demonstration code from
shipped hello; retained historical host fixture and production regressions.
Current process tests, exec recovery, line input, PID/PPID/env/exit remain.
Hello text4038 bytes versus16382 (75% smaller); updater embeds the smaller ELF.
New short hardware test: hello --time twice, then existing tree/wait/fork/exec,
ps, mm and disk; repeat after cold restart. Requires apps updater for ABI28.
Hardware acceptance pending; see docs/hardware/pending-tests.md.

Five clean builds pass: normal SD, updater, RAM, GPIO-SPI and GPIO-SPI updater.
Normal ELF _iram_end=0x4037439c, _iram_ext_end=0x4038c308,
_runtime_stack_end=0x3fce7430 (3024-byte static margin). Other margins:
updater21760, RAM5568, GPIO3968, GPIO updater22704. Heap and stack reservations
unchanged. ABI/image checks, ELF inspection, compiled stack budgets and fsck
(clean) pass. Marker [TEST PROC-TIMES 179] immediately precedes idle entry;
FEATURE PROC-TIMES179, ROOT/SD V179 and UPDATER179 identify this image.
No firmware/card flashed here. Full host-suite result is recorded below.

Final image179 validation: make tests passes all57 scripts. Coverage includes
production snapshot locking/ranges/unsigned wrap, runtime ABI/error mapping,
actual hello --time parent/child/timeout paths, and production broker copyout
of restored descendant totals plus non-destructive invalid-buffer rejection.
Historical filesystem and heap demos remain host-tested using the archived
fixture. Five clean builds, ELF/stack checks, fsck and git diff --check pass.
Hardware acceptance of image179 remains pending; no board/card was flashed.


[x] Image179 updater hardware: apps installed hello and sync passed; two
hello --time runs, tree, wait, fork and fork-exec all exit0. ps shows no app
rows, shell children6/175, MM0/disk0, root100MiB and all CORE checks. Capture:
docs/hardware/proc-times-v179-updater-pass.log. No normal-image or cold-restart
result was supplied for179; these remain in the regression checklist.

### Priority1: application fault termination — image180 APP-FAULT

MINIX2 kernel/exception.c routes user faults to signals; mm/signal.c's default
action terminates and forkexit.c reports signal death to wait. CP32 implements
this default-termination lifecycle for supported ESP32-S3 application faults:
EXCCAUSE0/6/9/28/29 -> SIGILL4/SIGFPE8/SIGBUS10/SIGSEGV11. The constants follow
Espressif ESP32-S3 core.h/corebits.h; no ESP-IDF runtime introduced.

Assembly checks application EPC bounds before stack writes, establishes a fresh
stack inside the dying app's fixed slot, and asks the C policy to validate the
active endpoint, owner, runnable state, cause and outer exception state. RFE
enters a reset call0 exit context; it never resumes the faulting instruction.
Normal broker exit cleanup releases files/MM/process state and restores a fork
parent, which collects a low-bit signal wait status. Foreground shell reports
128+signal. Normal exit encoding remains unchanged. The fault record is consumed
before parent restoration to prevent stale reasons affecting later exits.
Diagnostics are normal-path, removable and rate-limited: CTX V180, first4/64th.

ABI28 unchanged. New hello --fault checks four child faults, including invalid
SP, parent memory and inherited file offset; --crash checks foreground SIGILL
(exit132). Update hello via apps; repeat after normal and cold boots. No SD
format or heap/stack reservation changes. Hardware acceptance pending.
Kernel/callback faults, unsupported exceptions, out-of-slot fetch PC, double/
nested exceptions and arbitrary RAM corruption remain terminal. No signal
handlers, kill, memory isolation or concurrent application images are claimed.
Priority1 remains incomplete, before priorities2–4. See pending-tests.md.

Image180 final build validation: five clean variants pass (SD, updater, RAM,
GPIO-SPI and GPIO-SPI updater), including call0/image checks. Normal ELF:
_iram_end=0x403743e0, _iram_ext_end=0x4038c4a8,
_runtime_stack_end=0x3fce7600 (2560-byte static margin). Other margins:
updater20928, RAM4720, GPIO3504, GPIO updater21872. Hello text4374 bytes.
No heap/stack reservation was reduced. Fault classifier/reset uses144 bytes
of the512-byte budget; the conservative exit/IPC chain uses528 of3072 bytes.
Compiled FS/fork stack budgets, ELF sections/segments and fsck(clean) pass.
The linked hello fault instructions were inspected: ILL, null L32I/S32I, and
SP=0 followed by ILL. No hardware execution is claimed. Diagnostic identity:
[FEATURE APP-FAULT 180], [TEST APP-FAULT 180], SD/ROOT V180, UPDATER180 and
rate-limited [CTX V180 fault ...]. No board/card was flashed.

Final image180 host suite: all58 scripts pass against the final five builds.
Tests cover default signal mapping, owner/exception rejection gates, actual
assembly dispatch from both vectors with invalid original SP, full RFE context,
fatal fallbacks, nested fault exit/wait and MM/FD/PID/accounting cleanup, normal
exit preservation, real hello command/runtime paths and bounded stack use.
Hardware validation remains pending; follow docs/hardware/pending-tests.md.


### Image181 PROC-YIELD — cooperative application image exchange (ABI29)

[x] Image180 updater hardware acceptance: three fault-recovery runs, foreground
crash status132, time/tree/wait/fork-exec, ps, MM and disk passed. See
`docs/hardware/app-fault-v180-updater-pass.log`. No normal-image or cold-restart
capture was supplied.

Implemented `cp32_yield()` for one first-level parent/child pair. The FS broker
swaps the complete32KiB fixed-address image with its MM backing while both
processes are blocked receiving FS. Only the restored image receives a reply.
Private stacks, heap breaks, cwd/umask, process groups and wait state survive;
inherited open-file descriptions retain shared offsets. No memory protection
is required or introduced. MINIX forkexit.c supplies the live-child wait and
exit semantics; the fixed-address Xtensa image requires cooperative exchange.

A matching live-child WNOHANG returns0 without changing status. Blocking wait
resumes the child and completes after exit. Parent exec retains the parked child.
Parent exit/fatal termination drains that child before completing parent exit;
a nonterminating child can delay this. This is not INIT orphan adoption.
Nested child-first forks remain supported, but only the first-level pair exchanges.
A further parent fork while a live child is parked returns EAGAIN. Yield without
an eligible peer returns0. Timer preemption, multiple independent runnable pairs,
general process trees, MM-owned lifecycle and signals remain pending in item1.

Host tests cover production image/FS state exchange, private memory and heap,
shared offsets, live waits, exact IPC reply ownership/order, parent exit/fault,
cleanup and real hello yield/exit/exec command paths. Hardware is unverified.
New commands: `hello --yield`, `hello --yexit`, `hello --yexec`.

Five clean builds pass: SD, updater, RAM, GPIO-SPI and GPIO-SPI updater. Normal
ELF: _iram_end=0x403743e0, _iram_ext_end=0x4038c3f0,
_runtime_stack_end=0x3fce7580 (2688-byte static margin). Other margins are
20064,3856,3632,21008 bytes respectively. Read-only boot/process invariant
validators use Os to retain SRAM headroom; scheduler policy is unchanged and
heap/stack reservations are unchanged. ELF sections/segments, compiled stack
budgets and fsck(clean) pass. Marker: [TEST PROC-YIELD 181], feature181,
SD/ROOT V181 and UPDATER181. No board or card was flashed.

Final image181 validation: all59 host test scripts pass against the five clean
builds. Hardware acceptance remains pending; use docs/hardware/pending-tests.md.


### Image182 PROC-YTREE — nested cooperative process exchange

[x] Image181 updater hardware acceptance: apps/sync, three --yield runs,
--yexit, --yexec, ps with no application rows, MM and disk all pass. All CORE
checks pass and the SD root is100MiB. Capture:
`docs/hardware/proc-yield-v181-updater-pass.log`. No normal-image or cold-restart
capture was supplied, and the older regression commands were not in this log.

Image182 extends image181's cooperative exchange to the second supported fork
level. A child and grandchild can exchange private images and perform live waits
while the original parent stays blocked. After the grandchild exits, the child
can yield to its own parent. The broker's existing exit/fault/exec routing applies
at either level. PID/group reservation now scans both the parked child and all
suspended ancestors, without following the child's back-pointer in a cycle.

MINIX2 mm/forkexit.c keeps parent-child wait relationships regardless of tree
level. CP32 preserves those relationships using fixed-address image backing,
not address relocation or memory protection. Both participants must be blocked
receiving FS during exchange; only the restored image receives a reply. FS
ownership, private stacks/heap/cwd, shared file descriptions and accounting remain
intact. No allocation or disk I/O occurs in the locked image swap.

The new `hello --ytree` exercises nested private stack/data/heap and cwd/group
state, shared offsets, live WNOHANG, repeated yields and blocking wait, then
returns execution to the original parent and reaps the child. Expected output:
`Yield pair OK`, `Yield tree OK`, `Hello exit=0`. ABI29 is unchanged, but update
hello on SD. Markers: FEATURE/TEST PROC-YTREE182, SD/ROOT V182, UPDATER182.

This remains a bounded two-level tree with one deepest exchanging pair at a
time. A process with a parked live child yields toward that child, not its own
parent. Third-level forks and another fork by a parent with a parked child are
rejected. Parent exit drains descendants; no INIT adoption, timer-driven app
preemption or general independent process images are added. Plan item1 remains
incomplete. Hardware acceptance of182 is pending; no board/card was flashed.

Image182 final validation: all59 host test scripts pass, including repeated
nested exchanges, ancestor group reservations, live waits, exact broker reply
ownership, grandchild fault cleanup and all three real --ytree app branches.
Five clean builds pass (SD, updater, RAM, GPIO-SPI, GPIO-SPI updater). Normal
ELF: _iram_end=0x403743e0, _iram_ext_end=0x4038c400,
_runtime_stack_end=0x3fce7590; static margin2672 bytes. Other margins respectively:
19808,3600,3616,20752 bytes. No heap/stack reservations changed. Handoff frame
remains288 bytes; all compiled stack-budget checks pass. ELF sections/segments,
fsck(clean) and git diff --check pass. Hardware182 remains unverified.


### Image183 PROC-KILL — bounded default signal delivery (ABI30)

[x] Image182 updater hardware acceptance: application update, two --ytree runs,
--yield, ps with no remaining application rows, MM and disk pass. SD100MiB and
all CORE checks pass. Capture: docs/hardware/proc-ytree-v182-updater-pass.log.
The mistyped --ytre command was an ordinary hello invocation, not an acceptance
test. No normal-image/cold-restart capture or other regressions were supplied.

Implemented cp32_kill(pid,signal), positive PIDs only. Signal0 probes live
applications and the caller's retained, unreaped child results. SIGKILL9 and
SIGTERM15 terminate self or a parked direct child, at either supported fork
level. Other signals, nonpositive PIDs and pointer payloads return EINVAL;
missing targets return ESRCH. Terminating ancestors/other processes or probing
kernel/server slots returns EPERM. This is a bounded lifecycle policy, not full
MINIX UID-based signal permission or group selection. Retained exited children
can be probed but not signalled again; wait consumes their record.

MINIX2 mm/signal.c check_sig supplies the signal0, target selection and no-reply
on self-death behavior; forkexit.c supplies signal-status wait cleanup. CP32's
fixed-address parked child is restored solely for cleanup, never replied to or
made runnable. Parent memory/FS ownership are restored before its kill reply.
Self termination uses the existing exit/drain path; a parent with a live child
still drains that child before completing exit. Thus a nonterminating descendant
can delay parent termination, including SIGKILL: this remains a limitation of
the bounded broker, not general asynchronous signal delivery. No memory
protection, pending masks, handlers, signal return, group delivery or blocked-I/O
interruption is added. Item1 remains incomplete.

New hardware command: hello --kill. It probes self, rejects an invalid target,
terminates two parked children with SIGKILL/SIGTERM, tests child self-SIGTERM,
collects each signal wait status, checks ESRCH after reap, and verifies inherited
shared file offsets survive cleanup. Expected: Kill wait OK and Hello exit=0.
ABI30 requires an updated hello on SD. Existing older hello rejects newer ABI;
run the updater's apps command before application tests. No reformat is needed.
Markers: FEATURE/TEST PROC-KILL183, SD/ROOT V183, UPDATER183.
Hardware183 remains unverified. No board/card was flashed.

Image183 final validation: all60 host scripts pass. Tests exercise production
broker self/direct-child/nested-child termination, no reply to a dead process,
signal wait statuses, retained-child probes, reap/FD/MM/accounting cleanup,
invalid payload/PID/signal rejection, ancestor/service protection and ABI/runtime
command branches. Five clean builds pass: SD, updater, RAM, GPIO-SPI and
GPIO-SPI updater. Normal ELF: _iram_end=0x403743e0,
_iram_ext_end=0x4038c538, _runtime_stack_end=0x3fce76d0; margin2352 bytes.
Other margins respectively19072,2864,3296,20016 bytes. No heap/stack reservations
changed. Compiled stack budgets, ELF sections/segments, fsck(clean) and
git diff --check pass. Hardware183 acceptance remains pending.


### Image184 PROC-MASK — SIGTERM masking and pending delivery (ABI31)

[x] Image183 updater hardware acceptance: apps update, two --kill runs, --ytree,
--yield, ps without application rows, MM and disk pass. All CORE checks pass.
Capture: docs/hardware/proc-kill-v183-updater-pass.log. Normal-image and cold
restart, and regressions not shown in this capture, remain unverified.

Added cp32_sigprocmask(how,set,old) and cp32_sigpending(set). BLOCK, UNBLOCK,
SETMASK and null-set inquiry follow MINIX2 mm/signal.c do_sigprocmask and
do_sigpending. Mask values are transferred by value over IPC, so no application
pointer is dereferenced by the broker. Only SIGTERM has a blocked/pending
implementation; its bit is1<<14. SIGKILL's bit1<<8 is accepted but removed from
the mask. Other bits/operations are rejected with EINVAL without mutation.

Blocked SIGTERM to self or a parked direct child records one coalesced pending
bit and returns success. A child remains live/waitable until delivery. UNBLOCK
or SETMASK exposing pending SIGTERM enters normal signal termination cleanup
without replying to the application. SIGKILL remains immediate within the
existing bounded delivery policy. Each private process state owns mask/pending;
fork inherits the mask and clears child pending, image exchange preserves both,
and exec leaves them intact. A fresh foreground launch clears both. Existing
parent-exit descendant draining remains unchanged.

hello --mask tests mask inheritance, SIGKILL exclusion, repeated TERM requests,
live WNOHANG, pending inquiry and unblocking termination. Host tests additionally
cover KILL bypass and clearing pending signals in forked children. Expected: Signal mask OK,
Hello exit=0. Update hello on SD before tests: ABI31 rejects the previous hello.
Markers: FEATURE/TEST PROC-MASK184, SD/ROOT V184, UPDATER184.

This is not general asynchronous signal delivery: handlers, sigreturn,
sigsuspend, group targeting, signals interrupting blocked I/O, independently
scheduled images and timer-driven application preemption remain pending. No
memory protection was added. Plan item1 remains incomplete. Hardware184 is
unverified; no board/card was flashed.

Image184 final validation: all61 host test scripts pass. Production tests cover
mask inquiry/BLOCK/UNBLOCK/SETMASK, invalid input without mutation, coalesced
self/parked-child TERM, inheritance with pending reset, private image state,
SIGKILL exclusion, no reply on unblocking termination, correct wait status and
resource cleanup. Runtime tests cover version gating and unchanged output on
errors; real hello command tests cover parent/child and legacy-ABI rejection.

Five clean builds pass. Normal ELF: _iram_end=0x403743e0,
_iram_ext_end=0x4038c62c, _runtime_stack_end=0x3fce77e0; margin2080 bytes.
Updater/RAM/GPIO/GPIO-updater margins:18272/2048/3024/19216 bytes. The initial
RAM build was below the2048-byte threshold; compacting only the new hardware
test and keeping it out of line restored the margin. Additional fork/pending
cases remain in host tests. Hello text6966 bytes. No heap/stack reservation or
kernel functionality was removed. Handoff frame remains288 bytes. Compiled
stack checks, ELF section/segment inspection, fsck(clean) and git diff --check
pass. Hardware184 acceptance remains pending.


### Image185 PROC-IGNORE — default/ignore SIGTERM disposition (ABI32)

[x] Image184 updater hardware acceptance: two --mask runs, --kill, --ytree,
ps without application rows, MM and disk pass after updating apps. Capture:
docs/hardware/proc-mask-v184-updater-pass.log. Normal-image/cold-restart and
other regressions not present in this capture remain pending.

Added cp32_signal(signal,disposition), returning the previous disposition or -1.
Only SIGTERM and CP32_SIG_DFL/CP32_SIG_IGN are supported; SIGKILL, other signals,
handler addresses and invalid pointer payloads are rejected with EINVAL without
state mutation. This is a bounded disposition API, not POSIX signal handlers.
Following MINIX2 mm/signal.c do_sigaction, setting IGN discards pending TERM;
subsequent TERM is discarded even when masked. Fork inherits the disposition,
private image exchanges preserve it, exec keeps it, and a fresh foreground
launch resets it. SIGKILL remains unblockable/unignorable. Selecting DFL restores
the existing default termination behavior. General handlers, sigreturn, group
signals, asynchronous delivery and blocked-I/O interruption remain pending.

The hardware command hello --ignore creates pending TERM, discards it by setting
IGN, verifies empty pending state, forks, tests ignored self/direct-child TERM
and live WNOHANG, then execs hello --igexec in the child to check preservation.
The parent waits for normal exit and restores DFL. Expected: Signal ignore OK,
Hello exit=0. --igexec is an internal acceptance helper, not a standalone test
from the shell. Install updated hello: ABI32 requires updating the prior hello.
Markers: FEATURE/TEST PROC-IGNORE185, SD/ROOT V185, UPDATER185.

The broker now shares its existing reply path for signal operations instead of
duplicating send/continue blocks. To retain the RAM image margin, only ordinary
TTY character-processing routines (in_process, echo, rawecho, back_over, reprint,
out_process) use Os. Blocking task entry/resumption, hardware transport, IRQ
assembly, heap and stack reservations remain unchanged. This optimization needs
hardware line editing/echo regression alongside the signal test. No memory
protection or kernel functionality was removed. Item1 remains incomplete;
hardware185 is unverified and no board/card was flashed.

Image185 final validation: all62 host scripts pass, including disposition
validation, pending discard, mask/ignore interaction, private fork state,
SIGKILL bypass, shared signal reply routing, cleanup, runtime ABI gating and
real hello parent/child/exec-entry paths. TTY canonical/read/write regressions
and compiled stack budgets pass. Optimized TTY frames are16–32 bytes.

Five clean builds pass. Normal ELF: _iram_end=0x403743e0,
_iram_ext_end=0x4038c3f0, _runtime_stack_end=0x3fce7590; margin2672 bytes.
Updater/RAM/GPIO/GPIO-updater margins:18384/2160/3616/19328 bytes. Heap and stack
reservations unchanged. ELF sections/segments, fsck(clean), and git diff --check
pass. Tests of exec routing/entry are host modeled; the full on-card exec
inheritance acceptance is hello --ignore on hardware. Hardware185 is pending.


### Image186 APP-TEST — one-command hardware acceptance

[x] Image185 updater capture passes apps/sync, two --ignore runs, --mask, --kill,
--ytree, --time, line input, ps with no applications, MM and disk. Capture:
docs/hardware/proc-ignore-v185-updater-pass.log. The entered line was sdf; this
confirms input/echo but not the requested Backspace sequence. No normal-image
or cold-restart capture was supplied.

Added hello --t. The app explicitly requests the launcher suite; the launcher
loads a fresh foreground image for every case, preserving both fork levels.
A fork-based supervisor would consume one of those levels and invalidate tree
tests. Every case gets fresh BSS, runtime/FS/process state and normal cleanup.
The kernel and MM application lifecycle are unchanged. The same open executable
reader is reused with offset-based reads, then closed on success or failure.

The removable cp32_shell_app_tests cases list is the single place to update for
future feature acceptance. Current order: --ignore twice, --mask, --kill,
--ytree, --yield, --yexit, --yexec, --fault, --time, --tree, --wait, --fork-exec,
--exec-fail. Then ps prints, MM alloc/release and disk checks run automatically.
Each case prints [TEST option]; a load error/nonzero exit/service error stops
immediately with [TEST FAIL] and no all-pass marker. Only complete success
prints [TEST ALL PASS] followed by Hello exit=0. Stale hello without the explicit
request reports update hello, rather than pretending its generic greeting was
a test. Redirection is rejected. run /boot/hello --t is also supported.

Interactive --line remains manual; it cannot be a hands-free check. Foreground
--crash is omitted; --fault covers supported child faults without deliberately
terminating the test invocation. --igexec remains an internal --ignore helper.
ABI32 is unchanged; install updated hello before using --t. Markers:
FEATURE/TEST APP-TEST186, SD/ROOT V186, UPDATER186. No board/card was flashed;
hardware acceptance of the aggregate runner is pending.

To retain the existing RAM recovery margin, removable IPC/MM/TTY trace printers
and ordinary TTY transfer/attribute helpers use Os. Trace content/rate limits,
MM and TTY task entry loops, IRQ assembly and hardware transports are unchanged.
Heap and stack reservations are unchanged. These C helpers retain the same
MINIX line-discipline/transfer semantics; host TTY and compiled stack checks
cover them. Include line editing in hardware acceptance. No functionality was
removed. This is test-workflow work; plan item1 is still incomplete.

Image186 final validation: all62 host scripts pass. Production launcher tests
cover the full14-case sequence, stop at every individual failure, load failure,
stale app detection, MM/disk errors, direct run invocation, redirection rejection
and balanced executable closure. The real hello --t returns the explicit request;
individual application cases retain their existing host coverage. All cases
remain foreground launches rather than an extra application fork.

Five clean builds pass. Normal ELF: _iram_end=0x403743e0,
_iram_ext_end=0x4038c358, _runtime_stack_end=0x3fce75d0; margin2608 bytes.
Updater/RAM/GPIO/GPIO-updater margins:18336/2096/3552/19280 bytes. Launcher frame
is512 bytes, including the inlined runner; compiled FS/exec/fork stack budgets
pass. MM task remains O0 with a64-byte frame; its separate trace is16 bytes.
No heap/stack reservations changed. ELF section/segment inspection, fsck(clean)
and git diff --check pass. Aggregate hardware acceptance remains pending.


### Image187 PROC-SIGNALS — independent MINIX signal sets and USB-only suite

[x] Image186 updater hardware acceptance: apps/sync and the complete hello --t
sequence passed, including all14 application cases, ps with no application rows,
MM and disk, ending [TEST ALL PASS] / Hello exit=0. Capture:
docs/hardware/app-test-v186-updater-pass.log. Normal-image/cold restart remain
unverified; this capture does not add manual line-editing acceptance.

hello --t now suppresses LCD output during application tests, process listing,
service checks and the exit summary. Those messages remain on USB. Buffered
normal output is flushed before suppression; normal routing is restored on
success and failure. The display receives only the final [TEST ALL PASS] or
[TEST FAIL], followed by the normal shell prompt. A failed suite never displays
a pass marker. Standalone application/shell output is unchanged.

ABI33 extends explicit kill/default-ignore/mask/pending handling to MINIX2
signals1..15: HUP, INT, QUIT, ILL, TRAP, ABRT, EMT, FPE, KILL, USR1, SEGV, USR2,
PIPE, ALRM and TERM. SIGBUS10 is MINIX2's historical alias of SIGUSR1; the existing
Xtensa unaligned-fault mapping remains unchanged. Each signal has its own bit,
1U<<(signal-1). Independent ignore dispositions discard only that pending bit;
blocked delivery coalesces only that signal. Unblocking delivers the lowest
numbered pending signal first, matching MINIX2 mm/signal.c check_pending.
SIGKILL cannot be blocked or ignored. Fork/exec/private-image/reset behavior
extends to the full set. Out-of-range bits/signals remain EINVAL.

These are default fatal/ignore dispositions through the existing bounded self/
parked-direct-child delivery policy. No core dumps, user handlers, automatic
alarm/pipe/terminal producers, group selection, stop/continue, asynchronous
blocked-I/O interruption or timer-preempted images are claimed. Hardware faults
still force the supported default termination path rather than returning to a
faulting instruction. Plan item1 remains incomplete.

Added hello --signals at the start of --t. It queues USR1 and USR2 independently,
ignores just USR2, checks the remaining pending bit, unblocks USR1 and verifies
its wait status. Host tests cover default/ignore/mask behavior for every signal
1..15. Expected USB: Signals OK.
Update hello for ABI33 before tests. Markers: FEATURE/TEST PROC-SIGNALS187,
SD/ROOT V187, UPDATER187. No board/card was flashed; hardware187 is pending.

To retain RAM-recovery headroom without reducing heap/stacks, ordinary TTY
request handlers do_read/do_write/do_ioctl, handle_events and tty_init use Os.
Task entry/resumption loops and IRQ/hardware transport code remain unchanged.
Host TTY canonical/read/write/ioctl and compiled stack tests cover these helpers;
manual editing remains a hardware regression check.

Image187 final validation: all64 host scripts pass. Production signal helpers
cover every signal1..15, per-bit masks/ignore, selective pending discard,
coalescing, lowest-numbered delivery, KILL exclusion and fork/reset invariants.
Broker tests verify non-TERM self termination and status138 for SIGUSR1. The real
hello --signals paths are tested. Production output-routing tests verify USB-only
progress/application output, a single final LCD marker and normal output after
the suite; launcher tests cover success, every case failure, stale apps and MM/
disk failures with routing restored. TTY read/write/ioctl and stack tests pass.

Five final clean builds pass. Normal ELF: _iram_end=0x403743e0,
_iram_ext_end=0x4038c23c, _runtime_stack_end=0x3fce74d0; margin2864 bytes.
Updater/RAM/GPIO/GPIO-updater margins:18288/2048/3808/19232 bytes. A small
non-inlined hardware test keeps RAM recovery above its2048-byte threshold;
all-signal cases remain in host coverage. No heap/stack reservations changed.
ELF sections/segments, fsck(clean) and git diff --check pass. Hardware187 remains
pending; use updated apps and hello --t, capturing the USB output.


### Image188 TEST-DOTS — display progress for hello --t

[x] Image187 updater capture reaches [TEST ALL PASS] and Hello exit=0 after
hello --t, including the signals case and MM/disk checks. Capture:
docs/hardware/proc-signals-v187-updater-pass.log. Serial output confirms success;
it cannot independently prove the LCD appearance. No cold-restart capture supplied.

The suite starts a new LCD line and writes one dot immediately after each
successful check:15 application tests plus MM and disk,17 dots currently. ps is
informational and has no dot. Failure stops the dots at the last successful
check. The final pass/fail marker starts on the next line. Detailed progress,
application output and diagnostics remain USB-only. Normal shell output and
prompt resume afterward. The existing TTY DEV_WRITE path owns every dot; no
direct LCD hardware access is introduced. Failed TTY writes use the existing
panic policy. Host tests check exact dot counts, partial failure rows, USB
separation and routing restoration.

ABI33 and hello are unchanged; firmware update alone is sufficient from187.
Markers: FEATURE/TEST TEST-DOTS188, SD/ROOT V188, UPDATER188. Hardware188 pending.
The nonblocking MM source validation/allocator request dispatcher uses Os to
retain RAM recovery headroom. The MM receive/reply task loop remains O0;
allocator semantics, heap and stack reservations are unchanged. This is ordinary
MINIX-style dispatch C, without IRQ-frame or hardware-register responsibilities.

The ordinary TTY open/close/cancel request helpers also use Os to preserve
RAM-recovery margin. TTY task entry/resumption and IRQ code remain unchanged.

Image188 validation: all64 host scripts pass, including exact17-dot success
output, partial dot rows at every application failure, USB-only detail output
and immediate TTY writes for dots. Five clean builds and fsck(clean) pass.
Normal ELF: _iram_end=0x403743e0, _iram_ext_end=0x4038c220,
_runtime_stack_end=0x3fce74b0; margin2896 bytes. Updater/RAM/GPIO/GPIO-updater
margins:18304/2080/3840/19248 bytes. ELF sections/segments, compiled stack checks
and git diff --check pass. No heap/stack reservations reduced. Hardware188
acceptance remains pending; flash normal firmware and run hello --t.
