# CP32

**A bare-metal, Unix-like OS for the M5Stack Cardputer Adv.**

CP32 brings MINIX 2.0 architecture to the ESP32-S3 (Xtensa LX7): a small
keyboard, a small screen, and an operating system running directly on the
hardware, without an ESP-IDF or Arduino runtime.

## What works today

- Boot, task scheduling, timer interrupts and blocking message passing.
- Cardputer keyboard/LCD shell, with standalone startup and USB diagnostics.
- A read-only MINIX V2 filesystem on a 64 KiB RAM disk: directories, file
  reads, metadata, seeking and indirect blocks.
- Memory allocation and device services through IPC.
- Five separately compiled applications: `hello`, `echo`, `cat`, `wc` and `ls`,
  loaded from the filesystem with arguments, environment and exit status.
- Application file open/read/seek/close, directory iteration and stat/fstat,
  with four owned handles and automatic cleanup on normal exit.
- Buffered input/output, application-local I/O errors, PID/parent queries and
  heap allocation: malloc/free, calloc, realloc and explicit heap trimming.

**Current hardware-validated build: APP-DIRS 118.** The latest Cardputer log
confirms standalone directory and long listings, missing-path recovery, file
API checks, multi-file wc totals, and disk/MM checks. The matching build passes
all **39 host test scripts** and ELF layout checks.

On the Cardputer, type:

```text
$ hello um dois
Hello from a CP32 application!
arg: um
arg: dois
Hello exit=0
```

CP32 is still an experimental port. The shell is kernel-linked and supports
one trusted foreground application. General fork/exec/wait, memory protection,
application fault recovery, writable/persistent filesystems, full libc and a
user shell are still ahead. The RAM filesystem is recreated at every boot.

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

The normal build compiles all five applications and packages them in the boot
filesystem. Kernel outputs are `build/cp32.elf` and `build/cp32.bin`; application
outputs are `build/hello.elf`, `build/echo.elf`, `build/cat.elf`, `build/wc.elf`
and `build/ls.elf`. Use `make hello`, `make echo`, `make cat`, `make wc` or
`make ls` to build an application alone. Applications use `-Os`; the kernel
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
| `ramdisk` | Show RAM disk capacity. |
| `disk` | Exercise RAM-device IPC read/write, restoring the tested sector. |
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


## SD root filesystem (image 123)

The default `make` now builds firmware that reads MINIX V2 directly from SD and
creates `src/build/sdcard.img` for a spare card. This removes the resident RAM
disk/embedded image and increases the MM heap reservation to 192 KiB. Initial
SD support is read-only; hardware validation is pending. A FAT/exFAT card with
an image copied onto it is not sufficient. See [SD root setup and acceptance](docs/sd-root.md)
for media preparation, boot markers, tests, limits and the write-support roadmap.
Use `make STORAGE=ram` in `src/` for the original RAM-backed recovery build in
`src/build-ram/`. No build target writes an SD card.
