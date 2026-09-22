# RAM-disk device IPC

Image 49 activates `mem_task()` at the existing MINIX `MEM` endpoint (-4).
It serves the existing 65,536-byte volatile RAM disk without resetting or
formatting it. This is the device layer for future filesystem work; the
diagnostic shell still occupies `FS_PROC_NR`, and `ls` is not a directory read.

## Request and reply

Use the existing definitions in `src/include/minix/com.h` and
`src/include/minix/callnr.h`; no new message layout or opcode is introduced.
FS sends a request with SENDREC to MEM. Only `FS_PROC_NR` is accepted as the
message source, matching MINIX's `driver_task()`. Other sources, including
stale hardware notifications, are ignored rather than replied to.

| Field | Meaning |
| --- | --- |
| `m_type` | `DEV_OPEN`, `DEV_CLOSE`, `DEV_READ`, or `DEV_WRITE` |
| `DEVICE` | `RAM_DEV` (0); other memory-device minors are unavailable |
| `PROC_NR` | Owner of the buffer, supplied by trusted FS |
| `POSITION` | Nonnegative byte offset, not a sector number |
| `COUNT` | Positive byte count for reads/writes |
| `ADDRESS` | Virtual buffer address in `PROC_NR`'s map |

The reply goes to FS with `m_type=TASK_REPLY`, `REP_PROC_NR` set to the
original buffer owner, and `REP_STATUS` containing transferred bytes or a
negative error. Save the owner before filling reply fields: `REP_STATUS`
aliases the request's `PROC_NR`. Completion is synchronous; the memory driver
does not use SUSPEND/REVIVE.

OPEN and CLOSE validate the minor and return OK; there is no exclusive-open
state. Reads/writes need not be sector aligned. The backend's sectors are
512 bytes, and a normal MINIX 1 KiB block transfers in one request.
IOCTL and SCATTERED_IO are not implemented.

## Transfer contract

The driver validates the complete caller buffer with `numap()` before touching
disk or memory, as MINIX `memory.c:m_schedule` does. CP32 returns EFAULT for
mapping failures (the reference returns EINVAL). Negative positions and
nonpositive counts return EINVAL; unknown minors return ENXIO.

At or beyond the end of the disk, a valid read/write returns zero. A transfer
crossing the end returns the remaining byte count. Subtraction bounds the
range without adding an unchecked offset and length. The full requested
buffer must still map, even for an EOF or short transfer.

Data moves through a 64-byte task-stack buffer, using `phys_copy()` with the
physical address returned by `numap()`. The driver never dereferences the
client's virtual address directly. FS may broker another process's buffer;
this is the existing trusted MINIX FS/driver contract, not a new isolation
mechanism. `/dev/mem` and `/dev/kmem` are not exposed.

## Hardware check

After manually flashing image 49, confirm `[TEST RAM-IPC 49]` and run `disk`.
The command saves the last 512-byte sector through IPC, writes a pattern,
reads and checks it, restores the saved bytes, and reads them again to verify
restoration. Two bounded static buffers keep this work off the small FS stack.
Expected success is `[RAM IPC V49 result=0]`. RAM service traces report 512
bytes per operation for the first eight requests and every 5000th thereafter.

On failed or partial pattern I/O, the client still attempts restoration.
A failed restore or verification reports an error rather than success; the
preservation guarantee depends on those operations completing successfully.
Do not use this diagnostic concurrently with a future filesystem that owns
the last sector without first reserving a test region or replacing the check.

Repeat `disk`, then exercise `ls`, `write`, `mm`, `sys`, and `ipc`, checking
LCD behavior and continued CLOCK progress. Image 50 hardware confirmed one
complete disk cycle and the blank-superblock read; see hardware/minix-super-v50.log.
Image 51 adds a generated MINIX V2 boot image and root-directory reads. Its
63-zone geometry excludes the physical final block, keeping this diagnostic's
sector outside the filesystem. The older CP32 private format marker remains
unrelated to MINIX formatting. File-content operations and mounting remain
future work; see minix-directory.md for the current image's hardware procedure.
