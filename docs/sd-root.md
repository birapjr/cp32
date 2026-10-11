# SD-backed MINIX root (image 130)

The default build reads its MINIX V2 filesystem from the Cardputer Adv microSD
slot. Neither the 64 KiB RAM disk nor the compressed demo bytes are linked into
this firmware. The filesystem's four-block cache remains, plus one 512-byte SD
sector buffer. Programs are still loaded into the existing executable SRAM
slot; SD is storage, not executable RAM or swap.

Image 125 adds synchronous sector writes and a privileged `patch` maintenance
command for existing file data. Application descriptors remain read-only;
image 126 adds small root-file creation, image 127 adds append/growth, and
image 128 adds shrink-only truncation with zone reclamation. Image 129 adds
hard-link creation; image 130 adds file/link deletion and last-link reclamation.
Timestamps and
general POSIX write are not ported.
The device task serializes SD access. `disk` still validates the superblock
through read IPC and never performs its old RAM write test on SD.
A missing card or bad volume prints a boot error and leaves the console usable.
Insert the card before boot. Removal, replacement and live remount are not
supported; reboot after changing media. There is no silent RAM fallback.

## Build artifacts

From `src/`:

```sh
make clean && make
make tests
make size segments sections
```

Outputs:

- `build/cp32.bin`: default SD-root firmware, image 130, hardware SPI2 transport.
- `build/sdcard.img`: whole-card MBR image, with one primary type-0x81 MINIX
  partition starting at sector 2048. Contains all six current application ELFs.
- `build/minix-demo.img`: the bare 64 KiB MINIX volume, for an already prepared
  partition. The tiny fixture size is independent of the firmware's storage
  limit; larger externally prepared MINIX V2 volumes can be used.

`make sdimage` regenerates the card image. These targets only write regular
host files, never devices. Creating the image does not flash the board or card.

For the previous RAM-backed recovery configuration:

```sh
make STORAGE=ram clean
make STORAGE=ram
make STORAGE=ram flash
```

Recovery artifacts live in `build-ram/`, so switching backends cannot reuse
objects compiled for the other mode. The boot marker also reports `backend=ram`
or `backend=sd`. Only run a flash command when ready to update the board.

## Prepare a spare card

The generated whole-card image replaces the card's partition table and initial
data. Use a spare card or back up its contents first. Copying `sdcard.img` as an
ordinary file onto a FAT/exFAT card does not work: this driver mounts MINIX V2
blocks directly, and does not contain a FAT/exFAT layer.

On macOS, first identify the external SD reader with `diskutil list`. Confirm
its capacity and identity. Replace `N` below with that exact disk number; do
not target the system disk. From the repository root:

```sh
diskutil unmountDisk /dev/diskN
sudo dd if=src/build/sdcard.img of=/dev/rdiskN bs=1m
sync
diskutil eject /dev/diskN
```

Insert the card into the Cardputer Adv, flash `build/cp32.bin` using the existing
workflow, and reboot. No card or board has been written by this implementation
session.

One primary MINIX partition is supported; multiple type-0x81 partitions are
rejected as ambiguous. Bounds are checked against the card's CSD capacity.
Partitions must fit the existing signed-byte-position ABI (at most 0x7ffffe00
bytes). GPT, extended partitions, FAT and exFAT are not supported. Raw MINIX V2
media without an MBR are also supported: the superblock limits the visible
volume, including on a larger physical card. Existing V2 restrictions (14-byte
names, supported zone sizes and metadata validation) remain in force.

## Hardware acceptance

Expected boot identifiers:

```text
[FEATURE FS-UNLINK 130]1
[SD V130 transport=spi2 init-hz=400000 read-hz=4000000]
[ROOT V130 backend=sd result=0 capacity=65536]
[TEST FS-UNLINK 130]
```

Run:

```text
storage
fsinfo
disk
run /boot/ls /boot
hello --files
hello --exec-fail
hello --exec
run /boot/exec /boot/echo SD exec works
mm
```

Expect six executables listed, successful reads and exec, and no allocation
leaks. `disk` reports `[DISK IPC V125 result=0]`. Reboot and repeat to confirm
the data is loaded from the card again. Also boot once without a card: the
shell must remain responsive with a nonzero ROOT result and capacity=0.

