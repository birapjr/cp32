# CP32 port plan

Updated 2026-09-30 against the working tree and user hardware results through
**APP-STDIO 105**; image **APP-LINE 106** is built and host-tested. This file tracks current status and remaining work, not the
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
in image 87. All 33 host test scripts and the clean build pass for image 106.

The boundary is still **one trusted foreground application**. The shell is
kernel-linked; there is no general exec/fork/wait server, protected application
ABI, fault containment, per-process FS service, writable/persistent filesystem,
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
NUL termination and fragment continuation; hardware pending; general fd-based file
I/O, errno and FILE/stdio buffering remain unfinished.
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
  Latest inspected endpoints: `_iram_end=0x403742ac`,
  `_iram_ext_end=0x40386e80`, `_runtime_stack_end=0x3fce6030`;
  the application code's DRAM alias starts at `0x3fce8000`.
- For implementation changes, add meaningful tests, run `make clean && make`
  and `make tests` in `src/`, then inspect relevant ELF sections/segments and
  symbols. Current image identity is 106; increment feature/test markers for
  each new image, with the test marker immediately before the idle call.
- Record build and hardware results separately in `issues.md` and this plan.
  Mark completed items `[x]` only when their stated acceptance criteria pass;
  describe remaining hardware checks explicitly. Remove closed tasks during
  later compaction once their evidence is retained in the history.
- Leave flashing to the user. Never infer hardware success from compilation,
  a host model, or the presence of a function name.
