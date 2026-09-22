# MINIX V2 demo and root directory — image 51

The boot RAM disk now contains a deterministic MINIX V2 filesystem. It is
volatile and recreated before service tasks start on every boot. No hardware
storage or flash filesystem is modified. `tools/make_minix_demo.py` creates
`src/build/minix-demo.img` (65536 bytes) and an initialized-prefix C header
used by `minix-demo.c:cp32_minix_demo_init`. The prefix occupies 8253 bytes
of constant data in the firmware; the full disk remains the existing BSS array.

Layout: 1024-byte blocks and zones, 32 inodes, one inode bitmap, one zone
bitmap, two inode-table blocks, root at zone 6, boot directory at zone 7,
and README at zone 8. The superblock declares 63 zones on the physical
64-block disk. Block 63 is outside the filesystem, retaining the existing
disk diagnostic's last-sector scratch area. Allocation maps reserve bit zero
and unavailable bits. Timestamps are zero for reproducible output.

Reference behavior comes from MINIX 2.0 `fs/inode.c:new_icopy`,
`fs/read.c:read_map`, `fs/path.c:search_dir`, `fs/type.h` and `fs/super.c`.
CP32 decodes 64-byte V2 inodes and 16-byte directory entries explicitly.
Allocation-map chunks are endian-converted 16-bit words, as in MINIX.
This preserves the disk ABI without depending on Xtensa C struct alignment.

`minix-dir.c:cp32_minix_root_open` reads the superblock, verifies root inode
allocation/type/link count/size, and validates each used direct zone against
device geometry and the allocation map. It publishes a handle only on success.
`cp32_minix_root_next` skips deleted entries, checks inode numbers and their
allocation bits, and returns NUL-terminated 14-character names. Device reads
must return exactly the requested count. Both functions use the shell's MEM
IPC reader; they do not directly access the RAM backend.

This image-51 milestone's limits are explicit: root listing only, seven direct zones, printable
ASCII names without slash, and no file-content read, path traversal, indirect
zone support, mount state or file-descriptor API. Unsupported names or larger
directories return an error instead of silently truncating the listing. These
checks are not a filesystem consistency checker; target inode contents and
parent/dot relationships are not fully verified by the listing operation.
Image 53 subsequently adds root-name regular-file reads; see minix-file-read.md.

## Hardware check

Confirm `[TEST MINIX-DIR 51]`, then run `fsinfo`:

```
MINIX V2 superblock: inodes=32 zones=63
Not mounted
```

Run `ls`, expecting actual disk directory entries:

```
.
..
boot
README
```

Repeat `disk` (expected `[RAM IPC V49 result=0]`), `ls` and `fsinfo`. Follow
with write/mm/sys/ipc and LCD/clock regressions. Normal MEM traces keep V49
because the device service is unchanged; only the first eight requests and
every 5000th are printed. The subsequent image-51 hardware capture confirms
disk result=0, two root listings and the expected superblock geometry; see
hardware/minix-dir-v51.log. The user reported incorrect LCD periods, fixed
in image 52 (LCD-DOT 52), whose visual check remains pending.

## Host validation

Tests exercise production readers with the generated fixture, both byte
orders, scaled zones, directory zone crossings, full-length names, deleted
entries, malformed geometry/inodes/maps, duplicate/out-of-range zones and
short/failed reads. Fixture checks verify inode links and zone allocation.
An integration test initializes the real RAM backend from the generated
firmware header, then uses the production shell adapter and MEM handler for
ten listing/disk cycles while verifying an unchanged full-disk checksum.
IPC scheduling itself remains modeled in that test; silicon validation is
separate. All 23 host scripts and the clean Xtensa build pass.
