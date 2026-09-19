# CP32 MINIX 2.0.0 port status

Review date: 2026-09-19. Scope: current working tree, image 44 (`TTY-WRITE`), compared with `minix-2.0.0/src`. This report replaces previous review results. Existing uncommitted work is included. Source, plan and issues files were not changed.

## Summary

CP32 has a working bare-metal kernel foundation with timer-driven task execution and real blocking IPC. TTY, CLOCK, a small allocation service named MM, and selected SYS services run on the Cardputer. It is not yet a complete MINIX operating system: there is no filesystem server, complete MM process manager, executable loader, application runtime, or user shell.

The newest user-supplied capture confirms **two real TTY DEV_WRITE replies of 18 bytes**, followed by successful MM allocation/release, TTY ioctl and the diagnostic `ls` command. No exception occurs through the final heartbeat at tick 3198. This is bounded hardware evidence, not a long-duration or visual-output test. The capture contains no `sys` command and ends before the recurring IRQ-5000 report. Earlier image-39 evidence covers SYS queries; it does not substitute for an image-44 regression.

There are significant dormant defects in memory translation and process/signal context handling. These do not invalidate the demonstrated commands, but must be corrected before enabling a general process lifecycle. Assigning a percentage of MINIX completeness would obscure these missing end-to-end services.

Evidence reviewed: repository `agent.md` (no repository AGENTS.md found), `plan.md`, `issues.md`, build source inventory, actual kernel functions and corresponding MINIX implementations. Early overview statements in plan/guide are stale: live IPC, integer special-register preservation, CLOCK/MM/SYS activation and console output now exist. Later dated evidence and source take precedence.

Latest hardware source: `/Users/blackrock/.codex/attachments/77e0b88d-8f7c-48d0-83cf-b2496fae03b8/Pasted text.txt`. It remains an external attachment; this review did not copy it into the repository. The WDT dump is recorded evidence only; its raw reset-reason value is not decoded here.

Verification during this review: `make -C src tests` passed all **18** host scripts. No firmware was rebuilt or flashed for this documentation-only review. The prior clean image-44 build and ELF checks are recorded in `issues.md`. Host models and source-extracted tests do not establish complete device behavior.

## Implemented

These are implemented bounded capabilities, not declarations that their entire MINIX subsystem is complete.

| Capability | CP32 evidence | MINIX behavior / reference | Validation boundary |
|---|---|---|---|
| Bare-metal boot and internal-SRAM image | `src/kernel/mpx32.S`, `start.c`, `vectors.S`, `esp32s3.ld`; `src/Makefile` | Boot, BSS, stack and task setup in `minix-2.0.0/src/kernel/main.c`, `start.c`, `mpx386.s` | Image 44 reports correct data/BSS sentinels, vector placement and aligned stack. No BIOS or x86 segmentation required. |
| Integer task context and restart | `irq.S`, `irq_frame.h`, `proc.h`: general registers, PC/PS/SP, SAR/LBEG/LEND/LCOUNT | MINIX save/restart invariant in `kernel/mpx386.s` | Working repeated returns; optional extension state and nested interrupts not established. |
| Core blocking message transport | `port.c:_send/_receive/_sendrec`, `proc.c:sys_call/mini_send/mini_rec/cp32_task_ipc_dispatch`, `irq.S:cp32_ipc_trap` | `minix-2.0.0/src/kernel/proc.c`: SEND, RECEIVE, BOTH, sender queues, HARD_INT | Real TTY/MM/SYS/CLOCK exchanges; host tests exercise both arrival orders and blocked returns. |
| SYSTIMER interrupt production | `clock.c`, `include/esp32s3/systimer.h`, `irq.S` | PIT tick source replaced while retaining kernel clock notifications | Earlier logs reach IRQ 10000 with CLOCK progress. Image 44 starts CLOCK and remains responsive. |
| Small owned allocation service | `mm.c:cp32_mem_alloc/cp32_mem_free/cp32_mm_handle_request/mm_task` | Allocation subset of MINIX `mm/alloc.c`, not full `mm/main.c` protocol | Ownership, coalescing and repeated service exchanges tested; image 44 allocation/release result 0. |
| TTY output and attribute query | `tty.c:cp32_console_write/do_write`, TCGETS handling | `kernel/console.c:cons_write`, `kernel/tty.c:do_write` | Image 44 returns 18 twice through real IPC. Host tests cover bounded copies, expansion and errors. |
| Board keyboard and LCD primitives | `cardputer.c`, `display.c`, keyboard decode in `tty.c` | Hardware-specific replacements for `kernel/keyboard.c`, `console.c` | User confirmed scrolling and punctuation in prior turns; latest capture shows continued command entry. |
| Bounded RAM-disk primitives | `ramdisk.c:cp32_ramdisk_read/write/read_bytes/write_bytes` | Storage subset of `kernel/memory.c` | Host bounds/checksum tests; 65536-byte capacity. No filesystem implied. |

