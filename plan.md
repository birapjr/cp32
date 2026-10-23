# CP32 port plan

Updated 2026-10-11 against the working tree and user hardware results through
**FS-CREATE 126**. **FS-APPEND 127** builds and host-tests persistent append
and file growth; hardware append acceptance is pending. This tracks remaining work, not the
chronological bring-up history. See [issues.md](issues.md),
[hardware captures](docs/hardware/) and [port review](minix.port-status.md)
for implementation history, evidence and detailed MINIX comparisons.

## Current status

CP32 boots bare-metal on Cardputer Adv and runs real scheduled tasks with
blocking IPC. The keyboard/LCD console, USB development workflow, SYSTIMER,
RAM device, owner-tagged memory allocation and read-only MINIX V2 filesystem
are operational. Filesystem support includes directories, path traversal,
metadata, seeking, indirect/double-indirect reads, descriptors, duplication,
a clean cache and selected fcntl/directory-stream operations.

A separately linked Xtensa ELF at `/boot/hello` loads into a dedicated process
slot, receives argc/argv, writes through IPC, exits and returns to the shell.
The image-89 capture confirms both `hello` and `hello um dois`, with correct
arguments and exit=0. These paths are no longer pending implementation or
initial hardware validation. Repeated launch plus disk/MM regression passed
in image 87. SPI2 SD, GPIO SD and recovery RAM builds and all 43 host scripts pass for image 125.

The boundary is still **one trusted foreground application**. The shell is
kernel-linked; there is no general exec/fork/wait server, protected application
ABI, fault containment, per-process FS service, writable filesystem,
full libc or user shell. MM currently dispatches allocation/release requests;
application lifecycle is coordinated by the shell and kernel helper.

The review estimates approximately 35% of broad MINIX functionality and
60–65% of the narrower kernel/platform foundation. These are weighted estimates,
not acceptance criteria or source-line percentages.

## Next milestone: reusable application lifecycle

[ ] 1. Generalize the proven launch path to executable pathnames and a second
independently linked application. Preserve `hello`, argv, bounded loading,
BSS clearing and repeat-launch behavior. Enforce the supported image/permission
policy; return useful errors without losing the prompt.
Acceptance: launch two different applications sequentially, reject missing,
nonexecutable and malformed files, then launch a valid image successfully.
Image 90 hardware confirms sequential hello/echo execution and cwd-relative
lookup; capture: docs/hardware/app-path-v90.log. Missing/nonexecutable rejection
and malformed-image recovery remain host-tested, so this item stays open only
for remaining acceptance checks; implementation is complete.


[ ] 2. Consolidate context installation and process ownership in MM/kernel
interfaces. Replace the fixed hello-specific lifecycle with explicit process
metadata, parent/PID relationships and owned image/stack resources.
Before enabling legacy handlers, fix `system.c:do_fork` writing the child result
to a1 instead of a2; make `do_exec` initialize a1/a15, special registers and
blocked-frame state consistently with the working launcher. Audit the legacy
`proc.c:schedule` entry rather than reusing it unchanged.
Acceptance: fresh/reused slots have consistent frames/maps/queues; failed
creation leaves the parent and existing processes usable.
Image 91 adds shared cp32_exec_frame for the working launcher and SYS_EXEC,
clears legacy exec receive-frame bookkeeping, bounds the process-name copy,
and fixes SYS_FORK's a2 return register. Host tests pass; image 91 hardware confirms repeated hello/echo launches
and disk/MM checks through the shared helper. Image 97 assigns bounded distinct PIDs independently of the reusable slot,
skips active identities, and provides an IPC getpid callback (ABI v2). Host
checks cover repeated launches and PID wrap; image 97 hardware confirms
100/101/103 across hello/echo and disk/MM regression. Image 98 assigns the
kernel-linked shell PID 1 and captures that parent identity for getppid
until exit. Image 98 hardware confirms PPID=1 and PID progression; no
general parent table yet.
General MM metadata, image ownership,
parent/PID lifecycle and full legacy handler integration remain unfinished.

[ ] 3. Add abnormal completion, cancellation and cleanup. Define how an
application fault or nonexiting child is reported; reclaim its resources and
resume its parent. Keep the trusted-execution limitation explicit until an
ESP32-S3 protection design is implemented and validated.
Acceptance: normal/nonzero exit, cancellation and controlled faults leave no
stale queue entries, blocked parent, descriptor leaks or orphaned allocation.

Image 92 adds explicit `hello --exit N` and MINIX-style low-eight-bit exit
status reporting. Host tests cover explicit termination, invalid input, status
normalization and repeated reaping. Image 92 hardware confirms statuses
7/7/255/2, valid relaunch and disk/MM checks (docs/hardware/app-exit-v92.log).
Fault containment, cancellation and general wait are still missing.

[ ] 4. Implement general exec/exit/wait semantics, environment passing and
user heap management. Add fork with an explicit SRAM-compatible memory policy
and correct descriptor inheritance; do not equate fixed-slot launch with fork.
Image 93 wires a fresh HOME/PATH/USER environment into the existing bounded
stack builder and crt0 envp argument. `hello --env` displays it. Mutable shell
environments and general execve inheritance remain unimplemented; hardware
validation of this bootstrap environment passed in image 93
(docs/hardware/app-env-v93.log).
Image 99 adds a bounded per-launch sbrk heap after data/BSS, zeroed growth,
shrink and overflow/stack-boundary rejection. hello --heap exercises it.
Image 99 hardware confirms two successful heap checks. Image 100 adds
application-local cp32_malloc/cp32_free with splitting and coalescing; image
100 hardware confirms repeated malloc/free and heap checks. General MM brk dispatch and dynamic maps remain.
Acceptance: parent/child state, wait status, resource exhaustion and rollback
are tested, including repeated operations and concurrent runnable applications.

## Remaining server and user-environment work

[ ] 5. Add per-process filesystem service ownership and request dispatch:
descriptors, cwd, credentials, open/read/close/seek/stat, terminal descriptors,
inheritance, close-on-exec and exit cleanup. Preserve current V2 read behavior.
Image 117 adds ABI-v6 file requests over IPC, four application-owned handles
separate from shell descriptors, read-only open/read/seek/close, error translation,
and cleanup before launch/on normal exit. Relative paths use the waiting shell's
cwd. The real filesystem and libc/utility paths pass host tests, including
exhaustion, missing/directory operands, indirect reads and leaked-handle cleanup.
Image 117 hardware confirms file access, repeated leak/cleanup checks, cat/wc
multi-file operands and missing-file recovery. Image 118 adds ABI-v7 stat/fstat,
owned directory handles, readdir and rewind through lseek(fd,0,0), with unchanged
outputs on EOF/errors. Metadata-by-path uses no descriptor. Host tests pass;
hardware verification of directory APIs is pending. Concurrent clients,
independent mutable cwd, credential policy and descriptor inheritance remain.
Acceptance: two clients have independent descriptor/cwd state; shared open
file descriptions share offsets only when deliberately inherited/duplicated.

[ ] 6. Implement filesystem mutation: inode/zone allocation, create/write/
truncate, links/unlink/rename, mkdir/rmdir, permissions/timestamps, dirty cache
and sync. Add mount/unmount and a persistent Cardputer block-device backend.
Acceptance: mutate/read/remount checks and error injection preserve filesystem
consistency; reboot persistence is proven on hardware. RAM device writes alone
do not satisfy this item.

[ ] 7. Complete signals, alarms and applicable system services. Audit Xtensa
signal delivery/return frames; integrate MM signal policy, kill/pause and alarm
handling. Connect SYS reboot to the working hardware-reset implementation
instead of `system.c:system_reset`'s loop. Validate map/copy/tracing operations
before exposing them through a general application ABI.
Acceptance: interrupted applications resume correctly, alarms reach the right
process, invalid requests are rejected, and reboot restarts the image.

[ ] 8. Build a reusable application runtime: stable IPC/syscall wrappers,
per-process errno, read/write and file APIs, allocation and stdio. Add pipes,
record locking, needed terminal modes/ioctl and PTYs as their consumers arrive.
Image 100 adds apps/lib/heap.c and heap.h over the ABI-v4 sbrk callback,
with 16-byte alignment, overflow rejection and free-list reuse. This is a
prefixed bootstrap allocator, not yet standard libc symbols. Image 101 adds
cp32_calloc/cp32_realloc with overflow checks and failure preservation;
Image 101 hardware confirms hello --alloc and optimized application ELFs.
Image 102 adds adjacent-block growth and reusable shrink tails; hardware
validation passed in image 102. Image 103 adds explicit free-top-block
release through cp32_heap_trim; image 103 hardware confirms repeated trim,
resize, calloc/realloc, malloc/free and disk/MM regression checks.
Image 104 adds a bounded foreground console read callback (ABI v5), using
TTY canonical input and FS staging while the child blocks. hello --read
passed on image 104, including empty input and user-confirmed Backspace.
Image 105 adds cp32_read/cp32_write standard-stream wrappers with chunked
output and short-transfer handling. Image 105 hardware confirms input and
user-verified Backspace editing. Image 106 adds cp32_readline with bounded
NUL termination and fragment continuation, validated on image 106; general fd-based file
I/O and FILE/stdio buffering remain unfinished. Image 113 adds application-local
cp32_errno for standard-stream wrappers; allocator/general syscall errno is
still missing. Hardware confirmation of hello --errno passed in images 113 and 114.
Image 115 adds caller-owned buffered stdin with getc/fgets and EOF/error
indicators; wc uses it. Image 115 hardware confirms all three counting modes,
errno and disk/MM checks. Image 116 adds caller-owned stdout/stderr buffering,
explicit flush and retry without duplicating a successfully written prefix.
Image 116 hardware confirms flushing and regression checks. Image 117 adds
application file descriptors and buffered input from those descriptors. Full
FILE APIs and automatic exit flushing remain unfinished.
Acceptance: independently built programs use documented services rather than
hello-specific kernel callbacks; blocking/error/short-I/O behavior is tested.

