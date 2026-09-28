# CP32 MINIX 2.0.0 port status

Review date: 2026-09-28. Snapshot: working tree through **HELLO-ARGV 89**, including uncommitted work. Reference: local `minix-2.0.0`. Review performed using the `review-minix-port` skill. No source, plan or issues changes were made.

## Summary

**Estimated overall functional completion: approximately 35% (judgment range 30–40%).** The narrower kernel/platform foundation is approximately **62% (roughly 60–65%)**. Neither is a measured percentage of source lines, an official MINIX metric, a reliability score, or a prediction of remaining development time.

CP32 has crossed a significant boundary: it boots on Cardputer, schedules tasks, exchanges real blocking IPC, reads a MINIX V2 filesystem, and loads a separate Xtensa executable that prints, receives arguments, exits and returns to the shell. It is still a trusted, single-foreground-application system with a kernel-linked command interpreter. Most of the general MINIX process-management, mutable filesystem, application ABI, libc and user environment remain incomplete or absent.

The latest user-supplied image-89 capture proves both `hello` and `hello um dois` execute, print the expected argument strings, report `Hello exit=0`, and permit another shell interaction. This supersedes the image-89 hardware-pending note at the end of `issues.md`; that file is intentionally unchanged during this review. The capture does not prove maximum-argument behavior, fault containment, concurrency or long-duration stability.

### Scoring method and scope

The denominator is a representative MINIX 2 operating-system environment adapted to Cardputer: kernel mechanisms, servers, filesystem, devices, runtime, utilities and networking. PC-specific implementations are excluded, but their portable responsibilities remain. Weights below are reviewer choices, not a canonical feature inventory. Partial credit requires substantive source implementation; the report states separately which paths have hardware evidence. Full credit requires substantially matching the category's applicable behavior, not just a matching filename or symbol. Dependencies overlap conceptually, so this estimate must not be interpreted as an independent statistical measurement.

| Category | Weight in whole system | Estimated category completion | Weighted points | Basis |
| --- | ---: | ---: | ---: | --- |
| Platform startup, image and memory layout | 5 | 90% | 4.50 | Running bare-metal image, vectors, SRAM layout, reset/console startup; remaining hardening |
| Context switching, scheduling and IPC | 15 | 75% | 11.25 | Real blocked exchanges and application contexts; broader stress/exception validation incomplete |
| Clock, system services and signal infrastructure | 10 | 45% | 4.50 | Active clock/SYS loops, timer and queries; legacy lifecycle/signal handlers incomplete |
| MM and general process lifecycle | 15 | 20% | 3.00 | Owner-tagged allocation and single fixed-slot executable launch; no general fork/exec/wait server |
| Filesystem services | 20 | 25% | 5.00 | Substantial read-only V2 subset; no mutable/per-process FS service |
| Device I/O and terminal services | 10 | 45% | 4.50 | Cardputer console, TTY IPC and RAM block device; missing persistence/general device coverage |
| Application ABI and C library | 10 | 10% | 1.00 | Small freestanding library, call0 startup and trusted write/exit callbacks |
| User shell, utilities and system environment | 10 | 10% | 1.00 | Kernel-linked command subset and separate hello; no full user shell/init/login/tool suite |
| Networking | 5 | 0% | 0.00 | No corresponding CP32 protocol stack or NIC path |
| **Total** | **100** | | **34.75 ≈ 35%** | |

The kernel/platform number uses only startup, scheduling/IPC, clock/SYS and devices: 24.75 points out of 40 = 61.875%. It excludes MM/FS servers and the user environment and therefore must not be reported as whole-MINIX completion. Excluding networking from the whole-system scope gives about 36.6%, not a dramatic change. Different reasonable category weights explain the 30–40% judgment range. No line-count or file-count fraction was used.

## Implemented

These are implemented **bounded features**, not assertions that their entire MINIX subsystem is complete.

