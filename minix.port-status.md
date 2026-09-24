# MINIX port status — image 73

## Summary

CP32 remains a bare-metal kernel/service bring-up, not a complete MINIX OS.
The supplied image-58 capture confirms MM allocation/release result=0, exact
nested README contents, disk result=0 and heartbeat 1369. It contains no
IRQ-5000 check. Image 59 aligns five existing library primitives with the
reference lib/ansi filenames, preserving behavior and section placement.
FS and MM already follow matching reference responsibilities; CP32 hardware,
startup and fixture files remain in place. See docs/minix-source-layout.md
for the file-by-file mapping and deferred candidates.

Image 59 hardware now confirms service calls, nested listing/README reads,
TTY output and IRQ 5000 with unknown=0 (docs/hardware/lib-layout-v59.log).
Image 60 adds host-tested single-indirect regular-file reads; image 61
now includes boot/INDIRECT for hardware validation of that new path.

Image 61 hardware confirms the indirect tail, followed by disk/MM success
and IRQ 5000 unknown=0. Image 62 adds bounded file seeking in fs/open.c and
a last-ten-lines tail command; the new seek path is host-tested and awaits
hardware validation.

## Implemented

These are bounded features, not claims of complete subsystem equivalence.

- `src/kernel/mpx32.S:CP32`, `start.c:start`, `esp32s3.ld`: loader-compatible
  internal SRAM startup, BSS initialization and vector placement. Startup
  sentinels, vector addresses and stack alignment pass in the supplied log.
- `src/kernel/proc.c:mini_send/mini_rec/cp32_task_ipc_dispatch` and
  `port.c:_send/_receive/_sendrec`: actual blocking message delivery through
  task frames, with queue and owner checks. These are no longer IPC stubs.
- `src/kernel/tty.c:in_transfer` and the console read/echo paths: bounded
  mapped canonical input, editing and task-owned LCD output. The user accepted
  scrolling in image 48; host tests exercise error and terminal-mode cases.
- `src/kernel/ramdisk.c:cp32_ramdisk_read_bytes/cp32_ramdisk_write_bytes`:
  bounded 64 KiB volatile backing storage, tested on the host.

## Partially Implemented

| Feature / CP32 evidence | MINIX reference and behavior | CP32 difference and remaining work |
| --- | --- | --- |
| Scheduling: `src/kernel/proc.c:pick_proc/ready/unready`, `irq.S` | `minix-2.0.0/src/kernel/proc.c`: ready queues and message blocking | Xtensa call0 frames replace x86 save/restore. Task execution works; nested interrupts, complete architectural state and process lifecycle need validation. Preserve frame owner, saved SP and runnable-state invariants. |
| Clock: `src/kernel/clock.c:clock_task/do_clocktick` | `minix-2.0.0/src/kernel/clock.c`: accounting, alarms and tick delivery | SYSTIMER replaces PIT. Supplied logs show clock receipts; full alarm behavior and extended soak coverage remain. |
| SYS: `src/kernel/system.c:sys_task/do_fork/do_exec` | `minix-2.0.0/src/kernel/system.c`: kernel services for MM/FS | Dispatch and selected calls work. Lifecycle handlers retain assumptions needing Xtensa frame audit; presence of handlers does not prove usable fork/exec. |
| MM: `src/mm/main.c:mm_task/cp32_mm_handle_request`, `src/mm/alloc.c:cp32_mem_alloc/cp32_mem_free/cp32_mem_owned` | `minix-2.0.0/src/mm/main.c`, `alloc.c`: service loop and physical memory allocation; `forkexit.c`, `exec.c`, `signal.c`: lifecycle | Internal-SRAM allocation and ownership checks exist; image 58 aligns filenames. numap/mem_copy reside in kernel/system.c. No full MM server implementing executable loading, process lifecycle and signal policy. |
| TTY: `src/kernel/tty.c:tty_task/in_process/in_transfer`, `display.c:cardputer_display_end_batch` | `minix-2.0.0/src/kernel/tty.c`: line discipline, device replies and terminal control | Cardputer keyboard/LCD replace PC console. Canonical path accepted; raw/timed input, cancellation and signal behavior need hardware validation. Deferred newline waits for subsequent visible output. |
| MEM: `src/kernel/memory.c:mem_task/cp32_mem_request` | `minix-2.0.0/src/kernel/driver.c:driver_task/do_rdwt`, `memory.c:m_schedule`: mapped device requests and short transfers | FS-only READ/WRITE/OPEN/CLOSE for RAM_DEV, full-buffer mapping and byte-count replies. No scattered I/O or raw memory devices. Host tests pass; one disk diagnostic and blank-superblock read pass on image-50 hardware. |
| Superblock: `src/fs/super.c:cp32_minix_super_read` | `minix-2.0.0/src/fs/super.c:read_super`, `super.h`, `type.h`: disk geometry and version conversion | Explicit byte decoding avoids compiler layout/alignment assumptions. V2 magic 0x2468 in either byte order; validates capacity, metadata and bitmap sizes before publishing. Read-only recognition only; no mount or complete consistency check. |
| Directories and files: `src/fs/inode.c:cp32_fs_open_directory/cp32_fs_file_inode`, `path.c:cp32_fs_resolve_path/cp32_minix_root_next`, `open.c:cp32_minix_file_open/cp32_minix_dir_open`, `read.c:cp32_minix_file_read` | Same filenames under `minix-2.0.0/src/fs/`: inode decoding, path traversal, open orchestration and reads | Image 56 confirms nested listings/reads; image 57 preserves behavior in split translation units. Seven direct zones plus 256 single-indirect entries support regular files in both byte orders; directories remain direct-only. Double-indirect zones, non-ASCII names, cwd, symlinks, descriptors and permissions remain unsupported. |
| Shell: `src/kernel/cp32-shell.c:cp32_shell_command` | `minix-2.0.0/src/commands`: user programs over FS/MM interfaces | Kernel-linked command loop occupying FS endpoint, not a user shell. `ls` now reads disk entries. A real FS endpoint must replace this temporary arrangement. |

