# Path to running applications

The current shell and its commands are kernel-linked functions. Successful
shell commands do not demonstrate loading or executing an application file.

1. Define the executable/linker contract for Xtensa call0. MINIX 2 PC a.out
   binaries cannot run on Xtensa. Choose executable SRAM placement, data/BSS
   and stack regions, entry bounds, and a relocation or fixed-address policy.
   Account for the shared D/IRAM physical storage; an allocator's DRAM address
   alone is not a validated executable address.
2. Implement executable parsing/loading with bounds checks, segment copies,
   BSS zeroing and rollback. Validate every destination against reserved kernel
   memory before any write. FS currently exposes single-client read-only
   helpers, not an MM-to-FS exec protocol with per-process descriptors.
3. Build arguments/environment and startup state. The new src/mm/exec.c
   stages a bounded 32-bit little-endian argc/argv/envp stack with aligned SP.
   It accepts trusted MM-owned inputs only. User-pointer copying, a crt0 that
   consumes the stack, and integration with an exec request remain missing.
4. Complete process lifecycle and context installation. MM currently handles
   allocate/release only, with no mproc exec/fork/wait orchestration. Existing
   kernel SYS_EXEC sets PC/SP and clears RECEIVING, but does not constitute a
   complete Xtensa fresh context: general/special registers, a1/a15, blocked
   frames, runnable publication and failure rollback need coordinated handling.
5. Add application syscall stubs and services (at least write and exit), then
   parent wait/reaping and memory cleanup. Internal task IPC already works;
   a supported application ABI and runtime do not yet exist. Handle application
   faults so they cannot leave scheduling/process resources inconsistent.
6. Cross-build a tiny standalone hello application, include it in the RAM
   filesystem, load it into a dedicated process slot, print through TTY, exit,
   and return control to the shell. Verify repeated launches and failure paths.

First milestone: one trusted foreground application, without fork or dynamic
linking. Memory isolation is a separate architecture requirement; do not claim
that current kernel-linked services provide protected user execution.

References inspected: MINIX src/mm/exec.c and src/lib/i386/rts/crtso.s;
CP32 src/mm/main.c, src/kernel/system.c and initial contexts in main.c.
No new hardware mapping or protection behavior is assumed by the stack builder.

## Image 84: executable contract and separate hello artifact

Build with `make hello` in `src/`; output is `src/build/hello.elf` and its
linker map. This target does not flash or embed the application in the RAM
filesystem. `make tests` validates the real ELF when that artifact is present,
as well as synthetic malformed images on hosts without the cross-toolchain.

The v1 contract is ELF32 little-endian ET_EXEC, EM_XTENSA (94), System V ABI,
and exactly two PT_LOAD records in RX then RW order. No dynamic linking or
runtime relocation is implemented. Metadata flags are restricted to the
0x300 mask emitted by the installed Xtensa toolchain. The validator checks
format, file ranges, segment memory sizes, flags, alignment, fixed addresses,
nonoverlapping payloads and entry inside file-backed code. It validates a load
plan only: it does not copy bytes or prove the instructions use call0.

| Region | Address interval (end exclusive) |
| --- | --- |
| Code instruction alias | 0x403d8000–0x403dc000 |
| Same code physical SRAM via data alias | 0x3fce8000–0x3fcec000 |
| Data and BSS | 0x3fcec000–0x3fcef000 |
| Application stack | 0x3fcef000–0x3fcf0000 |

These use the kernel linker's existing D/IRAM alias relationship. Its new
assertion keeps the kernel stack end at or below 0x3fce8000, reserving the
application slot against kernel growth. Runtime copy ordering/instruction
synchronization and execution at these addresses still need implementation
and hardware validation. The loader must also reserve stack headroom below
the initial argument block; the stack builder's maximum capacity is not a
promise that a full 4096-byte argument block leaves room to execute C.