[ ] 9. Port a user shell and utilities, followed by init/login/session setup.
Support external commands, pipes, redirection and scripts once the required
process and FS APIs exist. Existing kernel builtins remain regression tools;
they are not substitutes for independently executable MINIX commands.
Image 94 adds external-command dispatch to the kernel shell: builtin priority,
fixed /boot lookup for bare names, and slash-containing paths resolved against
cwd. Explicit run keeps its previous semantics. This is not yet a user shell
or a configurable PATH implementation. Image 94 hardware passed direct and
relative launch, unknown-command recovery and disk/MM checks. Image 95 adds
bounded quote removal for application arguments (including empty strings),
escapes and syntax rejection before opening an executable; image 95 hardware
confirms these paths and regression checks. Image 96 fixes the three missing
LCD quote/backslash glyphs; user confirmed all font samples look correct
on image 96.
Image 107 adds a third independent ELF, /boot/cat, using shared standard
stream wrappers to copy stdin until EOF. Image 107 confirms repeated EOF
return. Image 108 adds shell-owned trailing input redirection; hardware
validation passed on image 108. Image 109 fixes the missing LCD < glyph;
visual confirmation pending.
Image 117 supplies file arguments through application FS interfaces.
Image 112 adds standalone /boot/wc with stdin counting and -lwc selection,
using the shared I/O library and input redirection. Image 112 hardware passed.
Image 117 adds standalone cat/wc filename operands and wc aggregate totals;
image 117 hardware confirms those paths. Image 118 adds standalone ls with
-a/-l/-d, multiple operands and failure recovery, through the new directory and
metadata APIs. It streams disk order; sorting, recursion, timestamps, pipelines
and a user shell remain unfinished.
Acceptance: boot into a user environment and run scripts and applicable MINIX
user tests through the public application interfaces.

[ ] 10. Add networking and remaining applicable device services after the
core interfaces stabilize. Port protocol behavior and choose Cardputer-specific
drivers. PC BIOS/PIC/PIT/VGA/floppy implementations are not CP32 requirements;
ESP-IDF runtime migration and SMP are not implied by this plan.

## Outstanding validation and maintenance

[ ] Stress scheduling/IPC with concurrent applications, long register-pressure
workloads, resource exhaustion and nested interrupts. Include any CPU extension
state required by the supported application compiler configuration.

[ ] Exercise remaining negative/boundary paths on hardware as their subsystems
change: loader failures and maximum arguments; directory/descriptor exhaustion,
fcntl/seek and numeric boundaries; terminal raw/canonical modes, cancellation
and backpressure. Do not reopen implemented features solely because an older
image's checklist lacks a later result. Host-only evidence remains distinct.

[x] LCD font samples visually confirmed by the user on image 96, including
quote/backslash corrections and the existing exclamation sample.

[ ] Update stale repository guidance/comments as the corresponding areas are
touched: obsolete IPC/frame descriptions, the hello ABI comment claiming its
bridge is absent, and legacy bring-up gates. Keep removable traces isolated
and do not remove working functionality during cleanup.

## Development and completion rules

- Compare each feature with `minix-2.0.0`; preserve its portable invariant
  while adapting architecture-specific mechanisms to Xtensa/ESP32-S3.
- Keep call0, freestanding execution and current boot/IPC/console behavior.
  Respect shared D/IRAM aliases and the full runtime-stack reservation.
  Latest inspected endpoints: `_iram_end=0x403743f8`,
  `_iram_ext_end=0x4038a3a4`, `_runtime_stack_end=0x3fce4bd0`;
  the application code's DRAM alias starts at `0x3fce8000`.
- For implementation changes, add meaningful tests, run `make clean && make`
  and `make tests` in `src/`, then inspect relevant ELF sections/segments and
  symbols. Current image identity is 127; increment feature/test markers for
  each new image, with the test marker immediately before the idle call.
- Record build and hardware results separately in `issues.md` and this plan.
  Mark completed items `[x]` only when their stated acceptance criteria pass;
  describe remaining hardware checks explicitly. Remove closed tasks during
  later compaction once their evidence is retained in the history.
- Leave flashing to the user. Never infer hardware success from compilation,
  a host model, or the presence of a function name.

Image 114 removes the seven-block application fixture limit using shared
zones and single-indirect tables. Host coverage includes larger ELF loading
and aggregate disk exhaustion; image 114 hardware regression passed. The 64 KiB
disk has 33 application zones including indirect tables. Runtime-stack end
0x3fce6f40 leaves 4288 bytes before the application data alias; larger embedded
files still consume firmware SRAM and must pass linker placement checks.

Image 117 compacts embedded disk runs across short zero gaps and links applications
with section garbage collection. All reconstructed disk bytes and required ELF
segments are checked. Heap, stack and application-slot reservations are unchanged.
Next substantial milestone: lifecycle ownership in MM, or writable filesystem
services with explicit recovery tests; avoid treating the foreground file bridge
as a completed multi-process filesystem server.


[x] Foreground exec and keyboard acceptance completed on image 122. The user
capture docs/hardware/app-exec-v122.log confirms failure recovery, two
PID/file-preserving exec calls, exec-to-echo, MM and disk checks, and one clean
startup prompt. Image 120's heap-alignment crash and image 121's stale-input
bug are fixed. Earlier image-specific pending checks are superseded by this
capture. General MM lifecycle, fork, mutable environments and fault isolation
remain unfinished. See issues.md for the failure history and fixes.

## Current direction: SD-backed root and reclaimed memory

[x] Image 122 hardware confirms one clean startup prompt (fifo=0), exec error
recovery, two successful PID/file-preserving replacements, exec-to-echo, and
successful MM/disk checks. Evidence: docs/hardware/app-exec-v122.log. This closes
the specific image-120/121 crash and stale-keyboard acceptance checks; general
fork/MM lifecycle, mutable environments and fault containment remain open.

[x] Image 123 basic SD-root hardware acceptance: user capture
`docs/hardware/sd-root-v123.log` proves mounting (capacity=65536), ls, exec
failure recovery, two PID/file-preserving replacements, exec-to-echo, MM and
disk checks. Separate no-card log: ROOT=-2, capacity=0, responsive shell.
Bad-volume behavior, card model/capacity and the full command matrix remain
unverified on hardware; do not infer them from the host suite.

[x] Image 123 build-only memory acceptance: 86,632 SRAM bytes recovered net
versus image 122. Assign 64 KiB to MM: SD heap reservation 192 KiB, whole-page
allocator 188 KiB. Keep stacks/application slot unchanged. Remaining placement
margin 21,968 bytes; _runtime_stack_end=0x3fce2a30. Both ELF layouts pass.

[ ] Follow SD read/exec hardware acceptance with hardware SPI throughput, then
SD sector writes and MINIX file/zone allocation, dirty-buffer ordering, sync
and interrupted-write recovery. Read-only SD storage is persistent but is not
a completed writable filesystem. Default disk diagnostic never writes media.

[x] Image 124 build/host acceptance: hardware SPI2 SD transport, 400kHz init
then 4MHz reads, 64-byte FIFO bursts, bounded UPDATE/USR waits, CRC/error
propagation and GPIO recovery build. MINIX block IPC/read-only FS/exec path
and 192KiB SD heap preserved. All three clean builds and 43 test scripts pass.
SPI2 endpoints: _iram_end=0x403743e8, _iram_ext_end=0x40388aa0,
_heap_start=0x3fc9fee8, _heap_end=0x3fccfee8,
_runtime_stack_end=0x3fce2ef0; remaining app-alias margin 20,752 bytes.

[x] Image 124 basic read hardware acceptance: [FEATURE SD-SPI 124]1 / [TEST SD-SPI 124],
[SD V124 transport=spi2 init-hz=400000 read-hz=4000000], ROOT V124 result=0.
User log confirms mount, ls, two exec replacements, disk and MM; see
`docs/hardware/sd-spi-v124-excerpt.log`. User reports faster access; no timing
benchmark supplied. No-card SPI2/bad-volume/card-variation tests remain open.

[x] Image 125 build/host milestone: synchronous CMD24 writes with CRC16,
bounded busy wait, CMD13 status, full-sector readback and error lockout;
DEV_WRITE IPC and privileged `patch path offset text` operate end-to-end on
existing allocated non-executable file data. 1–64 bytes, one sector, no EOF
growth; metadata remains untouched. Existing card image is compatible.
All 43 host scripts and three clean builds pass. SD heap remains 192KiB,
whole-page allocation 188KiB; SD app-alias margin is 17,744 bytes. RAM recovery
has 416 bytes remaining and excludes the SD-only patch command.

[ ] Image 125 hardware write acceptance: [FEATURE SD-WRITE 125]1 /
[TEST SD-WRITE 125]. Run `patch /readme 0 SD25`, read both hard links, reboot
and reread, then `patch /readme 0 CP32` and exec/disk/MM regression. See
`docs/sd-root.md`. No board/card writes performed by the agent. A failed write
may have changed media; no automatic retry or power-loss atomicity is claimed.

