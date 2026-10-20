# CP32 MINIX port status

> Image146 update: image145 hardware confirms writable application tests.
> Per-process FS records/lifecycle and ABI10 are now implemented; image146
> hardware validation is pending. The detailed review below remains the
> historical image142 baseline; see README for current scope and limits.

> Implementation update, image143: writable application descriptors are now
> implemented and host-tested (ABI v9). See README for scope and hardware
> acceptance. The review below records the image142 baseline; its first
> recommended milestone has been implemented, with hardware validation pending.


Review date: 2026-10-13. Baseline: working tree through **FS-EMPTY 142**, including uncommitted changes. Reference: `minix-2.0.0/src`. Review performed using the review-minix-port skill. This review changes only this report; source, plan and issues are unchanged.

## Summary

CP32 has an operational bare-metal kernel foundation and a useful, persistent **subset of the MINIX V2 filesystem**. It is not yet a complete MINIX filesystem service or general multi-process Unix environment. The largest gap is **writable application descriptors and per-process FS state**, not another shell command.

The shell can create, copy, append, patch, shrink, link, rename, remove and inspect files on SD. Applications have read-only file/directory handles and a trusted callback/IPC ABI. Those are different levels of completeness: a shell command calling a helper does not implement the corresponding MINIX system call for arbitrary processes.

The latest supplied image142 log confirms `create e` produces size0/links1, `append e hi` changes size to2, `cat e` returns hi, and unlink removes it. There is one boot in this capture: persistence across reboot and allocation bitmap contents are not directly demonstrated by it. Source and host tests cover zero-data-zone allocation. Image141's retained log confirms repeated cat and cleanup without recurrence of the image140 exception, but not exhaustive context stress.

No percentage is assigned: file counts and historical estimates obscure the difference between a working subset and complete syscall semantics. Previous build/test evidence is recorded in `plan.md` and `issues.md` (44 host scripts and SPI2/GPIO/RAM builds); no new build was needed or run for this documentation-only review.

## Implemented

These are implemented within the stated CP32 scope, not claims of full subsystem equivalence.

- **Bare-metal platform and IPC:** `src/kernel/mpx32.S`, `vectors.S`, `irq.S`, `proc.c` implement startup, saved Xtensa contexts, task queues and blocking SEND/RECEIVE/BOTH. `clock.c` supplies SYSTIMER ticks; `cardputer.c`, `tty.c`, `display.c` supply keyboard, canonical input, console output and blinking cursor. Logs show live scheduling during filesystem I/O.
- **SD block access:** `src/drivers/sd_spi.c`, `spi_fifo.c`, `src/kernel/sdcard.c`, `rootdisk.c` implement SD SPI access, volume selection, bounded reads and synchronous sector write/readback verification. Root storage is not copied wholesale into SRAM. GPIO transport and RAM recovery are separate builds.
- **V2 decoding and reads:** `src/fs/super.c:cp32_minix_super_read`, `inode.c`, `path.c`, `read.c:cp32_fs_read_map` decode either byte order, validate metadata, traverse paths and read direct/single-/double-indirect regular-file mappings, including sparse zeros. `cache.c` caches four clean 1KiB blocks.
- **Metadata and read handles:** `stadir.c` implements path stat, descriptor stat and cwd resolution. `filedes.c` implements bounded read handles, shared offsets, close, seek, directory iteration and selected fcntl operations; `misc.c` supplies dup/dup2. Directory-stream wrappers exist in `src/lib/posix` and `src/lib/other`.
- **Bounded mutations:** `src/fs/create.c` implements create, zero-zone empty create, append, shrink/reclaim, hard link, unlink, mkdir/rmdir, regular-file rename, chmod and independent copy. `write.c:cp32_minix_patch` overwrites existing non-executable data across sector boundaries. Details and limits below.
- **Trusted application lifecycle:** `src/kernel/application.c`, `src/mm/image.c`, `exec.c`, `src/kernel/context.c` implement ELF loading, argv/environment setup, one foreground app, normal exit cleanup and staged image replacement retaining PID/open files. App programs and runtime exist under `src/apps`; the repository guide's statement that there are no applications is obsolete.

## Partially Implemented

