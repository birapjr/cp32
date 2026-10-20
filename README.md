# CP32

**A bare-metal, Unix-like OS for the M5Stack Cardputer Adv.**

CP32 brings MINIX 2.0 architecture to the ESP32-S3 (Xtensa LX7): a small
keyboard, a small screen, and an operating system running directly on the
hardware, without an ESP-IDF or Arduino runtime.

## What works today

- Boot, task scheduling, timer interrupts and blocking message passing.
- Cardputer keyboard/LCD shell, with standalone startup and USB diagnostics.
- A persistent MINIX V2 filesystem on microSD over hardware SPI: directories,
  reads, metadata, seeking and indirect blocks.
- Shell file creation, patching, append, shrink-only truncation, hard links and
  deletion, renaming and numeric mode changes, with inode/data-zone allocation
  and reclamation.
- Memory allocation and device services through IPC.
- Six separately compiled applications: `hello`, `echo`, `cat`, `wc`, `ls` and `exec`,
  loaded from the filesystem with arguments, environment and exit status.
- Application file open/read/seek/close, directory iteration and stat/fstat,
  with four owned handles and automatic cleanup on normal exit.
- Buffered input/output, application-local I/O errors, PID/parent queries and
  heap allocation: malloc/free, calloc, realloc and explicit heap trimming.

**Current build: FS-EMPTY 142.** All **44 host test scripts** and clean
SPI2 SD, GPIO SD and RAM recovery builds pass. Hardware logs through image 129
confirm SD reads/exec, persistent patches and appended content, basic creation
and truncation, and matching hard-link inode/count/content after restart.
Image 131 also confirms a same-directory rename and `/boot/moved` readback
after restart. Full deletion/rename hardware matrices remain open; image 132
mode persistence still needs hardware validation. The image 132 log confirms
execute-bit removal blocks echo and restoring the bits permits it to run.
The user confirms the image 133 blinking cursor works on hardware; its log
shows interactive `hello --line` and return to the shell. Image 134 hardware confirms nested creation, same/cross-directory moves,
deletion and exec. The user confirms image 135 mkdir works, and its log reads a nested file
after boot. Image 136 confirms nonempty-directory rejection and child/parent removal.
Image 137 optimizes filesystem C for size and permits per-function linker garbage
collection in extended internal IRAM. SD placement headroom grows from 608 to
6,288 bytes; the 192 KiB heap and stacks are unchanged. Hardware image 137 confirms directory removal, SD/MM and exec.
Image 138 extends hard links to existing nested and current-directory destinations;
its hardware log confirms `/links/alias` reads `shared` after boot.
Image 139 adds sector/block-spanning patches; hardware validation is pending.

On the Cardputer, type:

```text
$ hello um dois
Hello from a CP32 application!
arg: um
arg: dois
Hello exit=0
```

CP32 is still an experimental port. The shell is kernel-linked and supports
one trusted foreground application. Application `exec` replaces the running
image while retaining its PID and open files; failed loads return an error.
General fork/wait, memory protection, application fault recovery, application
file-write syscalls, full libc and a user shell are still ahead. Filesystem
mutations currently use privileged shell commands, with synchronous verified
SD writes. Crash recovery and power-loss atomicity are not implemented.
The optional RAM recovery filesystem is recreated at every boot.

See the [remaining plan](plan.md), [MINIX port review](minix.port-status.md)
and [application roadmap](docs/application-execution.md) for details.

## Build and flash

Requirements: `make`, Python 3, the `xtensa-esp32s3-elf-gcc` toolchain and its
binutils, and `esptool`, available on your PATH. Host tests also use a host C
compiler (`cc`).

Run all Makefile commands from `src/`:

```sh
cd src
make clean
make
make tests
```

The normal build compiles all six applications and packages them in the boot
filesystem. Kernel outputs are `build/cp32.elf` and `build/cp32.bin`; application
outputs are `build/hello.elf`, `build/echo.elf`, `build/cat.elf`, `build/wc.elf`
and `build/ls.elf`, plus `build/exec.elf`. Use `make hello`, `make echo`,
`make cat`, `make wc`, `make ls` or `make exec` to build an application alone. Applications use `-Os`; the kernel
retains its `-O0` build.

To flash:

```sh
make flash
```

The flash target uses `/dev/cu.usbmodem2101` on macOS. Edit the device path in
`src/Makefile` to match your computer. If flashing fails, hold **Go (G0)**
while connecting the USB data cable to enter ROM download mode.

## Start the shell

### Without USB

1. Disconnect USB and turn on the Cardputer.
2. **After power-on**, press and release **Go (G0)** at the top right.
3. Wait for `CP32 OS` and the `$` prompt, then type on the Cardputer keyboard.