[ ] Next: writable file lifecycle (allocation/create/growth/truncate, inode
updates, dirty-buffer ordering/sync and interrupted-write recovery). The
maintenance patch is not the general MINIX/POSIX write implementation.

[x] Image 125 persistence/restoration hardware evidence retained in
`docs/hardware/sd-write-v125.log`: startup reads SD25, patch CP32 verifies,
and reread matches. Initial write/exec regression is absent from this capture.
User confirms the early restart was intentional to test SD persistence; closed.

[x] Image 126 build/host acceptance: `create /name text`, root only, 14-byte
names/64-byte initial content, inode and zone allocation, zeroing, ordered
inode-before-directory publication, duplicate/no-space rejection. Both byte
orders and every complete-write interruption boundary tested. All 44 host
scripts and three clean builds pass. SD heap 192 KiB preserved; app-alias
margin15,248 bytes. RAM keeps416 bytes and omits the SD-only allocator object.

[ ] Image 126 hardware acceptance: [FEATURE FS-CREATE 126]1 /
[TEST FS-CREATE 126], ROOT/SD V126. Create /note, ls/cat/stat, reboot/reread,
duplicate rejection, patch, exec/disk/MM. Existing card remains compatible.
No flashing performed. User clarified the earlier reset was intentional.

[ ] Next: growth/truncation/deletion, nested creation, timestamps, application
write syscalls, allocation reclamation and interrupted/torn-write recovery.
Creation may leak reservations after I/O failure; no crash-atomicity claim.

[x] Image 126 basic creation hardware acceptance: docs/hardware/fs-create-v126.log
shows create /note, ls, cat, stat (inode 13, size 8, links 1). Reboot/duplicate and
exec regression are not in this capture; keep those acceptance items open.
[x] Early-reset investigation closed: user intentionally restarted hardware
while validating SD persistence. It was not a reported spontaneous crash.

[x] Image 127 build/host milestone: `append path text`, 1–64 bytes per call,
existing non-executable files, direct/single-indirect growth through 263 KiB,
zeroed zone/table allocation and data-before-size publication. All 44 scripts
and three clean builds pass. SD 192 KiB heap preserved, margin 13,360 bytes;
RAM remains 416 bytes. Endpoints recorded in docs/sd-root.md and issues.md.

[ ] Image 127 hardware acceptance: [FEATURE FS-APPEND 127]1 /
[TEST FS-APPEND 127], ROOT/SD V127. Append ! to /note, cat/stat, reboot/reread,
then repeated exec/disk/MM. No flashing or media writes performed here.
[ ] Next: truncate/delete and safe zone reclamation, then writable application
handles/timestamps/recovery. Failed append can leak reservations or leave an
unused indirect entry; complete-write fault tests do not establish torn-sector
or power-loss atomicity.


[x] Image 127 persistence evidence: docs/hardware/fs-append-v127.log contains
 two boots and reads hello SD! from /note after restart. Append/stat commands
 and the full exec/disk/MM regression are absent; those checks remain open.

[x] Image 128 build/host milestone: shrink-only `truncate path length`,
 direct/single-indirect reclamation, partial-tail zeroing and released-zone
 reuse. MINIX link.c/super.c behavior adapted to synchronous detach-before-free
 ordering. Both byte orders and every complete-write failure boundary tested.
 All 44 scripts and three clean builds pass; ELF placement checked. SD heap
 remains 192 KiB, app-alias margin 9,808 bytes; RAM recovery margin 400 bytes.

[ ] Image 128 hardware acceptance: [FEATURE FS-TRUNCATE 128]1 /
[TEST FS-TRUNCATE 128], ROOT/SD V128. Create /trim, shrink to 5, cat/stat,
restart/readback, truncate to zero, append reused, restart/readback, then
repeated exec/disk/MM. See docs/sd-root.md. No board/card writes performed.
[ ] Next: deletion and inode reclamation, application write handles, nested
creation, timestamps and recovery. Failed mutations can leak unreachable
allocations; torn-sector/power-loss atomicity is not established.


[x] Image 128 basic hardware acceptance: docs/hardware/fs-truncate-v128.log
shows truncate /trim 0, empty readback, append reused, relative-path readback,
and successful exec/disk/MM. Partial-shrink and post-mutation reboot readback
are absent; those checks remain open. Startup restart cause is not inferred.

[x] Image 129 build/host milestone: MINIX hard links via `link existing /alias`,
shared inode/data, root-only destination, no allocation, link count persisted
before directory publication. Both byte orders, shared mutations and every
complete-write interruption boundary pass. All 44 host scripts and clean
SPI2/GPIO/RAM builds pass; ELF sections/segments checked. SD heap 192 KiB,
app-alias margin 8,112 bytes; RAM margin 416 bytes.

[ ] Image 129 hardware acceptance: [FEATURE FS-LINK 129]1 / [TEST FS-LINK 129],
ROOT/SD V129. Link /trim /trimlink, compare inode/link counts, append through
alias, read original, restart/readback, duplicate rejection, repeated exec,
disk and MM. No board/card writes performed. See docs/sd-root.md.
[ ] Next: unlink and last-link inode reclamation, then application write ABI
and recovery. Interrupted linking can overcount links; no crash atomicity.


[x] Image 129 hardware identity/persistence: docs/hardware/fs-link-v129.log
shows /trim and /trimlink with inode14, links2, size7, reused! after boot;
ls/exec/disk/MM pass. Original mutation and duplicate-rejection commands are
not shown, so their complete acceptance matrix remains open.

[x] Image 130 build/host milestone: `rm path` / `unlink path`, shared-link
preservation and last-link inode/data/indirect reclamation. Nested one-block
parents supported; open descriptors rejected pending deferred-last-close port.
MINIX unlink_file/put_inode/free_inode ordering adapted to synchronous writes.
All 44 scripts and clean SPI2/GPIO/RAM builds pass; both byte orders, every
complete-write interruption and remount/reuse checked. SD heap192KiB unchanged;
app-alias margin6,016 bytes, RAM416 bytes. ELF sections/segments pass.

[ ] Image 130 hardware acceptance: [FEATURE FS-UNLINK 130]1 /
[TEST FS-UNLINK 130], ROOT/SD V130. Disposable /deltest and /delalias: remove
one name, restart and check survivor, remove final name, recreate and verify
reused inode/content after restart. Repeated exec/disk/MM and directory/missing
name rejection. See docs/sd-root.md. No board/card writes performed.
[ ] Remaining: deferred open-unlinked inode lifetime, larger directory and
mapping support, rmdir, writable application ABI, timestamps and recovery.
Interrupted deletion can leak unreachable allocations; no torn-write guarantee.


[x] README refreshed for current persistent SD filesystem, six applications,
exec, storage diagnostics, mutation command table, examples and limitations.
Removed stale read-only-RAM/current-image118 claims; hardware vs host validation
is explicit. Documentation clean build completed before image131 work.

[x] Image131 build/host milestone: `mv oldpath newpath` / `rename`, regular-file
same/cross-directory moves with inode/open-handle/hard-link identity preserved.
Same-parent entry rewrite; cross-parent temporary counted link, destination
publication before source removal. No replacement of different target inode.
All44 scripts and clean SPI2/GPIO/RAM builds pass; both byte orders and all
complete-write failure boundaries, SD-mock remount and ELF placement checked.
SD heap192KiB preserved, app-alias margin3,456 bytes; RAM416 bytes.

[ ] Image131 hardware acceptance: [FEATURE FS-RENAME 131]1 /
[TEST FS-RENAME 131], ROOT/SD V131. Rename disposable linked file within root,
move into /boot, restart/readback, check old-name absence and unchanged inode/
links, same-inode no-op and destination collision, cleanup then exec/disk/MM.
No board/card writes; see docs/sd-root.md. Image130 deletion hardware remains
pending too. Failed moves can leave both names/overcounts; no crash atomicity.
[ ] Further namespace work: directories and larger parents, replacement rename,
application mutation ABI, timestamps, deferred unlink lifetime and recovery.
Account for only3,456 bytes of remaining SD placement margin before expansion.


[x] Image131 basic hardware evidence retained in docs/hardware/fs-rename-v131.log:
create /sample aaa, mv /sample /renamed, listing; subsequent boot lists and reads
/boot/moved as aaa. Cross-directory command, inode/link comparison and full
regression matrix are absent, so those checks remain open.

[x] Image132 build/host milestone: persistent numeric `chmod mode path` for
files/directories, shared inode/fstat visibility, execute-policy integration.
MINIX protect.c:do_chmod type/mode behavior preserved; credentials/ctime unported.
All44 host scripts and clean SPI2/GPIO/RAM builds pass; both byte orders,
non-mode-byte preservation, parser/errors, complete-write faults and SD-mock
remount tested. App exec denies cleared execute bits before MM staging and
preserves the running image. ELF sections/segments pass. Heap192KiB unchanged,
SD app-alias margin2,480 bytes; RAM416 bytes.

[ ] Image132 hardware acceptance: [FEATURE FS-CHMOD 132]1 / [TEST FS-CHMOD 132],
ROOT/SD V132. chmod through alias, long-list/readback after restart; clear echo
execute bits and check denied launch, restore555 and rerun, repeated exec/disk/MM.
See docs/sd-root.md. No board/card writes performed. Image130/131 full hardware
matrices remain pending. Numeric rwx metadata is not full access enforcement.
[ ] Before larger FS features, address limited placement headroom without
reducing the preserved MM heap or relying on undocumented memory behavior.