- **Bare-metal startup:** `src/kernel/mpx32.S`, `start.c`, `vectors.S`, `hardware_init.c`, `esp32s3.ld`: initialized image, BSS clearing, stack/vector setup, call0 execution, watchdog setup and SRAM reservations. User image-89 sentinels, alignment and CORE checks pass. MINIX reference responsibilities are in `kernel/start.c`, `main.c`, `mpx386.s`.
- **Production IPC path:** `src/kernel/port.c` `_send/_receive/_sendrec`, `proc.c` `sys_call/mini_send/mini_rec`, and exception assembly implement actual task suspension and reply resumption. This is no longer the old cooperative stub. `issues.md` image 33 records the first real TTY exchange; later CLOCK/MM/SYS/TTY/MEM and application captures corroborate use.
- **SYSTIMER-driven execution:** `src/kernel/clock.c` `systimer_irq_start/cp32_timer_irq_dispatch/clock_task` implements the CP32 timer replacement. Repeated IRQ/CLOCK progress is hardware evidenced; MINIX's 8253 implementation is not required.
- **Interactive device-backed console:** `tty.c` `scr_init` installs `cp32_console_read/write/echo`; Cardputer keyboard/LCD and USB diagnostics work. The earlier claim that `scr_init` installs no-op handlers is obsolete. Images 44–48, 74 and subsequent captures cover normal I/O and console bring-up.
- **Read-only V2 filesystem subset:** `src/fs/super.c`, `inode.c`, `path.c`, `read.c`, `stadir.c`, `filedes.c`, `cache.c`: pathname resolution, directories, regular-file reads including indirect/double-indirect and sparse regular data, stat/fstat, seek, bounded descriptors, dup/dup2 and selected fcntl operations. `src/lib/posix/dirent.h` and related files supply caller-owned directory streams. These are CP32 helper interfaces, not full POSIX filesystem syscalls.
- **Owner-tagged heap service:** `src/mm/alloc.c` `cp32_mem_alloc/free/owned` and `main.c` allocation/release IPC. Repeated MM round trips and host allocation/coalescing checks exist.
- **First separate executable:** `src/mm/image.c` validates/stages Xtensa ELF, `exec.c` builds the argument stack; `src/kernel/application.c` launches a dedicated slot and handles child output/exit through FS IPC; `src/apps/hello/start.S` and `hello.c` are separately linked. Images 87 and 89 demonstrate repeat launch and argument passing. These deserve application-execution credit even though the full MM `execve` syscall is absent.

## Partially implemented