## Partially Implemented

### Scheduling and execution ownership

- CP32: `src/kernel/proc.c:pick_proc/ready/unready/sched/switch_to`, `main.c`, `irq.S`.
- Reference: `minix-2.0.0/src/kernel/proc.c`, `main.c`, `mpx386.s`.
- Original behavior: ready queues, task/server/user priorities, quantum expiry, blocking and resumption through a captured context.
- Current behavior: FIFO within each class, rotating class selection, runnable requeueing, saved blocked frames and physical restoration at exception return. Ordinary C selection does not jump across uncaptured stacks.
- Architecture/invariant: Xtensa call0 requires matching stack, register image and selected owner. Rotating classes is an explicit deviation from MINIX priority policy, used to avoid bring-up starvation; it is not a missing x86 feature.
- Remaining: document intended final scheduling policy, measure fairness/latency under several clients, validate nested interrupts and remove obsolete bring-up gates only with coverage. Dormant descriptors entering idle loops are not implemented device services.

### CLOCK and CPU accounting

- CP32: `src/kernel/clock.c:clock_task/clock_handler`, alarm handlers and `syn_alrm_task`.
- Reference: `minix-2.0.0/src/kernel/clock.c`.
- Original: timer ticks, per-process accounting, uptime/wall time, alarms, watchdog callbacks and quantum scheduling.
- Current: live HARD_INT receive/dispatch and tick/uptime paths; substantial alarm code remains. The boot descriptor selection in `main.c` does not give the synchronous-alarm descriptor `syn_alrm_task` as its entry.
- Architecture/invariant: SYSTIMER replaces PIT; acknowledge/rearm must preserve continuing interrupts and task notification.
- Remaining: verify real alarm delivery, timeout/cancellation and stop/restart; reconcile `k_reenter` accounting semantics. `clock_handler` charges HARDWARE whenever `k_reenter != 0`, while normal IRQ entry increments it to 1. Uptime progress does not prove correct user/system CPU attribution.

### Memory translation and MM

- CP32: `src/kernel/mm.c:numap/mem_copy/mm_task`, `system.c:umap/alloc_segments`, `misc.c:mem_init`, `main.c` maps.
- Reference: `minix-2.0.0/src/kernel/system.c:umap/numap`, `src/mm/main.c`, `alloc.c`, `forkexit.c`, `exec.c`, `break.c`.
- Original: consistent segment translation, managed process address spaces, allocation and process lifecycle.
- Current: bounded allocator over the linker heap and custom allocate/release IPC; broad flat mappings for bring-up clients. No complete MM call vector or process-manager state.
- Architecture/invariant: flat SRAM replaces x86 segmentation; valid byte ranges and ownership must still be enforced in software. A shared flat map is not isolation.
- Remaining: unify mapping units and bounds, explicitly reserve every runtime stack, define an application memory/protection policy and implement process lifecycle. `numap` uses byte-valued `mem_vir`; `umap` still shifts it as a click count. Existing working IPC uses `numap`, so it does not validate SYS paths using `umap`.

### SYS services, lifecycle and signals