[x] Image132 basic chmod hardware evidence: docs/hardware/fs-chmod-v132.log
shows chmod644 /boot/echo, denied run, chmod555 restoration and successful run.
Reboot persistence and shared-alias mode acceptance remain unverified.

[x] Image133 build/host cursor milestone: solid10x10 white square overlay,
on/off every30 uptime ticks (half-second at60Hz), visible only for pending
console reads with ECHO. TTY task updates pixels after input transfer/reply;
clock only posts existing notifications. Restore underlying painted cell before
output, editing, deferred Enter and clear; do not alter text or wrap state.
MINIX console.c:flush cursor/text separation adapted to software LCD overlay.
All44 scripts and clean SPI2/GPIO/RAM builds pass; pixel and TTY models verify
blink timing, tick wrap, restoration, backspace, wrapping, scrolling, batching,
clear, faults and pending-read gating. ELF placement checked. SD heap192KiB,
margin2,400 bytes; RAM margin320 bytes.

[ ] Image133 visual hardware acceptance: [FEATURE TTY-CURSOR 133]1 /
[TEST TTY-CURSOR 133], ROOT/SD V133. Idle prompt blinks; type/edit across a line,
backspace, press Enter and scroll using ls; no ghost squares or extra lines.
Try hello --line for application input. Verify cursor hides on completion and
returns at prompt; no squares in command text/USB log. No flashing performed.


[x] Image133 basic hardware acceptance: user confirms blinking cursor visually
works; hello --line input/exit and subsequent pwd pass in supplied log.
Retained excerpt: docs/hardware/tty-cursor-v133-excerpt.log. Exhaustive visual
wrap/scroll cases are not established by serial text alone.

[x] Image134 build/host milestone: create in existing subdirectories, absolute
and cwd-relative paths, correct parent-only directory size update. Shared
mutation_location resolver with rename preserves initialized-inode-before-name
publication and reduces duplicate code. Root helper compatibility preserved.
All44 scripts and clean SPI2/GPIO/RAM builds pass; both endians, remount and
nested lifecycle, rejected paths/full parent, reuse and complete-write faults
checked. ELF sections/segments pass; heap192KiB, SD margin2,304 bytes, RAM320.

[ ] Image134 hardware acceptance: [FEATURE FS-CREATE-PATH 134]1 /
[TEST FS-CREATE-PATH 134], ROOT/SD V134. cd /boot/create nested/read/stat,
restart/readback, duplicate/missing-parent rejection, append/rename/delete/
recreate, repeated exec/disk/MM. See docs/sd-root.md. No board/card writes.
[ ] Next namespace work remains directory creation/removal and larger parents;
credentials, timestamps and recovery remain unported. Account for tight SRAM
headroom before further resident expansion; do not shrink the MM heap silently.


[x] Image134 basic hardware lifecycle: docs/hardware/fs-create-path-v134.log
shows nested relative create/read/stat, same-parent rename, cross-parent move,
read/delete and successful exec. Reboot/duplicate/fault matrix remains open.

[x] Image135 build/host milestone: mkdir under existing parent, mode0755,
initialized dot/dot-dot, links2 and counted parent link before publication.
Shared create allocator preserves verified inode/data-before-name ordering.
Both byte orders, nested traversal/create, slots/space/limit rejection and
all24 complete-write interruption boundaries tested. All44 scripts and clean
SPI2/GPIO/RAM builds pass; ELF placement checked. SD heap192KiB preserved,
app-alias margin1,232 bytes; RAM336 bytes. No new static buffer/hardware logic.

[ ] Image135 hardware acceptance: [FEATURE FS-MKDIR 135]1 / [TEST FS-MKDIR 135],
ROOT/SD V135. mkdir /work and sub, create note, cd .., verify directory modes
and counts, restart/read/traverse, duplicate/missing rejection, exec/disk/MM.
See docs/sd-root.md; no board/card writes performed. rmdir/directory rename,
parent block growth, credentials, timestamps and recovery remain unported.
[ ] Address resident SRAM headroom before adding larger filesystem features;
retain the192KiB heap and validate both executable aliases and IRAM endpoints.


[x] Image135 basic hardware acceptance: user reports mkdir works and attributes
capture garbling to their own handling. docs/hardware/fs-mkdir-v135.log reads
/work/sub/note as hello sd after boot. Do not infer a kernel fault from garbling.

[x] Image136 build/host milestone: empty-directory rmdir with root/dot/cwd/open
protection, validated dot identities, parent link decrement and inode/zone
reclamation. Shared remove_node and parent resolver preserve unlink behavior
and name/inode detachment before storage reuse. Both byte orders, nonempty/
tombstone/corrupt/protected cases, reuse and all five write failures tested.
All44 scripts and clean SPI2/GPIO/RAM builds pass; ELF sections/segments checked.
Heap192KiB preserved; SD placement margin608 bytes, RAM336 bytes.

[ ] Image136 hardware acceptance: [FEATURE FS-RMDIR 136]1 / [TEST FS-RMDIR 136],
ROOT/SD V136. Reject nonempty and cwd, remove emptied subdir, check parent links2,
restart/absence, remove parent/recreate/remove, exec/disk/MM. See docs/sd-root.md.
No board/card writes performed; no recursive removal or recovery claim.
[ ] Before further substantial features, recover resident memory headroom:
only608 bytes remain before the application alias. Preserve heap/stacks and
validate all three builds and linker endpoints when changing placement.


[x] Image136 basic hardware acceptance: retained fs-rmdir-v136.log confirms
nonempty rejection, links3 after child creation and child/parent removal.
Remaining cwd/open, reuse, reboot and exec/disk/MM hardware matrix stays open.
[x] Image137 build/host milestone: recovered resident placement headroom using
FS-only -Os and independently collectable extended-IRAM function sections.
All44 scripts pass, mutation tests now run optimized with UBSan; three clean
builds and ELF sections/segments checked. SD margin6288, RAM3760, GPIO7360;
heap/stacks unchanged. This completes build-side headroom recovery above.
[ ] Image137 hardware regression: FS-MEMORY137 markers, cursor, FS lifecycle,
restart absence, exec/disk/MM per docs/sd-root.md. No hardware/card writes made.

[x] Image137 hardware: fs-memory-v137.log confirms directory lifecycle, correct
rm-directory rejection, SD/MM and exec; restart persistence remains unverified.
[x] Image138 build/host: nested and cwd-relative hard-link destinations through
shared parent resolution; compatibility root helper retained. Both byte orders,
write faults, tombstone reuse and SD remount after source removal tested.
All44 scripts and clean SPI2/GPIO/RAM builds pass; ELF placement inspected.
[ ] Image138 hardware: FS-LINK-PATH138 / ROOT/SD V138, shared inode/count,
cwd-relative alias, original removal, restart/readback, rejection and cleanup.

[x] Image138 hardware post-boot readback: fs-link-path-v138.log shows alias
content shared; creation/deletion/count/rejection sequence is not in this log.
[x] Image139 build/host: cross-sector/block patches with two-chunk preflight,
unchanged metadata and explicit partial I/O error behavior. Both byte orders,
noncontiguous/direct-indirect boundaries, failure injection and SD remount tested.
All44 scripts and three clean builds pass; heap/stacks unchanged, SD margin6256.
[ ] Image139 hardware: FS-PATCH-SPAN139, offset510 overwrite/readback/reboot,
unchanged size, cleanup and exec/disk/MM per docs/sd-root.md.

[x] Image139 partial hardware acceptance: fs-patch-span-v139.log confirms
169-byte readback, deletion and exec/SD/MM. Cross-sector check remains open.
[x] Image140 build/host: bounded independent cp, empty/nested files, ordinary
mode preservation after copy and no overwrite. Both byte orders, independent
patching, fault injection and SD remount after source deletion tested. All44
scripts and clean SPI2/GPIO/RAM builds pass; sections/segments inspected.
[ ] Image140 hardware: FS-COPY140 / ROOT/SD V140. Short create/cp/patch/cat
sequence, existing-destination refusal, reboot persistence, removal and exec/MM.

[x] Image141 source/host fix: image140 syscall halt maps to IPC dispatch/return
rejection. Removed invalid nonzero-a15 runtime invariant; compiler/disassembly
uses zero legitimately. Production frame validator and zero-a15 IPC/scheduler
regressions pass, including invalid PC/SP cases. All44 scripts and three clean
builds pass, ELF placement checked. Assembly and heap/stacks unchanged.
[ ] Image141 hardware: CTX-A15 141, repeat existing cat a/cat b/ls, then exec,
disk/MM; log140 lacked saved a15, so hardware confirmation remains required.

[x] Image141 observed hardware acceptance: ctx-a15-v141.log repeats cat hi/yo,
cleans files/directories and passes SD/MM without an exception. Exec not shown.
[x] Image142 source/host milestone: zero-data-zone empty create and copy,
first-append allocation; both byte orders, full zone bitmap, write failures and
SD remount covered. Clean SPI2/GPIO/RAM builds; heap/stacks unchanged.
[ ] Image142 hardware: FS-EMPTY142 create e/stat/restart/append/read/duplicate/
remove, optional empty copy, then exec/disk/MM. See docs/sd-root.md.

Image142 validation: all44 scripts pass (43 in the full run, then the corrected
empty-file allocation assertion suite rerun successfully). All three clean
builds pass without warnings; ELF sections/segments inspected.


