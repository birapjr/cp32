# CP32

**A bare-metal, Unix-like operating system for the M5Stack Cardputer Adv.**

CP32 adapts MINIX 2.0 architecture to the ESP32-S3 (Xtensa LX7), running directly
on the hardware without ESP-IDF or Arduino. It provides a keyboard/LCD shell,
persistent microSD storage, kernel services and separately compiled applications.
The port is usable for development and experimentation, but is not a complete
MINIX or POSIX system.

[Build and flash](#build-and-flash) · [Start the shell](#start-the-shell) ·
[Commands](#shell-commands) · [Application API](#application-api) ·
[Debugging and validation](#debugging-and-validation)

## Current status

| Area | Implemented |
| --- | --- |
| Kernel | Standalone boot, exception vectors, task scheduling, timer interrupts, blocking message passing, memory allocation and system/device services through IPC. |
| Console | Cardputer keyboard, LCD text output, line input, backspace, scrolling and a blinking block cursor. USB Serial/JTAG provides diagnostics. |
| Storage | Persistent MINIX V2 root filesystem on microSD over hardware SPI2, with a GPIO-SPI alternative and a separate RAM recovery build. |
| Filesystem | File/directory reads, metadata, seeking, creation, writes, sparse extension, truncation, hard links, unlink, empty-directory removal, regular-file moves without replacement, numeric permission changes and free-space reporting. |
| File lifetime | Shared open descriptions, independent opens, descriptor duplication, close-on-exec, and continued access after unlink until final close reclaims storage. |
| Applications | Six ELF programs loaded from SD: `hello`, `echo`, `cat`, `wc`, `ls` and `exec`; arguments, initial environment, PID/parent queries, exit status and heap allocation. |
| Process filesystem state | Application-owned cwd, umask, descriptor namespace and flags, inherited on launch and preserved across successful exec as appropriate. Exit releases owned references. |
| Exec | Replace the foreground application's image while retaining PID and non-CLOEXEC files. Invalid paths/images and failed staging return an error without destroying the running program. |
| Maintenance | Optional firmware installs updated applications directly onto the inserted SD card. Normal firmware excludes its command and payload. |

The host suite contains **48 test scripts**. Normal SD, GPIO-SPI, RAM recovery
and both SD updater variants build and pass image-layout checks. Hardware
captures cover the implemented application filesystem operations; the latest
space-reporting check passed twice and restored the same free-space/inode counts.
Detailed results and outstanding checks live in [plan.md](plan.md),
[issues.md](issues.md) and [hardware captures](docs/hardware/).

### Current boundaries

- The shell is kernel-linked and runs **one trusted foreground application**.
  General fork/wait, concurrent user applications, memory protection, application
  fault recovery, a user-space shell and full libc remain unimplemented.
- Each application has **four file slots (3–6)**; the shared backend has eight.
  Standard streams 0–2 use callbacks. Shell input redirection is supported;
  general descriptor-based standard-stream redirection, output redirection and
  pipelines are not.
- The filesystem is single-mount. Names are at most **14 bytes**, paths at most
  **255 bytes**. Writable parent directories must fit in **one 1 KiB block**.
  Directory growth, directory moves, replacement rename, symbolic links,
  recursive deletion, mounts and hot removal/remount are unsupported.
- Writes/reclamation support **1 KiB zones and direct/single-indirect mappings
  through 263 KiB**, bounded by volume space and the superblock maximum.
  Read support also includes double-indirect files. The supplied demo volume
  is only **64 KiB**; use `df` to check its available space.
- Permission bits affect new file opens and executable loading, but owner/group
  access control, directory-search enforcement, special mode bits and timestamp
  updates are incomplete. Existing descriptors retain their open-time access.
- SD writes are synchronous and verified by readback. There is no journaling,
  automatic repair, rollback or power-loss atomicity. Failed writes can leave
  partial changes, leaked allocations or excess links; further device writes
  are disabled until reboot. Deletion does not securely erase data.

## Build and flash

Install `make`, Python 3, the `xtensa-esp32s3-elf-gcc` toolchain/binutils and
`esptool` on your PATH. Host tests also need a host C compiler (`cc`).
Run Makefile commands from `src/`:

```sh
cd src
make clean
make
make tests
make flash
```

The flash target uses `/dev/cu.usbmodem2101` on macOS. Adjust the device path in
[src/Makefile](src/Makefile) for your computer. If necessary, hold **Go (G0)**
while connecting the USB data cable to enter ROM download mode.

| Build | Command | Output directory |
| --- | --- | --- |
| SD root, hardware SPI2 (default) | `make` | `build/` |
| SD root, GPIO SPI | `make SD_SPI=soft` | `build-sd-soft/` |
| Read-only RAM recovery | `make STORAGE=ram` | `build-ram/` |
| SD application updater | `make updater` | `build-updater/` |
| GPIO-SPI updater | `make SD_SPI=soft UPDATE_APPS=1` | `build-sd-soft-updater/` |

Use the same configuration variables with `clean` and `flash`. RAM recovery
recreates its embedded filesystem at boot; its application mutation APIs return
read-only errors. Updater builds require SD storage.

The default outputs include:

- `build/cp32.elf`, `build/cp32.bin` and `build/cp32.map`.
- `build/{hello,echo,cat,wc,ls,exec}.elf` and individual linker maps.
- `build/minix-demo.img` (filesystem) and `build/sdcard.img` (whole-card image).

`make hello`, `make echo`, `make cat`, `make wc`, `make ls` and `make exec`
build applications individually. Applications, filesystem C and shell-service C
use `-Os`; the remaining kernel generally uses `-O0`. The build remains
freestanding with the Xtensa call0 ABI and internal-SRAM placement checks.

### Prepare storage

The default root is a MINIX V2 partition on the microSD card. The generated
`build/sdcard.img` contains an MBR and the demo filesystem with all six programs.
Copying that image as a file onto FAT/exFAT does not create a bootable CP32 root.
Follow [SD preparation](docs/sd-root.md) when preparing a new card.

Writing a whole-card image replaces existing card contents. Existing CP32 cards
can keep their files and receive application updates using the updater below.
Build targets generate host files; they do not write a physical SD device.
Insert the card before boot. Without usable media the console remains available,
but filesystem operations fail. SD storage does not provide extra executable RAM
or swap.

### Update applications without removing the card

From `src/`:

```sh
make updater
make flash-updater
```

On the Cardputer, run `apps` and wait for `Apps update OK`. Matching executables
are skipped; changed files are written and verified. The update is not atomic:
if it fails, retain the log and inspect the error. After an I/O failure, reboot
before retrying; after a space error, free space first. `df` reports available
filesystem space. Do not reformat the card merely to update applications.

Then restore normal firmware:

```sh
make
make flash
```

The SD applications remain updated. The normal build contains neither `apps`
nor the embedded updater payload. The normal SD heap reservation is 192 KiB;
the updater uses 160 KiB to accommodate its payload. RAM recovery uses 128 KiB.

## Start the shell

### Standalone

1. Disconnect USB and turn on the Cardputer.
2. **After power-on**, press and release **Go (G0)** at the top right.
3. Wait for `CP32 OS` and the `$` prompt, then use the Cardputer keyboard.

The kernel waits at startup for G0 or a USB serial reader. G0 selects standalone
operation and disables USB diagnostics for that boot. Holding G0 during power-on
selects download mode instead. G0 is not a general reset button for a running shell.

A filled square blinks every half-second at the current input position, follows
typing/backspace and disappears when input completes. It is a display overlay,
not part of the command text.

### USB diagnostics

Connect a USB data cable and open a serial reader:

```sh
screen /dev/cu.usbmodem2101 115200
```

A serial reader starts the waiting kernel with diagnostics enabled. Shell input
still comes from the **Cardputer keyboard**; USB carries the development log.

From a running standalone session, connecting USB and opening a serial reader
requests a full reset and reload. Some host drivers request data immediately
on connection; a power-only connection does not. Reopen the serial reader if
the device re-enumerates. Hardware reset and ROM flashing remain available.

## Shell commands

Paths are absolute or relative to the shell cwd. Bare `ls`, `cat` and `wc` are
builtins; use `run /boot/ls`, `run /boot/cat` and `run /boot/wc` for their standalone
applications. External bare names resolve under `/boot`; explicit paths are
used directly. Builtins take priority.

| Command | Description |
| --- | --- |
| `ls [path]` | List a directory; defaults to the current directory. |
| `pwd` | Show the current directory. |
| `cd [path]` | Change directory; no path selects `/`, and `cd -` returns to the previous directory. |
| `cat file` | Display one file as text. |
| `head [-N or -n N] file` | Show the first N lines; defaults to 10. N must be positive. |
| `tail [-n N or -c N] file` | Show the last N lines or bytes; defaults to 10 lines. |
| `wc [-lwc] file` | Count lines, words and bytes; flags select columns in that order. |
| `cmp file1 file2` | Report whether two files are identical. |
| `df` | Show free/total filesystem data space in KiB and free/total inodes. |
| `apps` | Updater firmware only: install or update the six applications on SD. |
| `stat path` | Show inode, size, links, owner IDs and modification time. |
| `rmdir path` | SD only: remove an empty directory and reclaim its inode/zones. Root, dot entries, the current directory and open handles are protected. |
| `mkdir path` | SD only: create a mode-0755 directory with `.`/`..`; parent must exist. No `-p`; remove empty directories with `rmdir`. |
| `create path [text]` | SD only: create a file in an existing directory, using an absolute or current-directory-relative path; final name 1–14 bytes, optional initial text 0–64 bytes. Existing names are rejected. |
| `cp source destination` | SD only: independent regular-file copy to a new path, using a 64-byte buffer; no overwrite. |
| `patch path offset text` | SD only: overwrite 1–64 bytes in an existing non-executable file, within current EOF, including sector/block boundaries. |
| `append path text` | SD only: append 1–64 bytes to a non-executable file, allocating zones as needed. |
| `truncate path length` | SD only: shrink a non-executable file to a decimal byte count, releasing unused zones. Zero empties the file. |
| `link existing newpath` | SD only: create a hard link in an existing parent directory sharing the source inode/data; the destination must be new. |
| `chmod mode path` | SD only: persist octal mode 000–777 on a file or directory. Execute bits affect program launch; full read/write/search permission enforcement is not implemented. |
| `mv oldpath newpath` or `rename oldpath newpath` | SD only: rename or move a regular file into an existing directory, preserving its inode; no replacement of another file. |
| `rm path` or `unlink path` | SD only: remove one regular-file name; the last link releases its inode and supported data/indirect zones. Open regular files survive until their last descriptor closes; directories are rejected. |
| `run path [arguments]` | Load an executable by absolute or current-directory-relative path; no PATH search. |
| `echo [arguments]` | Run `/boot/echo` through external-command lookup. Bare external names use `/boot`; names containing `/` use their explicit path. Builtins take priority. |
| `hello [arguments]` | Load `/boot/hello`, pass arguments and report its exit status. |
| `run /boot/ls [-ald] [path ...]` | Standalone listing: `-a` includes hidden entries, `-l` shows mode/links/UID/GID/bytes, `-d` shows directories themselves. Defaults to current directory. |
| `run /boot/wc [-lwc] < readme` | Run the standalone counter on redirected input. Without options prints lines, words, bytes. Accepts filenames too; multiple files include a total. |
| `run /boot/wc -c readme boot/readme` | Count both files and print their total. |
| `run /boot/cat readme boot/readme` | Copy file operands sequentially; `-` reads standard input. |
| `run /boot/cat < readme` | Feed a read-only file to application standard input. One trailing `< path` is supported; paths may be quoted. |
| `run /boot/cat` | Copy console input to output until Ctrl-D on an empty line. Use `run /boot/cat file ...` for file operands; `cat filename` remains the builtin. |
| `fsinfo` | Inspect the MINIX superblock and filesystem geometry. |
| `storage` or `ramdisk` | Show the selected storage backend and filesystem capacity. |
| `disk` | SD: validate the superblock through read IPC without writing media. RAM recovery: exercise read/write and restore the tested sector. |
| `mm` | Check allocation/release through the memory service. |
| `sys` | Query saved stack pointer and uptime through the system task. |
| `ipc` | Check TTY attributes through an IPC ioctl request. |
| `write` | Send a sample message through TTY output IPC. |
| `font` | Display sample letters and punctuation on the LCD. |

### Parsing and utility behavior

The shell accepts up to **63 characters per command** and eight application
arguments after the executable name. `hello`, `run` and direct external commands
support single/double quotes, empty quoted arguments and backslash escapes.
One trailing `< path` redirects application stdin; that path may be quoted.
Other builtins use their own parsing; filesystem mutation paths are whitespace
delimited. Variable expansion and multiline commands are unsupported.

```text
hello "one two" ""
echo one two
run /boot/ls -al /boot
run /boot/cat < readme
run /boot/wc -c readme boot/readme
```

- `echo` prints literal arguments with a newline; it has no `-n` option.
- `head`, `tail` and builtin `wc` operate on one regular file and accept `--`
  before a filename beginning with `-`. Tail has no follow mode; `+N` starts at
  line/byte N (one-based), while unsigned/negative counts select from the end.
- Standalone `cat`/`wc` accept multiple files and `-` for stdin. `wc` counts
  newlines, ASCII-whitespace-separated words and bytes, with totals for multiple
  operands. Standalone `ls` lists disk order, one entry per line, without sorting,
  recursion, date formatting or owner-name lookup.
- `create`, `append` and `patch` store literal text without an added newline.
  Their text limits are also constrained by the shell's command-line length.
- `mv` requires an explicit destination filename. A different existing inode
  is rejected; renaming to the same path or a hard link to the same inode is a no-op.

## Application API

The application runtime in [src/apps/lib/io.h](src/apps/lib/io.h) exposes:

| Operations | Behavior |
| --- | --- |
| Open/create/read/write/seek/close | Read-only, write-only and read/write access; exclusive create, truncate and append flags; per-application errors and exit cleanup. |
| `pread`, `pwrite` | Explicit offsets without changing the shared position. CP32 pwrite uses its offset even with APPEND set. Reads may be short; pread transfers at most 64 bytes, and pwrite loops bounded chunks. |
| `ftruncate` | Shrink or extend with zero-filled sparse regions; preserve the descriptor offset. |
| `stat`, `fstat`, directory iteration | Inspect metadata and enumerate directories through owned handles. |
| `mkdir`, `rmdir`, `link`, `unlink` | Create directories/hard links and remove names. Empty-directory removal protects root, open directories and both clients' working directories. Unlinked regular files survive until final close. |
| `rename_noreplace` | Rename/move regular files while retaining identity and open descriptors; no replacement of a different inode. |
| `chmod`, `fchmod` | Set rwx modes 0000–0777, independent of umask. Descriptor mode changes also work after unlink and through read-only handles. |
| `dup`, `dup2`, `fcntl(F_DUPFD)` | Share open descriptions; choose an exact target or the lowest free slot above a minimum. File slots only; 0–2 remain reserved. |
| `fcntl` flags | Independent CLOEXEC flags and shared access/status flags. Duplication clears new CLOEXEC; dup2 to itself is a no-op. |
| `chdir`, `getcwd`, `umask` | Process-local state inherited from the shell. Application changes do not change shell state. |
| `fsinfo` | Allocation counts for the mounted filesystem without consuming a descriptor. `df` formats those counts. |
| `execv` | Stage and replace the current application; preserve PID, cwd, umask and non-CLOEXEC handles. A failed exec leaves the original application intact. |

The runtime also provides console I/O, line reads, PID/parent queries, initial
environment access, exit, heap growth and malloc/free/calloc/realloc/trim.
Applications are fixed-layout Xtensa ELF programs, not arbitrary host binaries.
See [application execution and ABI](docs/application-execution.md) for contracts,
limits and implementation details.

## Debugging and validation

From `src/`:

```sh
make tests      # Host regression suite
make size       # Section sizes
make headers    # Section addresses and flags
make segments   # ELF load segments
make sections   # Detailed section table
make nm         # Symbols sorted by address
make map        # Symbols sorted by size
make disasm     # Source-interleaved disassembly
```

Watch USB `[FEATURE …]` and `[TEST …]` markers to identify the flashed firmware.
Startup checks cover initialized memory, vectors and stack alignment; `[CORE …]`
markers report kernel consistency checks. IRQ diagnostics can interleave with
other serial text. Linker maps are under the selected build directory.

Host tests cover filesystem byte orders, bitmap limits, descriptor/process
lifetimes, IPC bounds and injected I/O failures. They do not replace hardware
validation. Keep the serial log and distinguish a successful build from a
successful on-device test.

### Short hardware checks

After updating applications, start with:

```text
df
hello --space
hello --space
df
ls
hello --exec
disk
mm
```

Expect `Space OK` and `Hello exit=0` for the space tests, identical free counts
before/after, and no `stest` residue. `df` reports allocatable filesystem space,
not RAM or the whole SD-card capacity. The demo volume is small; insufficient
space can prevent tests or updates.

`hello` also provides focused checks:

| Command | Checks / cleanup |
| --- | --- |
| `hello --space` | Free-space/inode allocation and final-close reclamation; self-cleaning. |
| `hello --at` | Positional I/O, sparse writes and unchanged offsets; self-cleaning. |
| `hello --fmode` | Descriptor permission changes, including after unlink; self-cleaning. |
| `hello --fcntl` | Minimum-slot duplication and shared state; self-cleaning. |
| `hello --dup` | Exact-target duplication and CLOEXEC behavior; self-cleaning. |
| `hello --size` | File shrink/growth, sparse zero fill and unlink lifetime; self-cleaning. |
| `hello --move` | Same/cross-directory file moves and open descriptors; self-cleaning. |
| `hello --rmdir` | Directory protection, removal and recreation; self-cleaning. |
| `hello --mode` | Path permissions and open-time access; self-cleaning. |
| `hello --link` | Hard links, shared contents and unlink lifetime; self-cleaning. |
| `hello --unlink` | Removal while open and same-name recreation; self-cleaning. |
| `hello --dir` | Nested directory creation and umask; leaves `/dtest/sub` and `/dtest`. Remove with `rmdir dtest/sub`, then `rmdir dtest` from `/`. |
| `hello --state` | cwd, umask, descriptor flags and exec preservation; leaves `/statefile`. Inspect with `cat statefile`, then `rm statefile`. |
| `hello --file`, `hello --check` | Create writable `/wtest`, then verify it (also after reboot). Remove with `rm wtest` when finished. |
| `hello --exec`, `hello --exec-fail` | Image replacement and failed-exec preservation. |
| `hello --files` | Reads, offsets, seek, descriptor exhaustion and exit cleanup. |
| `hello --errno` | Invalid stream/buffer/argument errors. |
| `hello --line`, `hello --read`, `hello --io` | Interactive line and standard-stream I/O. |
| `hello --heap`, `hello --malloc` | Heap bounds, allocation, free/reuse and merging. |
| `hello --alloc`, `hello --resize`, `hello --trim` | calloc/realloc and heap trimming; `--resize` concerns memory, not files. |
| `hello --pid`, `hello --ppid`, `hello --env` | Process identity, launching shell and initial environment. |
| `hello --exit N` | Exit status; the shell reports the low eight bits. |

Successful self-cleaning tests can be repeated. Tests that create files require
SD storage and unused test names; most are omitted from the RAM demo. If a test
fails, retain its command and exit code and inspect leftovers before removing
them. Unknown `hello` options run its generic argument-printing path, so
`Hello exit=0` alone does not prove a specific test ran—check its success message.

## Source and further documentation

| Path | Contents |
| --- | --- |
| `src/kernel/` | Boot, scheduling, IPC, services, shell, keyboard, display and storage drivers. |
| `src/fs/`, `src/mm/` | Filesystem and memory/image-loading services. |
| `src/apps/`, `src/lib/`, `src/include/` | Applications, runtime support and compatibility headers. |
| `src/Makefile` | Cross-build, images, tests, flashing and ELF inspection. |
| `tests/`, `tools/` | Host tests, filesystem/image generation and layout checks. |
| `minix-2.0.0/` | Reference source for the port. |
| `docs/` | Architecture notes, storage/application details and hardware captures. |

- [Development plan](plan.md) and [issues / bring-up history](issues.md).
- [MINIX comparison review](minix.port-status.md) — a dated review, not the live feature list.
- [SD filesystem guide](docs/sd-root.md).
- [Application execution and ABI](docs/application-execution.md).
- [Hardware logs](docs/hardware/).
