# cp32
CP32 OS - M5Stack Cardputer Adv ESP32-S3 Unix like OS

CP32 is a work-in-progress, bare-metal, Unix-like operating-system port for the M5Stack Cardputer Adv, built around the Espressif ESP32-S3FN8 (Xtensa LX7, dual core). The kernel is based on the MINIX 2.0 architecture and source style, adapted incrementally for the ESP32-S3 rather than running on a PC BIOS, 8259 PIC, or 8253 PIT.

## Partially Implemented

| Feature               | Description and status |
| ---                   | ---                    |
| Scheduling            | Xtensa call0 frames replace x86 save/restore. Task execution works; nested interrupts, complete architectural state and process lifecycle need validation. Preserve frame owner, saved SP and runnable-state invariants. |
| Clock                 | SYSTIMER replaces PIT. Supplied logs show clock receipts; full alarm behavior and extended soak coverage remain. |
| SYS                   | Dispatch and selected calls work. Lifecycle handlers retain assumptions needing Xtensa frame audit; presence of handlers does not prove usable fork/exec. |
| MM                    | Internal-SRAM allocation and ownership checks exist; image 58 aligns filenames. numap/mem_copy reside in kernel/system.c. No full MM server implementing executable loading, process lifecycle and signal policy. |
| TTY                   | Cardputer keyboard/LCD replace PC console. Canonical path accepted; raw/timed input, cancellation and signal behavior need hardware validation. Deferred newline waits for subsequent visible output. |
| MEM                   | FS-only READ/WRITE/OPEN/CLOSE for RAM_DEV, full-buffer mapping and byte-count replies. No scattered I/O or raw memory devices. Host tests pass; one disk diagnostic and blank-superblock read pass on image-50 hardware. |
| Superblock            | Explicit byte decoding avoids compiler layout/alignment assumptions. V2 magic 0x2468 in either byte order; validates capacity, metadata and bitmap sizes before publishing. Read-only recognition only; no mount or complete consistency check. |
| Directories and files | Read-only MINIX V2 paths, cwd, metadata and shared file/directory descriptors. Regular files support direct, single- and double-indirect zones; directories support direct and single-indirect zones. `ls` uses directory descriptors in image 75 (root/boot listings confirmed on hardware). Writes, symlinks, permissions and a complete FS server remain unsupported. |
| Descriptor controls   | Image 76 adds MINIX-style `fcntl` duplication and descriptor/status flags. `tail` uses the new duplication path, confirmed on image-76 hardware. Other flag operations are host-tested. Close-on-exec flags are stored for future exec support; record locking is unsupported. |
| Shell                 | Kernel-linked command loop occupying FS endpoint, not a user shell. `ls` now reads disk entries. A real FS endpoint must replace this temporary arrangement. |
| Library               | Freestanding helpers plus caller-owned MINIX directory streams (open/read/close/seek/tell/rewind). Image 77 confirms root/large-directory `ls` and regular-file rejection on hardware; seek/tell/rewind remain host-tested. Image 78 adds range-checked `strtol`/`strtoul` (conversion cases host-tested; boot/filesystem regression confirmed on hardware). Full libc/syscall ABI and per-process errno remain incomplete. |

## Shell commands available

| Command | Description                                      |
| ---     | ---                                              |
| ls      | List directory contents                          |
| cd      | Change directory (use `-` for previous)          |
| cat     | Display file contents                            |
| tail    | Last 10 lines by default; `-n count` for lines, `-c count` for bytes |
| pwd     | Print current working directory                  |
| cmp     | Compare two files for equality                   |
| stat    | Show file/directory metadata (inode, size, etc.) |
| mm      | Verify memory management (alloc/release IPC)     |
| disk    | Check integrity of the RAM disk                  |
| ipc     | Test TTY device control (ioctl)                  |
| sys     | Query system status and uptime                   |
| write   | Test TTY output via IPC                          |
| font    | Show supported character set                     |
| fsinfo  | Show filesystem information                      |
| ramdisk | Show RAM disk capacity                           |

### Word counts

Image 82 adds `wc [-lwc] [--] filename` for one regular file:

```text
wc readme          # 2 8 61 readme
wc -l readme       # 2 readme
wc -wc readme      # 8 61 readme
```

Columns always follow line, word, byte order. Lines count newlines; words
are separated by ASCII whitespace, including a final word without a newline.
Bytes are counted before display formatting. Standard input and multiple-file
totals are not yet supported. Image 82 is host-tested; hardware validation
is pending.

### Head lines