| Library: `src/lib/ansi/{memcpy,memset,strcpy,strcmp,strtol}.c` | Same filenames under `minix-2.0.0/src/lib/ansi/`: standard memory/string/conversion operations | Existing freestanding implementations relocated unchanged. Xtensa call0 and section attributes retained; strtol range/error semantics remain incomplete. |

## Missing

- Filesystem cache, symlinks/mount-aware path lookup, directory mutation, file
  descriptors, permissions, cwd, writable I/O and mount lifecycle. Bounded
  subdirectory traversal and regular-file reads are now implemented.
  References: `minix-2.0.0/src/fs/cache.c:get_block`,
  `inode.c:get_inode`, `path.c:advance`, `open.c:do_open`,
  `read.c:read_write`, `mount.c:do_mount`. Extend the corresponding `src/fs/` files.
  Mostly architecture-independent, but all storage must use the device IPC ABI.
- Complete MM lifecycle and executable loader: reference
  `minix-2.0.0/src/mm/forkexit.c`, `exec.c`, `signal.c`; add matching files to `src/mm/`.
  Policy is mostly portable; executable format, relocation, stack setup and
  protection depend on the Xtensa/internal-SRAM design.
- User binaries, libc syscall entry and init/shell startup. Reference
  `minix-2.0.0/src/lib` and `src/commands`; future user build and image pipeline.
  The call0 syscall ABI needs explicit definition and tests.
- Persistent storage driver and persistent image provisioning. The RAM disk
  now boots a generated demo (`tools/make_minix_demo.py`,
  `src/kernel/minix-demo.c:cp32_minix_demo_init`) but remains volatile;
  card storage requires separate documented hardware work.

PC BIOS, 8259 PIC, 8253 PIT, x86 descriptors and x86 assembly are not missing
requirements: CP32 uses ESP image loading, Xtensa vectors and SYSTIMER instead.

## Requires Hardware Validation

- `src/mm/main.c` / `alloc.c` and `kernel/system.c:numap/mem_copy`: image 58
  confirms one mm call, nested cat and disk success. Image 59 must regress
  these operations after the library split.
  Host tests retain 200 allocator service cycles and address-map checks.

- `memory.c:mem_task` / `cp32-shell.c:cp32_shell_disk_check`: image 50 confirms
  one successful diagnostic with five 512-byte transfers and result=0.
  Repeated cycles and extended operation remain to be captured.
- `src/fs/super.c:cp32_minix_super_read` through `cp32_shell_fsinfo`: image 50
  reads 24 bytes via MEM and reports no filesystem on the blank disk in the
  supplied image-50 capture. Image 51 confirms the generated fixture's
  inodes=32 zones=63 result and root entries ., .., boot, README from real reads.
- `display.c:glyph_scaled`: the user accepts image 55's LCD and reports the
  known issues fixed. Keep display regressions while extending filesystem work.
- `src/fs/open.c:cp32_minix_file_open`, `read.c:cp32_minix_file_read` and shell cat:
  image 53 confirms README text and missing-file errors; image 54 confirms
  directory errors. Image 56 confirms nested cat/list. Repeat these after the
  image-57 layout change, including reads after disk. Host tests include exact
  bytes and preservation on failure.
- `tty.c:cp32_cardputer_key`: image 54 fixes reversed TCA8418 event polarity.
  The user reports known issues fixed on image 55. Exhaustive rollover and
  lost-event recovery remain outside that bounded acceptance.