## Image143 — writable application descriptors

Image142 hardware output confirms empty create, stat, append, read and removal.
Image143 APP-WRITE adds application ABI v9 open/create/exclusive/truncate/append,
regular-file write, dup, shared offsets, inode refresh across independent opens,
sparse and single-indirect allocation, errno/short-write reporting and exec/exit
handle lifetime. MINIX access/open-description semantics are retained within
CP32's four-handle trusted application and existing 263 KiB mutation limits.
No new heap, stack, MMU or peripheral behavior is assumed. Synchronous SD
readback and write-disable-on-fault remain; no crash-recovery guarantee is added.

Implementation and host coverage are present; hardware acceptance is pending,
so this milestone is not marked complete. Install matching firmware and SD
`/boot/hello`, run `hello --file`, fully restart, then `hello --check` and
`rm wtest`. See README for expected output and existing-file handling.

Image143 validation: all44 host test scripts pass, including ABI request bounds,
production hello write/reboot-check paths, writable duplicate handles across
exec, SD-driver remount, little/big-endian allocation and injected write faults.
Clean default SPI2, GPIO-SPI and RAM-recovery cross-builds pass image-layout
checks. Default ELF inspection: `_iram_end=0x40374620`,
`_iram_ext_end=0x4038c56c`, `_runtime_stack_end=0x3fce7530`; 2768 bytes remain
before the application alias boundary. Heap remains192 KiB. The host headroom
floor is now2 KiB (previously4 KiB); the linker boundary is unchanged.
Hardware not flashed or tested by this work; acceptance remains pending.


## Image144 — correct SD application-open selection

Hardware capture `docs/hardware/app-write-v143.log` confirms image143 boots and
reads SD directories, but `hello --file` exits20 without creating `/wtest`;
`hello --check` then exits31. No exception appears. Exit20 was previously
misdocumented as only EEXIST; it covers any initial-open failure.

Root cause: rootdisk.h defines CP32_ROOT_RAM=0 for SD, while the new shell
bridge used #ifndef, supplying no writer and returning EROFS. Use numeric
#if conditions throughout the new bridge/descriptor code. This preserves
MINIX common_open's writable-create semantics and CP32's read-only recovery
policy, without changing Xtensa ABI, memory bounds or peripheral behavior.
Host application/exec tests now compile with explicit CP32_ROOT_RAM=0, matching
the production setting; error-message tests cover existing and missing files.
Hello reports both stage and errno on failure. Image marker APP-WRITE144,
ROOT/SD V144. Hardware revalidation remains pending; run `hello --file`, reboot,
`hello --check`, `rm wtest`, `hello --exec` with matching firmware/application.

Image144 validation: all44 host test scripts pass, including the explicit-zero
SD configuration and failure diagnostics. Clean SPI2, software-SPI and RAM
builds pass with no compiler warnings; ELF/image layout checks pass.
`_iram_end=0x40374620`, `_iram_ext_end=0x4038c570`,
`_runtime_stack_end=0x3fce7530` (2768-byte margin). No hardware flash or card
write was performed; corrected hardware behavior is pending user validation.


## Image145 — optional embedded application updater

`make updater` / `make flash-updater` select UPDATE_APPS=1 and build-updater;
normal images exclude the `apps` command, updater object and generated payload.
Six built ELF applications are embedded in internal DRAM. The temporary build
uses a160 KiB heap rather than192 KiB; all stacks and application bounds remain.
The updater reuses MINIX open/truncate/write/chmod behavior and CP32's serialized
FS/verified SD writes. No new hardware access or flash mapping is introduced.
It rejects directory/hardlink targets, skips identical files, clears execute
permission during in-place replacement and restores0555 after complete readback.
Failure stops the batch; no atomic replacement or rollback is claimed. Reboot
before retry after an SD I/O error. Other SD files and the volume are retained.

Feature/TEST APP-UPDATE145, ROOT/SD V145; optional UPDATER145 enabled tag.
Hardware validation pending: flash updater, `apps` twice, `hello --file`, reboot,
`hello --check`, cleanup `rm wtest`, `hello --exec`; then flash normal firmware
and confirm updated apps still run. Do not mark this hardware-unverified item
complete. README documents build and recovery instructions.

Image145 validation: all45 host test scripts pass. Updater tests install the
actual six generated ELF payloads and verify no-write repeat installation;
replacement, hardlink/directory rejection and sampled write-failure retries
also pass. Normal, RAM, software-SPI, updater SPI2 and updater software-SPI
clean cross-builds pass without warnings and pass image-layout validation.
Normal ELF excludes payload/updater symbols and retains192 KiB heap. Updater
ELF has160 KiB heap, `_iram_end=0x40374630`, `_iram_ext_end=0x4038c8d0`,
`_runtime_stack_end=0x3fce36a0`; updater code is in extended internal IRAM and
payload in DRAM. Hardware updater validation remains pending. No flash or
physical SD-card writes were performed by the agent.


## Image146 — per-process filesystem state

[x] Writable application file milestone: user reports updater success, and
`docs/hardware/fs-app-write-v145.log` confirms File IO OK for --file/--check,
normal app exit and deletion on clean image145. Updater output was lost; the
retained capture has one boot, not a reboot between the two test commands.

Image146 FS-PROC adds fproc records, inherited cwd/root/identity/umask, separate
owned fd namespaces and stdin references, shared open descriptions, clone
rollback, close-on-exec at successful commit and exit cleanup. ABI10 exposes
chdir/getcwd/umask and descriptor/status fcntl. Production requests select the
kernel-bound child record; applications cannot provide a target PID. MINIX
fproc.h and misc.c fork/exec/exit semantics guide the implementation. CP32
retains the trusted single-foreground execution model and serialized FS; no
new privilege/MMU boundary, general fork or credential enforcement is claimed.
The shared backend's private bounded metadata is compacted, with compile-time
size checks, to retain the192 KiB heap and all stacks. Application FS dispatch
and pointer validation now live in the existing size-optimized FS module.

Hardware validation pending: updater `apps`, then clean146 firmware; `cd /`,
`pwd`, `hello --state`, `pwd`, `cat statefile`, `rm statefile`, repeat state test
and removal, then `hello --exec`. Expect cwd unchanged, FS state OK and ok!.
Do not mark the per-process milestone hardware-complete before that capture.

Image146 validation: all46 host test scripts pass, including explicit-client
isolation, creation masks, inherited identities, shared offsets/status/stdin,
local CLOEXEC flags, clone exhaustion rollback and fresh teardown. Production
hello --state/child paths pass host integration; injected exec read failures
retain flagged descriptors, while successful commit closes them. Pointer spans,
failed/empty chdir, small getcwd buffers and inactive records are covered.
Five clean builds (SPI2, GPIO-SPI, RAM, updater SPI2 and updater GPIO-SPI) pass
without compiler warnings; ELF/image sections and segments pass inspection.
Default `_iram_end=0x40374620`, `_iram_ext_end=0x4038c730`,
`_runtime_stack_end=0x3fce7800`:2048 bytes of placement margin,192 KiB heap.
Updater `_iram_ext_end=0x4038caa0`, runtime end0x3fce3cf0,160 KiB heap.
RAM runtime end0x3fce7e40;128 KiB heap and all stack sizes are preserved.
RAM's demo hello omits only the new SD-writing state acceptance command; the
kernel process-state API is present. This prevents the larger embedded demo
payload from overlapping the fixed application slot. No safety/linker bounds
were lowered. Hardware image146 validation remains pending; nothing flashed.


## Image146 hardware acceptance — 2026-10-15

[x] Per-process filesystem state acceptance passes on updater and clean firmware.
Retained captures: `docs/hardware/fs-proc-v146-updater.log` and
`docs/hardware/fs-proc-v146-clean.log`. The updater installs hello/echo, skips
four identical applications and reports Apps update OK. The first boot has
UPDATER146 enabled; the subsequent clean boot has FS-PROC146 without that tag.
Both execute hello --state with FS state OK and exit0, preserve shell cwd `/`,
and remove statefile successfully. The clean boot additionally reads `ok!`
from statefile. This validates the self-test's cwd/mask, descriptor sharing,
failed-exec preservation and successful-exec CLOEXEC behavior on hardware.
`cd statefile` returning error8 in the first capture is expected: it is a file,
not a directory. Neither capture contains an exception. The updated application
survives flashing clean firmware; statefile itself was deleted before reboot,
so these captures do not establish its persistence across that reboot.
General fork/concurrent applications and credential enforcement remain outside
this milestone. Documentation-only evidence update; no firmware changes.


## Image147 — open-file unlink lifetime

MINIX link.c unlink_file and inode.c put_inode separate directory links from
live inode references. CP32 now removes regular-file names while retaining
open inodes and allocations until the final backend reference closes. Pending
last-link removal attaches its writer to all open descriptions, including
read-only ones; access flags remain unchanged. Zero-link inode reads/fstat are
accepted only while a matching reader/inode has a live descriptor. Final close
resnapshots current mappings, so allocations made after unlink are also freed.
Duplicate, separate-open, process-clone, exec and exit lifetimes share this
path. Directory removal retains its existing restrictions. ABI11 adds unlink.

Write ordering remains detach-before-free; uncertain writes may leak, never
promise rollback or orphan recovery across reboot. Close consumes its fd even
when reclamation fails, and the app namespace drops that owned reference too.
The shell-service C compilation now uses -Os (like FS); IRQ/assembly/context
sources, call0 ABI, memory boundaries, heap and stack reservations are unchanged.
This recovers space for continued service growth without weakening the layout
check. RAM recovery excludes SD-writing acceptance commands and returns EROFS.