- CP32: `src/kernel/system.c:sys_task/do_getsp/do_times/do_fork/do_exec/do_xit/do_sendsig/do_sigreturn`; `src/include/sys/sigcontext.h`.
- Reference: `minix-2.0.0/src/kernel/system.c`; server-side policy in `src/mm/forkexit.c`, `exec.c`, `signal.c`.
- Original: privileged operations supporting MM/FS, correct process creation/replacement, signal frame construction and return, memory copying and reset.
- Current: real receive/dispatch/reply; saved-SP and uptime queries previously confirmed on hardware. Other handlers are present but not complete Xtensa ports. Presence in the switch is not proof of usability.
- Architecture/invariant: call0 result is a2, stack is a1, and all restored context fields must agree. Signal delivery needs a call0-compatible trampoline and complete register layout.
- Remaining: fix the concrete context defects listed below, reconcile translation, reset copied queue/blocked metadata in lifecycle operations, define caller permissions and implement the MM policy layer. No current evidence proves fork/exec/signals or reboot.

### TTY and interactive console

- CP32: `src/kernel/tty.c:tty_task/scr_init/do_read/do_write/in_process/out_process/cp32_console_write`, `cp32-shell.c:cp32_tty_read_client`, `display.c`, `cardputer.c`.
- Reference: `minix-2.0.0/src/kernel/tty.c`, `console.c:cons_write`, `keyboard.c:kb_read`, `rs232.c`, `pty.c`.
- Original: device input feeds line discipline; FS receives read/write/ioctl replies, including SUSPEND/REVIVE, canonical/raw behavior and terminal controls.
- Current: real output in bounded 64-byte copies with termios processing and byte-count replies. TCGETS works. The diagnostic client still polls keyboard input directly and renders most output directly. `scr_init` leaves input on `tty_devnop`; no device feeds normal input into `in_process`.
- Architecture/invariant: TCA8418/I2C and ST7789 replace PC keyboard/VGA; preserve line-discipline and IPC completion semantics, not PC register operations.
- Remaining: TTY-owned keyboard buffering/wakeup, DEV_READ and cancellation, canonical/raw/echo tests, then route the console through TTY. Escape sequences, PTYs and serial terminal backend are incomplete. A nested display batch may physically flush after the write reply; the current count acknowledges consumed bytes, not guaranteed display drain.

### Storage and diagnostic shell

- CP32: `src/kernel/ramdisk.c`, `cp32-shell.c:cp32_shell_command`, FS descriptor in `main.c`.
- Reference: `minix-2.0.0/src/kernel/memory.c`, `driver.c`, `src/fs/main.c/table.c`, `src/commands`.
- Original: block-device message protocol, actual filesystem namespace and user commands operating through descriptors.
- Current: volatile 64 KiB byte array and a custom format header. `ls` prints literal names `ramdisk`, `boot`, `README`; it does not read directory entries. The FS-numbered process is the diagnostic console, not FS.
- Architecture/invariant: RAM storage is valid for a first root device, but file and block protocols still need implementation. Custom RAM-disk formatting is not MINIX filesystem formatting.
- Remaining: block IPC adapter, real FS server, root contents, file descriptors, and separate diagnostic/client and FS roles.

## Missing

| Feature | MINIX reference and expected behavior | Suggested CP32 location | Architecture dependence |
|---|---|---|---|
| Filesystem server | `src/fs/main.c`, `table.c`, `open.c`, `read.c`, `path.c`, `inode.c`, `cache.c`: namespace, inodes, cache, open/read/write/close, mounts | New `src/fs/`, with device adapter over `kernel/ramdisk.c` | Mostly independent; storage transport is board-specific |
| Full process manager | `src/mm/main.c/table.c/forkexit.c/exec.c/break.c/signal.c`: fork/exec/exit/wait, image allocation, credentials, signals | New `src/mm/` or deliberately separated server alongside current allocation service | Policy independent; loader/context/protection are Xtensa-specific |
| Executable/application boot pipeline | `src/mm/exec.c`, `src/boot`, `src/tools`: load and start applications and initial servers | Loader plus build/image tooling | Executable format, call0 startup and memory placement need explicit design |
| User libc and POSIX call boundary | `src/lib/posix/_read.c`, `_write.c`, `_fork.c`, `_exec.c`, architecture library/startup files | Extend `src/lib/` with call0 startup and server request wrappers | POSIX interfaces independent; trap and calling convention specific |
| User shell, init and utilities | `src/commands`, user tests in `src/test` | New applications/commands and user test tree | Mostly independent once ABI/FS/process lifecycle work |
| Real reset | `src/kernel/system.c` reboot path | `src/kernel/system.c:system_reset` and centralized register definitions | ESP32-S3-specific; current implementation loops forever |
| Network service and board transport | `src/inet` and reference kernel network drivers | Future network server and supported board driver | Protocols largely independent; Wi-Fi/Ethernet transport not a PC-driver port |

