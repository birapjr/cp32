# CP32 port plan

## Implementation order:

1. Process table & scheduling:	complete the implementation tasks and process can fully run as part of the cp32 kernel like a Minix v2 OS.

| Pending | Current limitation |
|---|---|
| Independent application execution | Applications share fixed execution addresses; suspended images are backed up in RAM. |
| Preemptive application scheduling | Application switching is cooperative within a parent/child family. Multiple independently runnable applications are missing. |
| General process lifecycle | Creation, execution and termination still depend on the foreground FS broker; images195–198 give process records parent PID, heap bounds, groups, signal state, saved-image ownership and retained exit status; full MM-owned lifecycle management remains pending. |
| General process trees | Independent siblings and orphan adoption by an INIT process are missing. |
| General blocking and wakeup | Wait works within the current family model; independently scheduled children and interruptible blocking need integration. |
| Full signal delivery | User handlers, signal-return context restoration, asynchronous delivery and interruption of blocked I/O remain missing. |
| Concurrent validation | Competing applications, scheduling fairness, independent wakeups and resource cleanup under concurrent workloads need testing. |

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