The separate crt0 expects a1=argument-stack SP, a2=service-table pointer.
It calls app_main(argc,argv,envp,services), then services->exit(status).
The v1 services are version=1, write(pointer,length), and nonreturning exit.
They are trusted call0 function pointers, not protected syscalls or a stable
public ABI. The kernel bridges are not implemented yet. A missing/returning
exit leaves crt0 in a terminal loop; the loader must reject missing services.
The C hello prints `Hello from a CP32 application!` and checks initially-zero
BSS. Host tests exercise greeting output and service-version rejection;
assembly was cross-built and disassembled, not executed on hardware.

Next: runtime segment copy/BSS clearing, filesystem packaging, dedicated
process context setup and write/exit bridges with return to the shell.

## Image 85: segment staging and complete stack reservation

`cp32_image_load` validates then fills caller-owned inactive code/data buffers.
It zeroes BSS and unused tails and clears partial payloads on read failure.
This does not perform runtime alias writes, instruction synchronization or
process activation. It cannot replace a running image transactionally.

The kernel reservation now includes initialized task/server stacks through
`_stack_bottom + 0x13000`; checking the boot `_stack_top` alone was insufficient.
The current runtime end is 0x3fce7550, leaving 0xab0 bytes before application
code's DRAM alias. Embedding the ELF in firmware must address that budget
before adding bytes; the linker enforces the full runtime-stack boundary.

No `hello` shell command is installed yet. The next step remains filesystem
packaging plus runtime ownership, loading/synchronization and process lifecycle.

## Image 86: packaged executable

The default firmware build now builds compact `hello.elf` and includes it in
`/boot/hello` (mode 0555). `ls boot`, `stat boot/hello`, and `wc -c boot/hello`
can inspect it; the current ELF is 916 bytes. The hello linker map preserves
symbol addresses although the packaged ELF is stripped.

Sparse boot-image provisioning preserves all filesystem bytes while avoiding
stored zero gaps. Runtime stacks now end at 0x3fce2c40, leaving 21440 bytes
before the application slot. Integration tests provision the disk, read the
application through production descriptors and load/zero it in host buffers.
Actual runtime instruction alias writes/synchronization and process activation
are still missing; the `hello` shell command remains unavailable.

## Image 87: first foreground launch (hardware pending)

`hello` now loads `/boot/hello` into the reserved slot, clears BSS/stack and
publishes endpoint LOW_USER+1 with a fresh call0 context. Its trusted services
use SENDREC to FS: the shell prints writes through TTY and replies; on exit,
it reaps the child blocked in SENDREC and returns to the prompt. Repeated
launches reload and zero memory. This is a single trusted foreground process,
not a general exec/fork API or memory-protected application environment.
Unexpected faults are not yet isolated from kernel fatal handling.

The argument block is limited to 512 bytes at the top of the 4K stack. Code
writes use the SRAM data alias followed by memw/isync before ready publication.
The alias relationship is corroborated by Espressif's
[memory layout source](https://github.com/espressif/esp-idf/blob/master/components/heap/port/esp32s3/memory_layout.c).
Runtime instruction coherence and context restoration await hardware validation;
host tests substitute hardware and IPC delivery, while executing the actual
launch/wait/reap control flow. Test `hello` twice, then ordinary shell commands.

## Hardware result: image 87

The user capture confirms two successive hello greetings and exit=0 results,
followed by successful disk and MM checks. The basic trusted launch/write/exit
and shell-resume path is now hardware-verified; no general fault-isolation
claim follows from this test. Image 88 fixes the reported LCD ! glyph only.

## Image 89: argc/argv from the shell

The launch API now takes argc/argv instead of constructing a fixed hello
vector. Shell token storage stays alive while launch copies it into the child
stack; no parent string pointers are exposed as argv in the application.
hello prints arguments through its existing write callback. The existing
512-byte stack-image cap, single-process slot and trusted execution scope
are unchanged. No environment variables, quoting or expansion yet.
