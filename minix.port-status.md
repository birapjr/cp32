# CP32 MINIX port status

Review date: **2026-10-23**. Baseline: **FS-SPACE 158, application ABI 22**.
Reference: `minix-2.0.0/src`. This report replaces the historical image142 review.
Only this report was changed during the review; source, plan and issues were not.

## Summary

CP32 has a working bare-metal platform, scheduled kernel tasks, blocking IPC,
a keyboard/LCD shell, persistent SD storage and a substantial writable MINIX V2
filesystem API for separately linked applications. It is still a **single trusted
foreground application environment**, not a complete MINIX multi-process system.

Writable application descriptors, per-process filesystem records, umask,
close-on-exec, unlink-while-open, hard links, directory creation/removal,
regular-file moves, descriptor duplication, file resizing, positional I/O and
free-space reporting are now implemented. The old review's recommendations to
implement these from scratch are obsolete.

The largest filesystem gaps are **directory growth, full rename semantics,
credential-aware permissions, timestamps, device/pipe descriptors, mounts and
recovery tooling**. General process creation/wait/signals and application fault
containment remain larger system-level gaps. No new boot blocker was established
by this review; this is a status assessment, not an exhaustive correctness audit.

Evidence combines inspected function bodies, MINIX dispatch tables and matching
implementations, host-test sources, and recorded hardware results in `issues.md`
and `docs/hardware/`. The repository has 48 host test scripts. Image158's five
clean build variants and host-suite success are **previous recorded results**, not
new runs during this documentation-only review. No firmware was built or flashed.
No completion percentage is assigned: syscall semantics and hardware evidence
are more meaningful than file counts.

## Implemented

“Implemented” here means within the explicitly bounded CP32 interface, not full
MINIX subsystem equivalence.

| Capability | CP32 source evidence | MINIX comparison / preserved behavior |
| --- | --- | --- |
| Bare-metal startup and context switching | `src/kernel/mpx32.S`, `vectors.S`, `irq.S`, `context.c`, `proc.c` | Replaces MINIX kernel architecture entry/exit with Xtensa call0 frames; preserves runnable ownership, saved registers and interrupt return. |
| Scheduling and blocking IPC | `src/kernel/proc.c:mini_send`, `mini_rec`, `sys_call`, `pick_proc`; `port.c:_send/_receive/_sendrec` | `minix-2.0.0/src/kernel/proc.c`: queued tasks, rendezvous, blocked send/receive and resumption. The port wrappers invoke real traps, not dummy success returns. |
| Clock and console | `src/kernel/clock.c:cp32_timer_irq_dispatch`, `clock_task`; `tty.c`, `cardputer.c`, `display.c` | MINIX clock/TTY responsibilities adapted to SYSTIMER, Cardputer keyboard and LCD; canonical input, output and block cursor work. USB is the diagnostic channel. |
| SD root storage | `src/drivers/sd_spi.c`, `spi_fifo.c`; `src/kernel/sdcard.c`, `rootdisk.c:cp32_root_read/cp32_root_write` | Block-device responsibility supplied by SPI SD instead of PC disk controllers. Bounded synchronous writes are readback-verified; SD contents are not copied wholesale into RAM. |
| Filesystem decoding and reads | `src/fs/super.c`, `inode.c`, `path.c`, `read.c:cp32_fs_read_map` | MINIX V2 metadata, allocation checks, either byte order, paths, direct/single/double-indirect reads and sparse zeros. |
| Writable application files | `src/fs/filedes.c:cp32_fd_open_mode`, `cp32_fd_write`; `create.c:cp32_fs_write_inode`; `appfile.c:cp32_fproc_file` | MINIX `fs/open.c:common_open`, `read.c:read_write`, `write.c:new_block`: access modes, create/exclusive/truncate/append, offsets and bounded growth. |
| Descriptor and inode lifetime | `src/fs/filedes.c:cp32_fd_share`, `cp32_fd_defer_unlink`, `cp32_fd_close`; `create.c:cp32_fs_reap_inode` | MINIX `fs/filedes.c`, `inode.c:put_inode`, `link.c`: shared open descriptions, independent opens and deferred reclamation after last close. Unlink no longer simply rejects open regular files. |
| Client filesystem lifecycle | `src/fs/fproc.c:cp32_fproc_clone`, `cp32_fproc_exec`, `cp32_fproc_exit`; `kernel/cp32-shell.c:cp32_shell_app_files_exec` | MINIX `fs/misc.c:do_fork/do_exec/do_exit`: inherited cwd/umask/handles, local CLOEXEC flags, shared offsets/status and cleanup. This is integrated into shell/app launch and exec, not a general fork service. |
| Namespace and metadata subset | `src/fs/create.c:cp32_minix_mkdir_mode`, `cp32_minix_link`, `cp32_minix_unlink`, `cp32_minix_rmdir`, `cp32_minix_rename`, `cp32_fs_chmod_inode`; `stadir.c` | MINIX `fs/open.c`, `link.c`, `protect.c`, `stadir.c`: directory creation, hard links, removal, non-replacing regular-file moves, rwx mode changes and stat/fstat. |
| Descriptor operations | `src/fs/fproc.c:cp32_fproc_dup2`, `cp32_fproc_dupfd`, `cp32_fproc_flags`; `filedes.c:cp32_fd_truncate/cp32_fd_pread/cp32_fd_pwrite` | Duplication and flags preserve MINIX shared-description semantics. Resizing and positional APIs extend the CP32 interface; their names are not evidence that identical calls existed in MINIX2. |
| Free-space reporting | `src/fs/super.c:cp32_minix_space_read`, `count_free`; `appfile.c` FSINFO dispatch; shell `df` | MINIX `commands/simple/df.c`: count allocation bits, excluding reserved bits, metadata and padding. Counts are not a consistency check. |
| Application loading and exec | `src/kernel/application.c`; `src/mm/image.c:cp32_image_read/cp32_image_load`; `exec.c:cp32_exec_stack`; `src/apps/lib` | MINIX `mm/exec.c` responsibilities adapted to fixed-layout Xtensa ELF. Staging precedes replacement, PID/non-CLOEXEC handles survive, and failure preserves the old program. |
| Small application environment | `src/apps/{hello,echo,cat,wc,ls,exec}`, `src/apps/lib/{io,heap,stream}.c` | Arguments, initial environment, identity, exit, allocation and selected utilities. Optional updater installs application images without removing the card. |

