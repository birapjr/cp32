# MINIX port status — image 54

## Summary

CP32 has a working bare-metal kernel/service bring-up, not yet a complete
MINIX operating system. The supplied image-49 log shows TTY write, SYS, MM,
IPC and diagnostic ls success, with a heartbeat at tick 1608 and no exception
in the capture. It contains no disk command or RAM service trace. Image 50
adds read-only MINIX V2 superblock recognition. A subsequent image-50 hardware
capture confirms one restored-sector disk diagnostic and the blank-superblock
case, followed by successful MM/IPC/SYS/ls and TTY output. Evidence:
`docs/hardware/minix-super-v50.log`. Image 51 adds a generated MINIX V2 demo
and real root-directory listing through MEM. The image-51 hardware capture
confirms two listings, valid geometry and disk result=0, with heartbeat 2619
(docs/hardware/minix-dir-v51.log). It exposed a missing LCD period glyph;
image 52 defines that glyph. Its supplied log confirms ls/font/fsinfo/disk and
MM/SYS/IPC through heartbeat 3530, but not the visual LCD result. Image 53 adds
root-name lookup and read-only cat. The user's image-53 capture confirms README
content, missing-file handling and IRQ 5000, but exposes reversed keyboard
press/release semantics. Image 54 fixes TCA8418 event polarity; host/build
checks pass and keyboard hardware acceptance is pending.
This report supersedes historical status prose in the chronological plan.

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
| MM: `src/kernel/mm.c:mm_task/cp32_mm_handle_request` | `minix-2.0.0/src/mm/alloc.c`, `forkexit.c`, `exec.c`, `signal.c`: allocation plus process management | Internal-SRAM allocation and ownership checks exist. No full MM server implementing executable loading, process lifecycle and signal policy. |
| TTY: `src/kernel/tty.c:tty_task/in_process/in_transfer`, `display.c:cardputer_display_end_batch` | `minix-2.0.0/src/kernel/tty.c`: line discipline, device replies and terminal control | Cardputer keyboard/LCD replace PC console. Canonical path accepted; raw/timed input, cancellation and signal behavior need hardware validation. Deferred newline waits for subsequent visible output. |
| MEM: `src/kernel/memory.c:mem_task/cp32_mem_request` | `minix-2.0.0/src/kernel/driver.c:driver_task/do_rdwt`, `memory.c:m_schedule`: mapped device requests and short transfers | FS-only READ/WRITE/OPEN/CLOSE for RAM_DEV, full-buffer mapping and byte-count replies. No scattered I/O or raw memory devices. Host tests pass; one disk diagnostic and blank-superblock read pass on image-50 hardware. |
| Superblock: `src/kernel/minix-super.c:cp32_minix_super_read` | `minix-2.0.0/src/fs/super.c:read_super`, `super.h`, `type.h`: disk geometry and version conversion | Explicit byte decoding avoids compiler layout/alignment assumptions. V2 magic 0x2468 in either byte order; validates capacity, metadata and bitmap sizes before publishing. Read-only recognition only; no mount, inode or allocation-map integrity check. |
| Root directory and files: `src/kernel/minix-dir.c:cp32_minix_root_open/cp32_minix_root_next/cp32_minix_file_open/cp32_minix_file_read` | `minix-2.0.0/src/fs/inode.c:new_icopy`, `read.c:read_map/rw_chunk`, `path.c:search_dir`: decode inodes, locate data, search names and read bytes | Root listing is hardware-confirmed. Image 53 adds root-name lookup and regular-file reads with EOF and sparse holes, seven direct zones and both byte orders. Indirect zones, non-ASCII names, subdirectory lookup, descriptors and permissions remain unsupported; cat hardware pending. |
| Shell: `src/kernel/cp32-shell.c:cp32_shell_command` | `minix-2.0.0/src/commands`: user programs over FS/MM interfaces | Kernel-linked command loop occupying FS endpoint, not a user shell. `ls` now reads disk entries. A real FS endpoint must replace this temporary arrangement. |

## Missing

- Filesystem cache, general inode/path lookup, directory mutation, file
  descriptors, writable file I/O and mount lifecycle. Root listing and bounded
  regular-file reads are now implemented.
  References: `minix-2.0.0/src/fs/cache.c:get_block`,
  `inode.c:get_inode`, `path.c:advance`, `open.c:do_open`,
  `read.c:read_write`, `mount.c:do_mount`. Suggested location: future `src/fs/`.
  Mostly architecture-independent, but all storage must use the device IPC ABI.
- Complete MM lifecycle and executable loader: reference
  `minix-2.0.0/src/mm/forkexit.c`, `exec.c`, `signal.c`; future `src/mm/`.
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

- `memory.c:mem_task` / `cp32-shell.c:cp32_shell_disk_check`: image 50 confirms
  one successful diagnostic with five 512-byte transfers and result=0.
  Repeated cycles and extended operation remain to be captured.
- `minix-super.c:cp32_minix_super_read` through `cp32_shell_fsinfo`: image 50
  reads 24 bytes via MEM and reports no filesystem on the blank disk in the
  supplied image-50 capture. Image 51 confirms the generated fixture's
  inodes=32 zones=63 result and root entries ., .., boot, README from real reads.
- `display.c:glyph_scaled`: image 52 fixes the missing period glyph. Host
  pixel tests pass; verify ls/font periods, erasure and scrolling on the LCD.
- `minix-dir.c:cp32_minix_file_open/cp32_minix_file_read` and shell cat:
  image 53 confirms README text and a missing-file error. Directory errors and
  repeated reads after disk remain to be captured. Host tests include exact
  bytes and preservation on failure.
- `tty.c:cp32_cardputer_key`: image 54 fixes reversed TCA8418 event polarity.
  Confirm held Aa, lowercase after release and Fn independence on hardware.
- `clock.c`, `proc.c` and TTY: retain command/scrolling regressions and capture
  CLOCK/IRQ progress through 5000 and beyond. Current image-49 capture ends
  after a bounded run; it does not establish long-duration stability.

## Suspicious or Incomplete Code

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
2. Validate image 54's keyboard fix, then extend root-name lookup to
   subdirectory paths and support indirect reads. README reads are confirmed.
3. Separate the real FS server from the current diagnostic command loop, then
   implement file descriptors and read operations before writable allocation.
4. Audit SYS fork/exec frame semantics before attempting full MM/user startup.
5. Expand terminal-mode and long-duration context tests; defer legacy-device
   compatibility that does not serve Cardputer hardware.

Validation: clean Xtensa build and all 23 host test scripts pass for image 54.
ELF symbols: `_iram_end=0x403743b0`, `_iram_ext_end=0x40382414`,
`_stack_top=0x3fcd1fd0`; filesystem runtime code is in extended IRAM. Generated
initialized prefix: 8253 bytes of rodata. Real backend/MEM adapter tests cover
ten cat/directory/disk cycles with unchanged full-disk checksums.
No flashing was performed by the agent. Hardware evidence currently stops at
the user's image-53 capture; image 54's keyboard correction remains hardware-unverified.