Image 81 adds `head filename` (first ten lines), `head -1 filename`, and
`head -n 1 filename`. Counts must be positive decimal numbers; `--` ends
option parsing. For example, `head -1 readme` prints
`CP32 MINIX V2 RAM filesystem.`

This kernel-linked command supports one regular file, with the same text
formatting as `cat`. Standard input, multiple-file headers and a standalone
user executable are not yet implemented. Image 81 default and explicit counts are hardware-verified on `readme`.

### Tail counts

Image 79 adds MINIX-style counts to the kernel-linked shell:

```text
tail -n 1 boot/double
tail -n +2 readme
tail -c 3 boot/double
```

An unsigned or negative count selects the end of the file. `+N` starts at
line/byte N, counting from 1; `+0` also starts at the beginning. A count of
`0` without `+` produces no output. Use `--` before filenames beginning with
`-`. Byte counts select file bytes before the existing display formatting.
The command reads one regular file; follow mode, stdin and obsolete option
syntax are not implemented. Image 79 line/byte counts and zero-output behavior are hardware-verified.
Image 80 corrects the LCD `+` glyph, verified on hardware.

## Compiling source

Clean old build
```shell
make clean
```

Compile kernel
```shell
make
```

Flash to Cardputer
```shell
make flash
```
The flash will send the code to `/dev/cu.usbmodem2101` serial port on MacOs, adjust to your port.
If the flash process do not work, put the Cardputer in `download mode` by pressing and holding the `Go` button before
connection the data cacle to the Cardputer.

## Standalone startup with the Go button

You can use the Cardputer shell without a computer or USB connection:

1. Disconnect USB and turn on the Cardputer.
2. After power-on, press and release the **Go (G0)** button at the top right.
3. Wait for the display to show:

   ```text
   CP32 OS
   $
   ```

4. Type commands using the Cardputer keyboard, for example `ls`, `cat readme`,
   or `mm`.

CP32 waits at startup until you press G0 or a USB serial reader connects.
G0 selects standalone operation, which disables USB diagnostic output for
that boot so the shell does not wait for a connected computer.

Press G0 **after** power-on. Holding it while powering on selects the ROM
firmware download mode. G0 selects startup; it is not a general-purpose
reset button for a running shell.

## USB development and kernel restart

For development, connect the Cardputer with a USB data cable and open the
serial console:

```shell
screen /dev/cu.usbmodem2101 115200
```

Adjust the device path for your computer. From the startup wait, a USB
reader starts the kernel with diagnostics enabled.

To return to development from a running standalone shell:

1. Release G0 and connect USB.
2. Open `screen` using the command above.
3. The USB reader triggers a full system reset. CP32 reloads the image and
   starts kernel execution from the beginning, with a fresh shell and uptime.

The restart occurs when the host requests serial data; some host drivers
begin reading as soon as USB connects. A power-only connection does not
request a restart. If the serial device re-enumerates during reset, reopen
`screen`. Hardware USB reset and flashing remain available.

## Running applications: current status

Shell commands are currently kernel-linked. Standalone application execution
is not implemented yet. Image 83 adds the tested argument-stack builder as
a loader prerequisite; it introduces no new shell command. See the
[application execution roadmap](docs/application-execution.md) for the remaining
loader, process lifecycle and runtime work.

Build the separate hello executable with `make hello` in `src/`.
Image 84 adds ELF validation and reserves an application memory slot.
`build/hello.elf` is a development artifact; it is not yet packaged or runnable
from the shell. The write/exit bridge and runtime loader are the next steps.

Image 85 adds tested segment staging/BSS clearing and reserves all runtime
task/server stacks against application overlap. `hello` remains unavailable
until filesystem packaging and process startup/write/exit are connected.

Image 86 packages the executable as `/boot/hello` in every normal build.
Inspect it with `ls boot`, `stat boot/hello`, or `wc -c boot/hello` (916 bytes
in this build). The `hello` execution command is still pending process startup
and write/exit integration. Sparse disk provisioning preserves memory for it.

### First hello launch — image 87 (hardware-verified)

Run `hello` to load `/boot/hello` as a trusted foreground application.
Hardware-verified output:

```text
Hello from a CP32 application!
Hello exit=0
```

Try it twice, then `ls boot`, `disk`, and `mm`. This first launch path is not
memory-protected and does not yet recover from application faults. The earlier
image notes describe incremental milestones; image 87 adds the launch command.

Image 88 adds the missing LCD `!` glyph; check it with `font` or `hello`.
The glyph correction is host-tested and awaits LCD confirmation.
