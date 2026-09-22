# Read-only paths — image 56

Source layout update (image 57): implementation now lives in src/fs/path.c,
inode.c, open.c and read.c. See minix-source-layout.md for the book/reference map.

Image 56 extends `cat` and `ls` to subdirectories through MEM IPC. Examples:

```
ls boot
.
..
README

cat /boot/README
CP32 MINIX V2 RAM filesystem.
Read-only filesystem bring-up.
```

The demo now has two directory entries referencing the same README inode:
`/README` and `/boot/README`. Its link count is two. The boot directory size
grows from 32 to 48 bytes; inode and zone counts, 8253-byte initialized prefix,
and scratch-block reservation remain unchanged. This is a generated hard link,
not a new writable link syscall.

MINIX reference: `minix-2.0.0/src/fs/path.c:last_dir/advance/search_dir` walks
components using directory inodes and entries. CP32 preserves that traversal
model with explicit disk-byte decoding and validated direct zones. It uses
bounded automatic buffers and no recursion or new global mount state.

`minix-dir.c:open_directory` opens any validated directory inode.
`resolve_path` starts at inode 1, looks up each component, and requires every
intermediate inode to be a directory. `cp32_minix_dir_open` exposes directory
paths; the existing file open function uses the same resolver. Handle outputs
remain unchanged on failure. Root-only APIs remain available for existing users.

Supported: absolute paths, relative paths starting at root, repeated slashes,
dot and dot-dot entries, 14-byte printable ASCII components, and paths up to
255 bytes. Root dot-dot cannot move above the filesystem root. A trailing
slash requires a directory; regular-file traversal returns Not a directory.
Dot-dot is resolved through the directory, not removed textually, so
`README/../README` correctly fails. Oversized names are rejected, not truncated.
These explicit bounds and trailing-slash checks define CP32's current subset;
they do not claim identical handling of every legacy MINIX pathname edge case.

The shell input line remains limited to 63 bytes. No cd/current-directory state,
symlink following, permissions, mount crossing, indirect zones or file-descriptor
server is added. Directory/file reads still support seven direct zones.

## Validation

All 23 host test scripts and the warning-free clean Xtensa build pass. Tests
cover little/big endian paths, nested/dot/dot-dot/repeated-slash lookup, trailing
slashes, component and total length bounds, missing components, regular files
used as directories, corrupt directory metadata and short/error I/O at every
step. Failed opens do not publish partial handles. Generated inode link counts
are checked against all demo directory references.

The production shell/MEM/real-backend integration verifies nested cat and ls
output and retains the complete disk checksum across ten cat/list/disk cycles.
Hardware is pending. Confirm `[TEST MINIX-PATH 56]`, then run:

- `ls boot` and `ls /boot/..` (root listing).
- `cat /boot/README` and `cat boot/../README` (same README).
- `cat README/..` (Not a directory).
- `cat boot/missing` (File not found).
- `disk`, then repeat nested cat and ordinary service/LCD/clock checks.

User acceptance of image 55's LCD and previously reported issues is recorded
in plan.md/issues.md; its supplied serial log is docs/hardware/lcd-a-v55.log.