| Area | CP32 evidence | MINIX reference and original behavior | Remaining work / CP32 considerations |
|---|---|---|---|
| FS service and application ABI | `src/kernel/cp32-shell.c:cp32_shell_app_file`, `src/apps/hello/abi.h`, `src/fs/filedes.c` | `fs/main.c`, `table.c`, `fproc.h`, `filedes.c`: per-process requests, descriptors, cwd/root, credentials and shared open descriptions | CP32's eight backend slots and four foreground-app handles are bounded global tables. Implement independent clients, descriptor ownership/lifecycle and the missing request operations. Static limits are acceptable on SRAM; global state is not a substitute for per-process semantics. |
| Writable open/read/write | `src/fs/filedes.c:cp32_fd_open`, `write.c`, `create.c`; app ABI has OPEN/READ/CLOSE/SEEK but no file WRITE | `fs/open.c:common_open`, `read.c:read_write/rw_chunk`, `write.c`: access modes, creation/truncation, positioned writes and growth | Add O_WRONLY/O_RDWR/O_CREAT/O_EXCL/O_TRUNC/O_APPEND, write-at-offset, offset updates, partial counts and allocation on writes beyond EOF. Console `write` is not file write. Preserve existing metadata-before/after-data ordering and bounded buffers. |
| File growth/reclamation | `create.c:cp32_minix_append`, `cp32_minix_truncate`, `snapshot_zones`; `read.c` | `fs/write.c:new_block`, `read.c:read_map`, `link.c:truncate`: full supported map allocation/reclamation | Growth/shrink use one-block zones and direct/single-indirect mappings, up to263KiB; append is at most64 bytes per call. Reads can reach double-indirect data, but growth/reclamation are not equivalent. Patch cannot allocate holes or extend EOF. Generalize mapping mutations and coherent open-file size/map state. |
| Directory operations | `create.c:mutation_location`, `create_node`, `remove_node` | `fs/path.c:search_dir`, `open.c:do_mkdir`, `link.c:do_unlink`: searchable, growing directories | Mutation parents must fit one1024-byte block (64 entries including dot names). Add directory growth/allocation beyond this block, corresponding reclamation and boundary tests. Longer reads do not remove the mutation limit. |
| Rename | `create.c:cp32_minix_rename` | `fs/link.c:do_rename`: directory movement, replacement checks and ancestry rules | CP32 accepts regular files only; existing different destinations are refused. Add directory rename, dot-dot/parent link changes, cycle prevention and replacement semantics, with failure ordering. Current same-inode no-op is implemented. |
| Unlink and open inodes | `create.c:remove_node`, `filedes.c:cp32_fd_close` | `fs/link.c:unlink_file`, `inode.c:put_inode`: unlink a name while an open file survives until last close | CP32 rejects open target inodes. Add inode lifetime/reference management and deferred last-close reclamation before concurrent writers. Preserve detached-before-reuse safety. |
| Mode/identity/time | `create.c:cp32_minix_chmod`, `stadir.c:stat_inode`; zero uid/gid/time initialization | `fs/protect.c:forbidden/do_chmod/do_chown/do_umask`, `time.c`, inode timestamp updates | Mode metadata exists and execute bits gate loading, but read/write/search permission enforcement, per-process identities, umask, ownership changes and automatic timestamps are absent. Clock time support exists in `kernel/clock.c`; connecting filesystem time does not inherently require an RTC. |
| Cache and durability | `cache.c:cp32_cache_read`, `rootdisk.c:cp32_root_write`, `create.c:store` | `fs/cache.c`, `misc.c:do_sync`, inode dirty flushing | Four clean blocks with mutation invalidation; synchronous verified writes latch failures. Partial operations can leave reservations, extra links or partial copies. Define sync/unmount contracts and recovery. A dirty write-back cache is optional for correctness, not a mandatory first step. Verification is not power-loss atomicity. |
| Process lifecycle and FS integration | `kernel/application.c`, `mm/main.c:cp32_mm_handle_request`, `filedes.c:cp32_fd_fcntl` | `mm/forkexit.c`, `mm/exec.c`, `fs/misc.c:do_fork/do_exec/do_exit` | Real foreground exec exists; general fork/wait, concurrent apps, descriptor inheritance and credential-aware exec do not. CLOEXEC is stored in backend metadata but not exposed/consumed as complete app exec semantics. MM dispatch currently handles allocation/release, not a full POSIX MM service. |
| User environment | `kernel/cp32-shell.c`, `src/apps/*` | `commands`, libc and MINIX shell/init | Shell is kernel-linked; cp and mutation helpers are not general user utilities/syscalls. No general pipelines, redirection, job/session environment or complete libc. These depend on writable descriptors, pipes and process lifecycle. |