The reference paths in this table are relative to `minix-2.0.0/`. These are functional absences, not conclusions drawn solely from absent directories: the current Makefile links kernel sources and printk, MM accepts only two custom requests, and the FS descriptor executes the diagnostic console.

## Not Applicable to CP32

BIOS setup, 8259 PIC, 8253 PIT, x86 protected-mode GDT/LDT mechanics, VGA text memory, PC keyboard controller programming, and 68000 shadowing are not required implementations for this board. Xtensa vectors, SYSTIMER, flat-memory policy and board peripherals must preserve the corresponding useful behavior. Floppy/IDE/CD-ROM/printer drivers are optional hardware support, not immediate blockers. Networking is missing functionality if broad MINIX parity is desired, but not a first console milestone requirement. Dual-core/SMP support is not required to reproduce a uniprocessor MINIX design.

## Requires Hardware Validation

| CP32 function/path | Expected behavior and needed result | Existing evidence and limits |
|---|---|---|
| `tty.c:cp32_console_write` | Correct visible text, repeated and multi-chunk writes, newline/tab handling, scrolling and subsequent SYS/MM/TTY progress | Latest image44 capture proves two 18-byte replies, MM/ioctl/ls continuation; no LCD image, SYS query or long write in this capture |
| `irq.S`, `proc.c` restart paths | Preserve outgoing/selected registers under stress, including active hardware loops; nested IRQ ownership and fault reporting | `issues.md` images31/32 remove observed crashes; integer register host tests pass. Nonzero loop and nested hardware coverage remain absent |
| `clock.c:clock_handler/clock_task` | Measured rate, correct per-process CPU billing, alarms, timeout wakeup, stop/restart | `issues.md` images35–39 show increasing CLOCK dispatch and uptime; current accounting condition requires source correction/audit first |
| `mm.c`, `system.c` translation/lifecycle | Reject out-of-range buffers; safely create, replace and retire contexts without stale queues or stack corruption | Allocator and service tests pass; no hardware lifecycle evidence. Known source defects block meaningful lifecycle acceptance |
| `main.c` stacks / `esp32s3.ld` | All active stack extents explicitly reserved and disjoint, high-water marks safe under interrupt depth and service load | Image44 aligned SP and CORE checks pass; linker only reserves 32 KiB stack while descriptor formulas extend beyond it |
| `display.c`, `cardputer.c` | Sustained keyboard input without lost events and correct shared display state during preemption | Prior user confirmation covers scroll/punctuation; direct input remains polling-based, multiple independent writers unproven |
| `system.c:system_reset`, `wdt.c` | Documented reset and watchdog policy verified across actual reset/power cycles | New capture includes WDT dump but does not validate an OS-requested reset; current reset routine is a placeholder |

## Suspicious or Incomplete Code

Confirmed source findings; runtime consequences beyond tested commands are stated as risks, not observed failures:

