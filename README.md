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
- Separately compiled applications loaded from the filesystem, with arguments,
  quoted arguments, environment, PID/parent queries, console input/output,
  exit status and return to the shell. `hello`, pathname launch and `echo`
  are hardware-verified.
- Application heap services and allocation library: malloc/free, calloc,
  realloc, resizing in place and explicit heap trimming.

The latest hardware milestone is **APP-STDIO 105**: standard-stream input,
empty lines and Backspace editing pass. The current build, **APP-LINE 106**,
adds bounded line reading across buffer-sized fragments; hardware validation
is pending. On the Cardputer, type:

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

The normal build also compiles `hello` and `echo` and packages them in the boot filesystem.
Outputs include `build/cp32.elf`, `build/cp32.bin`, `build/hello.elf` and
`build/echo.elf`. Use `make hello` or `make echo` to build an application alone.

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
| `hello --line` | Read and echo a full line through a bounded 16-byte buffer, including longer lines. |
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

`head`, `tail` and `wc` accept `--` before a filename beginning with `-`.
They operate on one regular file; standard input, multiple-file processing
and tail follow mode are not implemented. File display sanitizes nonprintable
bytes. The shell accepts up to 63 characters per command; `hello` and `run` accept up
to eight arguments separated by whitespace. Quoting, expansion, pipelines
and redirection are not implemented.

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

Linker maps are in `build/cp32.map` and `build/hello.map` / `build/echo.map`.
`make tests` runs the host suite; the latest review passed all 33 scripts.
Host tests do not replace hardware validation. After a new flash, try `hello`
twice, `hello one two`, `ls boot`, `disk` and `mm`, and retain the USB output.

[Issues and bring-up history](issues.md) · [Hardware captures](docs/hardware/)

Application commands (`hello`, `run`, and direct external commands) accept
single/double quotes, empty quoted arguments and backslash escapes:
`hello "one two" ""` or `echo one\ two`. Unclosed quotes and trailing
backslashes are rejected. No variable expansion, pipelines or multiline input
is implemented. Other builtins retain their existing argument parsing.

Applications are size-optimized (`-Os`); the kernel retains its existing `-O0` build.
