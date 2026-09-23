# Reading CP32 alongside MINIX 2

Starting with image 57, portable filesystem code is under `src/fs/`, matching
`minix-2.0.0/src/fs/`. The former kernel/minix-dir.c and minix-super.c and their
headers have been removed; there is one implementation in the new locations.
Earlier bring-up notes retain their historical filenames.

| CP32 file | Same MINIX reference file and responsibilities | CP32 entry points |
| --- | --- | --- |
| `src/fs/super.c` | `src/fs/super.c`: `read_super`, bitmap access in `alloc_bit/free_bit` | `cp32_minix_super_read`, `cp32_fs_allocated` (read-only bitmap check) |
| `src/fs/inode.c` | `src/fs/inode.c`: `get_inode`, `rw_inode`, `new_icopy` | `cp32_fs_open_directory`, `cp32_fs_file_inode`: fetch/decode/validate inode state |
| `src/fs/path.c` | `src/fs/path.c`: `last_dir`, `advance`, `search_dir` | `cp32_fs_resolve_path`, `cp32_minix_root_next`: path walking and directory scanning |
| `src/fs/open.c` | `src/fs/open.c`: `do_open`, `common_open` | `cp32_minix_file_open`, `cp32_minix_dir_open`, `cp32_minix_root_open`: open orchestration |
| `src/fs/read.c` | `src/fs/read.c`: `read_write`, `rw_chunk`, `read_map` | `cp32_minix_file_read`: direct-zone reads, sparse holes and EOF |
| `src/fs/utility.c` | `src/fs/utility.c`: `conv2`, `conv4` | `cp32_fs_u16`, `cp32_fs_u32`: explicit disk-byte conversion |

The reference paths in the middle column are relative to `minix-2.0.0/`.
Each CP32 source begins with its reference responsibilities. Function names
remain prefixed because these bounded helpers do not yet implement the full
MINIX interfaces; renaming them to do_open/get_inode would imply semantics
such as descriptors, inode caches and credentials that are not present yet.

## Headers

The filenames also match MINIX: `fs.h` is the master include, `const.h` holds
status/bounds, `type.h` holds the reader callback and platform placement macro,
`super.h` holds validated geometry, `inode.h` holds directory inode/traversal
state, `file.h` holds open read-only handles, and `proto.h` groups declarations
by source file. They are reduced CP32 structures, not copies of the complete
MINIX tables. Disk structures remain explicitly decoded to avoid Xtensa
alignment/layout assumptions.

## What stays outside fs

- `src/kernel/memory.c`: MEM device task, corresponding to MINIX's kernel
  memory driver. Filesystem calls reach it through messages.
- `src/kernel/ramdisk.c`: CP32 internal-SRAM device backing.
- `src/kernel/minix-demo.c` and `tools/make_minix_demo.py`: CP32 boot fixture
  provisioning, not filesystem algorithms. No exact MINIX counterpart is claimed.
- `src/kernel/cp32-shell.c`: temporary kernel-linked command client. It calls
  filesystem helpers through `../fs/fs.h`; it is not yet a separate user shell.

No empty main.c/cache.c/write.c or fake server files are added just to resemble
the reference. Add each matching file as its actual functionality is ported.
Image 58 also aligns MM, as described below. Kernel IPC and task APIs remain
unchanged; the existing numap/mem_copy implementations move into system.c.

## Memory manager (image 58)

Compare `src/mm/` directly with `minix-2.0.0/src/mm/`:

| CP32 file | MINIX reference responsibility | Current functions |
| --- | --- | --- |
| `src/mm/main.c` | `src/mm/main.c`: receive work, dispatch, reply | `mm_task`, `cp32_mm_handle_request`, caller validation and removable receive trace |
| `src/mm/alloc.c` | `src/mm/alloc.c`: `alloc_mem`, `free_mem`, allocator initialization/coalescing | `cp32_mem_alloc`, `cp32_mem_free`, `cp32_mem_owned`, private initialization and block table |
| `src/mm/mm.h` | MM master include | Existing shared kernel/process types, MM protocol and local prototypes |
| `src/mm/proto.h` | Declarations grouped by implementation file | Allocation and service entry points |

The old `src/kernel/mm.c` is removed. CP32 retains its existing bounded
owner-tagged allocator rather than silently replacing it with MINIX's hole
list. `mm_task` keeps its name because a second `main` symbol would collide
with the kernel entry in the shared firmware image. A complete fork/exec/
signal server and independent MM process table are still future work, so no
empty forkexit.c/exec.c/signal.c/mproc.h files are added.

`numap` belongs to `src/kernel/system.c` in MINIX. Its CP32 implementation and
the checked `mem_copy` helper therefore move there, not into MM. They still
validate the process table/maps and handle flat internal-SRAM task buffers.
`src/kernel/memory.c` remains the MEM RAM-device task, separate from MM.
The early kernel heap geometry/startup code likewise remains in the kernel.

