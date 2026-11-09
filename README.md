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
| Filesystem | File/directory reads, growing directories, metadata, seeking, creation, writes, sparse extension, truncation, hard links, unlink, empty-directory removal, file replacement and directory moves, numeric permission changes and free-space reporting. |
| File lifetime | Shared open descriptions, independent opens, descriptor duplication, close-on-exec, and continued access after unlink until final close reclaims storage. |
| Applications | Six ELF programs loaded from SD: `hello`, `echo`, `cat`, `wc`, `ls` and `exec`; arguments, initial environment, PID/parent queries, exit status and heap allocation. |
| Process filesystem state | Application-owned cwd, umask, descriptor namespace and flags, inherited on launch and preserved across successful exec as appropriate. Exit releases owned references. |
| Fork/wait | ABI26 `cp32_fork()` and `cp32_waitpid()`: private image, inherited shared file offsets, distinct PID, encoded exit status and parent restoration, including after child exec. Child-first execution with up to two nested fork levels and eight retained exit results per parent. Nested forks and repeated waits passed hardware tests through a cold restart in image177. |
| Process accounting API | ABI28 `cp32_times()` copies caller user/system ticks, child user/system ticks and uptime at60Hz; image179 updater hardware tests passed. |
| Process groups | ABI27 `cp32_getpgrp()`, `cp32_setsid()` and group-selecting `cp32_waitpid()`. Groups inherit across fork and survive exec. Group creation and group-selecting waits passed image178 hardware tests. |
| Application faults | Default termination for illegal instruction, divide-by-zero, unaligned access and prohibited load/store in active application text. Parent wait status and resource cleanup; image180 updater hardware acceptance passed. |
| Exec | Replace the foreground application's image while retaining PID and non-CLOEXEC files. Invalid paths/images and failed staging return an error without destroying the running program. |
| Time and persistence | Explicit time APIs, automatic data/directory/permission-change timestamps, synchronous verified SD writes and `sync`/`fsync`. File contents and timestamps survive restart. |
| Maintenance | Optional firmware updates applications directly on SD. Host tools generate configurable SD images and check offline filesystem consistency without modifying media. |

Current firmware: **TEST-DOTS 188**; application service ABI: **33**.
The host suite contains **64 test scripts**. Normal SD, GPIO-SPI, RAM recovery
and both SD updater variants build and pass image-layout checks.

Latest hardware result (**image187 updater**): the full `hello --t` sequence
passed, including all application cases, process listing and MM/disk checks.
Normal-image and cold-restart captures remain pending:
[hardware log](docs/hardware/proc-signals-v187-updater-pass.log).

**Run `hello --t` for current hardware acceptance.** Detailed test logs now go only
to USB; the display shows a new row of progress dots (one per successful check), then the final `[TEST ALL PASS]` or `[TEST FAIL]` on the next line. The suite
stops on the first failure and restores normal shell output afterward. The
current test list lives in `cp32_shell_app_tests` in `src/kernel/cp32-shell.c`.
Interactive `hello --line` remains separate.

Image187 adds independent pending/mask/default-ignore handling for MINIX signals
1–15, with SIGKILL unblockable and unignorable. Its new `--signals` check is
included in `--t`. Update hello for ABI33 before running tests. Image187 updater acceptance passed. Image188 progress-dot hardware acceptance
is pending; general handlers and asynchronous delivery remain absent.

Hello remains small and retains current process tests, exec recovery, line
input, identity, environment and exit checks. Retired filesystem/allocator
demonstrations are host-only fixtures; their runtime APIs remain available.
There is no memory protection: arbitrary writes can still corrupt kernel RAM.
Faults in kernel/callback code, unsupported causes, nested/double exceptions and
instruction fetches outside the application region remain terminal. General
signal handlers, asynchronous/group signal delivery, job control and timer-preempted application images remain unimplemented.

