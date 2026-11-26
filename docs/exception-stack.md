# Runtime exception stack

MINIX 2 `mpx386.s:save` saves process registers and switches first-entry
interrupt handling to `k_stktop`. CP32 now applies that separation to the
Xtensa call0 level-1 IRQ and syscall paths.

The linker names a 16 KiB runtime exception window between
`_exception_stack_bottom` and `_exception_stack_top` in the lower half of the
existing boot-stack reservation. `_task_stack_start` is its upper boundary;
`main.c` starts task stacks there. The bootstrap uses the upper end of the boot
stack before runtime dispatch. Heap, boot-stack and task-stack reservations,
process-frame offsets and application layout are unchanged.

Entry uses only `a0` (whose interrupted value is in EXCSAVE1) until saving the
other registers directly into this kernel window. No entry frame is written
through the interrupted application SP. IRQ C dispatch receives its 80-byte
frame in the kernel window; syscall dispatch receives its complete 92-byte
frame there. Application-fault classification also uses this window, including
when the interrupted application SP is invalid. RFE restores the selected
process frame, not the temporary entry frame.

PS.EXCM remains set during dispatch and excludes normal level-1 reentry.
Saved-PS lock helpers preserve it. A level-1 entry with nonzero `k_reenter` is
rejected before reusing the window. Higher-level fallbacks do not call C and
nested/double faults remain terminal; this change does not add nested interrupt
support. Image204 uses final return on this stack to activate the selected
application image and prepare signal delivery before restoring its registers.

The CPU suppresses automatic loopback while `PS.EXCM` is set. A hardware
`LOOP` in the image 205 allocator therefore skipped every record after the FS
stack during the first application return. Image 206 compiles kernel-linked C
with `-fno-branch-count-reg` and checks the linked instructions before packaging
the binary. IRQ callees and shared library routines use ordinary branch loops.
Application ELFs can use hardware loops after RFE, and their saved LBEG/LEND/LCOUNT
values remain part of each process frame.
[Cadence Xtensa ISA Summary, processor status and CLOOPENABLE](https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf#page=135).

The removable production trace prints `[CTX V203 stack=1 ok=1]` on the first
IRQ C dispatch and `stack=2` on the first syscall C dispatch. It checks actual
C SP against linker bounds. Application-owned IRQ/syscall entries have separate
once-per-boot kinds4/8, so hello --t can demonstrate those paths after boot-task
entries. Image203 updater hardware passed these checks; image204 scheduling
and signal integration still requires hardware acceptance.

`tests/test_irq_registers.py` executes the real assembly in an instruction model
and checks all integer registers, SAR/loop state, PS and PC, immediate/blocked
syscalls, IRQ owner/handoff selection, no application-stack stores, destructive
replacement of application memory inside C dispatch, and rejection before
nested level-1 stack reuse. This is a contract test, not Xtensa hardware timing
validation. The fault policy model covers invalid SP and fatal fallbacks.
