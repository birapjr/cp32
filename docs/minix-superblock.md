# Read-only MINIX superblock recognition

Image 50 adds `fsinfo`, reading 24 bytes at byte offset 1024 through the same
MEM DEV_READ request path as disk I/O. It never writes or formats the disk.
The blank boot RAM disk should print `No MINIX filesystem`; its normal MEM
trace, if still within the first-eight rate limit, reports a 24-byte read.

The layout comes from `minix-2.0.0/src/fs/super.h`, `super.c:read_super` and
`type.h:d2_inode`. The parser accepts original V2 magic 0x2468 in little or big
endian form. Original V1 magic is recognized as unsupported; other unrecognized
magic is reported as absent. Extended variants are not supported.

Explicit byte decoding avoids dependence on C structure alignment. Validation
checks inode/map counts, zone shift (0–4), nonzero signed file-size limit,
device capacity, metadata before data zones, and bitmap capacity including
reserved bit zero. Division bounds zone counts without multiplying untrusted
32-bit values. Caller geometry remains unchanged on failure. Short or failed
device reads report a separate I/O failure, never an empty filesystem.

A valid result prints inode and zone counts and `Not mounted`. It does not
validate bitmap contents, root inode, directory entries, file block pointers,
or filesystem consistency. There is no filesystem mount or user file API yet.
Image 53 adds kernel-linked read-only cat; see minix-file-read.md.
As of image 51, ls separately validates and reads the root directory; see
minix-directory.md. The private RAM format helper still does not produce a
MINIX filesystem. Image 51 provisions a generated demo at boot, so its normal
fsinfo result is now 32 inodes and 63 zones, not the image-50 blank-disk result.

Host tests cover both byte orders, valid zone shifts, metadata/map bounds,
hostile geometry, short/error reads, and unchanged output on failure. The MEM
integration test reads a blank disk via the production shell adapter and
device handler and verifies its checksum is unchanged. These tests model IPC;
hardware validation is a separate step.