A solid white square blinks at the input position while the console waits for
an echoing read, including the shell prompt and interactive application input.
It alternates on/off every half-second, follows typing and backspace, and hides
when the read completes. It is a display overlay, not a character added to the
command. At a full line it stays on the final cell until the next character
wraps, preserving the existing newline/scroll behavior.

CP32 waits at startup for G0 or a USB serial reader. G0 selects standalone
operation and disables USB diagnostic output for that boot, so the shell does
not wait for a computer. Holding G0 during power-on selects download mode
instead. G0 is a startup selector, not a general reset button for a running shell.

### With USB diagnostics

Connect a USB data cable and open a serial reader:

```sh
screen /dev/cu.usbmodem2101 115200
```

Adjust the device path as needed. A USB reader starts the waiting kernel with
diagnostics enabled. Use the **Cardputer keyboard** for shell commands; USB
provides the development log.

To restart from a running standalone shell, release G0, connect USB and open
`screen`. The host's request for serial data triggers a full reset: the image
reloads and kernel execution starts from the beginning with a fresh shell and
uptime. Some host drivers request data as soon as USB connects; a power-only
connection does not request a restart. Reopen `screen` if the serial device
re-enumerates. Hardware USB reset and flashing remain available.

## Shell commands

Paths may be absolute or relative to the current directory. Start with
`ls`, `ls boot`, `cat readme`, then `hello one two`.

Bare `ls`, `cat` and `wc` are kernel builtins. Use `run /boot/ls`,
`run /boot/cat` and `run /boot/wc` to run the independent applications.

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
| `run /boot/wc -c readme boot/readme` | Count both files and print their total (61, 61, 122 bytes). |
| `run /boot/cat readme boot/readme` | Copy file operands sequentially; `-` reads standard input. |
| `run /boot/cat < readme` | Feed a read-only file to application standard input. One trailing `< path` is supported; paths may be quoted. |
| `run /boot/cat` | Copy console input to output until Ctrl-D on an empty line. Use `run /boot/cat file ...` for file operands; `cat filename` remains the builtin. |
| `hello --line` | Read and echo a full line through a bounded 16-byte buffer, including longer lines. |
| `hello --files` | Check independent file offsets, seek, descriptor exhaustion, errors and exit cleanup; run twice. |
| `hello --errno` | Check invalid-stream, bad-buffer and invalid-argument error reporting. |
| `hello --io` | Read from standard input and write through standard output/error wrappers. |
| `hello --read` | Prompt for a line on the keyboard, read through TTY and print it back. |
| `hello --trim` | Check top-of-heap release, preservation of live blocks and allocation after trimming. |
| `hello --resize` | Check resizing in place, live-neighbor preservation and reuse after shrinking. |
| `hello --alloc` | Check calloc zeroing, realloc growth/shrink and preservation after failure. |
| `hello --malloc` | Check aligned allocations, free/reuse and adjacent-block merging. |
| `hello --heap` | Exercise heap growth, writing, bounds rejection, shrink and zeroed regrowth. |
| `hello --ppid` | Print the launching shell PID (1). The shell is still kernel-linked; this is not a user init process. |
| `hello --pid` | Print this application process ID. Repeated launches receive different IDs, starting at 100 after boot. |
| `hello --env` | Show the initial application environment. These defaults are recreated on every launch. |
| `hello --exit N` | Exit immediately with decimal status 0..65535; the shell reports its low eight bits (e.g. 263 becomes 7). |
| `fsinfo` | Inspect the MINIX superblock and filesystem geometry. |
| `storage` or `ramdisk` | Show the selected storage backend and filesystem capacity. |
| `disk` | SD: validate the superblock through read IPC without writing media. RAM recovery: exercise read/write and restore the tested sector. |
| `mm` | Check allocation/release through the memory service. |
| `sys` | Query saved stack pointer and uptime through the system task. |
| `ipc` | Check TTY attributes through an IPC ioctl request. |
| `write` | Send a sample message through TTY output IPC. |
| `font` | Display sample letters and punctuation on the LCD. |

Examples, from the root directory:

```text
head -1 readme
head -n 2 readme
tail -n 1 boot/double
tail -n +2 readme
tail -c 3 boot/double
wc readme
wc -wc readme
stat boot/hello
run /boot/ls -al /boot/large
run /boot/wc readme boot/readme
run /boot/cat readme boot/readme
hello --files
run /boot/hello one two
run /boot/echo one two
cd boot
run echo again
```

`run /boot/echo one two` should print `one two` followed by
`Application exit=0`. `echo` currently prints arguments literally with a final
newline; options such as `-n` are not implemented. `run` requires a regular
file with at least one execute bit and a supported fixed-layout Xtensa ELF.
This is a trusted-application policy, not full user/group permission handling.

