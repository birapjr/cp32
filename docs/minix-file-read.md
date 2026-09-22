# Root file reads — image 53

`cat README` now resolves the directory entry and reads the file's actual
contents through MEM IPC. `cat /README` is equivalent. Expected output:

```
CP32 MINIX V2 RAM filesystem.
Read-only filesystem bring-up.
```

`cat missing` reports `File not found`; `cat boot` reports `Is a directory`.
Names are case-sensitive, at most 14 printable ASCII bytes, with an optional
single leading slash. Multiple components are explicitly rejected. This is
root-name lookup, not a complete MINIX pathname resolver or FS server.

The implementation extends `minix-dir.c` with `cp32_minix_file_open` and
`cp32_minix_file_read`. Reference behavior: MINIX `fs/path.c:search_dir`,
`fs/inode.c:new_icopy`, and `fs/read.c:rw_chunk/read_map`. Explicit endian
decoding preserves the disk ABI on Xtensa; every storage access uses the
provided reader, which the shell connects to MEM DEV_READ.

Open validates the target allocation bit, inode type/link count/size and all
used direct zones before publishing a handle. Zero zones represent sparse
regular-file holes and read as zeros, as in MINIX. Nonzero zones must be
within filesystem geometry and marked allocated. Duplicate direct zones and
files requiring indirect zones are rejected. Empty files return immediate EOF.

Each read returns up to 64 bytes, stopping at a zone boundary or EOF as needed.
A staged buffer ensures a failed/short device read leaves both the caller's
buffer and file position unchanged. No allocation, write or shared open-file
table is introduced. The caller-owned handle needs no resource-releasing close.

The shell is a text viewer: it converts LF to CRLF for the console, replaces
other nonprintable bytes with `?`, and puts the prompt on a new line. The file
reader itself returns exact bytes, including NUL and sparse-file zeros. Console
output still passes through the established TTY batching path.

Limits: seven direct zones, root filenames only, no seek, descriptor API,
permission enforcement, symlinks, indirect zones, mounted filesystem state,
or writable file operations. These helpers remain in the kernel-linked FS
diagnostic task until a real filesystem server is introduced.

## Validation

All 23 host scripts and a warning-free clean Xtensa build pass. Tests cover
both byte orders, full-length/case-sensitive names, missing and directory
targets, sparse/empty files, EOF, scaled/crossing zones, invalid metadata/maps,
duplicate/out-of-range zones, and injected short/error reads. Failed opens
preserve the output handle; failed reads preserve data and position.

The real RAM backend integration runs the production cat handler and MEM
adapter and verifies exact README console text. Ten cat/list/disk cycles
preserve the full-disk checksum. Host IPC is modeled; hardware remains pending.

Confirm `[TEST MINIX-READ 53]`, run cat README, cat /README, cat missing and
cat boot, then repeat cat after disk. Regress ls/fsinfo/mm/sys/ipc, LCD output
and clock progress. The demo image and its final-block scratch reservation
are unchanged from image 51.