1. **Incompatible signal register layout.** `src/include/sys/sigcontext.h` defines ESP32-S3 `sigregs` as six words; the actual saved process context is 23 words (92 bytes; `irq_frame.h`). `system.c:do_sendsig/do_sigreturn` memcpy that short structure to/from the start of `p_reg`. Consequently named PC/SP/PS fields refer to general-register positions rather than the real fields. Call0 signal argument/return handling also needs adaptation.
2. **Fork sets the wrong register.** `system.c:do_fork` assigns zero to `p_reg.a[1]` for the child result. That is the stack register; `irq_frame.h` and production IPC use a2 for results. The full-structure copy also carries CP32-specific blocked-frame/queue state without an explicit child reset. Do not enable this handler as a working lifecycle service yet.
3. **Exec leaves inconsistent context.** `system.c:do_exec` updates `p_reg.sp` and PC but does not synchronize a1/a15 or explicitly reset the saved blocked-frame state and integer special registers. Current boot initialization enforces stronger invariants. Adaptation must consider real call0 entry requirements and restart behavior.
4. **Two mapping conventions coexist.** `mm.c:numap` treats `mem_vir` as a byte base with overflow/range checks. `system.c:umap` treats it as clicks and retains older arithmetic without comparable complete-range/overflow checks. `main.c` initializes byte bases. Audit every SYS map/copy caller before declaring those services usable.
5. **Runtime stacks are not completely represented in linker reservations.** `main.c` allocates negative-task stacks from `_stack_bottom + 0x4000` and server stacks from `_stack_bottom + 0x10000`; `esp32s3.ld` reserves only 0x8000 bytes. For FS, the observed SP is `_stack_bottom + 0x12000`. This is beyond `_stack_top`, although still below the current DRAM ceiling. Existing linker assertions do not protect the highest runtime stack against later image growth. This is a reservation gap, not an observed current collision.
6. **CPU accounting reentry mismatch.** `clock.c:clock_handler` uses `k_reenter != 0` to bill HARDWARE; `irq.S` increments on ordinary IRQ entry and current logs show nest=1. Per-process billing is not established by a successful SYS_TIMES uptime response.
7. **No-op TTY input and serial hooks.** `tty.c:scr_init` installs `tty_devnop` for reads, `rs_init` for both directions; `tty_task` contains disabled `#if 0` keyboard draining. Retained line discipline is not end-to-end device input.
8. **Reset placeholder.** `system.c:system_reset` loops forever. It satisfies nonreturn but does not reset hardware.
9. **Bring-up scaffolding and outdated comments.** `proc.c` has a `TODO(clean-production)`, a “minimal scheduler stub” comment and numerous probe/gate paths. `port.c` describes disabled restore experiments despite live trap IPC. These need careful cleanup, not removal of working invariants based on comments alone.
10. **Synthetic storage listing and limited format.** `cp32-shell.c` literal `ls` output and `ramdisk.c` private magic/header cannot certify FS functionality.
11. **Documentation drift.** Early `plan.md` sections still say blocking handoff, special registers, task activation and device output are missing. Later entries supersede them, but image44's hardware-pending note is now partly superseded by the new attachment. This review deliberately leaves those files unchanged.

## Recommended Next Steps

1. **Protect continued kernel execution:** explicitly reserve/assert all runtime task stacks and reconcile `umap`/`numap` units and complete-range checks. Add focused regression tests reproducing each defect. Current logs show no active boot crash; these are growth and service-expansion blockers.
2. **Complete the next useful console feature:** give TTY ownership of keyboard buffering and a reliable wakeup, feed `in_process`, implement real DEV_READ completion/cancel, and test canonical/raw input, EOF, backspace, echo and SUSPEND/REVIVE. Then migrate the diagnostic client's input and remaining output to TTY IPC.
3. **Correct dormant lifecycle machinery before activation:** fix fork result/context initialization, exec restart state and signal frame/trampoline layout. Add context round-trip tests and reject unsupported operations until their intended contracts are implemented.
4. **Introduce a real filesystem path:** implement the RAM-disk block message interface and a minimal FS server, separate it from the FS-numbered diagnostic client, and replace synthetic `ls` with actual directory data.
5. **Build the application environment:** define executable format, call0 startup/user request ABI and initial image loading; add MM fork/exec/exit/wait policy and a minimal user program before a full shell. Credentials and isolation require explicit policies.
6. **Validate clocks and sustained concurrency:** fix CPU accounting semantics; exercise alarms and TTY timeouts, concurrent clients and interrupt/context stress. For image44, also capture repeated `write`, `sys`, `mm`, `ipc`, `ls` and continuing CLOCK counts, plus visible LCD output.
7. **Lower-priority completion:** real reset, terminal escape support, PTYs/serial as needed, broader libc/commands and optional network/storage drivers. Reconcile historical documentation after implementation or when documentation updates are authorized.

No source fixes are included in this review. The confirmed image44 byte-count result is a useful new milestone, while full TTY input is the next end-to-end feature after the memory/stack safeguards.
