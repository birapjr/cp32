# CP32 MINIX port status

Source inspection confirms a substantial kernel-shaped implementation, but
not a runnable MINIX environment. The main blockers are the gated Xtensa
context handoff/IRQ return path, lack of user processes and servers, and lack
of filesystem/user I/O integration.

Substantially present: boot image/startup scaffolding, process/IPC algorithms
and validation helpers, SYSTIMER clock logic, flat-address mapping/copy
helpers, system-task handlers, MINIX TTY line discipline, Cardputer
display/keyboard primitives, USB diagnostics, and RAM-disk primitives.
These are source-level findings only.

Partially implemented: boot task activation; `irq.S` dispatch; scheduling and
context switching; blocked IPC handoff; clock task execution and alarms; MM
ownership/isolation; SYS_TASK lifecycle; and device-backed TTY operation.

Missing: `src/fs/`, a real `src/mm/` server, user executable loading, libc and
syscall ABI, user shell/commands, and optional MINIX services/drivers.

Incomplete evidence: `irq.S` has `TODO: dispatch`; `proc.c` calls
`schedule()` a minimal scheduler stub; `tty.c`'s `scr_init()` and `rs_init()`
install `tty_devnop`; `system.c:system_reset()` loops forever; and `clock.c`
retains an ESP32-S3 architecture TODO. No `issues.md` or hardware log exists.

Priority: finish and validate one IRQ-to-task frame return; prove blocked IPC
handoff; run CLOCK/SYS/TTY/MM descriptors; connect Cardputer TTY; define the
memory/user ABI; add RAM-disk FS; then add userland.