## Missing

Confirmed absent from the current production FS request path, not inferred merely from absent filenames.

| Feature | MINIX reference / expected function | Suggested CP32 location | Nature |
|---|---|---|---|
| Pipes and FIFOs | `fs/pipe.c`, `open.c`: buffered IPC streams, endpoint counts, EOF/broken-pipe, blocking/revival | `src/fs/pipe.c`, descriptor and app request layer | Architecture-independent, depends on scheduling/signals |
| Device-special files | `fs/open.c:do_mknod`, `device.c`, `table.c:dmap`: file descriptors routed to character/block drivers | `src/fs` device routing and namespace | Generic semantics; Cardputer/SD drivers are platform-specific and already partly available |
| Mount/unmount namespace | `fs/mount.c`, `super.c`: mounted-device table, mount traversal, busy checks and read-only policy | `src/fs` mount/super state | Mostly architecture-independent; CP32 currently selects one root volume at boot |
| Access/ownership/root APIs | `fs/protect.c`, `stadir.c`: access, chown, umask, chroot with process credentials | `src/fs` protection layer and per-process state | Architecture-independent; no hardware reason to omit semantics |
| File record locking | `fs/lock.c`, `misc.c:do_fcntl`: F_GETLK/F_SETLK/F_SETLKW and wakeup | `src/fs/filedes.c` plus lock manager | Architecture-independent; CP32 explicitly returns unsupported |
| Full FS time interface | `fs/time.c`: time/stime/utime and timestamp policy | FS metadata updates plus clock service | Timekeeping source partly platform-specific; timestamp semantics generic |
| Filesystem checking/repair tools | `commands/simple/fsck.c`, `fsck1.c`, `mkfs.c`: validate/reconcile maps, references and disk format | Start with offline host tooling under `tools`, later target utilities | Architecture-independent; current image generators are not a checker/repair service |
| General FS fork/exit/signal-revival integration | `fs/misc.c`, `pipe.c`, `main.c`: clone/release client state, unpause and revive blocked calls | Per-process FS service plus MM/app lifecycle | Generic semantics with Xtensa context implementation |

Do not count symlinks, journaling or modern Linux-specific syscalls as required MINIX2 parity without a matching reference implementation. Journaling would be an additional durability design; repair and well-defined crash behavior are practical priorities even without it. Similarly, expanding truncate is a useful product extension, not proof that this historical reference exported modern ftruncate semantics.

## Not Applicable to CP32

PC BIOS startup, x86 descriptors/segmentation, 8259 PIC, 8253 PIT, floppy/IDE controllers and PC video are not missing CP32 ports. Xtensa reset/vectors/call0, direct ESP32-S3 peripheral registers, SYSTIMER, SPI SD and Cardputer LCD/keyboard replace those responsibilities. The replacement must preserve context, IPC, bounded memory access and filesystem consistency, not reproduce x86 instructions. No ESP-IDF migration is needed.

The absence of PC-style process isolation cannot be solved by copying segment-register code. CP32's trusted single-app model is explicit; expanding it needs an ESP32-S3 protection/fault-containment design as well as generic process bookkeeping. Hardware-specific system calls and inherited signal/trace routines require a dedicated audit before broad exposure.

## Requires Hardware Validation