| Subsystem and CP32 evidence | MINIX reference and original behavior | Current boundary, architectural considerations and remaining work |
| --- | --- | --- |
| Contexts/scheduler: `kernel/proc.c` `sched/pick_proc/mini_send/mini_rec`; `proc.h`; `irq.S`, `vectors.S` | `kernel/proc.c`, `mpx386.s`: scheduling, IPC, interrupts and complete saved context | Working integer call0 contexts including SAR and loop registers, three queue classes and real application handoff. Xtensa frame layout replaces x86 registers/segments. Need concurrent application fairness, register-pressure stress, nested/fatal exception policy and any extension state required by the supported app compiler options. Do not infer arbitrary CPU-feature support from hello. |
| Clock/SYS: `kernel/clock.c`, `system.c` `sys_task/do_times/do_getsp` | `kernel/clock.c`, `system.c`: time, alarms, system-task process/map/copy/signal services | Active loops and read-only queries have evidence. Alarm/signal delivery, broad SYS operations and accounting need integration/validation. `clock_stop` is implemented, not missing. Separate legacy syscall handlers from the working application launcher. |
| MM: `mm/main.c` `cp32_mm_handle_request`, `alloc.c`; `kernel/application.c` | `mm/main.c`, `table.c`, `alloc.c`, `forkexit.c`, `exec.c`, `mproc.h` | MM dispatcher only exposes allocate/release. Execution is currently coordinated by the FS shell and kernel helper with one fixed slot. Needs process table/parent relations, general exec transaction, fork, wait variants, brk and cleanup. Fixed SRAM/call0 constraints are legitimate adaptations, not reasons to omit portable process semantics. |
| ELF/runtime: `mm/image.c` `cp32_image_read/load`, `mm/exec.c` `cp32_exec_stack`, `apps/hello/*` | `mm/exec.c` `read_header/load_seg/patch_ptr`, `lib/i386/rts/crtso.s` | Fixed RX/RW regions, bounded argument staging and trusted callbacks work. Xtensa ELF replaces x86 a.out appropriately. No path-selected general execution, application environment, stable syscall libc, multiple simultaneous image placements or fault isolation. Current shell always opens `/boot/hello`; file mode metadata is not a credential-enforced exec permission model. |
| FS: `fs/filedes.c`, `path.c`, `read.c`, `cache.c`, `stadir.c` | `fs/main.c`, `table.c`, `fproc.h`, `filedes.c`, `read.c`, `write.c`, `mount.c` | Eight-entry single-client descriptors, clean four-block cache and read-only V2 operations. No general FS receive/dispatch server, per-process descriptors/cwd/credentials, dirty buffers, writes or mount lifecycle. RAM-backed disk is valid for bring-up; the same logical FS services are still needed. |
| Terminal/device: `kernel/tty.c`, `cardputer.c`, `display.c`, `serial.c`, `memory.c`, `ramdisk.c` | `kernel/tty.c`, `console.c`, `keyboard.c`, `rs232.c`, `pty.c`, `memory.c`, `driver.c` | Cardputer replaces PC keyboard/display and USB diagnostic transport replaces the development serial console. Termios/TTY device paths exist; complete terminal modes, cancellation, signals, PTYs and application read/ioctl ABI remain incomplete or insufficiently evidenced. RAM writes do not imply filesystem write support. |
| Library/commands: `lib/ansi/*`, `lib/posix/*`, `kernel/cp32-shell.c` | `lib/ansi`, `lib/posix`, `lib/stdio`; `commands/sh`, `commands/ash`, `commands/simple` | Tested bounded helpers, directory streams and kernel-linked ls/cat/tail/head/wc/cmp/stat/cd. No comprehensive stdio, malloc/user heap API, POSIX syscall wrappers, general user shell, pipelines, redirection or shell scripts. Kernel builtins are useful partial functional credit, not ports of independently executable MINIX commands. |

## Missing

“Missing” here means the full expected service is absent, even where some low-level helper exists.

| Feature | Reference and expected functionality | Suggested CP32 location | Nature |
| --- | --- | --- | --- |
| General process management | `mm/forkexit.c`, `mproc.h`, `table.c`: fork, exit cleanup, parent/wait/zombie semantics; `mm/exec.c`: general execve | `src/mm/` plus shared kernel context installation | Mostly architecture-independent orchestration; context/image mechanics Xtensa-specific |
| User heap and credentials/session APIs | `mm/break.c`, `getset.c`: brk, PID/UID/GID/session interfaces | `src/mm/` and application syscall library | Mostly portable policy; SRAM limits matter |
| Complete signals and application fault recovery | `mm/signal.c`, `trace.c`, kernel exception/signal support | `src/mm/`, `src/kernel/system.c`, exception dispatcher | Portable semantics plus Xtensa signal frame/trap adaptation |
| Per-process FS syscall server | `fs/main.c`, `table.c`, `fproc.h`, `device.c`: process ownership and syscall/device dispatch | `src/fs/` | Mostly portable |
| Mutable filesystem | `fs/write.c`, `link.c`, `open.c`, `inode.c`, `cache.c`: create/write/truncate, allocation, link/unlink/rename, mkdir/rmdir, dirty cache | `src/fs/` | Portable on-disk behavior; persistence requires a device |
| Mount, permissions and metadata mutation | `fs/mount.c`, `protect.c`, `time.c`: mount/unmount, access/umask/chmod/chown, timestamps | `src/fs/` | Portable policy |
| Pipes, file locking and general character-device semantics | `fs/pipe.c`, `lock.c`, `device.c`; `kernel/pty.c` | `src/fs/`, `src/kernel/` | Mostly portable |
| Persistent storage integration | MINIX block-driver + FS interfaces; CP32 only has RAM storage currently | Appropriate Cardputer SD/flash block driver | Hardware-specific replacement; no need to port PC ATA/floppy literally |
| Application libc/syscall surface | `lib/posix`, `lib/stdio`, runtime/allocator sources | `src/lib/` and a reusable app runtime | Portable API plus call0/IPC stubs |
| Full user environment | `commands/sh`, `commands/ash`, `commands/simple`, `src/etc`, `src/test` | Future `src/commands/`, root image/configuration | Portable programs after their APIs exist; much of the utility/test suite absent |
| Network stack | `src/inet/`, network utilities and drivers | Future `src/inet/` plus suitable NIC driver | Protocols portable; ESP32 network hardware integration distinct from PC NICs |

