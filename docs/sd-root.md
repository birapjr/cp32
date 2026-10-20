# SD-backed MINIX root (image 142)

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
Image 131 adds file renaming/moving to unused names; image 132 adds numeric chmod. Timestamps and general
POSIX write are not ported.
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

- `build/cp32.bin`: default SD-root firmware, image 142, hardware SPI2 transport.
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
[FEATURE FS-EMPTY 142]1
[SD V142 transport=spi2 init-hz=400000 read-hz=4000000]
[ROOT V142 backend=sd result=0 capacity=65536]
[TEST FS-EMPTY 142]
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

It produces `build-sd-soft/cp32.bin` and reports `[SD V142 transport=gpio]`.
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


## File renaming and moving (image 131)

`mv oldpath newpath` and `rename oldpath newpath` rename an existing regular
file, including an executable, within its parent or into another existing
directory. Both paths are explicit filenames; relative paths use the shell
working directory. Parents must fit one 1024-byte block, filenames are 1–14
bytes, and no new directory/data/inode allocation occurs. A destination entry
can reuse an empty slot or extend the directory within that block. Within the
same parent, the old slot is rewritten, so renaming also works when it is full.

The inode, content, permissions, timestamps, other hard links and final link
count remain unchanged. Open read descriptors remain usable across rename.
The same name, or a different name already referencing the same inode, is a
successful no-op matching MINIX. A destination referencing a different inode
is rejected with error 11. Directory renaming, replacement of existing files,
larger parent directories, and a public application rename syscall are not
ported. Cross-directory moves need one temporary link and reject link count
127; same-directory renames require no link-count adjustment.

MINIX reference: `fs/link.c:do_rename` publishes the destination before removing
the source when parents differ. CP32 adapts this for synchronous verified writes:
increase the inode link count, publish the destination (and parent size if
needed), clear the old entry, then restore the original count. Same-parent
rename is one entry write. Failure tests before/after each complete write show
at least one of the two names remains readable and counts never understate the
visible links. Failure may leave both names or an overcount; it does not imply
automatic rollback or successful completion. Torn-sector/power-loss atomicity
and repair remain unimplemented. No new hardware or scheduler behavior is used.

### Hardware acceptance

Image 130 deletion has not received a hardware log yet; retain its pending
acceptance checks. Image 131 also needs hardware verification. Flash firmware
only, keep the card, and use unused names:

```text
create /mvtest rename me
link /mvtest /mvalias
stat /mvtest
mv /mvtest /mvnew
cat /mvnew
cat /mvalias
stat /mvnew
mv /mvnew /boot/moved
cat /boot/moved
stat /boot/moved
```

Expect `Rename OK`, unchanged content `rename me`, original inode number and
links 2. Both old names should fail lookup. Restart and repeat cat/stat on
`/boot/moved` and `/mvalias`; verify the old names remain absent. Renaming
`/boot/moved` to `/mvalias` should succeed as a no-op and preserve both links.
Trying to rename it over `/readme` should fail with error 11, preserving both
files. Then test cleanup with `rm /mvalias` (survivor links 1),
`rm /boot/moved`, followed by repeated `hello --exec`, `disk`, and `mm`.
No board/card writes were performed by the agent.

All 44 host scripts and clean SPI2/GPIO/RAM builds pass without warnings.
Tests cover both byte orders, same/cross-parent rename, open descriptor and
hard-link identity, same-inode no-op, invalid/directory/destination/full/limit
rejection, in-place rename in a full directory, deleted-slot reuse, and every
complete-write failure boundary. The FS/root path over the SD mock remounts
after each rename and checks old-name absence, inode/count/data/zone identity;
all six existing app ELFs still load. Parser and ELF placement checks pass.