| CP32 path | Existing evidence | Remaining evidence needed |
|---|---|---|
| `create.c:create_node`, first append | Latest image142 user log: size0 → size2, hi readback, unlink | Restart between empty create and first append; empty copy and full-medium behavior. Host bitmap checks are not direct hardware observations. |
| `proc.c`, `irq_frame.h` context fix | Image140 IPC SYSCALL halt; image141 repeated cat/cleanup/SD/MM succeeds | Long mixed I/O/context stress under optimized code, exec regression. Exact rejected a15 was not captured in image140. |
| `write.c:cp32_minix_patch` | Small patches confirmed; image139 capture uses169-byte file | Cross-sector/noncontiguous/indirect boundary overwrite and reboot verification; do not mark covered by a short-file test. |
| `create.c:cp32_minix_copy` | Image140 Copy OK, independent hi/yo demonstrated before later context halt; image141 reads persisted contents | Larger and empty copies, ENOSPC/read/write errors, repeated executable copies and cleanup. |
| link/rename/chmod/rmdir | `issues.md` records positive cases and some restart readback; latest logs show nonempty rejection and cleanup | Full negative matrices, cwd/open rejection, count consistency, mode persistence and repeated resource reuse. |
| `rootdisk.c`, SD driver | SPI2 read/write and persistence evidence across prior logs | Card removal/error behavior, varied cards/full media and controlled power-loss checks; mocked complete-write failures do not model torn sectors. |
| memory/layout | Latest recorded ELF: runtime end0x3fce6be0, 5152-byte placement margin; heap192KiB | Long-run stack/allocation pressure. Placement headroom is not extra heap capacity and is small for a general FS service. |

## Suspicious or Incomplete Code

- `src/fs/filedes.c` says “no exec exists yet”; real foreground exec exists. Its CLOEXEC warning is still relevant, its historical claim is not.
- `src/fs/super.c` says “no allocator yet”; allocation now lives in `create.c`. `file.h` remains read-only state, while shell mutations bypass it: coherent mutable inode state must be designed before exposing writable handles.
- `src/kernel/proc.c` retains a “Minimal scheduler stub” comment despite real scheduling and IPC. `CP32_VERBOSE_HANDOFF_DIAGNOSTICS=0` disables historical probe blocks; these are not missing production scheduling.
- `src/kernel/system.c:system_reset` loops forever. It is not a completed hardware reset implementation. Inherited signal/trace routines existing in source do not establish a complete, tested Xtensa user signal ABI.
- `src/mm/main.c` dispatches only allocation/release and rejects other messages. Exec staging elsewhere does not turn this into the full MINIX MM server.
- `plan.md` opening status still describes image126/127, read-only FS and no writable filesystem; later entries supersede it. Old unchecked milestones coexist with later acceptance entries. Use function bodies and dated evidence rather than the opening summary or old percentage estimates.
- Global descriptor/cache state and static `create.c` mutation scratch are intentional serialization assumptions. They need ownership/serialization analysis before multiple FS clients; the review does not claim a current concurrent corruption bug.
- No new confirmed blocker to the currently demonstrated image142 boot/create/append/remove path was identified. Source review does not prove absence of all faults.

## Recommended Next Steps

1. **Consolidate stability and memory evidence.** Keep the a15 regression; run short repeated I/O/exec tests and account for stack/static growth before adding a service. Do not reduce safety checks or heap/stacks merely to fit features.
2. **Implement writable application descriptors as the next end-to-end FS milestone.** Start with open flags, create/exclusive open, write/append, offsets, close and error translation through the existing app IPC boundary. Use a small application that creates a file, writes twice, seeks, reads it back and closes it; reboot verifies persistence. Stage broader overwrite/hole behavior explicitly rather than pretending the current patch helper is POSIX write.
3. **Introduce coherent inode/open-description lifetime and per-process state.** Add independent descriptor tables, cwd/root, access modes, shared offsets, fork/exec/exit hooks and close-on-exec. Then implement unlink-while-open with deferred reclamation.
4. **Remove structural disk limits.** Grow parent directories beyond one block; unify map allocation/reclamation, including double-indirect mappings and supported zone sizes. Exercise ENOSPC and partial writes at each boundary.
5. **Complete namespace and protection semantics.** Directory rename/replacement, credentials/permission checks, umask/chown/access and timestamps.
6. **Add pipes/FIFOs and device descriptors**, enabling redirection and pipelines after process lifecycle support is ready.
7. **Define sync/mount/unmount and recovery tooling.** Start with an offline checker before claiming robust writable storage; optimize caching only after correctness contracts are clear.
8. **Broaden compatibility and userland** after those foundations: general syscall ABI, utilities, init/session shell and optional filesystem formats.

A complete CP32 filesystem need not implement every historical disk driver or copy MINIX's cache policy. It does need a documented application API, correct descriptor/inode lifetimes, permission and namespace semantics, supported allocation limits, and tested persistence/error behavior. Today it is a working SD filesystem with a substantial maintenance shell, not yet that complete service.
