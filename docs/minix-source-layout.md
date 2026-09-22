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
