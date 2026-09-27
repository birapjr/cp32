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

| Command | Description                                      |
| ---     | ---                                              |
| ls      | List directory contents                          |
| cd      | Change directory (use `-` for previous)          |
| cat     | Display file contents                            |
| tail    | Display the last 10 lines of a file              |
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