The [filesystem two-boot hardware result](docs/hardware/fs-data-time-v166-pass.log)
confirms the **100 MiB SD root**, 4,096 inodes, automatic timestamps and restart
persistence. The fresh volume reported **102,073 KiB free** and **4,084 free
inodes**. Creating `/u` initialized all timestamps; reading changed only access
time; appending changed modification/change time. After restart, size 3,
`atime=2009`, `mtime=ctime=3017` and contents `hix` were preserved.
Both boots used updater firmware. The subsequent [image167 hardware check](docs/hardware/fs-meta-time-v167-pass.log) confirms link/chmod/directory-read timestamps and restart persistence. The image166
capture does not establish a separate
normal-image boot or every application API path; host tests cover additional
cases. Detailed results and outstanding work live in [plan.md](plan.md),
[issues.md](issues.md) and [hardware captures](docs/hardware/).

### Current boundaries

- The shell is kernel-linked. The deepest parent/child pair can cooperatively exchange
  private images at either of two supported fork levels. General
  concurrent scheduling, arbitrary process trees, a user-space shell and full
  libc remain unimplemented. Applications are trusted; memory protection is
  outside the current port target.
- Each application has **four file slots (3–6)**; the shared backend has eight.
  Standard streams 0–2 use callbacks. Shell input redirection is supported;
  general descriptor-based standard-stream redirection, output redirection and
  pipelines are not.
- The filesystem is single-mount. Names are at most **14 bytes**, paths at most
  **255 bytes**. Directories grow through direct and single-indirect blocks
  (up to 263 KiB with 1 KiB zones), reusing deleted slots.
  Symbolic links, recursive deletion, mounts and hot removal/remount are unsupported.
  Replacement of open/current working directories remains protected.
- Writes/reclamation support **1 KiB zones and direct/single-indirect mappings
  through 263 KiB**, bounded by volume space and the superblock maximum.
  Read support also includes double-indirect files. The generated SD volume
  is **100 MiB with 4,096 inodes**; RAM recovery keeps its 64 KiB fixture.
  Use `df` to check available space.
- Permission bits affect new file opens and executable loading, but owner/group
  access control, directory-search enforcement and special mode bits remain
  incomplete. Existing descriptors retain their open-time access.
- Wall time starts at the Unix epoch after each boot until explicitly set.
  There is no persistent RTC or network time synchronization. Stored inode
  timestamps persist, but a later read can move access time backwards when
  the clock has reset.
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
- `build/minix-demo.img` (64 KiB recovery/test fixture) and `build/sdcard.img`
  (101 MiB whole-card image containing a 100 MiB filesystem).

`make hello`, `make echo`, `make cat`, `make wc`, `make ls` and `make exec`
build applications individually. Applications, filesystem C, MM allocation/image
helpers, shell, memory/storage, CLOCK and system-task C, and selected
application service routines use `-Os`. Startup, assembly entry paths and the
remaining kernel generally use `-O0`. Command handlers keep
separate stack frames so their temporary buffers do not inflate the shell’s
exec call chain. The host suite checks compiler stack-usage reports. The build
remains freestanding with the Xtensa call0 ABI and internal-SRAM placement checks.

The combined shell/filesystem server owns an 8 KiB stack allocated from the MM
heap at boot. This leaves 184 KiB of the normal SD heap, 152 KiB in the updater,
and 120 KiB in RAM recovery before other allocations. The stack remains reserved
for the entire boot. Compiled stack checks include exec and nested rename paths.

### Prepare storage

The default root is a MINIX V2 partition on the microSD card. The generated
`build/sdcard.img` contains an MBR and a 100 MiB MINIX V2 filesystem with
4,096 inodes and all six programs. The partition starts at sector 2048; the
remaining space on a larger card is unpartitioned. The image occupies 101 MiB
including that initial 1 MiB gap. This does not increase kernel RAM usage.
`make sdimage` builds it; `make sdimage SD_SIZE_MIB=200 SD_INODES=8192` selects
a different size. Supported generator bounds are 1–1024 MiB and up to 65,535
inodes, subject to metadata fitting. `make fsck` verifies the generated image.
Copying that image as a file onto FAT/exFAT does not create a bootable CP32 root.
Follow [SD preparation](docs/sd-root.md) when preparing a new card.