MM objects build under `src/build/mm/` so MM's main.o does not collide with
kernel/main.o. The service/allocator test moves to `tests/mm/test_main.py`;
its address-translation tests exercise numap from kernel/system.c. Run the
same `make -C src tests` suite. Image 58 identity is `[TEST MM-LAYOUT 58]`;
check mm repeatedly, then nested filesystem reads, disk and other services.

## Build and tests

The existing `src/Makefile` compiles filesystem objects under `src/build/fs/`;
separate object directories prevent collisions with kernel names such as
main.c or future read.c. Changes to FS headers rebuild filesystem objects and
the shell client. No additional runtime or hosted libc dependency is introduced.

Host tests moved to `tests/fs/test_super.py` and `tests/fs/test_path.py`; the
existing top-level wrappers still run with `make -C src tests`.
`tests/kernel/test_memory_task.py` retains the cross-layer MEM integration test
and now links the six FS translation units. The layout test checks matching
reference filenames and the absence of the old combined implementations.

Image identity: `[TEST FS-LAYOUT 57]`. Commands and disk contents are intended
to remain unchanged. Recheck ls boot, cat boot/README, cat README, disk, and
ordinary service/LCD behavior after flashing because splitting objects changes
ELF addresses even when behavior is preserved.

## Remaining tree comparison (image 59)

The five standard primitives formerly in kernel/klib.c now each occupy the
same filename as MINIX under src/lib/ansi/: memcpy.c, memset.c, strcpy.c,
strcmp.c and strtol.c. Implementations, signatures and section attributes are
preserved. Objects live under build/lib/ansi; runtime tests are in
tests/lib/ansi/test_runtime.c. This is source alignment, not replacement
with the reference libc. In particular, strtol still lacks full range/error
handling. CP32's calibrated busy-wait delay stays in kernel/klib.c.

| Existing location | Comparison and decision |
| --- | --- |
| kernel/start.c, main.c, proc.c, clock.c, system.c, tty.c, misc.c, memory.c | Already match MINIX kernel responsibilities; retain filenames. ESP32 startup, timer and register handling are adaptations. |
| lib/other/printk.c | Already matches the reference library location. |
| kernel/port.c | Retain CP32 trap/context glue. A future kernel/table.c split could centralize shared globals after auditing EXTERN ownership; do not move the whole file. |
| kernel/cardputer.c, display.c, serial.c, wdt.c, hardware_init.c | Cardputer/ESP32-specific drivers; retain. PC keyboard/console filenames would imply a misleading hardware correspondence. |
| kernel/mpx32.S, vectors.S, irq.S, klib32.S, esp32s3.ld | Xtensa startup, exception and linker implementation; retain. |
| kernel/ramdisk.c, minix-demo.c; tools/make_minix_demo.py | CP32 backing storage and boot fixture; retain. MINIX MEM request handling already lives in kernel/memory.c. |
| kernel/cp32-shell.c | CP32 diagnostic client; retain until an independent FS server and user ABI exist. Moving it to commands/sh would not implement a MINIX shell. |
| include/esp32* and include/minix/cp32* | Architecture/protocol additions; retain alongside existing compatibility headers. |

Add reference files such as fs/main.c, cache.c and mm/forkexit.c when their
actual server/cache/lifecycle functionality is implemented. Empty matching
files would not help readers follow the book. Image 59 requires a hardware
regression despite unchanged algorithms and memory-region boundaries.

## Single-indirect reads (image 60)

Regular-file mapping now extends MINIX read.c responsibilities with a private
cp32_read_map helper: seven direct zones and 256 single-indirect entries.
inode.c validates the indirect root; file.h retains superblock geometry in
the caller-owned handle. Metadata is decoded explicitly without allocating a
1024-byte cache block on a kernel task stack. Directory traversal remains
direct-only, and double-indirect files remain unsupported. The boot fixture
is unchanged, so hardware indirect-path validation needs a larger fixture.

## Hardware fixture (image 61)

The production generator now adds boot/INDIRECT: inode 4, seven direct data
zones 9..15, indirect table zone 16 and tail zone 17. Run cat boot/INDIRECT;
after 224 labeled lines, INDIRECT READ OK confirms the indirect tail reached
the console. The regular parser and shell need no manual diagnostic hook.
The final scratch block remains reserved; all content is volatile across reset.

## File positions (image 62)

cp32_minix_file_seek lives in fs/open.c, corresponding to MINIX do_lseek.
It updates caller-owned handle offsets for start/current/end origins within
the signed 32-bit range. There is no descriptor table or read-ahead cache yet.
The CP32 shell's tail command uses this API to find the last ten lines with
bounded stack buffers, then shares cat's rendering. tail boot/INDIRECT provides
a short hardware regression across the direct/single-indirect boundary.

## Double-indirect mapping (image 63)

fs/read.c follows MINIX read_map's two-level index calculation and inode.c
reads the double root. Regular files support 7+256+65536 zones; directories
remain direct-only. The caller-owned file.h handle stores both indirect roots.
boot/DOUBLE is a sparse fixture: tail boot/DOUBLE ends in DOUBLE INDIRECT READ
OK, exercising double-indirect metadata without reading its sparse prefix.