ROOT errors: -1 card command/I/O, -2 timeout, -3 unsupported card response/CSD,
-4 CRC failure, -5 first-sector read failure, -6 missing/ambiguous/out-of-range
MINIX partition, -7 oversized partition, -8 invalid/unreadable MINIX superblock.
Image 123 GPIO transport is hardware-tested: the retained
[card capture](hardware/sd-root-v123.log) confirms mounting, directory reads,
exec failure recovery, two PID/file-preserving replacements, exec-to-echo,
MM and disk checks. The user's separate no-card capture reports ROOT=-2,
capacity=0 and a responsive shell. Card model/capacity and bad-volume hardware
checks remain unrecorded.

Image 124 SPI2 reads are hardware-tested: the
[user-log excerpt](hardware/sd-spi-v124-excerpt.log) confirms mount, directory
reads, two exec replacements and disk/MM checks. The user reports faster SD
access; no measured throughput figure is available. No-card SPI2, card
variation and bad-volume hardware checks remain pending. TTY WRITE ticks
measure display output, not disk throughput.

Image 125 persistence/restoration is hardware-observed in the retained log. Keep the
existing prepared card; no image rewrite or format is needed.

## Memory and tests

Image 123 reclaimed 86,632 SRAM bytes versus image 122; image 124 used
1,216 bytes of that margin and image 125 uses another 3,008 bytes for writes. The SD build returns 64 KiB to MM by increasing the
heap reservation from 128 to 192 KiB. Inward page alignment exposes 188 KiB to
MM rather than the previous 124 KiB. Stack and fixed application reservations
are unchanged. Remaining space before the app code's data alias is 17,744 bytes
(17.3 KiB), versus 880 bytes in image 122. The optional RAM build keeps 128 KiB.

Image-123 SD ELF: `_iram_end=0x403743e8`, `_iram_ext_end=0x403885f8`,
`_heap_start=0x3fc9fa28`, `_heap_end=0x3fccfa28`,
`_runtime_stack_end=0x3fce2a30`. No ramdisk or embedded demo symbols remain.

Host tests exercise SD v1/v2 initialization, SDSC byte versus SDHC block
addressing, command CRC vectors, response/token timeouts, data CRC rejection,
chip-select cleanup, partition validation, sector-boundary reads, failed-read
cache invalidation, all six ELF loads through the real FS/loader, device IPC
write bounds/read-only lockout, no-media behavior, and the image/ELF memory contract. Host
protocol tests model a card, not physical SPI wiring. Image 124 additionally
tests scalar/burst equivalence, transport failures, minimum read-token wait,
FIFO byte order, unaligned buffers, 64-byte boundaries, UPDATE ordering,
counter wrap and stuck-controller timeouts. All three clean builds (SPI2 SD,
GPIO SD, RAM) and all 43 host scripts pass. ELF placement checks pass:
`_iram_end=0x403743f8`, `_iram_ext_end=0x40389368`,
`_heap_start=0x3fca0aa8`, `_heap_end=0x3fcd0aa8`,
`_runtime_stack_end=0x3fce3ab0` for image 125. These are image-125 placement figures; image-126 figures appear below. The RAM recovery image has 416 bytes of remaining app-alias
margin and deliberately retains its existing commands; `patch` is SD-only.

## Next milestones

1. Validate image 125 write completion, same-boot readback and reboot persistence.
2. Port MINIX allocation and write lifecycle: create, grow, truncate, inode
   metadata updates, dirty-buffer ordering and sync. Test interrupted updates
   and recovery before claiming a complete writable filesystem.
3. Expand process lifecycle and application allocation into reclaimed MM
   memory. SD does not enlarge the fixed executable slot or provide swap.

## References and port invariants

MINIX 2.0 `kernel/memory.c:m_schedule`, disk-driver request handling and
`fs/super.c` separate block transport from filesystem parsing. CP32 preserves
that separation, message ownership, bounded copies, and existing inode/ELF
validation. It replaces a resident RAM backing store with an SD SPI device;
it does not add ESP-IDF or change the call0/scheduler/context contracts.