## Partially Implemented

For generic filesystem semantics, the ESP32-S3 difference is constrained internal
SRAM and a trusted serialized service—not a reason to omit Unix lifetime or
permission rules. Bounded tables are legitimate; their capacity must remain explicit.

| Area / CP32 paths and functions | Original MINIX behavior and reference | Current boundary, architecture and remaining work |
| --- | --- | --- |
| FS service / `src/fs/fproc.h`, `appfile.c`, `kernel/cp32-shell.c` | `minix-2.0.0/src/fs/main.c`, `table.c`, `fproc.h`: dispatch requests from many processes with independent state | Two active roles, shell and foreground app; four app file slots 3–6, eight backend slots. Client selection is trusted, not application-supplied PID. Generalize ownership/dispatch for concurrent processes, including suspended operations. Keep bounded memory and existing validation. |
| Directory allocation / `src/fs/create.c:mutation_location`, `create_node` | `minix-2.0.0/src/fs/path.c:search_dir` calls `write.c:new_block` when a directory grows | Mutation rejects parent size above 1024 and nonzero log-zone size; new entries use the first zone. Implement mapped directory reads/writes, extra block allocation, reuse and reclamation. Preserve publication ordering and bounded scratch storage. |
| File mapping mutations / `src/fs/create.c:cp32_fs_write_inode`, `snapshot_zones`, `cp32_fs_resize_inode` | `minix-2.0.0/src/fs/write.c`, `link.c:truncate`: allocation/release across supported mappings | Writes and reclamation support 1 KiB zones, seven direct plus 256 single-indirect data zones (263 KiB). Reads support more. Add double-indirect mutation and scaled-zone handling, or keep explicit read-only rejection for those layouts. SRAM calls for streaming rather than large stack snapshots. |
| Rename / `src/fs/create.c:cp32_minix_rename` | `minix-2.0.0/src/fs/link.c:do_rename`: replacement, directory ancestry checks, dot-dot and parent-link updates | Regular-file, no-replace only; same inode is a no-op. Add replacement and directory movement with cwd/root identity handling, cycle protection and failure ordering. Current cwd strings cannot simply be left stale after directory moves. Generic semantics; no Xtensa-specific obstacle. |
| Permissions / `src/fs/filedes.c:cp32_fd_open_mode`, `create.c:cp32_fs_chmod_inode`, `fproc.h` | `minix-2.0.0/src/fs/protect.c:forbidden`: real/effective UID/GID selection, owner/group/other checks, root rules and read-only policy | rwx metadata, umask and aggregate read/write/execute-bit checks exist. UID/GID fields do not implement credentials. Parent search/write enforcement, chown/access, special bits and credential changes are missing. Existing descriptors correctly retain open access. |
| Time / `src/fs/create.c`, `stadir.c`; `kernel/clock.c:do_get_time/do_set_time` | `minix-2.0.0/src/fs/time.c`, `inode.c`: time APIs and timestamp updates | Metadata is decoded but mutation timestamps are not maintained. Connect a defined epoch/time source to inode updates and utime. Persistent wall time may need a platform policy; generic timestamp updates do not inherently require an RTC. |
| Cache/durability / `src/fs/cache.c`, `kernel/rootdisk.c:cp32_root_write`, `create.c:store` | `minix-2.0.0/src/fs/cache.c`, `misc.c:do_sync`, `mount.c`: cache flushing and mounted-volume lifecycle | Four clean cache blocks, invalidation and synchronous verified writes. Failure latches further device writes off until reboot. Multi-write operations can leak allocations or leave partial namespace changes. Define sync/unmount and recovery contracts. Dirty writeback is optional, not a prerequisite for correctness; readback is not power-loss atomicity. |
| Process/MM service / `src/mm/main.c:cp32_mm_handle_request`, `kernel/application.c`, `system.c` | `minix-2.0.0/src/mm/table.c`, `forkexit.c`, `exec.c`, `signal.c`: general process lifecycle and MM syscalls | MM task dispatch handles allocation/release; image and exec helpers are real but coordinated elsewhere. No general fork/wait/concurrent apps or complete signals. Preserve staged exec and resource ownership while designing relocatable/multiple app images and Xtensa contexts. |
| Isolation and exceptions / `src/kernel/serial.c:exception_dump`, `system.c:umap`, `application.c` | MINIX kernel/MM validate addresses, manage process maps and deliver faults/signals | IPC bounds checks are not protection against arbitrary trusted app loads/stores. Exceptions halt the system. Need an ESP32-S3-specific fault-containment/protection design; x86 segmentation code cannot be copied. |
| Userland / `src/kernel/cp32-shell.c`, `src/apps/lib/io.c` | `minix-2.0.0/src/commands`, `lib`: user shell, common fd streams, process/session APIs and libc | Kernel-linked shell, six utilities and custom ABI. Standard streams remain callbacks; input redirection exists, general output redirection/pipelines do not. Unify device/file stream descriptors before general shell composition. |

