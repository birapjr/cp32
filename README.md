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
| Directories and files | Image 56 confirms nested listings/reads; image 57 preserves behavior in split translation units. Seven direct zones plus 256 single-indirect entries support regular files in both byte orders; directories remain direct-only. Double-indirect zones, non-ASCII names, cwd, symlinks, descriptors and permissions remain unsupported. |
| Shell                 | Kernel-linked command loop occupying FS endpoint, not a user shell. `ls` now reads disk entries. A real FS endpoint must replace this temporary arrangement. |
| Library               | Existing freestanding implementations relocated unchanged. Xtensa call0 and section attributes retained; strtol range/error semantics remain incomplete. |

## Shell commands available

| Command | Description |
| ---     | --- |
| ls | List directory contents |
| cd | Change directory (use `-` for previous) |
| cat | Display file contents |
| tail | Display the last 10 lines of a file |
| cmp | Compare two files for equality |
| stat | Show file/directory metadata (inode, size, etc.) |
| mm | Verify memory management (alloc/release IPC) |
| disk | Check integrity of the RAM disk |
| ipc | Test TTY device control (ioctl) |
| sys | Query system status and uptime |
| write | Test TTY output via IPC |
| font | Show supported character set |


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

## Debugging

The initial kernel will output data to the serial port. This data can be seen with `screen` application.

```shell
screen /dev/cu.usbmodem2101 115200
```

The current version has also a shell on the Cardputer display and keyboard is enabled, but for now the kernel need to be started with the 'screen' command. After the kernel loads, the display will be read when it shows:

```shell
CP32 OS
$
```