## Not applicable to CP32

Do not subtract points for absent BIOS boot, x86 protected-mode setup/LDT, 8259 PIC, 8253 PIT, VGA text memory, PC keyboard controller, or specific PC floppy/ATA/printer hardware. CP32 must preserve boot/context/interrupt/console/storage responsibilities with its own implementations. No requirement to introduce ESP-IDF as a runtime follows from using Espressif documentation. SMP is not counted as a missing MINIX 2 requirement merely because ESP32-S3 has two cores. Dynamic linking and modern demand-paged virtual memory are not assumed mandatory MINIX 2 milestones in this score.

Networking is not a blocker for the first hello, but remains applicable to a broad whole-system comparison. Drivers tied to hardware not present are excluded rather than each counted as a missing feature.

## Requires hardware validation

| CP32 function/path | Expected behavior and needed test | Existing evidence and limits |
| --- | --- | --- |
| `application.c` `cp32_application_run`; `apps/hello/start.S` | Max args, repeated varied args, nonzero exit, malformed executable/load error followed by good launch | Latest image-89 user capture proves no-arg and two-arg success; image-87 evidence in `issues.md` proves two launches then disk/MM. Error branches mainly host-modeled. |
| `proc.c`, `irq.S`, `vectors.S` | Multiple runnable applications, long preemption/register stress, nested interrupt cases | CORE checks and long timer/task operation exist; hello does not certify every saved CPU extension or concurrent-user workload. |
| `clock.c` alarm handlers / `system.c` signal handlers | Alarm triggers, delivery to correct process, handler return with intact registers | CLOCK time/IPC progress evidenced. General signals currently need implementation corrections before hardware acceptance testing. |
| `tty.c` termios/cancel/control flows | Raw/canonical edge cases, backpressure, cancellation and device faults without deadlock | Normal keyboard, echo, DEV_READ/WRITE and scrolling evidenced; host models cover additional branches. |
| `system.c` legacy fork/exec/newmap/copy services | Correct fresh/duplicated context, ownership, negative cases and cleanup | SYS_GETSP/TIMES evidence (image 39) does not validate all SYS handlers. Fix known mismatches first. |
| `display.c` exclamation glyph | Visual confirmation on LCD | Image-88 serial capture includes `!`, but serial cannot establish glyph pixels. No explicit visual confirmation in latest captures. |

Review validation: reran `make -C src tests`; **30/30 scripts pass**. This includes modeled lifecycle, production filesystem/image loading and host hello behavior. Some tests replace hardware/IPC boundaries; passing them is not equivalent to executing Xtensa assembly. No clean rebuild or flash was necessary for this documentation-only review; previous image-89 build evidence is in `issues.md` and the current hardware capture. Existing application ELF integration tests used the available build artifact.

## Suspicious or incomplete code