Writing a whole-card image replaces existing partitions and files; back up
anything needed first. This is a fresh image, not an in-place expansion.
The `apps` command updates executables but does not expand a filesystem.
Existing CP32 cards can keep their files and receive application updates using the updater below.
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
| `stat path` | Show inode, size, links, owner IDs and access/modification/change times. |
| `rmdir path` | SD only: remove an empty directory and reclaim its inode/zones. Root, dot entries, the current directory and open handles are protected. |
| `mkdir path` | SD only: create a mode-0755 directory with `.`/`..`; parent must exist. No `-p`; remove empty directories with `rmdir`. |
| `create path [text]` | SD only: create a file in an existing directory, using an absolute or current-directory-relative path; final name 1–14 bytes, optional initial text 0–64 bytes. Existing names are rejected. |
| `cp source destination` | SD only: independent regular-file copy to a new path, using a 64-byte buffer; no overwrite. |
| `patch path offset text` | SD only: overwrite 1–64 bytes in an existing non-executable file, within current EOF, including sector/block boundaries. |
| `append path text` | SD only: append 1–64 bytes to a non-executable file, allocating zones as needed. |
| `truncate path length` | SD only: shrink a non-executable file to a decimal byte count, releasing unused zones. Zero empties the file. |
| `link existing newpath` | SD only: create a hard link in an existing parent directory sharing the source inode/data; the destination must be new. |
| `chmod mode path` | SD only: persist octal mode 000–777 on a file or directory. Execute bits affect program launch; full read/write/search permission enforcement is not implemented. |
| `mv oldpath newpath` or `rename oldpath newpath` | SD only: move files/directories to an explicit destination path; replace a file or an empty directory of the same type. Open replaced files remain usable until close. |
| `rm path` or `unlink path` | SD only: remove one regular-file name; the last link releases its inode and supported data/indirect zones. Open regular files survive until their last descriptor closes; directories are rejected. |
| `run path [arguments]` | Load an executable by absolute or current-directory-relative path; no PATH search. |
| `echo [arguments]` | Run `/boot/echo` through external-command lookup. Bare external names use `/boot`; names containing `/` use their explicit path. Builtins take priority. |
| `hello --fork`, `hello --fork-exec` | Test private fork memory, inherited files, cwd/umask, PID relationships and wait status; second case execs echo in the child. Requires updated ABI28 hello. |
| `hello --signals` | Independent USR1/USR2 pending masks, selective ignore and signal wait status; included in --t. Requires image187/ABI33 hello. |
| `hello --t` | Run the current noninteractive suite and ps/MM/disk checks; stop on first failure. Requires current hello and firmware; detailed logs are USB-only. |
| `hello --ignore` | Pending TERM discard, ignored self/child signals, fork/exec inheritance and normal wait. Requires image185/ABI32 hello. |
| `hello --mask` | Mask inheritance, pending TERM, SIGKILL mask exclusion and unblocking termination. Requires image184/ABI31 hello. |
| `hello --kill` | Signal0 probes, parked-child SIGKILL/SIGTERM, self-termination, signal waits and file cleanup. Requires image183/ABI30 hello. |
| `hello --ytree` | Nested cooperative exchange, private state and live waits; requires image182 hello. |
| `hello --yield`, `hello --yexit`, `hello --yexec` | Cooperative private-image exchange, live wait, parent exit and parent exec with a live child. Requires image181/ABI29 hello. |
| `hello --fault`, `hello --crash` | Four child fault/recovery cases; foreground illegal instruction returns shell status132. Requires image180 hello. |
| `hello --time` | Verify application CPU ticks, child accumulation and no double counting after wait; expects `Times OK (60 Hz)`. |
| `hello --wait` | Test eight retained child results, selective/any-child waits, exhaustion, reuse and exit cleanup. Requires image176 hello; two consecutive hardware runs passed. |
| `hello --tree` | Test parent/child/grandchild stack restoration, separate wait replies and rejection of a third fork level. Image178 also checks session/group creation and group-selecting waits; requires updated ABI28 hello. |
| `hello [arguments]` | Load `/boot/hello`, pass arguments and report its exit status. |
| `run /boot/ls [-ald] [path ...]` | Standalone listing: `-a` includes hidden entries, `-l` shows mode/links/UID/GID/bytes, `-d` shows directories themselves. Defaults to current directory. |
| `run /boot/wc [-lwc] < readme` | Run the standalone counter on redirected input. Without options prints lines, words, bytes. Accepts filenames too; multiple files include a total. |
| `run /boot/wc -c readme boot/readme` | Count both files and print their total. |
| `run /boot/cat readme boot/readme` | Copy file operands sequentially; `-` reads standard input. |
| `run /boot/cat < readme` | Feed a read-only file to application standard input. One trailing `< path` is supported; paths may be quoted. |
| `run /boot/cat` | Copy console input to output until Ctrl-D on an empty line. Use `run /boot/cat file ...` for file operands; `cat filename` remains the builtin. |
| `fsinfo` | Inspect the MINIX superblock and filesystem geometry. |
| `storage` or `ramdisk` | Show the selected storage backend and filesystem capacity. |
| `date [seconds]` | Read or set seconds since the Unix epoch (0–2147483647). |
| `touch path` | Create an empty file or update existing inode timestamps without truncation. |
| `sync` | Wait for root-device completion and check SD status; reports errors. |
| `disk` | SD: validate the superblock through read IPC without writing media. RAM recovery: exercise read/write and restore the tested sector. |
| `mm` | Check allocation/release through the memory service. |
| `ps` | List occupied task/process endpoints, PIDs, states, CPU tick counters and names. |
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
- `mv` requires an explicit destination filename. Replacement requires matching types and an empty destination directory.
  Renaming to the same inode is a no-op; moving a directory into itself is rejected.
  Working directories follow successful moves.