Feature/TEST FS-UNLINK147, ROOT/SD V147, optional UPDATER147 enabled. Hardware
acceptance pending: update with apps, run hello --unlink twice, ls (no utest),
then hello --state/cleanup and hello --exec. Do not mark hardware complete yet.

Image147 validation: all47 host test scripts pass. Coverage includes production
hello acceptance, bounded unlink requests, hard links, both disk byte orders,
zero-link read/write/fstat, duplicated/separate opens, same-name replacement,
indirect growth after unlink, final-close bitmap reuse, close-before/after-write
fault injection, clone/CLOEXEC/exit cleanup, and SD-driver unlink/readback/remount.
Clean default SPI2, GPIO-SPI, RAM, updater SPI2 and updater GPIO-SPI builds pass
without warnings and pass ELF/image layout checks. Default IRAM end0x40374620,
extended end0x4038b4a8, runtime end0x3fce6480 (7040-byte margin); heap192 KiB.
Updater extended end0x4038b768/runtime end0x3fce2ba0, heap160 KiB. RAM runtime
end0x3fce70f0, heap128 KiB. Stack reservations and linker bounds are unchanged.
Image147 hardware validation is pending. No flashing or physical SD writes
were performed by the agent.

## Image148: application directory creation

Image147 hardware acceptance: the supplied updater-boot log shows `apps`
succeeding, `hello --unlink` passing twice with exit0, and `ls` showing no
utest residue. Capture: `docs/hardware/fs-unlink-v147.log`. This confirms the
reported unlink scenario; it is not evidence of a clean-image reboot test.

ABI12 adds `cp32_mkdir(path, mode)` through the application IPC boundary,
process cwd and umask, and the existing synchronous SD filesystem. Following
MINIX open.c do_mkdir, requested rwx bits are masked by the process umask.
The inode mode and dot entries are initialized before directory publication;
parent link counts use the existing safe ordering. No new Xtensa mechanism is
needed: trusted call0 callbacks, bounded copied request records and single FS
ownership remain unchanged. RAM recovery returns EROFS. Existing limits remain:
one-block writable directories, no credential enforcement, no transactional
power-loss recovery. Application rmdir/link/rename are separate future work.

Marker: FEATURE/TEST APP-MKDIR148, SD/ROOT V148, optional UPDATER148.
Hardware validation pending: install via apps, run hello --dir (Mkdir OK,
exit0), inspect stat dtest and stat dtest/sub, then rmdir dtest/sub and
rmdir dtest. Repeat hello --dir after cleanup. Shell pwd should remain /.
The test uses exclusive directory creation and does not overwrite an existing
dtest; an existing directory produces exit70. Successful tests leave the two
empty directories for inspection and manual cleanup.

Image148 build validation: all 47 host test scripts pass, including production
hello --dir, runtime ABI/error handling, malformed IPC records, both filesystem
byte orders, and existing mkdir publication fault injection. Clean normal,
updater, RAM recovery, GPIO-SPI and GPIO-SPI updater builds pass without compiler
warnings; ELF/image layout checks pass. Default IRAM end0x40374620, extended
end0x4038b550, runtime end0x3fce6530 (6864 bytes below the application boundary).
Updater runtime end0x3fce2ea0; RAM end0x3fce7120. Heap and stack reservations are
unchanged. Hardware validation remains pending; no flashing was performed.

[x] Image148 application mkdir hardware acceptance passed on updater and clean
boots. Both logs show Mkdir OK/exit0, shell cwd /, expected directory link counts,
successful cleanup and no residual dtest. Clean image rejects apps as intended.
Captures: docs/hardware/app-mkdir-v148-updater.log and
 docs/hardware/app-mkdir-v148-clean.log.

## Image149: application hard links (hardware pending)

ABI13 adds cp32_link(source, destination) through the runtime, bounded IPC
record and process filesystem dispatch. Both paths resolve against the process
cwd. The existing SD link implementation preserves inode identity, shared data,
link counts and deferred open-file reclamation. Duplicate names are rejected;
127 links returns EMLINK. RAM recovery returns EROFS.

MINIX reference: src/fs/link.c do_link resolves both paths and increments the
same inode's link count. CP32 has one mounted filesystem and trusted call0
applications; it rejects directory hard links and does not enforce credentials.
No architecture-specific register or context change is needed. Fixed-size
512-byte request validation precedes access; all filesystem work remains under
the single FS owner. Existing durable link-count-before-name ordering and
uncertain-I/O leakage policy are unchanged. One-block writable-directory limits
remain; this milestone does not add directory growth or application rmdir.

FEATURE/TEST APP-LINK149, SD/ROOT V149, optional UPDATER149 identify the image.
Hardware: install apps with updater, run hello --link twice. Expect Link OK and
Hello exit=0 each time, shell pwd /, no /ltest or /boot/lalias afterward. The
acceptance command creates /ltest exclusively, links it into /boot using relative
paths, checks shared writes, duplicate/missing-source errors, link counts and
last-name deletion with open descriptors; successful runs clean up both names.
No hardware validation or flashing has been performed for image149.

Image149 validation: all 47 host scripts pass, including production hello link
acceptance, runtime null/overlong/old-ABI errors, malformed two-path IPC records,
and existing little/big-endian link publication and fault-injection coverage.
Clean normal, updater, RAM recovery, GPIO-SPI and GPIO-SPI updater builds pass
without compiler warnings. ELF sections/segments and image checks pass. Default
IRAM end0x40374620, extended end0x4038b5c4, runtime end0x3fce65b0 (6736-byte
margin below application data). Updater runtime end0x3fce31e0, RAM end0x3fce7150.
Heap/stack reservations are unchanged. Hardware status remains pending.

[x] Image149 hard-link hardware acceptance passed on the supplied updater boot:
apps succeeded, hello --link passed twice with exit0, shell pwd remained /,
and listings showed no ltest or lalias. Capture:
docs/hardware/app-link-v149-updater.log. No clean-image boot was supplied here.

## Image150: application chmod (hardware pending)

ABI14 adds cp32_chmod(path, mode) through runtime, validated request and process
cwd resolution to the existing synchronous inode-mode writer. Modes0000..0777
are supported; unsupported bits return EINVAL. Umask does not apply. The change
preserves inode identity/type, contents, mappings, link count and other metadata.
Hard links observe the same mode; open descriptors retain their open-time access.
RAM recovery returns EROFS. No descriptor slot is consumed by chmod.

MINIX reference: src/fs/protect.c do_chmod updates inode mode without recreating
the file. CP32 remains a single-owner, trusted call0 environment; credential
ownership/group checks, special bits and ctime are not implemented. Existing
aggregate read/write/execute checks apply to new opens/exec; directory search
permissions are not enforced. No Xtensa interrupt/context changes are needed.
The invariant is validation before request access and unchanged inode ownership
and open-file lifetime. Existing verified SD writes and uncertain-I/O policy apply.

FEATURE/TEST APP-CHMOD150, SD/ROOT V150, optional UPDATER150 identify this image.
Hardware acceptance pending: install apps then hello --mode twice. Expect Chmod
OK and Hello exit=0, shell pwd /, and no /mtest or /boot/malias residue. The test
creates its source exclusively and cleans up on success. It verifies relative
paths, shared mode through a hard link, umask independence, failed new opens,
continued read/write through the original descriptor, fstat refresh and invalid
mode rejection. No flashing or physical SD writes were performed by the agent.

Image150 validation: all 47 host test scripts pass, including production chmod
acceptance, runtime null/long-path/old-ABI handling, invalid flags/modes and
malformed IPC records, plus existing endian and fault-injection chmod coverage.
Clean normal, updater, RAM recovery, GPIO-SPI and GPIO-SPI updater builds pass
without compiler warnings and pass image-layout checks. ELF sections/segments
inspected: default IRAM end0x40374620, extended end0x4038b5d8, runtime end
0x3fce65c0 (6720-byte margin). Updater runtime end0x3fce3450; RAM end0x3fce7140.
Heap and stack reservations are unchanged. Hardware status remains pending.

[x] Image150 chmod hardware acceptance passed on the supplied updater boot:
apps succeeded, hello --mode returned Chmod OK/exit0 twice, pwd stayed / and
listings showed no mtest/malias residue. Capture:
docs/hardware/app-chmod-v150-updater.log. A clean-image boot was not supplied.

## Image151: application rmdir (hardware pending)

ABI15 adds cp32_rmdir(path). Runtime and kernel validate the terminated path;
relative lookup uses process cwd. The production owner supplies both live FS
records (foreground application and suspended shell), protecting their working
directories by inode identity. The filesystem also rejects open directories,
non-empty directories and root. Literal terminal . or .. components are rejected
before normalization. Successful removal uses the existing detach-before-free
path and adjusts the parent's link count. EBUSY and ENOTEMPTY have explicit
application error mappings. RAM recovery returns EROFS.

MINIX reference: src/fs/link.c remove_dir checks emptiness, dot components,
root and every process cwd. CP32 has only the two live foreground FS clients;
future concurrent process support must supply all live cwd records. This adds
no Xtensa context or register changes: trusted call0 IPC, validated spans and
single FS ownership remain unchanged. Open-directory removal is conservatively
rejected rather than deferred. One-block directory mutation, credential and
power-loss recovery limits remain unchanged.