For `tail`, unsigned or negative counts select from the end; `+N` starts at
line/byte N, counting from 1 (`+0` also starts at the beginning). Zero without
`+` produces no output. Byte counts refer to file bytes before display formatting.
`wc` counts newline characters, ASCII-whitespace-separated words and raw bytes;
a final word without a newline is included.

The builtin `head`, `tail` and `wc` accept `--` before a filename beginning
with `-` and operate on one regular file. Tail follow mode is not implemented.
The standalone cat/wc applications accept multiple files and `-` for stdin;
wc also accepts `-lwc` and `--`, and prints totals for multiple operands.

Standalone `ls` accepts `-a`, `-l`, `-d`, multiple paths and `--`. It lists
entries in disk order, one per line. Sorting, recursion, date formatting and
owner-name lookup are not implemented.

Application ABI v9 supports reading and writing regular files, with four
owned handles per foreground application. Relative paths use the application cwd, inherited from the shell; normal exit
closes handles left open. Missing-file errors return a nonzero exit status;
standalone cat/wc/ls continue to later operands when possible.

The shell accepts up to 63 characters per command and eight application
arguments after the executable name. Application commands (`hello`, `run`,
and direct external commands) accept single/double quotes, empty quoted
arguments and backslash escapes: `hello "one two" ""` or `echo one\ two`.
Unclosed quotes and trailing backslashes are rejected. One trailing `< path`
redirects application stdin; paths may be quoted. Other builtins retain their
own argument parsing. Variable expansion, output redirection, pipelines and
multiline input are not implemented. File display sanitizes nonprintable bytes.

## Debugging and validation

Watch the USB log for `[FEATURE …]` and `[TEST …]` to identify the flashed
image. Startup checks cover initialized memory, exception vectors and stack
alignment; `[CORE …]` markers report kernel consistency checks. IRQ diagnostics
can interleave with other serial text.

From `src/`, inspect the built kernel:

```sh
make size       # Section sizes
make headers    # Section addresses and flags
make segments   # Load segments and virtual/physical addresses
make sections   # Detailed section table
make nm         # Symbols sorted by address
make map        # Symbols sorted by size
make disasm     # Source-interleaved disassembly
```

Linker maps are in `build/cp32.map` and each application's `build/<name>.map`.
`make tests` runs the host suite. Host tests do not replace hardware validation.
After flashing, check the image marker and try:

```text
run /boot/ls -al /boot/large
run /boot/ls missing readme
hello --files
hello --files
run /boot/wc readme boot/readme
disk
mm
```

Expect `.`, `..` and `readme` in the large-directory listing, `File API OK`
from both file checks, and wc totals of `4 16 122`. The missing-path command
should still list `readme` and exit `1`; successful applications exit `0`.
Retain the USB output for regression checks.

[Issues and bring-up history](issues.md) · [Hardware captures](docs/hardware/)


## SD root filesystem (image 142)

The default build mounts MINIX V2 directly from the Cardputer Adv microSD slot
using hardware SPI2. It removes the resident RAM disk and embedded demo image,
leaving a 192 KiB MM heap reservation (188 KiB of whole allocator pages). SD is
persistent storage, not extra executable RAM or swap.

`build/sdcard.img` is a whole-card MBR image containing a 64 KiB MINIX fixture
and all six applications. A FAT/exFAT card with this image copied onto it as a
file will not work. Follow [SD preparation and acceptance](docs/sd-root.md) for
a new card. Existing CP32 MINIX cards need no reformat for these features;
replacing their image would overwrite saved files. Build targets produce host
files and never write an SD device. Insert the card before boot; missing media
leaves the console available but filesystem operations fail. Live remount and
hot removal are not supported.

`make STORAGE=ram` selects the original RAM recovery configuration in
`build-ram/`; its filesystem mutations are not enabled. `make SD_SPI=soft`
selects the GPIO SD transport in `build-sd-soft/`. Run these from `src/`.

### Persistent file operations

The shell commands in the table above form a small writable-file lifecycle.
`create`, `append` and `patch` store literal text without adding a newline.
The shell's 63-character command-line limit includes command names and paths,
so it can impose a smaller text limit than the 64-byte filesystem helper limit.
Mutation commands use whitespace-delimited paths, without the application
command parser's quoting or escape handling.

Try this with unused names:

```text
create /sample hello
append /sample SD
patch /sample 0 HELLO
link /sample /samplelink
stat /sample
stat /samplelink
cat /samplelink
truncate /samplelink 5
cat /sample
rm /sample
cat /samplelink
stat /samplelink
```

Both names initially share an inode and link count 2. The first read prints
`HELLOSD`. Truncating either name changes the shared file to `HELLO`; removing
`/sample` leaves `/samplelink` readable with link count 1. Restart and verify
that the surviving name and content persist. To remove the final link:

```text
unlink /samplelink
create /sample reused
cat /sample
```

The final deletion releases the inode and zones for reuse. This does not
securely erase old data. Use `hello --exec`, `disk` and `mm` for regression
checks. Detailed [image 130 hardware acceptance](docs/sd-root.md#file-and-link-deletion-image-130)
is still pending; the examples are test instructions, not hardware results.

### Creating directories

`mkdir path` creates a persistent directory under an existing parent. Relative
paths use the current directory. Names are 1–14 bytes; new directories have
mode0755, links2, and correct `.`/`..` entries. Creating a subdirectory increases
its parent's link count. Use unused names for this example:

```text
mkdir /work
cd /work
mkdir sub
cd sub
create note hello SD
cat note
cd ..
pwd
```

`pwd` should show `/work`. Restart and verify `cat /work/sub/note` still prints
`hello SD`. Existing names and missing parents are rejected; `mkdir -p` is not
supported. Files inside new directories support the existing mutation commands.
`rm` continues to reject directories. Use `rmdir` after removing their files:

```text
rm /work/sub/note
rmdir /work/sub
rmdir /work
```

Run this from `/`, outside the directories being removed. `rmdir` rejects
non-empty directories (error14) and the current directory/open handles
(error13). It decrements the parent's link count and releases the removed
inode and zones for reuse. Restart to verify absence. See
[empty-directory removal acceptance](docs/sd-root.md#empty-directory-removal-image-136).

See [directory creation acceptance](docs/sd-root.md#directory-creation-image-135).

### Creating files in subdirectories

The parent directory must already exist. Relative paths use the current shell
directory, so these commands create `/boot/nested` (use an unused name):

```text
cd /boot
create nested hello SD
cat nested
stat nested
cd /
cat /boot/nested
```

Restart and verify the file remains readable. Then use the normal lifecycle:
`append /boot/nested !`, `mv /boot/nested /boot/renamed`, and
`rm /boot/renamed`. Creation never replaces an existing name or creates missing
directories. See [nested creation acceptance](docs/sd-root.md#creation-in-existing-subdirectories-image-134).

### Renaming and moving files

`mv oldpath newpath` (also `rename`) changes a file's name within a directory
or moves it between existing directories. It preserves inode identity, data,
link count and open read handles. Other hard links remain valid. Renaming to
the same path or another existing hard link to the same inode succeeds without
changing either name. An existing destination with a different inode is
rejected; automatic replacement and directory renaming are not implemented.

For example, after the preceding deletion/recreation example:

```text
mv /sample /renamed
stat /renamed
mv /renamed /boot/moved
cat /boot/moved
stat /boot/moved
```

Use unused destination names. The inode and content `reused` should remain
unchanged. Restart and verify `/boot/moved` still exists and the old names do
not, then finish with `hello --exec`, `disk` and `mm`. A move across directories
can leave both names or an excess link count after I/O failure; inspect both
paths after reboot before taking further action. See
[rename implementation and acceptance](docs/sd-root.md#file-renaming-and-moving-image-131).

### Persistent mode changes

`chmod 644 path` sets ordinary read/write/execute mode bits; an optional leading
zero is accepted (`0644`). Symbolic modes and setting special bits are not
supported. The change applies to the inode, so all hard links and `fstat` see
it; file content, allocation and timestamps remain unchanged. Existing special
bits (setuid, setgid and sticky) are cleared. Inspect modes with the standalone
listing application:

```text
chmod 644 /boot/echo
run /boot/ls -l /boot/echo
run /boot/echo test
chmod 555 /boot/echo
run /boot/echo restored
```

The first echo launch should report `File is not executable`; after restoring
execute bits it should print `restored`. Restart to verify mode persistence.
This is a trusted-shell metadata operation: read/write bits and directory
search bits are recorded but not yet enforced by a user/group access model.
See [chmod acceptance](docs/sd-root.md#persistent-mode-changes-image-132).

### Current limits

- File creation and new hard-link names accept existing nested parents. Each filename is 1–14 bytes. Source paths and deletion can use
  existing nested directories. Mutating a parent
  directory requires it to fit in one 1024-byte block; directory growth beyond
  that block and recursive deletion are unported. Empty-directory removal
  supports one-block directories with valid dot entries and link count2.
- Append, truncate and last-link reclamation support 1024-byte zones and direct/
  single-indirect mappings through 263 KiB, subject to free volume space.
  Truncate only shrinks. The supplied fixture volume is 64 KiB.
- Patch, append and truncate reject executable files. Link and deletion accept
  regular executable files too. Open regular inodes remain usable after unlink and are reclaimed at final close.
- Application ABI v9 supports writable descriptors (see image143 below).
  The system still uses a trusted foreground application, without per-user
  credentials or a complete POSIX permission implementation.
  Timestamps are not updated. Rename accepts explicit file paths, not a
  destination-directory shorthand; overwriting a different inode is rejected.
- Writes are synchronous and verified by sector readback. I/O failure may
  occur after a change became visible and may leave unreachable allocations
  or excess link counts. Further device writes are disabled until reboot.
  There is no automatic retry, rollback, filesystem repair or torn-sector/
  power-loss atomicity guarantee.

See [SD filesystem implementation and validation](docs/sd-root.md) for the
MINIX references, write ordering, memory placement and per-feature hardware
captures. Host tests cover both disk byte orders, shared-link changes, remount
persistence, resource reuse, and failures before/after complete writes.


### Nested hard links (image 138)

`link existing newpath` resolves both arguments from the current directory;
absolute paths also work. The destination must be new and its parent must exist.
For example: `mkdir /links`, `link /readme /links/alias`, then `cat /links/alias`.
Both names refer to the same inode and data; deleting one preserves the other.
See [hardware acceptance](docs/sd-root.md#nested-hard-links-image-138).


### Cross-sector overwrites (image 139)

`patch path offset text` now splits a 1–64 byte overwrite across SD sectors and
mapped filesystem blocks. It prevalidates all mappings and preserves the file
size, allocation and bytes outside the overwrite. Sparse holes and executable
files remain unsupported. An I/O failure may leave a partially applied patch.
See [hardware acceptance](docs/sd-root.md#cross-sector-patches-image-139).


### File copies (image 140)

`cp a b` creates an independent copy, including empty files and nested paths.
It preserves ordinary permission bits, not ownership, timestamps or special bits.
The destination must be new; directory copying and replacement are unsupported.
A failed copy may leave a partial destination. Copies use a64-byte buffer and
support up to263KiB, subject to available disk space and existing parent limits.

Short hardware test: `create a hi`, `cp a b`, `patch b 0 yo`, `cat a`, `cat b`.
Expect `hi` and `yo`, demonstrating independent storage. Restart and read `b`
again, then clean up with `rm a` and `rm b`.
Image139 logs confirm readback of a169-byte file, deletion and exec/SD/MM; they
do not establish the cross-sector hardware check. Image140 hardware is pending.


### Image141 context-return fix

Image140 hardware copied and patched a file successfully, then halted on a normal
IPC SYSCALL while running `cat b`. Runtime frame checks incorrectly required a15
to be nonzero. Optimized call0 code legitimately uses a15 as a general register;
image141 accepts zero while preserving its exact saved value. Stack/owner checks
remain enforced. All44 scripts and three clean builds pass; hardware confirmation
is pending. On existing files, repeat `cat a`, `cat b`, `ls`, then `hello --exec`,
`disk`, `mm`. Expected contents from the reported run are hi and yo.


### Empty files (image142)

`create e` creates a size-zero regular file without reserving a data zone.
The first `append e hi` allocates storage; empty copies also reserve no data zone.
Existing files are never truncated by create. Short check: `create e`, `stat e`,
restart, `stat e`, `append e hi`, `cat e`, `rm e`.
Image141 hardware confirms repeated cat readback, directory cleanup and SD/MM
without another exception; image142 hardware acceptance remains pending.


## Writable application descriptors — image143

`[FEATURE APP-WRITE 143]1` / `[TEST APP-WRITE 143]` identifies this build.
Applications can use `cp32_open_flags(path, flags, mode)`, `cp32_creat`,
`cp32_write` and `cp32_dup`; `cp32_open(path)` remains a read-only convenience.
Access modes are RDONLY/WRONLY/RDWR, with CREAT, EXCL, TRUNC and APPEND.
Duplicates share offsets and access/status flags. Independent handles observe
writes and truncation; successful exec preserves handles and exit closes them.
Writes support overwrite, growth, sparse holes, sector boundaries and direct/
single-indirect allocation. Append selects the current EOF on each request.
Errors report errno, and a later failure returns already confirmed progress.

The existing mutation limit is 263 KiB with 1 KiB zones, further limited by
free card volume space. There are four application handles. Open checks the
inode's read/write mode bits under the current trusted application model;
there is no user/group credential enforcement or umask. O_TRUNC truncates to
zero. Writes remain synchronous and verified; this is not a journaled or
power-loss-atomic filesystem. RAM recovery rejects writable opens with EROFS.

Hardware acceptance is pending. Build with `make clean && make` in `src/`.
Install the new firmware **and the new `/boot/hello` executable**. The generated
`src/build/sdcard.img` contains the matching applications; follow the SD setup
instructions and back up the existing card before replacing its volume.
Flashing firmware alone does not update applications already on the SD card.

On hardware, use these short commands:

```text
hello --file
```

Expect `File IO OK` and `Hello exit=0`. This creates `/wtest`, checks exclusive
creation, truncate visibility, duplicate offsets, read/write access errors,
sector-crossing writes, sparse growth, append and full readback. It deliberately
leaves one handle for exit cleanup. Status20 means the initial open failed; the diagnostic now prints errno.
Only errno17 means `/wtest` already exists: preserve it if needed, then
`rm wtest` and retry.

Fully restart the hardware, then run:

```text
hello --check
rm wtest
hello --exec
```

Expect `File IO OK` / `Hello exit=0` after reboot, successful removal, then the
existing `Exec PID and file OK` result. The check validates all 1026 bytes,
including the zero-filled gap. Host tests additionally cover single-indirect
writes, both disk byte orders, full descriptor tables, disk-full conditions,
write faults, malformed application requests and writable-handle exec survival.


### Image144 correction

Use `[FEATURE APP-WRITE 144]1` / `[TEST APP-WRITE 144]` for these tests.
Image143 incorrectly treated the defined-but-zero `CP32_ROOT_RAM` option as
RAM recovery in the application-open bridge, rejecting writable opens with
EROFS. Image144 tests the option's value consistently in the bridge and file
backend. Install the new firmware and updated `/boot/hello` for explicit failure
messages such as `File IO stage=20 errno=30`. The test commands remain
`hello --file`, then after a full restart `hello --check`, `rm wtest`, and
`hello --exec`. The image143 capture contains no successful `/wtest` creation
and no kernel exception; it does not validate descriptor writes on hardware.


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


## Per-process filesystem state — image146

Image145 hardware capture confirms writable application files: `hello --file`
and `hello --check` report `File IO OK`, and removal succeeds. The user also
reports the updater succeeded; its output was not retained. The supplied clean
firmware capture has one boot, so it does not independently establish a reboot
between those two checks.

`[FEATURE FS-PROC 146]1` / `[TEST FS-PROC 146]` adds explicit filesystem records
for the shell and foreground application. Each record owns its file namespace,
cwd, root-inode identity, creation mask, inherited identity fields and
close-on-exec flags. Shared open descriptions retain offsets and access/status
flags across duplication and cloning. Input redirection gets a separately owned
reference. Application exit releases its references; a new launch inherits
fresh state from the shell. Failed exec preserves everything; successful exec
closes only descriptors marked FD_CLOEXEC and retains cwd/mask/other handles.

ABI v10 adds `cp32_chdir`, `cp32_getcwd`, `cp32_umask` and `cp32_fcntl` with
GETFD/SETFD/GETFL/SETFL. The shell starts with mask0022; creation applies the
application's own mask. Changing an application's directory/mask does not
change the shell. Existing ABI v6–9 applications remain supported.

Install the matching applications using the updater, without removing the card:

```sh
cd src
make updater
make flash-updater
```

At the Cardputer prompt, run `apps` and expect `Apps update OK`. You may then
flash the clean image with `make` and `make flash` from `src/`. Test:

```text
cd /
pwd
hello --state
pwd
cat statefile
rm statefile
hello --state
rm statefile
hello --exec
```

Expect both `pwd` calls to print `/`, each state test to report `FS state OK`
and `Hello exit=0`, and `cat statefile` to print `ok!`. The second run checks
fresh-launch state and cleanup. The test changes the app cwd to `/boot`, creates
`/statefile` with mode0600 through mask0077, duplicates a descriptor, shares
APPEND, checks failed-exec preservation, then execs relative `hello` to verify
cwd/mask preservation and CLOEXEC closure. If exit42 appears, inspect whether
`/statefile` remains from an earlier run before removing it and retrying.

This is process-state infrastructure for the current trusted foreground model,
not a new general fork syscall or concurrent application scheduler. Four user
file handles per application and eight backend handles remain. The FS clone
hook is tested with independent clients and rollback on resource exhaustion;
launch and exec/exit use real lifecycle hooks. Root remains the sole mounted
root (no chroot); cwd is a validated canonical path, with directory rename and
application directory removal still unavailable. Identity fields are inherited
but credential-based permission enforcement is a later protection milestone.
Blocking pipes and general process suspension remain separate. Image147 adds
unlink-while-open as described below.

The default heap stays192 KiB, updater heap160 KiB, and stacks are unchanged.
Private descriptor metadata uses bounded byte/16-bit fields to leave room for
process records. Host and build results are recorded in plan.md/issues.md;
image146 hardware validation is pending. RAM recovery retains the process-state
API but omits the new SD-writing `hello --state` acceptance command from its
embedded demo application to preserve the fixed memory layout.


Image146 hardware acceptance (2026-10-15): updater and clean firmware both
report `FS state OK` / `Hello exit=0`, preserve shell cwd `/`, and successfully
remove the test file. Clean firmware reads `ok!` from it. The updater reports
`Apps update OK`; the clean boot omits the updater-enabled tag. Captures are in
`docs/hardware/fs-proc-v146-updater.log` and `fs-proc-v146-clean.log`.
This supersedes the image146 hardware-pending status above.


## Open-file unlink and final-close reclamation — image147

`[FEATURE FS-UNLINK 147]1` / `[TEST FS-UNLINK 147]` adds ABI v11
`cp32_unlink(path)`. Removing a regular-file name immediately makes that name
unavailable to new opens. Existing descriptors continue to read, write, seek
and fstat the same inode; its link count becomes zero after the last name is
removed. Creating the same pathname produces a different inode. Storage is
released only when all references are gone, including duplicated descriptors,
independent opens, cloned process references, close-on-exec and process exit.
Removing one of several hard links keeps the remaining names and data valid.
The shell's `rm` / `unlink` use the same implementation.

Update with `make updater` and `make flash-updater` in `src/`, then `apps` on
the Cardputer. Optionally restore clean firmware with `make` and `make flash`.
The short hardware test is:

```text
hello --unlink
hello --unlink
ls
hello --state
rm statefile
hello --exec
```

Expect each unlink test to print `Unlink OK` / `Hello exit=0`, and no `utest`
entry in `ls`. It checks zero-link fstat, duplicated/separate open references,
old/new contents under the reused name, and inode reuse after final close.
It cleans up its test files. Exit60 can mean `/utest` remains from a previous
failed run: inspect it before removing it and retrying. The state/exec commands
are regressions; remove a pre-existing statefile before rerunning --state.

The existing one-block parent-directory and direct/single-indirect mutation
limits remain. RAM recovery is read-only and returns EROFS for application
unlink; its demo omits the SD-writing acceptance commands. No crash-recovery
facility is added: power loss while an unlinked inode remains open can leave
an unreachable allocation on disk. An I/O failure during final reclamation
can also leak storage. Explicit close reports an error but still consumes the
descriptor; do not retry the same fd. The implementation detaches inode pointers
before releasing allocation bits to avoid recycling referenced data.

The ordinary shell-service C file now uses `-Os`, like the FS module, to retain
heap and stack sizes as services grow. Assembly, IRQ/context code, call0 ABI,
and internal-SRAM placement remain unchanged. Hardware validation is pending.

### Image148: application mkdir

Applications can call `cp32_mkdir(path, mode)` (ABI12). Relative paths use the
application cwd; the process umask filters permission bits before publication.
The existing shell `mkdir` retains its default0755 behavior. Writable directory
capacity remains one1KiB block; permissions are metadata, not credential enforcement.

From `src/`, run `make updater && make flash-updater`, then on the Cardputer:

```text
apps
hello --dir
pwd
stat dtest
stat dtest/sub
rmdir dtest/sub
rmdir dtest
```

Expect `Mkdir OK` and `Hello exit=0`, shell cwd `/`, and mode0700 on both
new directories (the current stat display may omit mode). The test also checks
modes internally, dot/parent inode identities, parent link counts, duplicate
creation, nested file I/O and invalid parent errors. It leaves empty directories
for inspection; remove them before repeating. After testing, `make && make flash`
restores the normal image without the updater payload; the SD apps remain updated.
Hardware acceptance for image148 is pending.

### Image149: application hard links

ABI13 adds `cp32_link(source, destination)` for regular files on SD. Both paths
use the application's cwd. Linked names share the inode and contents; unlinking
one name preserves the others and any open descriptors. Existing destinations
are rejected. Directory hard links and multiple mounts are unsupported.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --link
hello --link
pwd
ls
ls /boot
```

Expect `Link OK` and `Hello exit=0` on both runs, shell cwd `/`, and no `ltest`
or `lalias` residue. A successful run cleans up its own files. If it fails,
report the exit code before removing any leftover test files. Restore the normal
firmware with `make && make flash`; updated SD applications remain installed.
Image149 has passed host/build checks; hardware acceptance is pending.

### Image150: application chmod

Applications can call `cp32_chmod(path, mode)` (ABI14), using their own cwd.
Supported modes are0000..0777, independent of umask. Hard links share the mode;
existing descriptors keep their open-time access. This uses CP32's current
aggregate permission-bit checks, without owner/group enforcement or special bits.

From `src/`, run `make updater && make flash-updater`, then on the Cardputer:

```text
apps
hello --mode
hello --mode
pwd
ls
ls /boot
```

Expect `Chmod OK`, `Hello exit=0` twice, cwd `/`, and no `mtest` or `malias`.
Successful runs clean up their files. On failure, report the exit code before
removing any leftovers. Restore the clean firmware with `make && make flash`.
The updated applications remain on SD. Image150 hardware acceptance is pending.

### Image151: application rmdir

ABI15 adds `cp32_rmdir(path)`. It removes empty directories, protects both the
application and shell working directories, and rejects open directories.
Relative names use the application's cwd. Root and terminal dot components
cannot be removed. The existing one-block writable-directory limit remains.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --rmdir
hello --rmdir
pwd
ls
```

Expect `Rmdir OK` and `Hello exit=0` twice, cwd `/`, and no `rtest` residue.
Successful runs clean up automatically. If a run fails, report its exit code
before removing leftovers. Restore clean firmware with `make && make flash`;
the updated SD applications remain installed. Hardware validation is pending.

### Image152: application file renaming

ABI16 adds `cp32_rename_noreplace(source, destination)`. Applications can rename
regular files or move them between existing directories using their own cwd.
Open descriptors keep working. A destination referring to a different inode
returns EEXIST; the same path or another hard link to the source is a no-op.
Directory moves, replacement and crash-atomic rename remain unsupported.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --move
hello --move
pwd
ls
```

Expect `Rename OK`, `Hello exit=0` twice, cwd `/`, and no `ntest`. Successful
runs clean up automatically. On failure, report the exit code before removing
leftovers. Restore the clean firmware with `make && make flash`; applications
remain updated on SD. Image152 hardware validation is pending.

### Image153: application file resizing

ABI17 adds `cp32_ftruncate(fd, length)` for writable regular-file descriptors.
It shrinks files or extends them with zero-filled sparse regions, without changing
the descriptor offset. It works after unlink while the file remains open.
Maximum length is263 KiB or the volume's smaller maximum. The shell's existing
truncate command retains its shrink-only behavior.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --size
hello --size
pwd
ls
```

Expect `Resize OK`, `Hello exit=0` twice, cwd `/`, and no `ztest` residue.
Successful runs clean up automatically. Report any failing exit code before
removing leftovers. Restore clean firmware with `make && make flash`; updated
applications remain on SD. Image153 hardware validation is pending.

### Image154: application dup2

ABI18 adds `cp32_dup2(source, target)` for file descriptors3..6. It replaces the
target with a reference to the source's open file, sharing offsets and status
flags. Replacement clears target CLOEXEC; identical source/target is a no-op.
Standard-stream redirection through descriptors0..2 remains unsupported.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --dup
hello --dup
pwd
ls
```

Expect `Dup2 OK`, `Hello exit=0` twice, cwd `/`, and no `qtest` or `qold`.
Successful runs clean up automatically. Report any failing exit code before
removing leftovers. Restore clean firmware with `make && make flash`; updated
applications remain on SD. Image154 hardware validation is pending.

### Image155: application F_DUPFD

ABI19 adds `cp32_fcntl(fd, CP32_F_DUPFD, minimum)`, selecting the lowest free
file slot at or above the requested minimum. It never replaces an occupied
slot. Offsets/status are shared and the new descriptor has CLOEXEC cleared.
Slots0..2 remain reserved; a minimum below3 searches from3. Maximum slot is6.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --fcntl
hello --fcntl
pwd
ls
```

Expect `Dupfd OK`, `Hello exit=0` twice, cwd `/`, and no `ftest` residue.
Successful runs clean up automatically. Report any failing exit code before
removing leftovers. Restore clean firmware with `make && make flash`; updated
applications remain on SD. Image155 hardware validation is pending.

### Image156: descriptor chmod

ABI20 adds `cp32_fchmod(fd, mode)`. It changes permission bits through an open
file or directory descriptor, including read-only handles and files whose last
name has been unlinked. Supported modes are0000..0777; umask does not apply.
Owner/group enforcement remains unimplemented in CP32's trusted environment.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --fmode
hello --fmode
pwd
ls
```

Expect `Fchmod OK`, `Hello exit=0` twice, cwd `/`, and no `htest` residue.
Successful runs clean up automatically. Report any failing exit code before
removing leftovers. Restore clean firmware with `make && make flash`; updated
applications remain on SD. Image156 hardware validation is pending.

### Image157: positional reads and writes

ABI21 adds `cp32_pread(fd, buffer, count, offset)` and
`cp32_pwrite(fd, buffer, count, offset)`. They leave the shared file position
unchanged. In CP32, pwrite uses the requested offset even with APPEND set;
normal write retains append behavior. pread returns at most64 bytes and may
stop at block boundaries. pwrite handles larger buffers in bounded chunks,
returning partial progress if a later write fails.

From `src/`, run `make updater && make flash-updater`, then:

```text
apps
hello --at
hello --at
pwd
ls
```

Expect `PosIO OK`, `Hello exit=0` twice, cwd `/`, and no `ptest` residue.
Successful runs clean up automatically. Report any failing exit code before
removing leftovers. Restore clean firmware with `make && make flash`; updated
applications remain on SD. Image157 hardware validation is pending.