## Application API

The application runtime in [src/apps/lib/io.h](src/apps/lib/io.h) exposes:

| Operations | Behavior |
| --- | --- |
| Open/create/read/write/seek/close | Read-only, write-only and read/write access; exclusive create, truncate and append flags; per-application errors and exit cleanup. |
| `pread`, `pwrite` | Explicit offsets without changing the shared position. CP32 pwrite uses its offset even with APPEND set. Reads may be short; pread transfers at most 64 bytes, and pwrite loops bounded chunks. |
| `ftruncate` | Shrink or extend with zero-filled sparse regions; preserve the descriptor offset. |
| `stat`, `fstat`, directory iteration | Inspect metadata and enumerate directories through owned handles. |
| `mkdir`, `rmdir`, `link`, `unlink` | Create directories/hard links and remove names. Empty-directory removal protects root, open directories and both clients' working directories. Unlinked regular files survive until final close. |
| `rename` | Replace files or empty directories, move directories across parents and preserve inode identity/cwd. Available through the current application ABI. |
| `rename_noreplace` | Rename/move regular files while retaining identity and open descriptors; no replacement of a different inode. |
| `chmod`, `fchmod` | Set rwx modes 0000–0777, independent of umask. Descriptor mode changes also work after unlink and through read-only handles. |
| `dup`, `dup2`, `fcntl(F_DUPFD)` | Share open descriptions; choose an exact target or the lowest free slot above a minimum. File slots only; 0–2 remain reserved. |
| `fcntl` flags | Independent CLOEXEC flags and shared access/status flags. Duplication clears new CLOEXEC; dup2 to itself is a no-op. |
| `chdir`, `getcwd`, `umask` | Process-local state inherited from the shell. Application changes do not change shell state. |
| `fsinfo` | Allocation counts for the mounted filesystem without consuming a descriptor. `df` formats those counts. |
| `sync`, `fsync` | Check root-device completion/status; fsync accepts owned file/directory handles, including open-unlinked files. |
| `time`, `stime`, `utime` | Read/set wall time and set explicit or current inode times; clock changes require root, and explicit inode times require owner/root. |
| `getpgrp`, `setsid` | Read the inherited process group or create a group led by the caller; an existing group leader gets EPERM. Bootstrap launches inherit shell group1. No controlling-terminal management is provided. |
| `fork`, `waitpid` | Child-first private-image fork and exit-status collection. Single-child, child-exec and eight-result wait tests pass on hardware. |
| `execv` | Stage and replace the current application; preserve PID, cwd, umask and non-CLOEXEC handles. A failed exec leaves the original application intact. |

The current service ABI is **27**. Runtime names use the `cp32_` prefix;
failures are reported through application-local `cp32_errno`.

The runtime also provides console I/O, line reads, PID/parent queries, initial
environment access, exit, heap growth and malloc/free/calloc/realloc/trim.
Applications are fixed-layout Xtensa ELF programs, not arbitrary host binaries.
See [application execution and ABI](docs/application-execution.md) for contracts,
limits and implementation details.

## Filesystem behavior