## Missing

Confirmed against production dispatch and interfaces, not merely filenames.
All reference paths below are relative to `minix-2.0.0/src/`.

| Feature | MINIX reference / expected behavior | Suggested CP32 location | Nature |
| --- | --- | --- | --- |
| General fork/wait and process tree | `mm/forkexit.c`, `mm/mproc.h`: child creation, zombie/reaping, parent notification | `src/mm`, application/context lifecycle and FS client table | Generic semantics plus Xtensa image/context design |
| Application signals and sessions | `mm/signal.c`, `getset.c`, `trace.c`: signal disposition/masks/return, kill/alarm/pause, session/group identity and tracing | `src/mm`, `src/kernel/system.c`, application ABI | Generic policy plus architecture-specific signal frames/debug support |
| Pipes/FIFOs | `fs/pipe.c`, `open.c`: endpoint counts, buffering, EOF/broken pipe, blocking/revival | `src/fs` descriptor types and request service | Generic; depends on process scheduling and signal policy |
| Device-special descriptors | `fs/device.c`, `open.c:do_mknod`, `table.c:dmap`: character/block driver routing and ioctl | `src/fs`, existing TTY/storage drivers | Generic dispatch, platform-specific devices |
| Mount/unmount and chroot | `fs/mount.c`, `stadir.c`: namespace traversal, busy checks, per-client root | `src/fs` superblock/path/client state | Generic; current root selection at boot is not mount semantics |
| Credential APIs | `fs/protect.c`, `mm/getset.c`: access/chown/setuid/setgid with authorization | FS protection layer and MM records | Architecture-independent |
| Record locks | `fs/lock.c`, `misc.c:do_fcntl`: GETLK/SETLK/SETLKW, conflicts and wakeup | `src/fs` lock manager and descriptor service | Architecture-independent |
| Full FS time API | `fs/time.c`: time/stime/utime/times | FS metadata plus clock service | Generic API, platform time-source policy |
| Checker/repair tooling | `commands/simple/fsck.c`, `fsck1.c`: reconcile allocation maps, references and directories | Prefer offline tools under `tools/` first | Architecture-independent; current image generators are not fsck |
| Full libc, init/login and user shell | `lib/`, `commands/`: general user environment | `src/lib`, `src/apps` after process/descriptor work | Mostly generic, ABI/startup adaptation needed |
| Networking | `inet/` and networking commands: protocol service and applications | Future network service plus hardware driver | Protocols generic; ESP32 radio support is a separate hardware project |

