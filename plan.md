# CP32 port plan

Updated 2026-09-28 against the working tree and user hardware results through
**HELLO-ARGV 89**. This file tracks current status and remaining work, not the
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
in image 87. All 30 host test scripts passed during the latest review.

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

[ ] 2. Consolidate context installation and process ownership in MM/kernel
interfaces. Replace the fixed hello-specific lifecycle with explicit process
metadata, parent/PID relationships and owned image/stack resources.
Before enabling legacy handlers, fix `system.c:do_fork` writing the child result
to a1 instead of a2; make `do_exec` initialize a1/a15, special registers and
blocked-frame state consistently with the working launcher. Audit the legacy
`proc.c:schedule` entry rather than reusing it unchanged.
Acceptance: fresh/reused slots have consistent frames/maps/queues; failed
creation leaves the parent and existing processes usable.

[ ] 3. Add abnormal completion, cancellation and cleanup. Define how an
application fault or nonexiting child is reported; reclaim its resources and
resume its parent. Keep the trusted-execution limitation explicit until an
ESP32-S3 protection design is implemented and validated.
Acceptance: normal/nonzero exit, cancellation and controlled faults leave no
stale queue entries, blocked parent, descriptor leaks or orphaned allocation.

[ ] 4. Implement general exec/exit/wait semantics, environment passing and
user heap management. Add fork with an explicit SRAM-compatible memory policy
and correct descriptor inheritance; do not equate fixed-slot launch with fork.
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
Acceptance: independently built programs use documented services rather than
hello-specific kernel callbacks; blocking/error/short-I/O behavior is tested.

[ ] 9. Port a user shell and utilities, followed by init/login/session setup.
Support external commands, pipes, redirection and scripts once the required
process and FS APIs exist. Existing kernel builtins remain regression tools;
they are not substitutes for independently executable MINIX commands.
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

[ ] Confirm the LCD `!` appearance visually. Image-88 serial output verifies
the character stream, not the pixels; the code fix and pixel-model test exist.

[ ] Update stale repository guidance/comments as the corresponding areas are
touched: obsolete IPC/frame descriptions, the hello ABI comment claiming its
bridge is absent, and legacy bring-up gates. Keep removable traces isolated
and do not remove working functionality during cleanup.

## Development and completion rules

- Compare each feature with `minix-2.0.0`; preserve its portable invariant
  while adapting architecture-specific mechanisms to Xtensa/ESP32-S3.
- Keep call0, freestanding execution and current boot/IPC/console behavior.
  Respect shared D/IRAM aliases and the full runtime-stack reservation.
  Latest inspected endpoints: `_iram_end=0x4037422c`,
  `_iram_ext_end=0x40386564`, `_runtime_stack_end=0x3fce3590`;
  the application code's DRAM alias starts at `0x3fce8000`.
- For implementation changes, add meaningful tests, run `make clean && make`
  and `make tests` in `src/`, then inspect relevant ELF sections/segments and
  symbols. Current image identity is 89; increment feature/test markers for
  each new image, with the test marker immediately before the idle call.
- Record build and hardware results separately in `issues.md` and this plan.
  Mark completed items `[x]` only when their stated acceptance criteria pass;
  describe remaining hardware checks explicitly. Remove closed tasks during
  later compaction once their evidence is retained in the history.
- Leave flashing to the user. Never infer hardware success from compilation,
  a host model, or the presence of a function name.