1. **Confirmed Xtensa mismatch in legacy fork:** `src/kernel/system.c:do_fork` sets `rpc->p_reg.a[1] = 0` with a comment describing the child's fork return value. `proc.h` defines a1 as the stack register; CP32's call0 result is a2. This path must not be treated as a working fork port. It is distinct from the tested `application.c` path.
2. **Incomplete legacy exec context installation:** `system.c:do_exec` changes `p_reg.sp` and `pc` but does not synchronize a1/a15 or reset the complete special-register/blocked-frame state as `application.c` does. General SYS_EXEC is not validated by hello.
3. **Signal ABI remains suspicious:** `system.c:do_sendsig` retains generic historical frame logic, uses a2 as a frame-related register and updates SP/PC without matching the fresh call0 context contract. `do_sigreturn` exists, but neither presence nor compilation proves correct Xtensa signal delivery. Full frame audit required.
4. **Reset service placeholder:** `system.c:system_reset` loops forever. This does not mean all reset is missing: `hardware_init.c:cp32_hardware_reset` implements RTC reset and `serial.c` uses it for the G0/USB workflow. General SYS reboot remains disconnected from that implementation.
5. **Legacy scheduler entry:** `proc.c:schedule` labels itself a stub and directly manipulates `proc[1]`. The running scheduler is `sched/pick_proc`; verify/remove or replace the legacy entry before using it. Do not characterize all scheduling as stubbed.
6. **Incomplete device branch:** `tty.c:rs_init` installs `tty_devnop`; `scr_init` correctly installs the Cardputer implementation. PC UART and USB diagnostic behavior must not be conflated.
7. **Metadata-only fcntl portions:** `fs/filedes.c:cp32_fd_fcntl` stores APPEND/NONBLOCK and close-on-exec flags, but has no writable files/pipes/general exec integration to supply all effects. Record-lock commands explicitly return unsupported.
8. **Trusted execution boundaries:** `application.c` exposes native kernel callback pointers, accepts only one fixed slot, has no timeout/kill/fault recovery, and can wait indefinitely for a nonexiting child. Its pointer checks constrain serviced writes, not arbitrary CPU memory access. No protection claim is justified.
9. **Inactive-slot loading only:** `mm/image.c:cp32_image_load` clears and fills the slot and clears partial copies on failure. It is not rollback-preserving replacement of an existing running image; its API documents that restriction.
10. **Diagnostic/conditional scaffolding:** `proc.c` retains `CP32_ENABLE_*_PROBE` paths and extensive counters; normal builds do not thereby gain the test-only functionality. `port.c` has stale “disabled during bring-up” commentary while main enables runtime gates. Classify active behavior from callers/build flags.
11. **Documentation drift:** the beginning of `plan.md` claims FS/app/runtime work is absent, and AGENTS describes obsolete frame sizes/placeholder IPC. `apps/hello/abi.h` still says the bridge/lifecycle are unimplemented. Later issues and actual source supersede these claims. This review leaves those files untouched as required.

## Recommended next steps

1. **Consolidate the working executable path into a reusable process lifecycle.** Preserve hardware-proven launch/write/exit/argv while adding path-based launch and explicit process/image ownership. Unify context installation before enabling the legacy SYS_FORK/EXEC paths; correct a1/a2 and signal-frame mismatches.
2. **Add safe abnormal completion.** Define app-fault, cancellation and nonzero-exit reporting; ensure parent resumes and queues/descriptors/memory are reclaimed. Test it in host models before hardware. Continue describing execution as trusted until a protection design exists.
3. **Introduce MM process metadata and public services.** General exec/exit/wait, parent/PID tracking, environment and user heap; decide constrained fork semantics for SRAM. The current `app_busy`/single-slot lifecycle is a useful milestone, not the final MM server.
4. **Move file ownership into a real FS service.** Per-process descriptors/cwd/credentials and application read/open/close/stat interfaces, then device-node and terminal integration. Keep existing V2 read tests as regressions.
5. **Implement writable filesystem operations and persistence.** Zone/inode allocation, mutation, dirty cache and failure handling; add an appropriate persistent block device. Do not confuse the already writable RAM device with a writable MINIX FS.
6. **Grow the reusable application runtime and user environment.** Syscall wrappers, errno discipline, stdio/heap, more independently linked utilities, then a user shell with pipes/redirection and init/session support.
7. **Broaden hardware stress and optional subsystems.** Concurrent contexts, signals/alarms, terminal modes and resource exhaustion; networking and lower-priority drivers after the core user/server interfaces stabilize.

The latest hello result is a real application milestone. It substantially improves the execution subset, but does not by itself port the many services those future applications will need.