Symlinks and journaling are not requirements inferred from modern Unix: this
MINIX FS dispatch table does not supply symlink calls, and journaling would be
an additional design. Likewise pread/pwrite/fchmod/resize are useful CP32 APIs,
not automatic claims of identical MINIX2 syscall parity. MINIX V1 compatibility
is also absent; V2 is the current deliberate format scope.

## Not applicable to CP32

PC BIOS, x86 segment descriptors, 8259 PIC, 8253 PIT, VGA, IDE/floppy controllers
and PC keyboard hardware are not missing ports. Xtensa startup/call0, SYSTIMER,
SPI SD and Cardputer peripherals replace their responsibilities. Invariants are
correct register preservation, scheduling/IPC ownership, bounded memory access
and storage consistency. No ESP-IDF migration is required.

Dual-core SMP is not a requirement for matching this MINIX reference. A single
active CPU and serialized FS are intentional constraints; enabling another core
would require an independent concurrency audit.

## Requires hardware validation

Existing positive evidence is credited only to the commands and images recorded.
A successful updater boot does not establish a separate clean-image boot.

| CP32 function/path | Expected behavior / remaining hardware check | Existing evidence |
| --- | --- | --- |
| `super.c:cp32_minix_space_read`, `filedes.c:cp32_fd_close` | Repeat allocation/reclamation on normal firmware; eventually check full/large media | Image158 updater: two `Space OK` / exit0 results, before/after 5/57 KiB and 20/32 inodes, no `stest`; `docs/hardware/fs-space-v158-updater.log`, accepted in `issues.md`. Clean-image result absent. |
| `fproc.c` clone/exec/dup helpers; `appfile.c` | Repeated normal-image lifecycle and exhaustion tests without leaks | `issues.md` records later duplication/CLOEXEC tests, including image154 twice and image155 once. The mistyped option is correctly excluded. |
| `create.c`, `filedes.c` writable operations | Larger volumes, indirect boundaries, restart persistence and resource exhaustion across operation combinations | `issues.md` records images148–153 mkdir/link/mode/rmdir/move/resize acceptance. Image156 fchmod and image157 positional I/O each pass twice on updater boots. Not exhaustive failure coverage. |
| `rootdisk.c:cp32_root_write`, SD transport | Card removal, timeout, alternate cards, full media, controlled interrupted writes on disposable media | SPI2 read/write and restart persistence previously recorded; host fault injection does not model torn sectors or controller power-loss behavior. |
| `proc.c`, `irq.S`, `context.c` | Long mixed filesystem/exec/timer stress and stack-pressure testing | Prior image140 crash and image141 regression history; later logs show sustained operation. No claim of exhaustive context validation. |
| Linker/runtime memory | Confirm stack/heap pressure with the largest app and repeated exec | Recorded image158 normal runtime end `0x3fce6fc0`, 4160-byte placement margin; updater `0x3fce51a0`, RAM `0x3fce75f0`. Static placement margin is not unused heap or proof of stack safety. |
| Legacy system-task operations | Validate each exposed Xtensa handler before using it for general process/signal support | Existing boot/core markers and trusted application tests do not validate every inherited SYS handler. |

## Suspicious or incomplete code

These are confirmed limits or misleading comments, not newly proven corruption bugs.

- `src/kernel/system.c:system_reset` remains an infinite-loop placeholder. This
  does **not** mean all reset support is absent: `src/kernel/serial.c` calls
  `cp32_hardware_reset` for the implemented USB/standalone transition.
- `src/mm/main.c:cp32_mm_handle_request` accepts only allocate/release and returns
  `E_BAD_FCN` otherwise. Do not mistake inherited syscall names or image helpers
  for a complete MM server.
- `src/kernel/serial.c:exception_dump` is terminal. There is no application-only
  fault recovery demonstrated by the foreground loader.