FEATURE/TEST APP-RMDIR151, SD/ROOT V151, optional UPDATER151 identify the image.
Hardware pending: install apps then hello --rmdir twice. Expect Rmdir OK and
Hello exit=0 both times, shell pwd / and no rtest. The test verifies own-cwd,
root, dot, non-empty and open-directory rejection, file-vs-directory errors,
parent link counts, cleanup and recreation. A separate host integration test
protects the suspended shell cwd. No flashing was performed by the agent.

Image151 validation: all 47 host test scripts pass; production application
integration passed again after final cleanup. All five clean build variants
(normal, updater, RAM, GPIO-SPI and GPIO-SPI updater) pass without compiler
warnings and pass image-layout checks. ELF sections/segments inspected: default
IRAM end0x40374620, extended end0x4038b790, runtime end0x3fce6780 (6272-byte
margin below application data). Updater runtime end0x3fce3800; RAM end0x3fce7170.
Heap and stack reservations are unchanged. Hardware validation remains pending.

[x] Image151 rmdir hardware acceptance passed on the supplied updater boot:
apps succeeded, hello --rmdir returned Rmdir OK/exit0 twice, pwd stayed / and
ls showed no rtest residue. Capture: docs/hardware/app-rmdir-v151-updater.log.
This log does not establish a clean-image boot result.

## Image152: application no-replacement rename (hardware pending)

ABI16 adds cp32_rename_noreplace(source, destination), resolving both paths
against application cwd and reusing the existing regular-file rename backend.
The bounded two-path IPC record is validated before access. Same-directory
renames rewrite the entry; cross-directory moves use the established counted
link publication sequence. Inode identity, contents, mode and live descriptors
are preserved. A different inode at the destination returns EEXIST; a same-path
or same-inode alias is a no-op. RAM returns EROFS. Cross-directory link-count
exhaustion now maps to EMLINK rather than generic unsupported.

MINIX reference: src/fs/link.c do_rename resolves both parents and preserves
inode identity. This remains CP32's explicitly named no-replacement subset:
directory moves and destination replacement are not implemented. No claim of
POSIX-complete rename or power-loss atomicity is made. Uncertain writes can
leave both names or an overcount. One-block writable-directory limits remain.
The Xtensa call0, single-owner FS, request bounds and descriptor ownership
invariants are unchanged; no hardware register/context changes were required.

FEATURE/TEST APP-RENAME152, SD/ROOT V152, optional UPDATER152 identify the image.
Hardware pending: update apps, then hello --move twice. Expect Rename OK and
Hello exit=0 each time, pwd /, no ntest. The test creates a private directory,
checks same-directory and cross-directory moves, occupied destination errors,
open descriptor I/O and same-inode no-ops, then removes all test artifacts.
No flashing or physical SD writes were performed by the agent.

Image152 validation: all 47 host scripts pass, including production application
rename, runtime version/null/length errors, malformed IPC records, and existing
little/big-endian rename publication and fault tests. All five clean firmware
variants (normal, updater, RAM, GPIO-SPI, GPIO-SPI updater) build without compiler
warnings and pass image-layout checks. ELF sections/segments inspected: default
IRAM end0x40374620, extended end0x4038b798, runtime end0x3fce6790 (6256-byte
margin). Updater runtime end0x3fce3b00; RAM end0x3fce71a0. Heap and stack
reservations are unchanged. Hardware validation remains pending.

[x] Image152 rename hardware acceptance passed on the supplied updater boot:
hello --move returned Rename OK/exit0 twice, pwd stayed / and ls showed no ntest.
Capture: docs/hardware/app-rename-v152-updater.log. No clean boot was supplied.

## Image153: descriptor file resizing (hardware pending)

ABI17 adds cp32_ftruncate(fd, length) for writable regular-file descriptors.
Shrinking reuses inode truncation/reclamation; growth publishes a larger EOF
with sparse holes. Before publishing growth, every mapped byte entering the
visible range is zeroed, including stale mappings from interrupted writes.
Growth never allocates blocks. Shared descriptor offsets remain unchanged;
independent readers refresh metadata. Open zero-link inodes are supported.
Lengths are nonnegative signed32-bit bytes and bounded by the superblock maximum
and direct/single-indirect limit263 KiB; oversized lengths return EFBIG. RAM
recovery returns EROFS. Read-only/invalid application descriptors return EBADF.

MINIX reference: link.c truncate releases inode zones and filedes.c separates
shared open descriptions from descriptors. CP32 combines that existing shrink
path with sparse extension and a descriptor-facing runtime operation; this is
not a claim that MINIX2 exposes this exact added API. Xtensa differences remain
bounded call0 IPC and one FS owner. No interrupt/context changes were made.
The preserved invariant is inode identity and descriptor offset/lifetime;
shrink detaches before freeing, growth zeroes before exposing EOF. Uncertain
writes may still partially commit or leak; there is no transactional recovery.
The shell's existing shrink-only truncate command remains unchanged.

FEATURE/TEST APP-RESIZE153, SD/ROOT V153, optional UPDATER153 identify the image.
Hardware pending: update apps then hello --size twice. Expect Resize OK and
Hello exit=0, pwd / and no ztest. The test checks read-only rejection, dup offset
preservation, independent metadata refresh, shrink/regrow zeroing, sparse growth
past the direct range, indirect allocation and shrink after unlink, bounds,
and final descriptor cleanup. No flashing was performed by the agent.

Image153 validation: all 47 host scripts pass. New tests cover sparse extension
through the maximum size without bitmap allocation, zeroing stale allocated
tails, every growth write failure before/after commit in both disk byte orders,
production hello acceptance and runtime error handling. Existing shrink fault
and reclaim tests also pass. All five clean variants (normal, updater, RAM,
GPIO-SPI and GPIO-SPI updater) build without compiler warnings and pass image
layout checks. ELF sections/segments inspected: default IRAM end0x40374620,
extended end0x4038b9f0, runtime end0x3fce69f0 (5648-byte margin). Updater runtime
end0x3fce3ff0; RAM end0x3fce71f0. Heap/stack reservations remain unchanged.
Hardware validation is pending.

[x] Image153 resize hardware acceptance passed on the supplied updater boot:
hello --size returned Resize OK/exit0 twice, pwd stayed / and ls showed no ztest.
Capture: docs/hardware/app-resize-v153-updater.log. No clean boot was supplied.

## Image154: application dup2 (hardware pending)

ABI18 adds cp32_dup2(source, target) for application file slots3..6. Validate
both local descriptors before mutation, reuse the existing backend dup2 when
the target is occupied, or duplicate into a free backend slot otherwise. Source
and target share the open description, offset and status flags. Replacement
clears target CLOEXEC; dup2(fd,fd) preserves it. Invalid sources leave the target
untouched. An occupied target can be replaced with a full backend table; failure
to allocate for an empty target leaves the namespace unchanged. Replacing the
last open reference to an unlinked file invokes normal final-close reclamation.

MINIX reference: src/fs/misc.c do_dup validates source/target, treats equal fds
as a no-op and reuses the shared filp after closing an occupied target. CP32
retains its bounded process namespace and trusted call0 IPC. Standard streams
0..2 remain callback-backed and are not dup2 targets; full redirection is future
work. The single FS owner serializes replacement; no Xtensa register/context
changes are needed. Backend close errors follow existing dup2 semantics (not
reported), with uncertain reclamation allowed to leak instead of unsafe reuse.

FEATURE/TEST APP-DUP2154, SD/ROOT V154, optional UPDATER154 identify the image.
Hardware pending: install apps and run hello --dup twice. Expect Dup2 OK and
Hello exit=0, pwd / and no qtest/qold residue. The command checks full local
namespace replacement, shared offsets/status, CLOEXEC/no-op behavior, invalid
sources, empty targets and unlinked-target cleanup. Host integration also
fills all backend slots and verifies empty-target allocation failure. No
flashing or physical SD writes were performed by the agent.

Image154 validation: all 47 host test scripts pass, including production hello
with a saturated backend, process-table exhaustion rollback, CLOEXEC/exec
lifecycle, runtime version/range checks and existing backend descriptor tests.
All five clean builds (normal, updater, RAM, GPIO-SPI, GPIO-SPI updater) pass
without compiler warnings and pass image layout checks. ELF sections/segments
inspected: default IRAM end0x40374620, extended end0x4038ba90, runtime end
0x3fce6a90 (5488-byte margin). Updater runtime end0x3fce42f0; RAM end0x3fce72a0.
Heap and stack reservations remain unchanged. Hardware validation is pending.

[x] Image154 dup2 hardware acceptance passed on the supplied updater boot:
Dup2 OK/exit0 twice, pwd /, and no qtest/qold residue. Capture:
docs/hardware/app-dup2-v154-updater.log. No clean boot was supplied.

## Image155: application F_DUPFD (hardware pending)

ABI19 adds cp32_fcntl(fd, CP32_F_DUPFD, minimum). Search the application's own
namespace for the lowest free descriptor at/above minimum, skipping reserved
standard streams0..2. Unlike dup2, never replace an occupied slot. Source and
new descriptor share offsets/status flags; new CLOEXEC is clear. Invalid source
returns EBADF, out-of-range minimum EINVAL, and local/backend exhaustion EMFILE.
Backend failure leaves the local table unchanged. This also works with readable
RAM files; no disk mutation is intrinsic to duplication.