- `clock.c`, `proc.c` and TTY: retain command/scrolling regressions and capture
  CLOCK/IRQ progress through 5000 and beyond. Images 53–55 reach IRQ 5000;
  image 55 has unknown=0 and heartbeat 5165. This is not indefinite soak proof.

## Suspicious or Incomplete Code

- `src/lib/ansi/strtol.c:strtol`: existing implementation lacks overflow and
  invalid-base handling and correct no-conversion end pointers after prefixes
  or whitespace. Relocation preserves this behavior; full ANSI semantics need
  a separate tested change.

- `system.c:do_fork` sets `p_reg.a[1] = 0` for the child's return value.
  On Xtensa a1 is the stack pointer (`proc.h`, `irq_frame.h`); this requires
  correction and lifecycle tests before enabling fork. `do_exec` also needs
  a coherent full-frame initialization audit, not just PC/SP updates.
- `proc.c` has a clean-production TODO for embedded trace blocks and a stale
  scheduler-stub comment. `port.c` has old bring-up gate commentary although
  its IPC wrappers now invoke the trap. Comments are not implementation proof.
- `main.c:cp32_boot_process_flags` deliberately stops unimplemented task slots.
  Their descriptor/frame presence must not be counted as implemented drivers.
- `ramdisk.c:cp32_ramdisk_format` uses a private CP32 format, not a MINIX
  superblock. The image-51 boot fixture uses a separate generator; legacy
  private-format status is not the filesystem's validity indicator.
- `cp32_shell_disk_check` temporarily writes the last sector and restores it.
  Image 51 excludes that block from the filesystem geometry. Preserve this
  reservation or retire the diagnostic before changing storage ownership.

## Recommended Next Steps

1. Basic MEM and blank-disk probe checks passed on image 50. Retain these and
   the display/service regressions while extending disk-cycle and soak coverage.
2. Validate image-60 single-indirect reads with an on-device large-file fixture,
   then extend mapping to double-indirect zones and directories. Follow reference filenames
   for later features, as requested for studying alongside the book.
3. Separate the real FS server from the current diagnostic command loop, then
   implement file descriptors and read operations before writable allocation.
4. Audit SYS fork/exec frame semantics before attempting full MM/user startup.
5. Expand terminal-mode and long-duration context tests; defer legacy-device
   compatibility that does not serve Cardputer hardware.

Validation: image 62 clean Xtensa build and all 23 host test scripts pass.
ELF size, sections and load segments inspected; image-layout checker passes.
`_iram_end=0x403743b0`, `_iram_ext_end=0x40382b04`, `_stack_top=0x3fcd4ad0`.
Library primitives remain in low IRAM; strtol's private helpers remain in
extended IRAM. No flashing performed. Image 60 direct-file/service regression passed through IRQ 5000 (unknown=0).
Image 61 indirect-fixture hardware validation is pending: cat boot/INDIRECT
must end with INDIRECT READ OK after its 224 direct-zone lines. The host MEM
integration test verifies that entire output and an unchanged disk checksum.

Image 62 hardware check: tail boot/INDIRECT must show zone 7 lines 24–32
then INDIRECT READ OK. Image 61 acceptance supersedes the pending fixture
validation above; see docs/hardware/indirect-demo-v61.log.

## Image 63 update

Image 62 seeking/tail, README, MM and subsequent disk diagnostic pass in the
user's two supplied captures. Image 63 extends regular-file read mapping to
MINIX V2 double-indirect zones in fs/read.c and inode.c, retaining direct-only
directories. The boot/DOUBLE sparse fixture supports a short tail regression.
All 23 host scripts, clean build and ELF checks pass; double-indirect hardware
validation remains pending. Region endpoints: 0x403743b0 / 0x40382c4c; stack
top 0x3fcd5910. Prior statements that regular-file double-indirect reads are
unsupported are superseded by this implementation; full FS service, directory
indirection, descriptors, permissions and writes remain future work.

## Image 64 update

Image 63 double-indirect and single-indirect tail commands, disk/MM and IRQ
5000 unknown=0 are hardware-confirmed in docs/hardware/minix-double-v63.log.
Metadata inspection now lives in fs/stadir.c:cp32_minix_stat, following MINIX's
same file. Regular/directory metadata is decoded with immutable failure output;
full stat/fstat ABI, device nodes, permission policy and timestamp updates remain
future work. All 23 host scripts, clean build and ELF checks pass. Image 64
hardware validation is pending: stat README, stat boot and stat boot/DOUBLE.
Current endpoints: IRAM 0x403743b0, extended IRAM 0x40382f60, stack 0x3fcd5ca0.

## Image 65 update

