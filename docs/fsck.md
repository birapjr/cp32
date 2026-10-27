# Offline MINIX V2 consistency checks

`tools/fsck_minix.py` is a read-only host tool. It reconstructs inode references
and zone ownership and compares them with on-disk metadata, following the
responsibilities of MINIX2 `commands/simple/fsck.c`. It does not run on the
Cardputer, repair an image, or change the firmware.

## Run

From `src/`, check the generated whole-card image:

```sh
make fsck
```

The target builds the default image if necessary. To check an existing offline
image without building firmware:

```sh
make fsck FSCK_IMAGE=/absolute/path/card-copy.img
```

Or, from the repository root:

```sh
python3 tools/fsck_minix.py card-copy.img
python3 tools/fsck_minix.py card-copy.img --json
```

Use an image copied from an offline card. The tool accepts regular files only,
opens them read-only, and does not operate directly on mounted filesystems or
block/character devices. Checking `build/sdcard.img` checks the generated image;
it does **not** check the card currently in the Cardputer.

Exit statuses:

- `0`: no inconsistency found within the supported format.
- `1`: inconsistent or unsupported inode contents found.
- `2`: invalid/unsupported container or superblock, or host I/O failure.

Text output starts with `MINIX V2: clean` or `MINIX V2: check failed`. JSON adds
allocation totals, byte order and partition offset when geometry is valid.
Reports retain at most 1,000 diagnostic strings and count additional findings.

## Coverage

- Raw MINIX V2 volumes or one primary MBR partition of type `0x81`.
- Both byte orders, 14-byte names, 1–16 KiB zones.
- Seven direct zones, single and double indirection; indirect tables occupy
  one 1 KiB block even when zones are larger. Sparse regular files are valid.
- Superblock, inode table and bitmap geometry; reserved bitmap bit zero.
- Referenced-free and allocated-unreferenced zones; duplicate data/table ownership,
  invalid zone addresses, allocations beyond EOF and reserved tenth-zone use.
- Allocated inode types, file-size limits and nonzero link counts.
- Directory sizes, missing zones, entry names, duplicate names, inode references,
  dot/dot-dot placement and identity, parent cycles and directory hard links.
- Reachability from root and exact link counts, including dot entries and
  regular-file hard links. Stale bytes in free inodes are ignored.

The checker targets CP32 regular files and directories. MINIX V1, 30-byte names,
GPT/logical partitions, special-device inodes and automatic repair are outside
this implementation. It is not a journal, a recovery engine or a guarantee that
file contents are correct. Offline unlinked-but-open inodes are reported because
the checker cannot recover a live process's descriptor state.

No firmware flash or SD application update is required. Run `make tests` in
`src/` to exercise clean and deliberately corrupted fixtures, both byte orders,
scaled zones, direct/indirect/sparse mappings, CLI statuses and unchanged input
bytes. The suite also checks generated raw and whole-card images when present.