- [M5Stack Cardputer Adv pin map](https://docs.m5stack.com/en/core/Cardputer-Adv):
  SD CS=12, MOSI=14, clock=40, MISO=39; LCD uses different pins.
- [M5Stack SD example](https://docs.m5stack.com/en/arduino/m5cardputer/sdcard).
- [Espressif GPIO register definitions](https://raw.githubusercontent.com/espressif/esp-idf/v5.4.2/components/soc/esp32s3/register/soc/gpio_reg.h)
  and [IO mux definitions](https://raw.githubusercontent.com/espressif/esp-idf/v5.4.2/components/soc/esp32s3/register/soc/io_mux_reg.h):
  register references only, no runtime/library dependency.
- [SD Association specifications](https://www.sdcard.org/downloads/pls/archives/),
  Physical Layer SPI chapter; command sequence/capacity interpretation also
  cross-checked against the primary [Arduino SD implementation](https://github.com/arduino-libraries/SD/blob/master/src/utility/Sd2Card.cpp).

## Hardware SPI2 transport (image 124)

MINIX `kernel/at_wini.c:w_waitfor` bounds controller waits and fails requests
when hardware stops responding. CP32 preserves bounded error returns and
single-owner block transfers; ESP32-S3 uses MMIO, GPIO-matrix routing and a
64-byte FIFO instead of x86 port I/O/ATA. No interrupts are masked, no DMA or
SPI interrupt is enabled, and no ESP-IDF library is linked.

SPI2 uses the board's 40MHz XTAL divided to 400kHz for identification, then
4MHz after valid OCR/CSD. GPIO12 keeps CS asserted across command, payload
batches and CRC. Each batch resets CPU FIFOs, writes little-endian FIFO words,
synchronizes configuration with UPDATE, starts USR and waits for its documented
self-clear. Each wait is bounded by 2,400,000 CPU cycles (at least 10ms at
240MHz). A controller timeout latches failure until reboot/reinitialization;
subsequent requests cannot start a transfer on the stuck controller. Card CRC
errors remain ordinary failed reads. Token polling includes delays so the
faster clock cannot shorten the card's allowed response interval below 256ms.

The previous GPIO transport is available explicitly, with isolated objects:

```sh
make SD_SPI=soft clean && make SD_SPI=soft
make SD_SPI=soft flash
```

It produces `build-sd-soft/cp32.bin` and reports `[SD V130 transport=gpio]`.
The default `make flash` uses SPI2. Neither build automatically falls back.

Register/sequence references (definitions only):
- [Espressif SPI register definitions](https://raw.githubusercontent.com/espressif/esp-idf/v5.4.2/components/soc/esp32s3/register/soc/spi_reg.h)
- [Espressif SPI low-level sequencing](https://raw.githubusercontent.com/espressif/esp-idf/v5.4.2/components/hal/esp32s3/include/hal/spi_ll.h)
- [Espressif GPIO signal matrix](https://raw.githubusercontent.com/espressif/esp-idf/v5.4.2/components/soc/esp32s3/include/soc/gpio_sig_map.h)

## Persistent file patching (image 125)

From `src/`, build/flash using the existing workflow, then run on the board:

```text
cat /readme
patch /readme 0 SD25
cat /readme
cat /boot/readme
```

Expect `Patch verified: 4 bytes`. Both paths refer to the same inode and must
start with `SD25`. Reboot normally and run `cat /readme` again: it must still
start with `SD25`. Restore the original prefix with:

```text
patch /readme 0 CP32
cat /readme
hello --exec
hello --exec
disk
mm
```

`patch path offset text` accepts a decimal byte offset and 1–64 literal bytes,
all within one 512-byte sector and the existing file size. It adds no newline.
This is a privileged kernel-shell maintenance operation: it can patch the
fixture's mode-0444 data file. It is not an application write syscall or a
permission model. Direct/single/double-indirect allocated data are supported;
executables, directories, holes, sector crossings and growth are rejected.
Size, allocation maps, inode pointers, timestamps and other metadata are
unchanged. Application handles stay read-only. No new card image is needed.

The path is shell -> filesystem mapping -> DEV_WRITE IPC -> root sector
read/modify/write -> SD CMD24, including FIFO bursts and CRC16. Completion
requires an accepted response token, bounded busy release, clean CMD13 R2,
and a full-sector readback matching every byte. There is no dirty write cache
or automatic retry. Cache entries are invalidated before and after patching.
A device write/readback failure disables further writes until reboot; reads
remain available. An error after CMD24 can leave media changed. Sector writes
are not claimed atomic under power loss; journaling/recovery is still pending.

Boot and `disk` never write SD. Only an explicit `patch` command uses this new
file operation. Host tests cover scalar/FIFO writes, SDSC/SDHC addressing,
CRC rejection, busy timeout, status errors, readback mismatch, write lockout,
cache invalidation, remount persistence, unchanged surrounding bytes/metadata,
indirect mapping and parser/preflight rejection. All 43 host scripts and the
three clean firmware builds pass. Image 125 persistence/restoration is now observed; image 126 creation is pending.

MINIX reference: `fs/read.c:rw_chunk` maps existing blocks and preserves bytes
outside a partial write; this milestone uses synchronous completion instead
of MINIX dirty buffers, and rejects its not-yet-ported `new_block` path.
SD command/status sequencing was cross-checked against the primary
[Arduino SD implementation](https://raw.githubusercontent.com/arduino-libraries/SD/master/src/utility/Sd2Card.cpp)
and the SD Physical Layer SPI chapter; no hosted/Arduino runtime is linked.

## Small-file creation (image 126)

Image 125's retained [two-boot log](hardware/sd-write-v125.log) shows `SD25`
read from the card after startup, then a verified patch back to `CP32` and
successful reread. The initial write is not in this capture. The user confirms the first boot was intentionally restarted to test persistent
SD changes. There is no unexplained-reset issue from that capture; this still
does not establish power-loss atomicity.

Image 126 adds a privileged, create-only shell operation. Keep the existing
card image; no reformat or card rewrite is needed. After flashing, run:

```text
create /note hello SD
ls
cat /note
stat /note
```

Expect `Created /note: 8 bytes`, a regular mode-0644 file of size 8, and
`hello SD`. Reboot and run `cat /note` again. Repeating the create command
must report `Create failed: 11` without changing the file. Then test:

```text
patch /note 0 HELLO
cat /note
hello --exec
hello --exec
disk
mm
```

Names are root-only, 1–14 bytes; text is 1–64 literal bytes with no added
newline. The operation reserves a free inode and one 1024-byte data zone,
zeros stale inode/data bytes, writes the contents and inode, then publishes
the directory entry. It reuses deleted directory slots or extends the root
within its existing first block. Existing names, exhausted maps/directory,
other zone sizes and larger root directories are rejected before mutation.
File growth, deletion, nested creation and application write syscalls remain
future work. UID/GID and unavailable timestamps are zero.

MINIX reference: `super.c:alloc_bit`, `inode.c:alloc_inode`, and
`open.c:new_node` (inode written before directory publication). CP32 keeps
that publication order using synchronous verified SD writes and its existing
single FS owner, instead of introducing MINIX's dirty buffer lifecycle yet.
An I/O error can leave reserved but unreachable resources, or a fully created
file if final publication completed. There is no automatic rollback/retry;
inspect after reboot. Tests inject failures before/after each complete write,
not torn physical sectors. Reclamation and torn-sector recovery are unported.

Build-only validation: all 44 host scripts and clean SPI2/GPIO/RAM builds pass.
Tests cover both disk byte orders, exact name limits, bitmap padding/final
bits, disk/inode/directory exhaustion, deleted-slot reuse, zeroing, duplicate
protection, cached reads, remount persistence and every one of the 23 write
boundaries. The production FS -> root -> SD model also creates and remounts
a file before loading all six existing ELFs. Shell parser tests pass.

SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x40389c94`,
`_heap_start=0x3fca1470`, `_heap_end=0x3fcd1470`,
`_runtime_stack_end=0x3fce4470`. Heap remains 192KiB (188KiB whole pages), with
15,248 bytes before the application alias. RAM recovery keeps 416 bytes of
margin and excludes the new SD-only allocator from linking because extended
IRAM sections are retained by the linker. No board/card writes were performed.
Image 126 hardware creation/reboot acceptance remains pending.

## Persistent append and file growth (image 127)

Image 126 [hardware capture](hardware/fs-create-v126.log) confirms creation of
/note, directory visibility, contents `hello SD`, inode 13, size 8 and link count 1.
Reboot readback/duplicate rejection are not in that capture. The earlier
image 125 restart was intentional, as clarified by the user; investigation closed.

Image 127 adds `append path text`, a privileged shell operation using normal
pathname resolution (relative paths and nested directories are supported).
Each call appends 1–64 literal bytes, with no automatic newline. Existing
non-executable regular files can grow through seven direct and 256 single-
indirect zones, up to 263 KiB with 1024-byte zones, subject to available disk
space and the superblock size limit. The current fixture volume is only 64 KiB.
Applications still have read-only descriptors; this is not the POSIX write ABI.

Keep the existing card. Flash from `src/` with `make flash`, then run:

```text
cat /note
append /note !
cat /note
stat /note
```

For the 8-byte note from image 126, expect `Appended: 1 bytes`, `hello SD!`,
and size 9. Reboot and repeat `cat /note` and `stat /note`; content and size
must persist. Appending is not idempotent: repeating the command adds another
byte. Follow with `hello --exec` twice, `disk`, and `mm`. No card-image rewrite
is necessary, and the agent has not flashed or written your card.

MINIX reference: `fs/read.c:read_write/rw_chunk` selects EOF for append,
allocates missing blocks, then advances inode size. CP32 reuses its bitmap
allocator, reserves/zeros new data and (when needed) indirect-table zones,
verifies data via existing SD write/readback, and publishes inode size last.
Sector crossings are split; at most one data zone and one indirect table
are needed for a 64-byte call. UID/GID, mode, link count and timestamps are
preserved. There is no wall-clock timestamp or multi-client write support yet.

Sparse partial EOF zones, nonzero unused pointers, double-indirect growth,
larger zone geometries, executables and directories are rejected. Complete
space checks occur before mutation, including both zones needed at the
seven-direct-zone boundary. Errors can leave reservations or a pointer beyond
EOF; such a pointer blocks later growth until a recovery policy can handle it.
No rollback/retry is attempted. Tests show old EOF or complete append under
errors before/after complete writes; torn physical sectors/power-loss atomicity
are not covered. Original bytes before EOF remain unchanged in those tests.

Validation: 44 host scripts and clean SPI2/GPIO/RAM builds pass without compiler
warnings. New tests cover both byte orders, empty files, sector/zone crossings,
direct-to-indirect transition, existing indirect tables, final supported index,
zeroing of reused storage, ENOSPC preflight, invalid inputs and every write
failure boundary. The production FS -> root -> SD model verifies append after
remount and then loads all six application ELFs; command parsing is tested too.

SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x4038a3a4`,
`_heap_start=0x3fca1bc8`, `_heap_end=0x3fcd1bc8`,
`_runtime_stack_end=0x3fce4bd0`. Heap 192 KiB (188KiB whole pages) is preserved;
app-alias margin 13,360 bytes. RAM recovery remains at 416 bytes and keeps its
existing command set. Image 127 hardware append/reboot acceptance is pending.
Next: truncation/deletion with zone reclamation, followed by writable application
handles, timestamps and recovery rather than treating this as a complete FS.


## Persistent truncation and zone reclamation (image 128)

The latest [image 127 two-boot capture](hardware/fs-append-v127.log) reads
`hello SD!` from `/note` after restart, confirming persistence of appended
content. The append command and stat output are not in this capture; the
remaining hardware regression matrix is still open.

Image 128 adds `truncate path length`. It keeps the inode, directory entry,
links, permissions and file prefix while shrinking an existing non-executable
regular file. Length is a decimal byte count from zero through the current
size; growth is rejected. Normal relative/nested pathname resolution works.
Supported files use 1024-byte zones and direct/single-indirect mappings up to
263 KiB. Double/triple-indirect mappings are rejected before mutation.
Applications still have read-only descriptors.

Keep the existing card and flash only the firmware using `make flash` in
`src/`. Use an unused name for this disposable test file:

```text
create /trim hello SD!
truncate /trim 5
cat /trim
stat /trim
```

Expect `Truncated to 5 bytes`, content `hello`, and size 5. Fully restart the
board and repeat cat/stat. Then run:

```text
truncate /trim 0
stat /trim
append /trim reused
cat /trim
stat /trim
hello --exec
hello --exec
disk
mm
```

Expect size zero before append, then content `reused` and size 6. Restart and
check those again. `/note` remains available for the earlier persistence test.
No card-image rewrite is needed. Hardware image-128 acceptance is pending;
no firmware flashing or card writes were performed by the agent.

MINIX reference: `fs/link.c:truncate` releases data and indirect zones, using
`fs/super.c:free_bit`. CP32 preserves the zone-bitmap layout but synchronously
persists the smaller inode and detaches discarded mappings before clearing
allocation bits. A retained partial zone has its discarded tail zeroed. Empty
files release their data and indirect table; later appends can reuse them.
Preflight checks every mapped zone, allocation bit and duplicate pointer.
A same-size call can remove unused mappings left beyond EOF by a failed append.
This is not a volume-wide consistency checker or recovery of unreachable leaks.

An I/O error can leave the smaller size visible and leak detached allocations;
there is no automatic rollback or retry. The device still disables further
writes after write/readback failure until reboot. Tests inject failures before
and after complete writes and verify that no remaining mapping refers to a
freed zone. They do not model torn sectors or establish power-loss atomicity.
Deletion, nested creation, timestamps, application write handles and recovery
remain future work.

Validation: all 44 host scripts and clean SPI2 SD, GPIO SD and RAM builds pass
without compiler warnings. Tests cover both byte orders, zero/same/partial
sizes, direct and indirect reclamation, retained-table cleanup, tail zeroing,
invalid mappings, every complete-write failure boundary, and released-space
reuse. The real FS/root path over the SD mock is remounted after shrinking and
emptying a file, then reuses its zone; all six application ELFs still load.
Shell parsing and ELF placement checks pass.

Image-128 SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x4038ad08`,
`_heap_start=0x3fca29b0`, `_heap_end=0x3fcd29b0`,
`_runtime_stack_end=0x3fce59b0`. Heap remains 192 KiB (188 KiB whole pages),
with 9,808 bytes before the application alias. The SD-only mapping snapshot
uses 1,056 static bytes instead of task stack space. RAM recovery excludes
this allocator/command and has 400 bytes of remaining alias margin.


## Persistent hard links (image 129)

The [image 128 capture](hardware/fs-truncate-v128.log) confirms truncate-to-zero,
empty readback, append of `reused`, reading via a relative path, and successful
exec/disk/MM checks. It does not show partial shrink or readback after a restart
following those mutations. The first boot ends during startup and is followed
by another boot; this capture alone does not identify the cause. The user's
previous clarification about an intentional image-125 reset remains unchanged.

Image 129 ports MINIX `fs/link.c:do_link` to `link existing /alias`. The existing
source can use normal relative or nested paths. The new name must be a root
entry of 1–14 bytes, and must not already exist. Regular files, including
executables, can be linked; directory links are rejected. The new entry refers
to the same inode and data, allocates no inode or zone, and increments the link
count (MINIX limit 127). Changes through either name are visible through both.
The destination uses a free slot or grows within the existing first root block;
additional directory blocks and other zone geometries remain unsupported.

This remains a privileged shell operation, not a new application syscall.
On ESP32-S3 it uses the existing single FS owner and synchronous verified SD
sector writes, with no new hardware behavior or scheduler changes. Unlike
MINIX's deferred dirty-buffer updates, CP32 writes the increased link count
before publishing the directory entry. Failed writes can leave an overcount,
or an entry outside the old directory size; no rollback or automatic retry is
attempted. Recovery and torn-sector/power-loss atomicity remain unimplemented.
Data, timestamps and allocation bitmaps are unchanged by successful linking.

Keep the card as it is. After flashing image 129, use an unused alias:

```text
link /trim /trimlink
stat /trim
stat /trimlink
cat /trimlink
append /trimlink !
cat /trim
```

Expect `Linked /trimlink`, matching inode numbers, link count 2 (if initially
1), and the appended `!` visible through `/trim`. Restart, then repeat both
stat commands and cat. Repeating `link /trim /trimlink` must report
`Link failed: 11` without increasing the link count. Finish with `hello --exec`
twice, `disk`, and `mm`. The existing card needs no reformat. No board or card
writes were performed here; image-129 hardware acceptance is pending.

All 44 host test scripts and clean SPI2/GPIO/RAM builds pass without compiler
warnings. Tests cover both disk byte orders, shared append/truncate visibility,
unchanged allocation maps/data, duplicate/invalid/directory/full/link-limit
rejection, deleted-slot reuse, and failures before/after every complete write.
The real FS/root path over the SD mock remounts the alias, checks shared inode
and link count, modifies it and remounts again; all six application ELFs load.
Shell parser and ELF sections/segments checks pass.

Image-129 SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x4038b344`,
`_heap_start=0x3fca3048`, `_heap_end=0x3fcd3048`,
`_runtime_stack_end=0x3fce6050`. Heap stays 192 KiB (188 KiB whole pages);
app-alias margin is 8,112 bytes. RAM recovery excludes the SD-only command and
allocator and retains 416 bytes of margin. Next: unlink with last-link inode
reclamation, followed by writable application handles and recovery.


## File and link deletion (image 130)

The [image 129 capture](hardware/fs-link-v129.log) confirms both `/trim` and
`/trimlink` after boot: inode 14, size 7, links 2, content `reused!`. Directory
listing and exec/disk/MM pass. The log does not include the original link or
append commands. It supports persisted shared identity/content, not the entire
failure/rejection test matrix.

Image 130 adds `rm path` and its synonym `unlink path`. They remove exactly
one directory name. Other hard links retain their contents and the link count
is reduced. Removing the last link clears the inode, frees all supported data
and indirect-table zones, then releases the inode bitmap bit. Directory slots,
inodes and zones can be reused by `create`. File bytes are not securely erased.

Regular files, including executables, are supported. Relative paths use the
shell working directory; existing nested parents such as `/boot` work. Parent
directories must fit one 1024-byte zone. Last-link reclamation supports direct
and single-indirect mappings, including sparse files, up to 263 KiB. Unsupported
geometry, double/triple-indirect last-link mappings, directories, dot entries,
trailing slashes and missing names are rejected before writes. There is no
recursive removal, wildcard expansion, rmdir or application unlink syscall.

CP32 rejects any target inode held by an open descriptor with error 13 (busy).
MINIX retains an unlinked inode until the last open reference closes; that
lifecycle is not ported here. The maintenance shell runs after applications
return and close descriptors, so its normal delete flow does not need deferred
reclamation. This guard also applies when another name still references the
file. Parent directory size is preserved; the deleted slot becomes empty.

MINIX references: `fs/link.c:do_unlink/unlink_file`,
`fs/inode.c:put_inode/free_inode`, and `fs/super.c:free_bit`. CP32 keeps
name-removal-before-link-count ordering. Last-link preflight validates and
snapshots every direct/single-indirect allocation before changing media, using
the same bounded snapshot as truncation. The name and inode pointers are
persistently detached before bitmap bits can be reused. Synchronous verified
SD writes replace MINIX's dirty-inode/buffer lifecycle; no new MMIO behavior,
ESP-IDF dependency, scheduler or call0 ABI changes are introduced.

Errors after a write may mean the name has already disappeared. An interrupted
operation may leave an overcount or allocated but unreachable inode/zones;
there is no automatic retry, rollback or recovery. Tests cover errors before
and after complete writes, not torn sectors or power-loss atomicity. The
existing device write-failure latch remains in force until reboot. Filesystem
metadata is assumed consistent across different inodes; this is not fsck.

### Hardware acceptance

Keep the existing card and flash only firmware (`make flash` from `src/`).
Use unused disposable names, leaving `/trim` and `/trimlink` intact:

```text
create /deltest delete me
link /deltest /delalias
rm /deltest
cat /delalias
stat /delalias
```

Expect `Removed /deltest`, content `delete me`, and links 1. `cat /deltest`
should fail. Fully restart, then repeat both reads and stat: deletion and the
surviving link must persist. Next run:

```text
unlink /delalias
ls
create /deltest reused
cat /deltest
stat /deltest
hello --exec
hello --exec
disk
mm
```

The alias should disappear; the recreated file should contain `reused`, size
6, links 1. Without intervening allocations the original inode should be
reused. Restart and verify the alias is absent and the recreated file persists.
Repeating removal of the missing alias must fail without further mutation;
`rm /boot` must reject directory removal. No card reformat is needed.
Hardware image-130 acceptance is pending; no board/card writes performed here.

### Validation and placement

All 44 host scripts and clean SPI2/GPIO/RAM builds pass without warnings;
ELF sections/segments and whitespace checks pass. Tests cover both byte orders,
shared-link preservation, nested deletion, busy/invalid rejection, zero/direct/
indirect/sparse last-link reclamation, inode/zone/name reuse, and every complete-
write interruption point. Shared-link failures never undercount visible names;
last-link failures never free still-referenced storage. The real FS/root path
over the SD mock remounts after each deletion, verifies the remaining content,
then recreates a file using its freed inode and zone. All six app ELFs load.

Image-130 SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x4038bb18`,
`_heap_start=0x3fca3880`, `_heap_end=0x3fcd3880`,
`_runtime_stack_end=0x3fce6880`. The heap remains 192 KiB (188 KiB whole pages),
with 6,016 bytes before the application alias. RAM recovery retains 416 bytes
and its existing command set; these mutation commands are SD-only.