Image-131 SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x4038c480`,
`_heap_start=0x3fca4278`, `_heap_end=0x3fcd4278`,
`_runtime_stack_end=0x3fce7280`. Heap stays 192 KiB (188 KiB whole pages);
remaining app-alias margin is 3,456 bytes. RAM recovery keeps 416 bytes and
excludes these SD-only mutation commands. Future features must account for
this shrinking placement margin before adding more resident code or buffers.


## Persistent mode changes (image 132)

The [image 131 hardware log](hardware/fs-rename-v131.log) shows creation of
/sample with `aaa`, successful `mv /sample /renamed`, and directory listing.
After another boot it lists `/boot/moved` and reads `aaa`. The cross-directory
move command, inode/link-count comparison and full regression matrix are not
in this capture. Retain those checks, and image130 deletion checks, as pending.

Image132 adds `chmod mode path` for regular files and directories. Mode is
one to four octal digits with value 000–777; a leading zero is allowed.
Symbolic forms and requests to set setuid/setgid/sticky are rejected. Like
MINIX `fs/protect.c:do_chmod`, the type bits are preserved and permission bits
are replaced; unsupported existing special bits are cleared. An unchanged mode
requires no write. The two-byte inode mode is changed via synchronous verified
SD write/readback and cache invalidation. No file data, allocation, owner,
link count or timestamps are changed. No geometry restriction beyond the
existing read-only inode/path validation is introduced.

This is a privileged shell operation. MINIX owner/group authorization and
ctime updates remain unported. Read/write and directory search bits are
metadata only at present. Existing execute-bit checks in launch and app exec
use the updated mode. Clearing all execute bits denies launch/replacement;
setting them does not bypass ELF validation. The patch/append/truncate guards
also reject files carrying execute bits. Hard links share the updated mode,
and open descriptors observe it through fstat without losing their offsets.

### Hardware acceptance

Keep the prepared card and flash firmware only. Use unused disposable names:

```text
create /modetest mode
link /modetest /modealias
chmod 600 /modealias
run /boot/ls -l /modetest /modealias
```

Expect both names to show `-rw-------`, the same inode (check stat), and links2.
Restart and repeat the long listing. Then exercise the executable policy:

```text
chmod 644 /boot/echo
run /boot/ls -l /boot/echo
run /boot/echo blocked
chmod 555 /boot/echo
run /boot/echo restored
hello --exec
hello --exec
disk
mm
```

Expect `File is not executable` while echo is mode644, then successful output
and exit after mode555. Restart and verify the restored mode. Invalid
`chmod 888 /modetest` must print usage and leave the mode unchanged. Cleanup
can use `rm /modealias` then `rm /modetest`. No reformat is needed. Image132
hardware validation is pending; no board/card writes performed by the agent.

All44 host scripts and clean SPI2/GPIO/RAM builds pass without warnings.
Tests cover both byte orders, exact preservation of all non-mode bytes,
hard-link/fstat visibility, directory type preservation, zero/no-op modes,
special-bit clearing, invalid modes, read failure and both sides of the write
failure boundary. The production app exec test clears/restores execute bits
and verifies denied replacement leaves the process intact without MM staging.
FS/root over the SD mock remounts both a changed alias and its restoration;
all six application ELFs still load. Parser and ELF placement checks pass.

Image132 SD ELF: `_iram_end=0x403743f8`, `_iram_ext_end=0x4038c7f8`,
`_heap_start=0x3fca4648`, `_heap_end=0x3fcd4648`,
`_runtime_stack_end=0x3fce7650`. Heap remains192KiB (188KiB whole pages);
app-alias margin2,480 bytes. RAM recovery excludes the command/allocator and
retains416 bytes. No new static staging buffer or hardware behavior is added.
Failed writes may leave either mode visible; the existing write lockout until
reboot applies. No torn-sector/power-loss atomicity is claimed.


Image133 retains the filesystem behavior and adds a blinking input cursor.
The [image132 hardware capture](hardware/fs-chmod-v132.log) confirms chmod644
on echo blocks execution and chmod555 restores a successful launch. Mode
persistence through restart and alias checks remain pending. Image133 keeps
192KiB of SD heap; _runtime_stack_end=0x3fce76a0 leaves2,400 bytes of placement
margin. Its cursor also fits the RAM recovery build (320 bytes remaining).


## Creation in existing subdirectories (image 134)

The user visually confirms the image133 cursor works. Its retained
[log excerpt](hardware/tty-cursor-v133-excerpt.log) shows a successful
`hello --line` interaction and `pwd` after returning to the shell. This accepts
the basic visual/input behavior, not every wrap/scroll edge case in the host
model. The cursor remains enabled in image134 and RAM recovery.

Image134 extends `create path text` to existing subdirectories. Absolute paths
and shell-current-directory-relative paths work; the parent is resolved before
allocation. The final component remains1–14 bytes and initial data1–64 bytes
(with no added newline and subject to the63-character shell line limit).
New regular files have mode0644, links1, uid/gid0 and zero timestamps. Existing
names are rejected without changes; parent directories must already exist and
fit one1024-byte block. Deleted directory entries can be reused. Creating
missing directories, additional directory blocks and general application
write/creat syscalls remain unported. New hard-link destinations remain root-only.

MINIX reference: `fs/open.c:new_node` uses `last_dir`, allocates the inode,
forces it to disk, then publishes the directory entry. CP32 retains this order
with synchronous verified SD writes and one FS owner. Creation and rename now
share the bounded parent/name/slot resolver. Only the actual parent inode size
is extended; creating under /boot must not change root's inode or entries.
The old root-only helper remains compatible for existing internal callers.
No new hardware, dynamic buffer, scheduler or call0 ABI change is introduced.

### Hardware acceptance

Keep the existing card; flash firmware only. Use unused names:

```text
cd /boot
create nested hello SD
ls
cat nested
stat nested
cd /
cat /boot/nested
```

Expect `Created /boot/nested: 8 bytes`, contents `hello SD`, size8 and links1.
Restart, then repeat cat/stat on `/boot/nested`. Repeating creation of that
path must return error11 without changing the file. Next run:

```text
append /boot/nested !
mv /boot/nested /boot/nested2
cat /boot/nested2
rm /boot/nested2
create /boot/nested reused
cat /boot/nested
hello --exec
hello --exec
disk
mm
```

Expect `hello SD!` before removal and `reused` after recreation. Check that
root listing has no new `nested` entry. `create /missing/note test` must fail
without creating any file or directory. No reformat is necessary; image134
hardware validation is pending and no board/card writes were performed here.

All44 host scripts and clean SPI2/GPIO/RAM builds pass without compiler warnings.
Tests cover both byte orders, cwd path resolution, nested parent growth only,
root and nested names coexisting, duplicate/missing/non-directory/invalid/full
rejection, deleted-slot reuse and every complete-write interruption boundary.
FS/root over the SD mock creates under /boot, remounts, reads, appends, renames,
remounts, deletes and confirms absence after remount; six existing ELF loads
still pass. Existing root-create, rename and other mutation tests also pass.

SD ELF: `_iram_end=0x40374624`, `_iram_ext_end=0x4038c89c`,
`_heap_start=0x3fca4700`, `_heap_end=0x3fcd4700`,
`_runtime_stack_end=0x3fce7700`. Heap stays192KiB (188KiB whole pages), with
2,304 bytes of alias margin; RAM recovery retains320 bytes. Both IRAM endpoints
and ELF sections/segments were checked. As before, interrupted creation can
leak allocations or have completed despite an error; no automatic rollback,
retry, torn-sector atomicity or recovery is claimed.


## Directory creation (image 135)

The [image134 hardware capture](hardware/fs-create-path-v134.log) confirms
relative creation under /boot, read/stat (inode13, size8, links1), rename within
/boot, moving to /root-new, reading, deleting, and successful hello --exec.
Reboot persistence, duplicates and the complete fault matrix are not shown.

Image135 adds `mkdir path`, absolute or current-directory-relative, under an
existing parent. A new directory receives mode0755, size32, links2 and one
zeroed1024-byte zone containing `.` (self inode) and `..` (actual parent inode).
The parent gains one link. UID/GID and unavailable timestamps stay zero. New
directories support normal traversal, listing, file creation and deeper mkdir.
Final names are1–14 bytes. Parents must fit one1024-byte block; an empty slot
is reused or directory size increases by16 within that block. Duplicates,
missing/non-directory parents, full maps/parents and parent link count127 are
rejected before mutation. No automatic intermediate creation, -p, rmdir,
directory rename or application mkdir syscall is added.

MINIX reference: `fs/open.c:do_mkdir/new_node` creates the inode, dot entries
and parent link accounting. CP32 shares the existing create allocator rather
than adding a second allocator or buffer. Data and dot entries are initialized
and the new inode is persisted before the parent's directory entry is published.
The incremented parent link count is persisted before publication, so complete-
write failures cannot expose a child with an undercounted parent. An error can
leave unreachable allocations or an excess parent count; no rollback or retry
is attempted. Synchronous verified SD writes, single FS ownership and the
existing call0/IPC contracts remain unchanged. No new hardware is accessed.

### Hardware acceptance

Flash firmware only; keep the prepared card and use unused names:

```text
mkdir /work
cd /work
mkdir sub
cd sub
pwd
ls
create note hello SD
cat note
cd ..
pwd
run /boot/ls -ld /work /work/sub
```

Expect /work/sub then /work from pwd, `.`/`..` from the initially empty listing,
content `hello SD`, directory modes `drwxr-xr-x`, links3 for /work and links2
for /work/sub. Creating a regular note does not increase directory links.
Restart, then run:

```text
cat /work/sub/note
cd /work/sub
cd ..
pwd
mkdir /work
mkdir /missing/child
hello --exec
hello --exec
disk
mm
```

The file and parent traversal must persist. Existing /work reports error11;
missing parent creation fails without changes. `rm /work` must still reject a
directory. Directory removal remains future work; file cleanup inside the tree
can use `rm /work/sub/note`. Image135 hardware acceptance is pending. No board
or card writes were performed here and no reformat is required.

All44 host scripts and clean SPI2/GPIO/RAM builds pass without compiler warnings.
Tests cover both disk byte orders, dot entries, nested mkdir/create/chdir,
parent-link accounting, existing/deleted slots, bad parents/names, link limit,
inode/zone exhaustion, and failures before/after every one of24 complete writes.
Visible directories always have initialized dot entries and sufficient parent
link counts. The FS/root SD mock remounts a two-level tree with a file, checks
parent traversal/content/mode/count and still loads all six application ELFs.
Shell parser and ELF sections/segments checks pass; cursor regression remains.

SD ELF: `_iram_end=0x40374624`, `_iram_ext_end=0x4038cc84`,
`_heap_start=0x3fca4b30`, `_heap_end=0x3fcd4b30`,
`_runtime_stack_end=0x3fce7b30`. Heap stays192KiB (188KiB whole pages), with
1,232 bytes of remaining app-alias margin. RAM recovery has336 bytes and retains
its existing command set; mkdir is SD-only. Larger resident features require
placement/memory work first. No power-loss/torn-sector atomicity is claimed.


## Empty-directory removal (image 136)

The user confirms mkdir works and attributes the garbled capture to their own
handling. The [retained image135 log](hardware/fs-mkdir-v135.log) reads
/work/sub/note as `hello sd` after boot. No kernel failure is inferred from
the garbling; the complete mkdir error matrix is not evidenced by this log.

Image136 adds `rmdir path` for empty one-block directories. Absolute and
cwd-relative names work. Only valid `.` and `..` entries may remain; deleted
slots are ignored. A directory containing files or child directories is
rejected with error14. Root, final dot/dot-dot components, and the current
working directory are protected. Open directory descriptors are busy(error13).
The cwd check uses inode identity, including alternate path spellings. Parent
and target must use the supported1024-byte layout; target links must equal2,
so unexplained link-count excesses are rejected rather than guessed away.

MINIX reference: `fs/link.c:remove_dir` checks emptiness, root/cwd and dot names
before unlinking. CP32 shares the unlink removal engine and bounded parent
lookup. It validates dot identities, snapshots allocations, removes the parent
entry, clears the child inode, decrements the parent count, releases zones,
then frees the inode bit. Detaching dot-dot before reducing its parent count
prevents undercounting a still-referenced parent at complete-write boundaries.
An I/O failure can leave the directory absent and leak storage or retain an
excess parent count. There is no retry, rollback, fsck or torn-sector/power-loss
atomicity guarantee. Deleted data is not securely erased. No new MMIO behavior,
static staging buffer, syscall ABI or scheduler change is introduced.

### Hardware acceptance

Keep the existing card and flash firmware only. From `/`, use unused names:

```text
mkdir /rmtest
mkdir /rmtest/sub
create /rmtest/sub/note hello
rmdir /rmtest/sub
```

Expect error14 and the file to remain readable. Continue:

```text
rm /rmtest/sub/note
cd /rmtest/sub
rmdir /rmtest/sub
cd /
rmdir /rmtest/sub
stat /rmtest
```

The attempt while inside sub must report busy(error13). After cd /, removal
should succeed and /rmtest should have links2. Restart and check that sub is
absent, then run:

```text
rmdir /rmtest
mkdir /rmtest
ls /rmtest
rmdir /rmtest
hello --exec
hello --exec
disk
mm
```

The recreated directory should list only dot entries, and final removal must
persist after another restart. `rm /boot` still rejects directories, and
`rmdir /boot` must reject its non-empty contents. No recursive removal is
implemented. Image136 hardware acceptance is pending; no board/card writes
were performed by the agent and no reformat is necessary.

All44 host scripts and clean SPI2/GPIO/RAM builds pass without warnings.
Tests cover both byte orders, non-empty/tombstone directories, root/dot/cwd/open
protection, malformed dot-dot, link counts, inode/zone reuse, and failures
before/after all five complete writes. A visible directory always retains its
allocations and sufficient parent count. FS/root SD-mock remount checks removal
of sub then parent and recreation; all six existing ELF loads still pass.
Existing file unlink/parser/cursor regressions and ELF placement checks pass.

SD ELF: `_iram_end=0x40374624`, `_iram_ext_end=0x4038ceac`,
`_heap_start=0x3fca4d98`, `_heap_end=0x3fcd4d98`,
`_runtime_stack_end=0x3fce7da0`. Heap stays192KiB (188KiB whole pages), but only
608 bytes remain before the application alias. RAM recovery retains336 bytes.
The next substantial feature must first recover placement headroom; do not
silently reduce heap or stacks to fit it.


## Filesystem memory headroom (image 137)

Image 136 hardware evidence is retained in `hardware/fs-rmdir-v136.log`:
`mkdir /rmtest/sub`, parent links=3, nonempty rejection (14), then successful
child and parent removal and an `ls` without either name. Restart persistence,
cwd/open protection and the remaining hardware matrix are still unverified.

Image 137 (`[FEATURE FS-MEMORY 137]1`, `[TEST FS-MEMORY 137]`, ROOT/SD V137)
recovers resident SRAM space before further filesystem extensions. MINIX 2
builds FS as a separate executable; CP32 links it into internal D/IRAM. FS C
now uses `-Os` and ordinary independent function sections. The linker excludes
filesystem objects from low IRAM and explicitly puts their code/literals in
extended internal IRAM, allowing unreachable functions to be garbage-collected.
`CP32_FS_TEXT` documents that placement without merging functions into one
retained section. Non-FS kernel code remains `-O0`; call0 and freestanding flags,
heap reservations, stacks, application alias and on-disk format are unchanged.
The unused create_root compatibility wrapper remains in source for host callers.

All 44 host scripts pass, including mutation/fault tests compiled with `-Os`
and undefined-behavior sanitization. Clean SPI2 SD, GPIO SD and RAM builds pass.
ELF checks cover filesystem entry-point placement, unused-wrapper removal,
heap size and bounds. SPI2 headroom is 6,288 bytes (previously 608); RAM has
3,760 bytes (previously 336); GPIO SD has 7,360 bytes. This is placement headroom,
not additional heap capacity. Target hardware behavior remains to be validated.

After flashing normally, check the image137 marker, blinking cursor, `ls`,
`hello --exec`, `disk` and `mm`. Then use an unused directory name:

```text
mkdir /memtest
create /memtest/note hello
cat /memtest/note
rmdir /memtest
rm /memtest/note
rmdir /memtest
ls
```

The first rmdir must fail with 14; the second must succeed. Reboot and verify
`/memtest` remains absent. Existing images/cards need no reformatting.


## Nested hard links (image 138)

Image137 hardware log (`hardware/fs-memory-v137.log`) confirms file/directory
removal, rejection of rm on a directory, listing, MM allocation/release, SD IPC,
and successful exec preserving PID/files. No restart persistence test is shown.

Image138 ports MINIX link.c:do_link destination-parent lookup to the trusted
shell: `link existing newpath` now accepts nested and cwd-relative destinations.
The source must be a regular file; the destination must not exist. Its parent
must exist and fit in one1024-byte block. Names remain at most14 bytes. Link
counts remain capped at127; no new inode or data zone is allocated. Both names
share data and metadata, and deleting one name retains the other. Directory
links, cross-volume linking, permission enforcement and application link syscall
are not introduced by this feature.

CP32 uses synchronous verified SD writes rather than MINIX's dirty inode cache.
It persists the incremented count before publishing the directory entry and
extends the actual parent inode's size last. A failed write can overcount/leak;
this is not journaling or power-loss atomicity. Existing root-name callers retain
a compatibility wrapper. Production code shares the existing parent resolver.

All44 host scripts pass, including both byte orders, nested/invalid parents,
duplicates, tombstone reuse, every write failure before/after commit and SD
remount/readback after deleting the original name. Clean SPI2/GPIO/RAM builds
pass. SD heap remains192KiB; placement headroom is6352 bytes. Hardware acceptance
is pending; marker `[FEATURE FS-LINK-PATH 138]1` / `[TEST FS-LINK-PATH 138]`.

Use fresh names on hardware:

```text
mkdir /links
create /links/original shared
cd /links
link original alias
stat original
stat alias
rm original
cat alias
cd /
```

Both stat results must show the same inode and links=2. After rm, alias must
still read `shared`. Restart, then `cat /links/alias` and `stat /links/alias`
(expected links=1). `link /readme /links/alias` must reject the existing name;
`link /readme /missing/alias` must reject the missing parent. Finally remove
`/links/alias`, `rmdir /links`, and check `hello --exec`, `disk`, `mm`.


## Cross-sector patches (image 139)

Image138 capture (`hardware/fs-link-path-v138.log`) boots with FS-LINK-PATH138
and reads `shared` from `/links/alias`. This confirms post-boot readback, but the
capture does not include link creation, original deletion or the rejection tests.

Image139 follows MINIX read.c:read_write/rw_chunk transfer splitting. CP32 uses
512-byte verified SD writes instead of dirty MINIX block buffers. A 1–64 byte
patch may cross a sector boundary or a filesystem zone boundary, including
noncontiguous direct zones and transitions to indirect mappings. The two bounded
chunks are fully mapped and checked before any write. No large transfer buffer,
allocation, file-size change or metadata write is introduced. Executable files,
sparse destination holes and writes past EOF remain rejected. All linked names
observe the same changed data. This is still a trusted-shell maintenance command,
not a port of the application write syscall.

Each chunk completes synchronously with cache invalidation on success or error.
A failed second write may leave the first chunk changed; there is no rollback
or whole-patch atomicity. The existing SD write-error latch still requires reboot.

All44 host scripts pass. Optimized/UBSan tests cover both byte orders, byte
preservation, exact/crossed sector boundaries, noncontiguous zones, the direct
to single-indirect boundary, missing second mapping with zero writes, and both
write failures before/after completion. The real FS/root/SD-mock test remounts
and verifies a patch crossing offset512. Three clean builds pass; SD placement
headroom6256 bytes, heap192KiB and stacks unchanged. Hardware is pending:
`[FEATURE FS-PATCH-SPAN 139]1`, `[TEST FS-PATCH-SPAN 139]`, ROOT/SD V139.

Hardware check using a fresh file (each text below contains48 bytes):

```text
create /span 0123456789abcdef0123456789abcdef0123456789abcdef
```

Run the following append command eleven times, producing576 bytes total:

```text
append /span 0123456789abcdef0123456789abcdef0123456789abcdef
```

Then run:

```text
stat /span
patch /span 510 ABCD
cat /span
```

Expect `Patch verified: 4 bytes`, size576 unchanged and `ABCD` replacing bytes
510–513 across the sector boundary. Restart and read the file again; then remove
it with `rm /span`. Also check `hello --exec`, `disk`, `mm` and cursor behavior.


## Independent file copies (image 140)

Image139 hardware capture (`hardware/fs-patch-span-v139.log`) confirms169-byte
file readback, removal, exec, SD IPC and MM allocation/release. It does not show
a sector-spanning patch; that hardware test remains pending.

Image140 adds `cp source destination` to the SD shell. MINIX commands/simple/cp.c
streams descriptor reads/writes; CP32's writable application descriptors are not
yet ported, so this bounded implementation composes existing filesystem helpers.
Both paths resolve relative to cwd or from root. The destination must be new in
an existing one-block parent directory. Copies have independent inode/data and
link count1. Empty files work; their initial reserved zeroed zone is reclaimed
on removal. A64-byte stack buffer replaces MINIX's much larger copy buffer.
Files up to263KiB are accepted, subject to free space. Sparse source holes are
materialized as zero bytes rather than preserving sparse allocation.

The destination starts as mode0644. Ordinary mode bits are applied after all
data is copied, so executable bits are not published on an incomplete copy.
Ownership, timestamps and special bits are not preserved. Existing destinations,
including the same inode under another name, are never overwritten. Directories,
recursive copying, options and automatic replacement are not supported.
Read/write/allocation errors can leave a partial destination or reserved storage;
there is no rollback, atomic-copy or power-loss recovery claim. The source is
never modified. Inspect the destination after an error before removing/retrying.

All44 host scripts and clean SPI2/GPIO/RAM builds pass. Tests cover both byte
orders, multi-block copies, independent patching, empty files, preserved modes,
invalid/existing destinations, all write failures before/after completion, and
SD remount/readback after source deletion. ELF sections/segments checked; SD
placement margin5152 bytes, heap192KiB and stacks unchanged. Hardware pending:
`[FEATURE FS-COPY 140]1`, `[TEST FS-COPY 140]`, ROOT/SD V140.

Use unused short names for the hardware check:

```text
create a hi
cp a b
patch b 0 yo
cat a
cat b
cp a b
```

Expect `Copy OK`, then independent contents `hi` and `yo`. Repeating the copy
must fail with11 (destination exists), leaving b unchanged. Restart and run
`cat b` (expect yo). Then `rm a`, `cat b` (still yo), and `rm b`.
Optional empty-file check: `create e x`, `truncate e 0`, `cp e f`, `stat f`
(size0), then `rm e`, `rm f`. Finish with `hello --exec`, `disk`, `mm`.


## IPC context-return correction (image 141)

Image140 hardware reached Copy OK, verified patch, and read hi from a, then
halted during cat b: EXCCAUSE1, EPC1=0x40374360, PS=0x110. Image140 ELF identifies
that address as the normal SYSCALL in cp32_ipc_trap. The dispatcher routes a
rejected input/return frame to exception_dump with that same cause/address.
The log does not include the rejected frame's general registers.

Disassembly shows optimized filesystem C assigning zero to a15 (including
cp32_cache_read). Runtime validation incorrectly rejected that legal register
value. A timer can save it and IPC scheduling can later select the frame, making
this a timing-dependent return rejection consistent with the observed fault.
MINIX pick_proc selects runnable queue heads; CP32 must additionally validate its
Xtensa call0 frame layout, but only a1 is the stack pointer. a15 is preserved
verbatim and need not be a nonzero frame pointer. Startup-only diagnostic checks
for deliberately initialized test contexts are unchanged.

Image141 removes the nonzero-a15 runtime constraint from trap/frame validation,
return selection and saved-context checks. Blocked handoff and user queue checks
use a1==sp instead. PC, aligned/nonzero SP, owner and runnable-state checks remain.
The host IPC suite now extracts the actual production frame validator and runs
exchanges, blocking/wakeup, interrupts and return selection with a15=0, checking
that the value survives. Invalid PC/SP/alignment cases still fail. All44 scripts
and clean SPI2/GPIO/RAM builds pass; assembly save/restore is unchanged. ELF
sections/segments checked. Hardware confirmation is pending, CTX-A15141.

No card reformatting or file changes are needed. With the existing a and b,
repeat `cat a`, `cat b`, `ls` several times, with pauses to let timer scheduling
run. Expect hi and yo, and a responsive blinking cursor. Then `hello --exec`,
`disk`, `mm`. Send the log if any exception remains; the exact hardware frame
was not captured by image140, so this source-level fix still needs confirmation.


## Empty-file creation (image142)

Image141 capture (`hardware/ctx-a15-v141.log`) confirms repeated hi/yo cat
readback, removal, nonempty-directory rejection/cleanup and SD/MM checks with
no exception in the capture. Extended stress and exec are not shown there.

MINIX open.c creates regular inodes with NO_ZONE and allocates data on write.
Image142 follows this allocation behavior: `create path [text]` accepts zero
through64 initial bytes. An empty inode has size0 and all zone pointers zero;
only the inode bitmap and parent entry/size change. It works with a full data
bitmap if an inode and existing parent slot remain available. Copying an empty
file uses the same path. The existing first-append allocator supplies a zeroed
zone when data arrives. mkdir and nonempty create retain their existing behavior.

This is create-only, not POSIX O_TRUNC: existing paths return error11 and keep
contents. Parents must exist and fit in one block. Names remain1–14 bytes.
After the optional separating space, text remains literal, so additional spaces
are file content. I/O failures can still leak an inode reservation; no rollback
or power-loss atomicity is claimed. No data-zone reservation occurs for size0.

Host coverage includes both disk byte orders, empty copy/read/delete, unchanged
zone bitmap, first append, full-data-map operation, interrupted creation and SD
remount before/after first append. Heap/stacks and on-disk format are unchanged.
Marker `[FEATURE FS-EMPTY 142]1` / `[TEST FS-EMPTY 142]`, ROOT/SD V142.
Hardware validation pending; use an unused short name:

```text
create e
stat e
cat e
```

Expect size0 and no file content. Restart, then:

```text
stat e
append e hi
cat e
create e
cat e
rm e
```

Expect size0 after reboot, hi after append, error11 on duplicate create, and hi
still intact. Optional: before appending, `cp e f`, `stat f`, `rm f` verifies an
empty copy. Finish with `hello --exec`, `disk`, `mm`.


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


## Optional on-device application updater — image145

The SD card can stay inside the Cardputer. Build and flash maintenance firmware
from `src/`:

```sh
make updater
make flash-updater
```

`make updater` creates `src/build-updater/cp32.bin`, embedding the freshly built
hello, echo, cat, wc, ls and exec ELF files. `flash-updater` uses the configured
Makefile serial port, like `make flash`. No SD image needs to be written.
The boot log must show `[UPDATER 145 enabled]`. At the Cardputer prompt type:

```text
apps
```

Expect `/boot/hello installed` (or `unchanged`), one result for each application,
and `Apps update OK`. Repeat `apps`: all six should say `unchanged`. Then run
`hello --file`, restart the hardware, and run `hello --check`, `rm wtest`, and
`hello --exec`. If `/wtest` already exists from an earlier successful test,
check/preserve it before removing it and repeating `hello --file`.

Only the six named `/boot` executables are replaced; the card is not formatted
and other files remain. The command requires an existing mounted, writable
MINIX volume and `/boot` directory. It refuses directory and hard-linked
application targets. Replacement is **in place**, not atomic: keep power on.
A failure stops the batch, leaving earlier applications updated. An incomplete
file stays non-executable until a successful retry verifies it. After a device
I/O failure, reboot the updater before retrying `apps` (the SD driver disables
further writes after such a fault). FS error12 means insufficient free volume
space; free space before retrying. There is no automatic rollback.

The optional payload resides in internal SRAM loaded by the existing firmware
loader. Only updater builds reduce the heap from192 to160 KiB; normal builds
retain192 KiB and contain neither command nor embedded payload. To remove the
temporary updater after installation:

```sh
make
make flash
```

This flashes `src/build/cp32.bin` and leaves the updated SD executables intact.
`UPDATE_APPS=1` selects a separate build directory (also with `SD_SPI=soft`);
RAM recovery rejects this option. No new flash/cache mapping or ESP-IDF runtime
is used. Host/build validation passes; on-device updater validation is pending.


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