### Filesystem synchronization

`sync` checks completion/status through the serialized storage task. Applications
use ABI24 `cp32_sync()` and `cp32_fsync(fd)`; both return 0 or -1 with
`cp32_errno`. `fsync` accepts owned regular-file or directory descriptors,
including read-only, duplicated and unlinked-open files. It preserves offsets
and flags. Because all writes already complete synchronously, synchronization
checks the whole root device; there are no dirty FS cache blocks to flush.
A missing device or SD error is reported, and an earlier uncertain write remains
an error until reboot. RAM recovery has only an in-memory completion boundary.
This does not provide journaling, multi-write atomicity or guaranteed survival
of power loss inside the card.

### Time and file timestamps

`date [seconds]` reads or sets seconds since the Unix epoch (0–2147483647).
Application ABI25 exposes `cp32_time(&seconds)`, `cp32_stime(seconds)` and
`cp32_utime(path, times)`, returning 0 on success or -1 with `cp32_errno`.
A null `times` sets access/modification time to now; `struct cp32_utimbuf`
supplies explicit `actime` and `modtime`. Change time is always now. Root or
the inode owner may set explicit times; another user needs write permission
for current-time updates. Hard links share timestamps.

Creation initializes all three times. Regular-file reads, including nonzero
EOF requests, update `atime`. Writes, appends, patches and resizing update
`mtime`/`ctime` while preserving `atime`. Zero-byte reads/writes leave times
unchanged. These rules apply to shell operations and application descriptors,
including positional I/O and open-but-unlinked files. Loading executable data
also counts as reading. Times have one-second resolution; unchanged read times
avoid redundant SD writes. Directory enumeration also updates `atime`, including
EOF; pathname lookup and `stat` do not. Adding/removing/renaming names updates
the parent directories' `mtime`/`ctime`. Link-count changes, rename and
`chmod`/`fchmod` update the affected inode's `ctime`, including files still open
after unlink. Moving a directory across parents also updates its `mtime` because
its `..` entry changes. A same-inode rename remains a timestamp-preserving no-op.
RAM recovery does not write timestamps.

Timestamp I/O errors are reported. Failed file or directory read timestamp updates do not publish
staged output or advance the offset; mutation errors can follow partial disk
changes. Timestamp updates are synchronous, without journaling or rollback.
The clock setting is volatile, even though inode timestamps persist on SD.

## Debugging and validation

From `src/`:

```sh
make tests      # Host regression suite
make fsck       # Read-only check of the generated SD image
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

### Check an offline filesystem image

`make fsck` in `src/` checks the generated SD image. Use
`make fsck FSCK_IMAGE=/absolute/path/card-copy.img` for an existing offline copy.
The read-only checker detects allocation, inode, directory and link-count
inconsistencies; it never repairs or modifies media. It supports CP32 MINIX V2,
both byte orders and direct/single/double-indirect mappings. This is a host tool;
no firmware flash is needed. See [checker usage and limits](docs/fsck.md).

### Short hardware checks

Use the normal SD build for routine testing. Update applications only when
their payloads or required ABI have changed. The following commands exercise
allocation cleanup, exec and basic device services:

```text
df
hello --time
hello --time
df
ls
hello --exec
disk
mm
```

Expect `Times OK (60 Hz)` and `Hello exit=0` for each timing test. No test
files are created. `df` reports filesystem space, not RAM or whole-card capacity.
Use it to distinguish data-space exhaustion from inode exhaustion.

The current `hello` checks are:

| Command | Checks |
| --- | --- |
| `hello --fault`, `hello --crash` | Four child fault/recovery cases; foreground illegal instruction returns shell status132. Requires image180 hello. |
| `hello --time` | Caller CPU ticks, child CPU totals and collection without double counting. |
| `hello --tree` | Nested fork restoration, process groups and group waits. |
| `hello --wait` | Retained child results, selective waits, exhaustion and reuse. |
| `hello --fork`, `hello --fork-exec` | Private memory, inherited files/state and child exec. |
| `hello --exec`, `hello --exec-fail` | Image replacement and failed-exec preservation. |
| `hello --line` | Interactive line input. |
| `hello --pid`, `hello --ppid`, `hello --env` | Identity, parent and environment. |
| `hello --exit N` | Exit status; shell reports the low eight bits. |

Old filesystem, descriptor and allocator demonstration options have been removed
from the shipped application. The host suite retains the old fixture and the
production filesystem/runtime tests. These removals do not remove filesystem
commands or runtime functionality. Unknown hello options still print arguments;
check the specific success message, not just `Hello exit=0`.

### Timestamp and restart check

Use an unused filename:

```text
date 1000
create u hi
stat u
date 2000
cat u
stat u
date 3000
append u x
stat u
sync
```

Creation sets all three times to at least 1000. Reading advances only `atime`
to at least 2000. Appending leaves `atime` unchanged, advances `mtime`/`ctime`
to at least 3000 and sets size to 3. Restart and run `stat u` **before** `cat u`:
timestamps should persist and contents should be `hix`. Reading after reboot
updates `atime` using the reset clock. Clean up with `rm u`, then `sync`.
This sequence passed on the 100 MiB volume in the linked hardware capture.

### Directory and metadata timestamp check

This check needs only `make && make flash` from `src/`; existing ABI25 applications
and the current SD card remain compatible. Use an unused directory name:

```text
date 1000
mkdir m
create m/a hi
date 2000
link m/a m/b
stat m
stat m/a
date 3000
chmod 600 m/a
stat m/a
date 4000
ls m
stat m
sync
```

After linking, the directory modification/change times and file change time
should be at least 2000; file access/modification times remain near creation.
After chmod, only the file change time advances to at least 3000. Listing the
directory advances its access time to at least 4000 without changing its
modification time. Restart and inspect `stat m` and `stat m/a` before listing
or reading: the stored values should persist. `cat m/b` should print `hi`.
Clean up with `rm m/a`, `rm m/b`, `rmdir m`, then `sync`.

Host tests additionally cover same/cross-directory rename, replacement of open
files, directory moves, rmdir, descriptor chmod after unlink, failed directory
reads with unchanged output/offset, and both disk byte orders. Hardware
coverage beyond the linked shell sequence remains pending.

### Process-slot lifecycle check

Image168 follows priority 1 of the revised plan. Launch reserves a free process
slot, validates its Xtensa context and data/stack maps before scheduling, and
releases it on failure or foreground exit. IPC follows the chosen endpoint.
It still runs one foreground application; general fork/wait and concurrent
application images are not implemented.

From `src/`, run `make && make flash`. No SD rewrite or application update is
needed for existing ABI25 applications. Confirm `[TEST PROC-SLOTS 168]`, then:

```text
hello --pid
hello --pid
hello --exec
hello --exec-fail
hello --pid
```

Each fresh launch should have a different PID and return to the prompt.
Exec should print `Exec PID and file OK`; failed exec should print
`Exec failure recovery OK`. Repeat the sequence to exercise slot reuse.
Host tests additionally occupy the first slot, exhaust all available slots,
and reject unsafe release/publication. The [image168 hardware capture](docs/hardware/proc-slots-v168-pass.log) confirms repeated launch/reap and both exec checks. Alternate-slot and exhaustion cases remain host-tested.

### Process inspection check

Image169 adds `ps`. Flash with `make && make flash` from `src/` and confirm
`[TEST PROC-INFO 169]`. Existing SD contents and ABI25 applications need no update.

```text
ps
hello --pid
ps
hello --exec
ps
```

The header is `EP PID ST U/S NAME`. Negative endpoints are kernel tasks; PID0
means a boot/kernel role without a user PID. `RUN` is the current process,
`RDY` runnable, `SND`/`RCV` IPC blocked, `SR` both IPC flags, `MAP` awaiting a
map, `STP` stopped and `BLK` another blocking flag. U/S are existing 60Hz CPU
accounting counters, not elapsed seconds or percentages. Each row is copied
under the scheduler lock; rows can represent different instants. `ps` itself
and display output also consume CPU time. Reserved boot placeholders may appear.

After each foreground command exits, its application row should be absent.
The shell should keep returning to the prompt with working keyboard/cursor.
The foreground shell cannot run `ps` while its application is active; this
feature does not add background jobs or concurrent applications. The [image169
hardware capture](docs/hardware/proc-info-v169-pass.log) confirms this sequence,
but exposed incorrect CPU accounting, corrected in image170.

### CPU accounting check

Image170 corrects the Xtensa interrupt-depth interpretation used for CPU
accounting. Flash with `make && make flash` from `src/`; confirm
`[TEST PROC-TIME 170]`. No SD rewrite or ABI25 application update is needed.

Run `ps`, wait a few seconds, then run `ps` again. Run `hello --pid`, followed
by another `ps`. Idle and sampled task counters should advance; `hardware`
should no longer collect almost all timer ticks. A task that runs between
60Hz samples can still show zero. U/S counts overlap by design: task CPU time
may also be billed as system time to the billable process. The [image170 capture](docs/hardware/proc-time-v170-pass.log) confirms corrected
sampled ownership and a successful foreground launch.

### Child accounting check

Image171 retains child CPU totals in the launching shell when a foreground
application exits. `ps` shows `children U/S` below a parent with nonzero totals;
these include a child's own and accumulated descendant ticks. Failed launch
cancellation adds no usage. Billing after reap returns to the shell.

Flash from `src/` with `make && make flash`; confirm `[TEST PROC-REAP 171]`.
No SD rewrite or ABI25 application update is needed. Run:

```text
ps
hello --pid
ps
hello --exec
ps
```

Child totals should persist and never decrease during this short test. A
program can finish between timer samples and add zero; repeat if needed.
Exited application rows must disappear, exec should report success, and the
shell must remain responsive. These are foreground-reap totals, not general
fork/wait support. Later image174/175 logs show accumulated shell child totals;
forced failure/cancellation coverage remains host-tested.

### Kernel fork preparation

Image172 validates MM's `SYS_FORK` requests and prepares a blocked child without
copying live scheduler/IRQ links or stale alarm, signal and accounting state.
Occupied slots, duplicate PIDs, invalid parents and unauthorized callers are
rejected without modifying the target. The child retains `NO_MAP|RECEIVING`.

This is kernel infrastructure, not an application `fork()` API. The inherited
map still needs independent storage appropriate to Xtensa's flat address space;
MM lifecycle policy and concurrent FS clients remain outstanding. The clone
path is host-tested. Images171 and172 have not been tested on hardware.

When hardware is available, flash with `make && make flash` from `src/` and
confirm `[TEST PROC-FORK 172]`. Existing SD contents and ABI25 apps remain
compatible. Run `ps`, `hello --pid`, `hello --exec`, then `ps` to check existing
launch/exec/reap and child accounting. This regression does not exercise
SYS_FORK; no hardware fork test is claimed.

### Current cumulative hardware checklist

**Image188 keeps ABI33; existing image187 hello works. Run `hello --t`.** Use `make updater` and
`make flash-updater` from `src/`, run `apps` and `sync` on the Cardputer, then
install the normal image with `make && make flash`. No SD reformat is needed.

The [cumulative hardware checklist](docs/hardware/pending-tests.md) covers all
remaining acceptance: additional repeated cycles, earlier exec recovery, service
checks and a cold restart. The image176 wait/fork sequence has passed. It supersedes
the firmware/version-specific installation notes in the historical checks above.
Image174 hardware passed SD startup, the updater, both fork tests and post-exit
process accounting; see [the normal boot capture](docs/hardware/app-fork-v174-pass.log).
Image175 hardware passes both fork regressions. Its `hello --wait` exit107
was caused by missing application runtime initialization, fixed in image176.
Image176 hardware confirms two consecutive wait tests, both fork regressions
and no remaining application rows in ps; see [the passing capture](docs/hardware/proc-wait-v176-pass.log).

Fork initially runs the child. A child at either supported level may call `cp32_yield()` to
return the fork result to its still-live parent; the parent can yield back or
wait. WNOHANG returns0 while that child lives. Blocking wait resumes the child
and completes on exit. Parent exec preserves the child; parent exit drains the
child before completing, so a nonterminating child can delay exit. There is no
INIT adoption. A further parent fork while a live child is parked returns EAGAIN.
Two nested child-first levels and eight retained wait results per parent remain
supported. Each saved image requires about33KiB of MM storage. Wait accepts a
PID, -1 for any child, 0 for the caller's group or a negative group ID; invalid
status pointers do not consume results. This is cooperative execution with one
live image slot; timer-driven application preemption remains pending.

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