Image 64 stat metadata, disk/MM and IRQ 5000 unknown=0 are hardware-confirmed
in docs/hardware/minix-stat-v64.log. Image 65 adds shell cwd, cd/pwd and relative
ls/cat/tail/stat via helpers in fs/path.c and stadir.c. This supersedes older
statements that all relative shell paths start at root; per-process cwd inode
references and permission policy remain missing. All 23 host tests, clean
build and ELF checks pass. Hardware validation pending. Current endpoints:
IRAM 0x403743b0, extended IRAM 0x40383418, stack 0x3fcd6280.

## Image 66 update

Image 65 cwd/relative command handling passes on hardware through IRQ 5000;
cd - was unsupported. Image 66 adds command-client previous-directory toggling
with atomic failure behavior. All 23 host scripts and clean build/ELF checks
pass; hardware pending. This remains temporary single-client state, not full
MINIX process/environment semantics. Current endpoints: 0x403743b0,
0x403834d4 and stack 0x3fcd6450.

## Image 67 update

Image 66 previous-directory and relative read regressions pass through IRQ
5000 (unknown=0), recorded in docs/hardware/cwd-prev-v66.log. Image 67 lifts
the seven-direct-zone directory limit to include single-indirect mapping,
using the regular-file mapper from path.c. Double-indirect directories remain
unsupported. All 23 host tests, clean build and ELF checks pass; hardware
validation pending via ls/cat boot/LARGE. Current region endpoints are
0x403743b0, 0x40383628 and stack 0x3fcd88b0. Older statements that directories
are direct-only are superseded by this bounded extension.

## Image 68 interrupt correction

Image 67 exercises large-directory navigation successfully, but at IRQ 15000
raw disabled internal-timer bits inflate unknown reporting. Image 68 filters
IRQ dispatcher input with INTENABLE, preserving raw diagnostics and enabled
unregistered sources. Both assembly entry/handoff paths pass host tests;
all 23 scripts and clean build/ELF checks pass. Hardware soak beyond 15000
is pending. This supersedes any blanket clean-soak claim for image 67.
Current endpoints: IRAM 0x403743b8, extended IRAM 0x40383628, stack 0x3fcd88b0.

## Image 69 update

Image 68 interrupt masking is hardware-confirmed through IRQ 20000 with
unknown=0 even after raw compare-timer pending bits appear. Image 69 adds
fs/cache.c's four-block clean cache; source-level validation includes bounded
reads, atomic failed fills and invalidation before raw writes. All 24 host
scripts and clean build/ELF checks pass; hardware validation pending. This
supersedes prior claims that no block cache exists; full MINIX buffer locking,
LRU/hash queues, dirty/write-back state and independent FS server remain absent.
Region endpoints: 0x403743b8, 0x40383920 and stack 0x3fcd9bd0.

## Image 70 update

Image 69 cache read/disk/read regression and IRQ 15000 unknown=0 pass on
hardware. Image 70 adds single-client read-only descriptors in fs/filedes.c
and routes production cat/tail through them. This supersedes older blanket
claims that there is no descriptor table; per-process descriptors, shared
open-file descriptions, dup/fork and a syscall FS server remain missing.
All 24 host tests, clean build and ELF checks pass; hardware validation pending.
Current endpoints: 0x403743b8, 0x40383ae8 and stack 0x3fcda060.

## Image 71 update

Image 70 passes the twelve-read reuse/error/disk regression in the second boot
of docs/hardware/minix-fd-v70.log. Image 71 adds reference-counted descriptions
in filedes.c and dup/dup2 in misc.c; tail's production scan uses dup. cmp tests
two independent opens. All 24 host scripts, clean build and ELF checks pass;
hardware validation pending. dup2 has host coverage but no production caller.
This supersedes prior blanket statements that descriptor sharing is absent;
per-process tables, fork inheritance and FS syscall dispatch remain missing.
Current endpoints: 0x403743b8, 0x4038400c, stack 0x3fcda600.

## Image 72 update

Image 71 comparisons, descriptor-backed tail/cat, disk/MM and IRQ 10000 pass
on hardware. Image 72 renames the fixture's uppercase entries to lowercase
for Cardputer typing; filesystem contents and behavior are unchanged. Use
readme, boot/indirect, boot/double and boot/large/readme in current commands.
All 24 host scripts, clean build and ELF checks pass; hardware pending.
Memory boundaries are unchanged from image 71.

## Image 73 update

Image 72 lowercase listing/read/tail/cmp and IRQ 10000 unknown=0 pass on hardware.
Image 73 adds descriptor fstat to stadir.c with inode identity in open state;
production tail uses it. Metadata access preserves offsets and does not redo
path lookup. All 24 host scripts, clean build and ELF checks pass; hardware
validation pending. Prior statements that fstat is absent are superseded for
this bounded internal API; full syscall ABI and inode-lifetime management are
still missing. Endpoints: 0x403743b8, 0x403840e4, stack 0x3fcda700.