MINIX reference: src/fs/misc.c do_fcntl F_DUPFD uses get_fd(start) and increments
the shared filp reference. CP32 translates local descriptor numbers separately
from backend numbers and keeps its four-slot namespace. No Xtensa context or
register changes are needed. Trusted call0 IPC, single FS ownership, descriptor
reference lifetimes and validation-before-mutation are preserved. Standard
streams remain callback-backed, so this is not full shell redirection.

FEATURE/TEST APP-DUPFD155, SD/ROOT V155, optional UPDATER155 identify the image.
Hardware pending: install apps and run hello --fcntl twice. Expect Dupfd OK and
Hello exit=0, pwd / and no ftest. The command checks requested minimum, skipped
occupied slots, full-table errors, shared offsets/status, CLOEXEC and invalid
sources, then cleans up. No flashing or physical SD writes were performed.

Image155 validation: all 47 host scripts pass, including production hello,
backend exhaustion rollback, invalid source/minimum and runtime ABI handling.
All five clean builds (normal, updater, RAM, GPIO-SPI, GPIO-SPI updater) pass
without compiler warnings and pass image layout checks. ELF sections/segments
inspected: default IRAM end0x40374620, extended end0x4038bb20, runtime end
0x3fce6b20 (5344-byte margin). Updater runtime end0x3fce4570; RAM end0x3fce7330.
Heap and stack reservations are unchanged. Hardware validation remains pending.

[x] Image155 F_DUPFD hardware acceptance passed once on the supplied updater
boot: hello --fcntl returned Dupfd OK/exit0, pwd stayed / and ls had no ftest.
The earlier hello --fcnyl was a typo and ran the generic hello path; it is not a
second F_DUPFD test. Capture: docs/hardware/app-dupfd-v155-updater.log.
No clean-image boot was supplied.

## Image156: descriptor chmod (hardware pending)

ABI20 adds cp32_fchmod(fd, mode) for regular-file and directory descriptors.
Both path chmod and fchmod now use a common validated inode-mode writer. Mode
updates preserve type, identity, contents, offsets and all non-mode metadata.
Read-only descriptors are accepted because metadata permission is separate from
open data-access mode. The mounted FS writer is supplied by the trusted owner,
not by an application. Open zero-link inodes remain valid targets. Modes outside
0000..0777 return EINVAL; invalid fds EBADF; RAM recovery EROFS.

MINIX reference: src/fs/protect.c do_chmod updates inode mode and stadir.c
fstat resolves an open inode. CP32 adds the descriptor-facing combination using
its existing trusted permission model. Owner/group enforcement, special bits,
ctime and full directory-search permissions remain unimplemented. No Xtensa
context or hardware changes are needed. Bounded call0 IPC, single FS ownership,
validated inode lifetime and existing verified write ordering remain unchanged.

FEATURE/TEST APP-FCHMOD156, SD/ROOT V156, optional UPDATER156 identify the image.
Hardware pending: install apps then hello --fmode twice. Expect Fchmod OK and
Hello exit=0, pwd / and no htest. The command tests read-only handles, umask
independence, new-open rejection, metadata/offset preservation, updates after
unlink, invalid modes/closed handles and directory descriptors. Successful runs
clean up. No flashing or physical SD writes were performed by the agent.

Image156 validation: all 47 host test scripts pass, including production hello,
read-only/directory/unlinked descriptor coverage, runtime ABI/mode errors and
existing chmod byte-order/write-failure tests using the shared inode helper.
All five clean builds (normal, updater, RAM, GPIO-SPI, GPIO-SPI updater) pass
without compiler warnings and pass image layout checks. ELF sections/segments
inspected: default IRAM end0x40374620, extended end0x4038bbe0, runtime end
0x3fce6be0 (5152-byte margin). Updater runtime end0x3fce4880; RAM end0x3fce7350.
Heap and stack reservations remain unchanged. Hardware validation is pending.

[x] Image156 fchmod hardware acceptance passed on the supplied updater boot:
Fchmod OK/exit0 twice, pwd / and no htest residue. Capture:
docs/hardware/app-fchmod-v156-updater.log. No clean-image boot was supplied.

## Image157: positional file I/O (hardware pending)

ABI21 adds cp32_pread and cp32_pwrite. Both use explicit byte offsets without
changing shared descriptor positions. Reads refresh inode metadata and use a
local cursor. Writes use the existing inode writer directly with the requested
offset, ignoring APPEND for that call while preserving flags. Regular files
only; access modes remain enforced. Sparse and indirect mappings and open
zero-link files use existing read/write machinery. RAM permits pread and
rejects pwrite. Requests hold offset/count and at most64 bytes; the kernel
checks size, alignment, span, count and signed-range overflow before copying.

MINIX reference: read.c read_write copies filp_pos into a local position and
publishes it afterward. CP32's positional interface deliberately omits that
publication, building on the port's existing read/write machinery rather than
claiming an identical MINIX2 syscall. Trusted call0 IPC, bounded buffers and
single FS ownership are unchanged. No hardware register/context changes are
needed. pwrite may complete partially on an I/O error; no rollback is promised.
pread may return fewer bytes at block boundaries and returns at most64 bytes.
The runtime loops pwrite chunks and returns completed progress on later errors.

FEATURE/TEST APP-POSIO157, SD/ROOT V157, optional UPDATER157 identify the image.
Hardware pending: install apps, hello --at twice, pwd and ls. Expect PosIO OK,
Hello exit=0, cwd / and no ptest. Acceptance tests shared-offset preservation,
explicit writes under APPEND, zero-filled holes, direct/indirect crossing,
read-only/negative-offset errors and reads after unlink. No flashing performed.

Image157 validation: all 47 host scripts pass. New tests cover production hello,
malformed positional requests, short writes and partial progress, range errors,
EOF/zero counts, shared offsets on read/write failures before/after commit in
both disk byte orders, sparse and direct/indirect crossing. All five clean builds
(normal, updater, RAM, GPIO-SPI, GPIO-SPI updater) pass without compiler warnings
and pass image layout checks. ELF sections/segments inspected: default IRAM
end0x40374620, extended end0x4038bdc0, runtime end0x3fce6dc0 (4672-byte margin).
Updater runtime end0x3fce4e10; RAM end0x3fce7490. Heap and stack reservations
remain unchanged. Hardware validation is pending.

[x] Image157 positional I/O hardware acceptance passed on the supplied updater
boot: PosIO OK/exit0 twice, pwd / and no ptest residue. Capture:
docs/hardware/app-posio-v157-updater.log. No clean-image boot was supplied.

## Image158: filesystem space reporting (hardware pending)

ABI22 adds cp32_fsinfo and the shell adds df. The allocation-map scanner reports
zone size, allocatable data zones, free data zones, total/free inodes and maximum
name length for the sole mounted volume. Reserved bit0, metadata zones and bitmap
padding are excluded. Both disk byte orders and validated scaled zones work.
A failed read leaves the output unchanged; no descriptor slot is consumed.
The filesystem cache is invalidated before scanning so reports use current
allocation maps. The service's single-owner execution serializes the snapshot.

MINIX reference: commands/simple/df.c bit_count counts allocation-map bits and
excludes the reserved inode. CP32 exposes this via bounded trusted call0 IPC;
no Xtensa register or context changes are needed. df displays free/total KiB
of the allocatable data area and free/total inodes, not SD-card capacity or RAM.
Allocated indirect tables count as used zones. Unlinked open files remain used
until final close. Reports are also available for the RAM recovery volume.
Existing crash-recovery/bitmap corruption limitations remain unchanged: the
report counts recorded allocations, it does not repair or independently audit
inode references.

FEATURE/TEST FS-SPACE158, SD/ROOT V158, optional UPDATER158 identify the image.
Hardware pending: install apps, df, hello --space twice, df, ls. Expect Space OK
and Hello exit=0, equal free counts before/after and no stest residue. The test
checks inode-only empty-file allocation, first data allocation, retained space
while unlinked/open, and restoration on final close. No flashing was performed.

Image158 validation: all 48 host scripts pass. Coverage includes production
application allocation/reclamation, shell df success/error formatting, both byte
orders, scaled zones, reserved/padding bits, full/free maps, every scan-read
failure and output preservation, runtime errors and IPC bounds. The standalone
superblock unit test now links the cache implementation required by the scanner.
All five clean firmware builds (normal, updater, RAM, GPIO-SPI, GPIO-SPI updater)
pass without compiler warnings and pass image layout checks. ELF sections and
segments inspected: default IRAM end0x40374620, extended end0x4038bf7c, runtime
end0x3fce6fc0 (4160-byte margin). Updater runtime end0x3fce51a0; RAM end0x3fce75f0.
Heap and stack reservations are unchanged. Hardware validation remains pending.


[x] Image158 FS-SPACE: hardware allocation/reclamation validation accepted.
Capture: docs/hardware/fs-space-v158-updater.log. The updater reports Apps update
OK. Two correctly entered hello --space commands report Space OK and Hello
exit=0. Before/after df reports match: 5/57 KiB free data space and 20/32 free
inodes. Final ls contains only dot entries, boot and readme, with no stest
residue. Mistyped options ran generic hello and are not counted as test passes.
This supersedes the pending space-test status above for the updater image;
a separate clean-image boot is not demonstrated by this capture.

README reorganized around current features, boundaries, command/API references,
build/SD-update instructions and debugging/hardware checks. Historical version
notes remain in the development logs. Documentation links and whitespace checked;
no firmware source changes were made for this documentation update.