- `src/fs/super.c` still says “no allocator yet”; `create.c` has an allocator.
  `create.c:cp32_minix_copy` says “Until writable app descriptors exist”; those
  descriptors now exist. The shrink-helper comment must not be read as denying
  the separate implemented `cp32_fs_resize_inode` growth path.
- `src/kernel/proc.c` retains “Minimal scheduler stub” wording and a
  `TODO(clean-production)` for guarded diagnostic blocks. Its real queue and IPC
  functions are implemented; disabled historical tracing is not disabled scheduling.
- `src/fs/create.c` uses shared mutation scratch and `filedes.c` global backend
  tables. These fit the serialized owner model; future concurrency must preserve
  that serialization or introduce explicit synchronization.
- `src/fs/fproc.c:cp32_fproc_exit` ignores close/reclamation errors during cleanup.
  `filedes.c:cp32_fd_close` explicitly consumes the descriptor even on reclamation
  failure. This favors avoiding double-free/retry but can leave leaked storage;
  diagnostics and offline recovery remain important.
- `plan.md` opening summary and parts of `AGENTS.md` still describe read-only FS,
  absent applications or stub IPC. Later acceptance entries and current function
  bodies supersede them. They were left unchanged as required by this review.

## Recommended next steps

1. **Preserve stability evidence.** No new demonstrated boot blocker needs a
   speculative kernel rewrite. Run the existing short tests on the clean image,
   retain logs, and watch memory/stack budgets as features grow.
2. **Next filesystem port: growing directories.** Replace first-block assumptions
   in `mutation_location` and all namespace writers with mapped directory access.
   Test more than 64 entries, holes/reuse, nested directories, rename/link/unlink,
   full media, partial I/O failures and reboot. Provide a short self-cleaning
   application command so hardware testing does not require long typed scripts.
3. **Complete rename semantics**, including replacement and directory moves, with
   inode-based cwd/root identity, ancestry checks, parent links and failure tests.
4. **Complete permissions and timestamps.** Use real/effective credentials,
   parent search/write checks, access/chown and consistent inode timestamp policy.
   Keep trusted-application policy explicit until protection exists.
5. **Broaden allocation and recovery.** Implement double-indirect/scaled-zone
   mutation only with bounded memory, and add an offline checker plus explicit
   sync/unmount behavior. Larger demo media alone does not remove directory or
   mapping limits. Power-loss safety requires more than readback verification.
6. **Generalize process and stream infrastructure.** Multiple FS clients,
   fork/wait or a clearly scoped spawn design, device-backed standard descriptors,
   pipes and wakeup/EOF semantics unlock output redirection and pipelines.
7. **Complete remaining compatibility**, including signals, mounts, locks,
   user shell/init/libc and optional networking, after the foundations above.

The next work should extend structural semantics, not reimplement writable
application files or per-process state that are already present and tested.

## Subsequent checkpoint: image164 and offline checker

This supplements the earlier review rather than claiming a new full-tree audit.
Directory growth, replacement/directory rename, the dedicated FS stack, and
explicit sync/fsync were implemented after that baseline. Image164's supplied
updater log confirms sync and ordinary file operations; it does not establish
power-cycle persistence. `tools/fsck_minix.py` now provides read-only offline
V2 consistency checking; automatic repair is still missing. See `docs/fsck.md`
for supported formats, checks and limits. Credentials, time APIs, mount/process
infrastructure and broader mutation geometry remain separate port milestones.

### Subsequent checkpoint — image165 FS-TIME

Application ABI25 now exposes TIME/STIME/UTIME through CLOCK and synchronous
V2 inode timestamps, with shell date/touch and three-time stat output. MINIX2
owner/root and current-time write-permission rules are covered by host tests
in both byte orders. All 51 host scripts and five clean firmware variants pass.
Hardware validation is pending. Automatic timestamps on other filesystem
operations, full pathname authorization, and a persistent RTC remain missing.

### Subsequent checkpoint — image166 FS-DATA-TIME

Image165 shell timestamps and contents survived restart in the supplied updater
transcript (docs/hardware/fs-time-v165-pass.log). Image166 adds automatic inode
creation/read/write/append/patch/resize timestamps at the shared FS data layer,
including hardlink and open-unlinked semantics. CLOCK supplies volatile seconds;
V2 timestamps are synchronous. Directory enumeration and namespace/chmod times
remain missing, as do full access control and persistent wall time. Image166
hardware acceptance is pending; ABI25 application payloads remain compatible.
