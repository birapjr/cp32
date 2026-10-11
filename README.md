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
  deletion, with inode/data-zone allocation and reclamation.
- Memory allocation and device services through IPC.
- Six separately compiled applications: `hello`, `echo`, `cat`, `wc`, `ls` and `exec`,
  loaded from the filesystem with arguments, environment and exit status.
- Application file open/read/seek/close, directory iteration and stat/fstat,
  with four owned handles and automatic cleanup on normal exit.
- Buffered input/output, application-local I/O errors, PID/parent queries and
  heap allocation: malloc/free, calloc, realloc and explicit heap trimming.

**Current build: FS-UNLINK 130.** All **44 host test scripts** and clean
SPI2 SD, GPIO SD and RAM recovery builds pass. Hardware logs through image 129
confirm SD reads/exec, persistent patches and appended content, basic creation
and truncation, and matching hard-link inode/count/content after restart.
Image 130 deletion and reclamation still need hardware validation.

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
| `create /name text` | SD only: create a new root file; name 1–14 bytes, initial text 1–64 bytes. Existing names are rejected. |
| `patch path offset text` | SD only: overwrite 1–64 bytes in an existing non-executable file, within one sector and current EOF. |
| `append path text` | SD only: append 1–64 bytes to a non-executable file, allocating zones as needed. |
| `truncate path length` | SD only: shrink a non-executable file to a decimal byte count, releasing unused zones. Zero empties the file. |
| `link existing /alias` | SD only: create a root hard link sharing the source inode/data; the destination must be new. |
| `rm path` or `unlink path` | SD only: remove one regular-file name; the last link releases its inode and supported data/indirect zones. Open files and directories are rejected. |
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

Application file access is read-only, with four owned handles per foreground
application. Relative paths use the shell's current directory; normal exit
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


## SD root filesystem (image 130)

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

### Current limits

- Creation and new hard-link names are root-only, with 1–14-byte names. Source
  paths and deletion can use existing nested directories. Mutating a parent
  directory requires it to fit in one 1024-byte block; directory growth beyond
  that block, directory creation/removal and recursive deletion are unported.
- Append, truncate and last-link reclamation support 1024-byte zones and direct/
  single-indirect mappings through 263 KiB, subject to free volume space.
  Truncate only shrinks. The supplied fixture volume is 64 KiB.
- Patch, append and truncate reject executable files. Link and deletion accept
  regular executable files too. Deletion rejects open inodes with error 13;
  keeping an unlinked file alive until its last descriptor closes is unported.
- Application descriptors remain read-only. These are privileged shell
  maintenance operations, not a general POSIX write/permission implementation.
  Timestamps are not updated, and rename is not implemented.
- Writes are synchronous and verified by sector readback. I/O failure may
  occur after a change became visible and may leave unreachable allocations
  or excess link counts. Further device writes are disabled until reboot.
  There is no automatic retry, rollback, filesystem repair or torn-sector/
  power-loss atomicity guarantee.

See [SD filesystem implementation and validation](docs/sd-root.md) for the
MINIX references, write ordering, memory placement and per-feature hardware
captures. Host tests cover both disk byte orders, shared-link changes, remount
persistence, resource reuse, and failures before/after complete writes.
