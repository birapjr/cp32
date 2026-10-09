# SD-backed MINIX root (image 123)

The default build reads its MINIX V2 filesystem from the Cardputer Adv microSD
slot. Neither the 64 KiB RAM disk nor the compressed demo bytes are linked into
this firmware. The filesystem's four-block cache remains, plus one 512-byte SD
sector buffer. Programs are still loaded into the existing executable SRAM
slot; SD is storage, not executable RAM or swap.

This first SD milestone is read-only. Existing pathname, directory, metadata,
file-offset and exec operations use the same FS -> device IPC path as before.
The device task serializes SD access. DEV_WRITE returns EROFS; `disk` validates
the superblock through read IPC and never performs its old RAM write test on SD.
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

- `build/cp32.bin`: default SD-root firmware, image 123.
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
[FEATURE SD-ROOT 123]1
[ROOT V123 backend=sd result=0 capacity=65536]
[TEST SD-ROOT 123]
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
leaks. `disk` reports `[DISK IPC V123 result=0]`. Reboot and repeat to confirm
the data is loaded from the card again. Also boot once without a card: the
shell must remain responsive with a nonzero ROOT result and capacity=0.

ROOT errors: -1 card command/I/O, -2 timeout, -3 unsupported card response/CSD,
-4 CRC failure, -5 first-sector read failure, -6 missing/ambiguous/out-of-range
MINIX partition, -7 oversized partition, -8 invalid/unreadable MINIX superblock.
The initial SPI transport deliberately runs slowly (mode 0, bounded polling,
no DMA, CPU cycle delays of at least 2 microseconds per half-clock at 240 MHz).
GPIO/timing and real-card compatibility remain **hardware-unverified**.

## Memory and tests

Compared with hardware-tested image 122, the DRAM heap starts 86,632 bytes
earlier (84.6 KiB reclaimed). The SD build returns 64 KiB to MM by increasing the
heap reservation from 128 to 192 KiB. Inward page alignment exposes 188 KiB to
MM rather than the previous 124 KiB. Stack and fixed application reservations
are unchanged. Remaining space before the app code's data alias is 21,968 bytes
(21.5 KiB), versus 880 bytes in image 122. The optional RAM build keeps 128 KiB.

Image-123 SD ELF: `_iram_end=0x403743e8`, `_iram_ext_end=0x403885f8`,
`_heap_start=0x3fc9fa28`, `_heap_end=0x3fccfa28`,
`_runtime_stack_end=0x3fce2a30`. No ramdisk or embedded demo symbols remain.

Host tests exercise SD v1/v2 initialization, SDSC byte versus SDHC block
addressing, command CRC vectors, response/token timeouts, data CRC rejection,
chip-select cleanup, partition validation, sector-boundary reads, failed-read
cache invalidation, all six ELF loads through the real FS/loader, device IPC
write rejection, no-media behavior, and the image/ELF memory contract. Host
protocol tests model a card, not physical SPI wiring. Both storage builds and
all 43 host scripts pass; the next required step is the hardware sequence above.

## Next milestones

1. Validate SD root reads and exec on the Cardputer, including absent-card and
   bad-volume behavior. Retain capture and identify card model/capacity.
2. Move SPI transfers to a documented hardware controller for throughput,
   preserving bounded errors and task ownership.
3. Add tested SD sector writes, then MINIX inode/zone allocation, create/write/
   truncate, dirty-buffer ordering and sync. Add interrupted-write/reboot
   recovery tests before claiming writable persistent filesystem support.
4. Expand process lifecycle and application allocation into the reclaimed MM
   memory. This change does not enlarge the fixed application slot or add swap.

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
