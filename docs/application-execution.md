> Current image188 uses ABI33. Older versioned sections below describe historical
> bring-up commands; retired hello filesystem/allocator options are host-only
> fixtures now. See README for the current command list.

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

## Image 90: pathname launch and second executable

`run path [arguments]` selects a file using the shell cwd, checks a regular-file
execute bit and uses the existing ELF loader/process slot. It passes the typed
pathname as argv[0] and derives the diagnostic process name from its basename.
`hello` remains a shortcut to /boot/hello. The new /boot/echo is independently
linked and prints literal arguments separated by spaces and a final newline.
Both executables share the same fixed slot and run sequentially, never at once.

All 30 host scripts pass; hardware validation is pending. This is still a trusted
foreground runner, not the MINIX execve syscall or complete credential model.
Missing files, execute-bit rejection and load errors return to the shell with
descriptors closed. No PATH search, quoting, expansion or echo -n support yet.

## Image 91: shared context initialization

Image 90 is hardware-verified for sequential hello/echo launch and relative
paths (docs/hardware/app-path-v90.log). Image 91 routes launch register setup
through cp32_exec_frame, also used by legacy SYS_EXEC. The latter now rejects
inappropriate callers/targets and discards obsolete blocked receive state
before publishing the new frame. SYS_FORK's child result uses a2 rather than
clobbering a1. General lifecycle/parent bookkeeping and legacy fork/exec service
integration remain unfinished; those handlers have host tests, not hardware
coverage. The working shell launch path remains the regression target.


## APP-EXIT 92 — explicit termination and exit status (2026-09-29)

Image 91 hardware result supplied by the user: CORE checks pass;
`hello one two`, `run /boot/echo one two`, and `hello` exit successfully;
`disk` and `mm` report result=0. This validates the shared frame initializer
through the foreground launcher, not legacy SYS_EXEC/FORK.

MINIX reference: src/mm/forkexit.c do_mm_exit/mm_exit suppresses the reply to
an exiting child and retains an eight-bit exit status. CP32 keeps its existing
trusted call0 callback/SENDREC bridge and fixed slot; the parent now reports
status & 255, directly rather than as a POSIX wait word. The child remains
blocked and is reaped before another image may reuse its slot.

`hello --exit N` invokes the exit callback directly, without a greeting or
return through main. Decimal 0..65535 is accepted; malformed/out-of-range
arguments print usage and return 2. Ordinary hello arguments are unchanged.
Host tests exercise explicit exit via a nonreturning modeled callback, parser
boundaries, unsigned/signed status normalization and repeated production reaping.

Hardware pending: [FEATURE APP-EXIT 92]1 / [TEST APP-EXIT 92].
Try hello --exit 7, hello --exit 263 (both report 7), hello --exit 65535
(reports 255), hello --exit 65536 (usage and 2), then hello, run /boot/echo ok,
disk and mm. No flashing performed. Fault recovery, cancellation, general MM
exit/wait, signals and resource ownership remain unfinished.

Build validation: clean cross-build and all 31 host scripts pass, without
compiler warnings. ELF section/segment checks pass: _iram_end=4037422c,
_iram_ext_end=4038682c, _runtime_stack_end=3fce4000, below app alias 3fce8000.


## APP-ENV 93 — initial application environment (2026-09-29)

Image 92 hardware passed explicit exits 7, 263 -> 7, 65535 -> 255 and
invalid 65536 -> usage/2, followed by hello, echo and disk/MM success.
Full capture: docs/hardware/app-exit-v92.log.

MINIX src/mm/exec.c installs arguments and environment in the new process
stack. CP32 now supplies HOME=/, PATH=/boot and USER=root through its existing
bounded stack builder. Xtensa crt0 already computes envp after argv's NULL
and passes it as the third call0 C argument. Strings and vector entries are
copied into application SRAM on every launch, rather than exposing pointers
to the kernel's environment. The 512-byte startup budget and 16-byte stack
alignment are preserved; remaining stack space is unchanged.

hello --env prints the received environment. This is a fixed bootstrap policy,
not mutable shell state, permission enforcement, PATH command search or full
execve inheritance. Applications remain trusted and share address space.
Tests cover loader environment selection, vector pointers/terminators, all
nine permitted arguments, fresh copies after mutation, populated/empty envp,
and existing exit/relaunch behavior.

Hardware pending: [FEATURE APP-ENV 93]1 / [TEST APP-ENV 93]. Try hello --env,
run /boot/hello --env, hello one two, hello --exit 7, hello --env, disk and mm.
Expected environment: HOME=/, PATH=/boot, USER=root, followed by exit=0.
Flashing remains with the user.

Validation: clean build and all 31 host scripts pass without compiler warnings.
ELF section/segment checks pass: _iram_end=4037422c, _iram_ext_end=40386830,
_runtime_stack_end=3fce41e0, below the application alias at 3fce8000.


## SHELL-EXEC 94 — direct external commands (2026-09-29)

Image 93 hardware confirms hello --env and run /boot/hello --env receive
HOME=/, PATH=/boot, USER=root, including relaunch after exit 7. Arguments and
disk/MM checks pass. Full capture: docs/hardware/app-env-v93.log.

MINIX reference: commands/ash/exec.c shellexec searches PATH for names without
slashes and executes slash-containing names directly. CP32 now dispatches
unmatched shell commands through its proven bounded foreground loader. Builtins
retain priority; the fixed bootstrap PATH has one directory, /boot. Explicit
run still resolves its argument against cwd. No environment mutation, general
PATH list parsing, quoting, pipes or separate user shell is implied.

The same ELF validation, execute-mode check, argument limits, blocking wait,
exit and reap path are reused. No architecture/frame/assembly change: this is
shell-side selection of the existing trusted call0 application path. Tests
cover bare-name lookup from another cwd, relative/absolute slash paths,
missing/nonexecutable errors and descriptor closure, alongside prior regressions.

Hardware pending: [FEATURE SHELL-EXEC 94]1 / [TEST SHELL-EXEC 94].
Try echo one two, /boot/hello --env, cd boot, ./echo again, cd /, nosuch,
echo recovered, hello --exit 7, disk and mm. No flashing performed.

Validation: clean build, all 31 host scripts and ELF section/segment checks
pass, with no compiler warnings. _iram_end=4037422c, _iram_ext_end=4038687c,
_runtime_stack_end=3fce4240 remain within their reserved regions.


## SHELL-ARGV 95 — quoted application arguments (2026-09-29)

Image 94 hardware passed echo, /boot/hello --env, ./echo after cd, unknown
command recovery, exit 7 and disk/MM checks. Capture: docs/hardware/shell-exec-v94.log.

MINIX reference: commands/ash/parser.c quote handling preserves quoted word
boundaries and removes quoting syntax. CP32 implements bounded in-place word
decoding for application launch only: single/double quotes, empty words,
adjacent quoted/unquoted fragments and backslash escapes. In double quotes,
backslash is removed before quote, backslash, dollar or backtick; otherwise it
is literal. No expansions or command operators are added. Other builtins keep
their existing parsers. Input remains 63 characters and at most eight app args.

Invariant: decoding never grows its buffer, rejects syntax/argument overflow
before opening a file, and passes ordinary strings through the existing SRAM
stack-copy and call0 startup path. No kernel frame or hardware changes.
Host tests cover spaces, empty strings, concatenation, escapes, malformed input,
argument exhaustion and rejection without a file open or process launch.

Hardware pending: [FEATURE SHELL-ARGV 95]1 / [TEST SHELL-ARGV 95]. Try
hello "one two" "", echo one\ two, hello 'unfinished, echo recovered,
hello --env, disk and mm. Flashing remains with the user.

Validation: clean build, all 31 host scripts and ELF section/segment checks
pass. _iram_end=4037422c, _iram_ext_end=403869e0,
_runtime_stack_end=3fce43c0 remain within reserved regions.


## APP-PID 97 — process identity and short executable reads (2026-09-29)

User confirms image 96 font looks correct on LCD. Serial capture confirms
quoted/empty arguments, escapes and recovery after malformed input:
docs/hardware/lcd-quotes-v96.log.

MINIX mm/forkexit.c allocates PIDs separately from process slots, wraps at
30000 and avoids live identities. CP32 now allocates 100..30000, skips live
process PIDs, and publishes the chosen identity with the new frame under the
scheduler lock. The reserved range avoids bootstrap process identities.
There are no process groups yet; this remains one trusted foreground slot,
not general fork, parent bookkeeping or waitpid.

Application ABI v2 appends getpid after the unchanged write/exit table offsets.
The callback validates the active child and obtains its PID through SENDREC;
the parent responds from the live child metadata. hello --pid prints it.
Bundled hello/echo accept v1/v2; old binaries that require exactly version 1
must be rebuilt for the v2 service table. crt0 offsets and call0 ABI unchanged.

The larger hello exposed short reads at filesystem block boundaries. Fix the
shell's executable reader to accumulate positive reads, rejecting premature
EOF/errors. The filesystem-to-ELF integration test now extracts this actual
production reader rather than maintaining a duplicate. ELF bounds and failed
load cleanup are preserved.

Host tests cover collision avoidance, repeated fresh identities, wrap at
30000, getpid responses in the production launch loop, hello PID formatting,
and actual multi-block ELF loading. Hardware pending: [FEATURE APP-PID 97]1 /
[TEST APP-PID 97]. Try hello --pid twice (100,101 after a fresh boot),
echo ok, hello --pid (103), hello --env, hello --exit 7, disk and mm.
Other successful application launches also consume PIDs. No flashing performed.

Validation: clean build, all 31 host test scripts and ELF section/segment
checks pass without compiler warnings. _iram_end=40374298,
_iram_ext_end=40386b64, _runtime_stack_end=3fce47d0 remain in reserved regions.


## APP-PARENT 98 — bootstrap parent identity (2026-09-29)

Image 97 hardware confirms PID 100,101, echo using 102, then PID 103,
exit 7 and disk/MM success. Capture: docs/hardware/app-pid-v97.log.

MINIX mm/getset.c GETPID returns process PID plus the parent's PID from its
MM parent relationship. CP32's shell is still the kernel-linked FS process;
assign it bootstrap PID 1, leaving IPC endpoint numbers unchanged. Capture
its positive PID in the foreground launch activation before loading. That
activation survives the wait and is discarded on return/reap, avoiding stale
parent identity on the reused child slot. This is not an MM parent table,
init process, reparenting, orphan handling or general waitpid.

ABI v3 appends getppid after getpid, preserving prior field offsets and crt0.
The callback checks active child ownership and uses SENDREC; its response is
the captured parent PID. hello --ppid displays the value. Bundled applications
accept v1..v3; previously compiled versions with strict version checks require
rebuilding. No process structure/assembly/register-frame layout changes.

Host checks cover getppid replies across repeated child slots, application
formatting, legacy PID/exit services and filesystem loading. Hardware pending:
[FEATURE APP-PARENT 98]1 / [TEST APP-PARENT 98]. After boot run hello --pid,
hello --ppid, echo ok, hello --ppid, hello --pid, disk and mm. Expected first
PID 100, PPID always 1 and final PID 104. No flashing performed.

Validation: clean build and all 31 host scripts pass without compiler
warnings. ELF section/segment checks pass: _iram_end=403742ac,
_iram_ext_end=40386be4, _runtime_stack_end=3fce49a0.


## APP-HEAP 99 — bounded per-application sbrk (2026-09-29)

Image 98 hardware confirms PPID=1, successful application execution and disk/MM
checks. PIDs 101 and 105 are correct: initial hello --pip consumed PID 100.
Capture: docs/hardware/app-parent-v98.log.

MINIX mm/break.c validates data growth against stack space. CP32 uses its
fixed 12 KiB data region: heap begins at the 16-byte-aligned end of ELF data/BSS
and may grow up to CP32_APP_STACK, never into the separate 4 KiB stack. ABI v4
appends sbrk(int); returns the old break or (void *)-1, with sbrk(0) querying it.
The callback checks active-child ownership and requests changes through IPC.
Each launch gets a fresh break; failed changes leave it unchanged. Positive
growth is zeroed, including after shrinking. Signed-minimum negative increments
and oversized positive increments are handled without overflow.

No changes to flat memory protection or proc/frame layouts: applications remain
trusted and the entire reserved data window is mapped. This is not malloc/free,
general MM brk syscall dispatch, dynamic address-space allocation or fork.
Bundled apps accept ABI 1..4; older strict-version binaries need rebuilding.

hello --heap checks growth, zeroes, writes, oversized request rejection,
shrink, below-floor rejection and zeroed regrowth. Host tests exercise break
boundaries, INT_MIN, unchanged-on-failure, zeroing extent, repeated launch
reset and application behavior. Hardware pending: [FEATURE APP-HEAP 99]1 /
[TEST APP-HEAP 99]. Try hello --heap twice, hello --pid, hello --ppid,
hello --env, echo ok, disk and mm. Expect Heap grow/shrink OK and exit=0.
No flashing performed.

Validation: clean build and all 31 host test scripts pass without compiler
warnings. ELF section/segment checks pass: _iram_end=403742ac,
_iram_ext_end=40386d38, _runtime_stack_end=3fce4e90, below app alias 3fce8000.


## APP-MALLOC 100 — application allocation library (2026-09-29)

Image 99 hardware confirms two Heap grow/shrink OK results, PID/PPID,
environment, echo and disk/MM checks. Capture: docs/hardware/app-heap-v99.log.

MINIX lib/ansi/malloc.c grows through sbrk, tracks slots and merges free slots.
CP32's small bootstrap adaptation lives in apps/lib/heap.c: initialize once
with cp32_heap_init(services), then cp32_malloc(unsigned)/cp32_free(void *).
It uses first-fit, 16-byte payload alignment, split blocks and adjacent free
block coalescing. Metadata and payload live in application SRAM and reset on
image reload. Zero size, overflow and exhaustion return NULL; free(NULL) is
harmless. Reused bytes are unspecified. No kernel ABI bump or process/frame
change; the library uses the hardware-tested ABI-v4 sbrk callback.

The allocator requires exclusive ownership of sbrk after initialization and
is single-threaded. Freed blocks remain reusable rather than shrinking the
break; process-slot reload reclaims the whole region. No calloc/realloc,
errno, standard libc symbol replacement or protected heap is claimed.

hello --malloc exercises allocation, writes, preservation of a neighboring
live block, free/reuse, merge and oversized request rejection. Host tests
also cover splitting, alignment, exhaustion, interleaved frees and large
allocation from merged space without further growth, under UBSan.

Hardware pending: [FEATURE APP-MALLOC 100]1 / [TEST APP-MALLOC 100]. Run
hello --malloc twice, hello --heap, hello --pid, hello --ppid, echo ok,
disk and mm. Expect Malloc/free OK and exit=0. No flashing performed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386d38,
_runtime_stack_end=3fce57c0 remain below the reserved application alias.


## APP-REALLOC 101 — calloc/realloc (2026-09-30)

Image 100 hardware confirms repeated Malloc/free OK, heap, identity, echo
and disk/MM checks. Capture: docs/hardware/app-malloc-v100.log.

MINIX lib/ansi/malloc.c realloc preserves contents, allocates for NULL and
frees for zero size; calloc.c clears allocated bytes. Add cp32_calloc and
cp32_realloc to the application library. Multiplication overflow is rejected;
calloc clears reused memory. Realloc keeps capacity when shrinking and uses
allocate/copy/free when growing. Failure leaves the original allocation and
contents intact. No in-place growth optimization or errno yet. ABI v4, SRAM
bounds and kernel interfaces stay unchanged.

hello --alloc exercises zeroing, growth, prefix preservation, oversized
failure, shrink, zero-size free and reused zeroed memory. Host allocator tests
cover multiplication overflow, exhaustion preservation, adjacent live data,
NULL allocation and zero-size behavior. Applications now compile with -Os:
unoptimized hello exceeded the existing 7168-byte RAM fixture capacity. Kernel
remains -O0; both generated optimized ELF files are validated/loaded by tests.

Hardware pending: [FEATURE APP-REALLOC 101]1 / [TEST APP-REALLOC 101]. Try
hello --alloc twice, hello --malloc, hello --heap, hello --pid, hello --ppid,
echo ok, disk and mm. Expect Calloc/realloc OK and exit=0. Flashing remains
with the user; optimized application execution needs hardware confirmation.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. hello.elf=4400 bytes, echo.elf=1012 bytes.
_iram_end=403742ac, _iram_ext_end=40386d38, _runtime_stack_end=3fce5240.


## APP-RESIZE 102 — allocator resizing in place (2026-09-30)

Image 101 hardware confirms repeated calloc/realloc checks, malloc/free,
sbrk, identity, echo and disk/MM checks with size-optimized applications.
Capture: docs/hardware/app-realloc-v101.log.

MINIX lib/ansi/malloc.c realloc consumes adjacent free space and splits excess
space into a free tail. Add these behaviors to cp32_realloc: retain address on
growth when the next free block satisfies the full request, and split useful
space on shrink. Freed tails coalesce through the existing free implementation.
If the neighbor is insufficient, leave it intact while trying the existing
allocate/copy/free fallback, preserving the original allocation on failure.
No kernel ABI, frame, mapping or heap-boundary change.

Host tests check original contents, live neighbor data, unchanged heap break
and tail reuse. hello --resize performs the same visible application check.
Hardware pending: [FEATURE APP-RESIZE 102]1 / [TEST APP-RESIZE 102]. Try
hello --resize twice, hello --alloc, hello --malloc, hello --heap, echo ok,
disk and mm. Expect Resize in place OK and exit=0. Not flashed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386d38,
_runtime_stack_end=3fce5520 remain within reserved regions.


## APP-TRIM 103 — return free heap tail (2026-09-30)

Image 102 hardware confirms two Resize in place OK results, calloc/realloc,
heap, echo and disk/MM checks. hello -malloc was an ordinary argument (single
dash), not a malloc test. Capture: docs/hardware/app-resize-v102.log.

MINIX malloc maintains a top-of-heap boundary via brk; CP32 adds an explicit
cp32_heap_trim extension over the existing bounded sbrk service. It returns
a free last block, including its header, verifies the current break and
unlinks metadata only after shrink succeeds. Live last blocks are untouched;
failed shrink retains the list and contents. Return value is bytes released,
zero if nothing can be released, or -1 on failure. free still retains blocks
until trim is requested. The fixed address reservation is not released to
other processes; only this application's logical break shrinks.

No kernel ABI, map or frame changes. Host tests cover failed shrink, full
trim, live-prefix preservation and allocation after trim. hello --trim checks
the same behavior via real IPC/sbrk on hardware.

Hardware pending: [FEATURE APP-TRIM 103]1 / [TEST APP-TRIM 103]. Run hello
--trim twice, hello --resize, hello --alloc, hello --malloc, echo ok, disk and
mm. Expect Heap trim OK and exit=0. Not flashed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386d38,
_runtime_stack_end=3fce57f0 remain within reserved regions.


## APP-TRIM 103 hardware confirmation (2026-09-30)

User capture docs/hardware/app-trim-v103.log confirms two Heap trim OK runs,
Resize in place OK, Calloc/realloc OK, Malloc/free OK and echo, all exit=0.
Disk and MM report result=0; CORE checks pass and IRQ count 5000 reports
unknown=0. This validates the planned image-103 hardware sequence. It does
not establish general fork/wait, fault isolation or per-process FS support.
Documentation-only update; firmware and image marker unchanged. No flashing.


## APP-READ 104 — foreground application console input (2026-09-30)

MINIX fs/device.c handles DEV_READ with immediate replies or SUSPEND/REVIVE.
CP32 now bridges an application read callback through the foreground parent
and the existing TTY protocol. ABI v5 appends read(char *,unsigned), max 64
bytes; the parent validates the full application buffer before servicing it.
Zero-length reads return zero without touching TTY. Input uses FS stack
staging because the TTY request carries the FS endpoint/map, then copies to
the blocked child's buffer. Flush pending application output before waiting.
No process layout or assembly changes. This is canonical console input only,
not file descriptors, arbitrary file reads or a general FS server.

hello --read prints a prompt, reads up to 64 bytes and writes the result.
Bundled apps accept ABI versions 1..5; strict older binaries need rebuilding.
Host checks cover count/range rejection, valid read replies, FS staging,
short read copying, error propagation, prompt flush ordering and application
output. Existing TTY tests cover suspend/revive and canonical editing.

Hardware pending: [FEATURE APP-READ 104]1 / [TEST APP-READ 104]. Run hello
--read, type test then Enter; expect Read: test and exit=0. Repeat with an
empty line and a line edited using Backspace. Then hello --alloc, echo ok,
disk and mm. No flashing performed.

Validation: clean build, all 32 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce5a60 remain within reserved regions.


## APP-STDIO 105 — standard-stream wrappers (2026-09-30)

Image 104 confirms normal and empty console reads and return to the shell;
user explicitly confirms Backspace editing. Allocator, echo and disk/MM checks
pass. Capture: docs/hardware/app-read-v104.log.

MINIX lib/posix/_read.c and _write.c provide descriptor-based byte-count APIs.
CP32's bootstrap io library now exposes cp32_io_init, cp32_read and cp32_write
for standard stream numbers only: 0 reads TTY, 1/2 write the same TTY.
Read returns at most 64 bytes, preserving canonical short reads. Write splits
requests into callback-sized chunks, collects short writes, stops on zero
progress, and returns transferred bytes if a later callback fails. Invalid
streams, null nonempty buffers, oversized requests or invalid callback counts
are rejected. Zero-length valid requests are no-ops. ABI remains v5.

This is not general FS descriptors, errno, FILE buffering, printf or a full
POSIX syscall implementation. hello --io prompts, reads through fd 0 and
writes through 1/2, then verifies wrong-direction stream rejection. Host tests
cover 600-byte output, partial/error/zero transfers, EOF and count bounds.

Hardware pending: [FEATURE APP-STDIO 105]1 / [TEST APP-STDIO 105]. Run
hello --io, type a line then Enter; expect Read: followed by that line and
exit=0. Repeat with an empty line and Backspace editing. Then hello --alloc,
echo ok, disk and mm. No flashing performed.

Validation: clean build, all 33 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce5e30 remain within reserved regions.


## APP-LINE 106 — bounded line input (2026-09-30)

Image 105 hardware confirms --io input, empty lines, allocator and device
regressions; user confirms Backspace editing. Capture: docs/hardware/app-stdio-v105.log.
README now summarizes the implemented application runtime and validated status,
while retaining build, debug, flash, standalone startup and shell instructions.

MINIX lib/stdio/fgets.c bounds input, retains newline and NUL-terminates.
CP32 adds cp32_readline over standard input, returning fragment length, zero
at EOF or -1 on error; partial error output remains terminated. Capacity must
be at least two. Byte-at-a-time reads avoid consuming the following line;
excess characters remain in the existing TTY queue. No FILE buffering or full
fgets ABI is claimed. Existing ABI v5 and kernel services remain unchanged.

hello --line consumes fragments through a 16-byte buffer until newline/EOF.
Host tests cover long-line fragmentation, subsequent lines, empty lines, EOF,
partial error, tiny buffers and the application loop. Hardware pending:
[FEATURE APP-LINE 106]1 / [TEST APP-LINE 106]. Run hello --line, enter a line
longer than 15 characters, then repeat with empty input and Backspace editing.
Then echo ok, hello --alloc, disk and mm. No flashing performed.

Validation: clean build, all 33 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce6030 remain within reserved regions.


## APP-CAT 107 — standalone standard-input utility (2026-09-30)

Image 106 confirms line-reading and allocator, echo, disk/MM, IPC and SYS
regressions. Capture: docs/hardware/app-line-v106.log.

MINIX commands/simple/cat.c copies standard input when no filenames are given.
Add independently linked apps/cat/cat.c using the shared cp32_read/cp32_write
library. Copy until EOF, fail on I/O errors, reject unsupported file arguments
with usage/exit=2. The kernel's cat filename builtin remains unchanged.
This is the stdin subset, not the complete MINIX utility or file API.

Package inode 9 at zones 44..50, preserving the other application extents and
reserved disk diagnostic sector. Reuse the fixed ELF slot, call0 crt0 and ABI
v5. make cat builds it independently. Host tests cover multi-read input,
short writes, EOF, read/write failures and unsupported arguments. The real
filesystem-to-ELF test now loads all three programs sequentially.

Hardware pending: [FEATURE APP-CAT 107]1 / [TEST APP-CAT 107]. Run ls boot,
run /boot/cat, enter two lines, then Ctrl-D on an empty line. Expect copied
lines and Application exit=0. Repeat for slot reuse, then hello --alloc,
echo ok, disk and mm. EOF after typed text may require a second Ctrl-D.
No flashing performed.

Validation: clean build, all 34 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40386e80,
_runtime_stack_end=3fce67c0 remain within reserved regions.


## APP-REDIRECT 108 — application stdin from files (2026-10-01)

Image 107 confirms repeated standalone cat input/EOF, allocator and device
regressions. Capture: docs/hardware/app-cat-v107.log.

MINIX commands/ash/redir.c NFROM opens input read-only and restores/closes
redirected descriptors. CP32 adds one trailing unquoted < path to application
launch only. The scanner respects quote/escape state; the filename is decoded
with the bounded word parser. Paths resolve against shell cwd. Missing,
multiple or extra operands are rejected before launch. Quoted '<' is literal.

The parent opens the file after validating the executable, services app reads
through the existing FS staging buffer, and closes/resets its input descriptor
on normal exit or load failure. Subsequent launches again use TTY. The shell
itself does not lose its keyboard input. No ABI/frame/map changes. This is
single foreground redirection, not per-process FS tables, output redirection,
pipes or application open/close. Other builtins retain existing parsers.

Host tests cover parser quoting/errors, redirected read routing, successful
launch and failed-load cleanup, plus existing TTY and filesystem tests.
Hardware pending: [FEATURE APP-REDIRECT 108]1 / [TEST APP-REDIRECT 108]. Try
run /boot/cat < readme, run /boot/cat < boot/readme, run /boot/cat < nosuch,
then run /boot/cat (type a line and Ctrl-D), hello --alloc, disk and mm.
Not flashed.

Validation: clean build, all 34 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403742ac, _iram_ext_end=40387074,
_runtime_stack_end=3fce69f0 remain within reserved regions.


## APP-WC 112 — standalone word count (2026-10-07)

User confirms image 111 looks good. Retain expanded font samples.
MINIX commands/simple/wc.c counts newline bytes, whitespace-delimited words
and bytes with -l/-w/-c selection. Add a standalone /boot/wc implementing the
stdin subset with combined/separate options and fixed lines/words/bytes output
order. Reuse shared I/O callbacks and shell-owned input redirection. Preserve
word state across reads, count the final non-newline word, reject unsupported
file operands and fail on I/O/counter overflow. No new kernel ABI or layout.

Package inode 10 in zones 51..57; keep existing fixtures and diagnostic block
63 intact. make wc builds its own ELF. Tests cover byte-at-a-time input,
short output, empty/whitespace-only input, option selection and error paths.
Real filesystem-to-ELF tests load all four application images sequentially.

Hardware pending: [FEATURE APP-WC 112]1 / [TEST APP-WC 112]. Run
run /boot/wc < readme, run /boot/wc -l < readme, run /boot/wc -c < readme,
then run /boot/wc interactively (type one two, Enter, then Ctrl-D on an empty
line; expect 1 2 8). Existing builtin wc remains available for comparison.
Then hello --alloc, disk and mm. No flashing performed.

Validation: clean build, all 35 host scripts and ELF section/segment checks
pass without compiler warnings. _iram_end=403743d4, _iram_ext_end=40387008,
_runtime_stack_end=3fce7360 stays below app alias 3fce8000 (3232-byte margin).


## APP-ERRNO 113 — application I/O errors (2026-10-07)

Image 112 hardware confirms readme counts 2 8 61, -l=2, -c=61 and interactive
1 2 8. Capture: docs/hardware/app-wc-v112.log.

MINIX include/errno.h defines positive userspace EIO=5, EBADF=9, EFAULT=14,
EINVAL=22. Add application-local cp32_errno with scoped CP32_ constants to
avoid kernel sign conventions. Standard-stream wrappers distinguish invalid
stream direction, null nonempty buffer, initialization/count errors and
callback failure. Success/EOF leaves errno unchanged; partial-write failure
returns progress and records EIO. Opaque bootstrap callback errors map to EIO
rather than inventing underlying FS error identities. Heap errno and full libc
errno/syscall translation remain unfinished. Kernel ABI stays v5.

hello --errno checks the new behavior. Consolidate repeated hello option
matching without removing modes. The added code exceeded the fixed 7168-byte
fixture, so independently linked applications use -mno-longcalls alongside
-Os: all direct callees are within their validated 16 KiB text region. The
linker resolves local call0 relocations; service callbacks remain callx0.
Kernel compilation remains unchanged. This compiler change needs hardware
regression. Final hello ELF is 7156 bytes, only 12 bytes below its fixture
limit; further growth needs a deliberate packaging/runtime-size solution.

Hardware pending: [FEATURE APP-ERRNO 113]1 / [TEST APP-ERRNO 113]. Run hello
--errno twice, hello --alloc, hello --io (enter a line), run /boot/wc < readme,
echo ok, disk and mm. Expect I/O errno OK and exit=0. Not flashed.

Validation: clean build, all 35 host scripts, ELF section/segment checks and
application call disassembly pass without compiler warnings.
_iram_end=403743d4, _iram_ext_end=40387008, _runtime_stack_end=3fce7540.


## APP-ZONES 114 — variable application file allocation (2026-10-07)

Hardware image 113 confirms repeated I/O errno checks, calloc/realloc, interactive
I/O, echo, disk and MM. Capture: docs/hardware/app-errno-v113.log. The wc command
in that capture was consumed by hello --io, so it is not a new wc regression.

MINIX V2 fs/const.h and read.c use seven direct zones followed by a table of
32-bit zone numbers. The image generator now uses this format for applications
and packs actual file sizes into shared zones 30..62, instead of reserving seven
blocks per executable. It accounts for indirect tables, updates allocation
bitmaps and rejects aggregate exhaustion. Block 63 stays reserved for the disk
diagnostic. ESP32 application addresses, call0 ABI, fixed executable slots and
read-only runtime filesystem are unchanged. This removes a packaging limit,
not the SRAM limit: embedded bytes and sparse-run records still consume SRAM.

Validation: clean build and all 35 host scripts pass. Tests cover the 7169-byte
boundary, exact zone capacity, aggregate overflow, bitmaps, following-file
placement and a padded valid ELF read through production FS and image loader.
ELF size/sections/segments pass; _iram_end=403743d4,
_iram_ext_end=40387008, _runtime_stack_end=3fce7550 (2736 bytes margin).

Hardware pending: [FEATURE APP-ZONES 114]1 / [TEST APP-ZONES 114]. Run hello
--errno, hello --alloc, echo ok, run /boot/wc < readme, run /boot/cat < readme,
disk and mm. Large-file loading is host-tested; current production hello still
fits direct zones. No flashing performed.


## APP-STREAM 115 — buffered application input (2026-10-07)

Image 114 hardware confirms errno/allocator checks, echo, redirected wc
(2 8 61), cat, disk and MM; capture: docs/hardware/app-zones-v114.log.

Add apps/lib/stream.c and stream.h: caller-owned 64-byte stdin buffering,
cp32_getc, cp32_fgets, cp32_feof, cp32_ferror and cp32_clearerr. MINIX 2
lib/stdio/fgetc.c and fgets.c establish unsigned-character results, retained
newline, bounded NUL termination, EOF after partial text and error distinction.
CP32 uses existing ABI-v5 read callbacks over IPC instead of a full FILE/syscall
runtime. EOF/error remain sticky until clearerr; clearing flags preserves unread
buffered bytes. Capacity below two is rejected. A valid stream pointer is
required. Do not mix buffered and raw reads from the same input source.

The standalone wc now uses buffered getc in its normal counting path. No kernel
ABI or process layout changes. Full stdio, file opening and output buffering
remain unfinished. Tests cover high-bit/NUL bytes, refill boundaries, line
fragments, final lines without newline, sticky EOF/error, recovery and partial
line failure; existing wc checks cover counting and read/write errors.

Validation: clean build and all 36 host scripts pass, including UBSan stream
checks; ELF sections/segments pass. _iram_end=403743d4,
_iram_ext_end=40387008, _runtime_stack_end=3fce7870. Only 1936 bytes remain
before the application text's DRAM alias; further embedded growth needs care.
Hardware pending: [FEATURE APP-STREAM 115]1 / [TEST APP-STREAM 115]. Test
run /boot/wc < readme (2 8 61), run /boot/wc -l < readme (2),
run /boot/wc -c < readme (61), then hello --errno, disk and mm.
Line helper behavior is host-tested; wc exercises character buffering on hardware.
Not flashed.


## APP-FLUSH 116 — buffered application output (2026-10-08)

Image 115 hardware confirms wc default/line/byte counts (2 8 61 / 2 / 61),
hello --errno, disk and MM. The capture also shows sustained timer activity
through a long idle interval; this is not concurrent-application stress proof.
Capture: docs/hardware/app-stream-v115.log.

MINIX 2 lib/stdio/fflush.c reports completion or a stream error when buffered
output cannot be written. CP32 adds apps/lib/output.c and output.h with a
caller-owned 64-byte stdout/stderr buffer, cp32_putc, cp32_flush, error inspection
and clearerr. It uses the existing ABI-v5 bounded IPC write wrapper, preserving
bare-metal call0 and current process ownership. A partial write removes only
the delivered prefix, retaining unsent bytes for retry after clearerr. A full
buffer is flushed before accepting the next character; failure leaves that
character unconsumed. Zero progress is an I/O error. Flush is explicit before
exit or blocking input; automatic exit cleanup, full FILE and general file
descriptors remain unimplemented. Do not interleave raw and buffered writes.

The standalone wc now buffers its result and flushes before returning; its
usage errors still use immediate stderr. Tests cover high-bit/NUL characters,
full buffers, empty flush, repeated short writes, partial failure and retry,
sticky errors, zero-progress writes, invalid streams and failure before
accepting the next character. Existing wc tests exercise normal counts and
input/output failure through the production application.

Validation: clean build, all 37 host scripts including UBSan checks, ELF
sections/segments and image layout pass without compiler warnings.
_iram_end=403743d4, _iram_ext_end=40387008, _runtime_stack_end=3fce7b60.
Only 1184 bytes remain before the application code DRAM alias. Embedded
application growth will need a memory-placement or packaging improvement soon.
Hardware pending: [FEATURE APP-FLUSH 116]1 / [TEST APP-FLUSH 116]. Run
run /boot/wc < readme, run /boot/wc -l < readme, run /boot/wc -c < readme,
hello --errno, disk and mm. Expected counts remain 2 8 61 / 2 / 61.
Write-failure injection remains host-tested. Not flashed.


## APP-FILES 117 — application file access and utility operands (2026-10-08)

Image 116 hardware passes default/selected wc counts, errno, disk and MM.
Capture: docs/hardware/app-flush-v116.log.

This feature spans the application ABI, IPC, foreground FS ownership, libc
wrappers and utilities. MINIX 2 fs/open.c do_open/do_close/do_lseek and
filedes.c establish per-client descriptors, independent open offsets, seek
validation and closing references. CP32 retains its single trusted fixed
application slot and adapts these rules to four owned handles (3..6), backed
by the existing read-only MINIX filesystem. Shell executable/redirection
handles are never exposed to the child. The parent handles requests while
waiting for that child and closes leftovers before launch and on normal exit.
This does not add concurrent FS clients, fork inheritance or fault cleanup.

ABI v6 appends one typed file request callback; earlier fields keep their
layout. Application callbacks validate process identity and use SENDREC to FS;
the parent checks operation, transfer/path length and complete application
memory span before dereferencing. Open names include a terminator and are
bounded to 255 bytes. Relative paths resolve from the waiting shell cwd.
Directories are rejected; a regular file needs read bits. This is the trusted
bootstrap permission policy, not full credential-based POSIX authorization.
File reads are staged through FS memory and capped at 64 bytes. Seek supports
signed 32-bit offsets and SET/CUR/END. Helpers translate private FS statuses
into positive MINIX errno in the application, preserving specific missing,
directory, descriptor, permission, exhaustion and argument errors.

Public wrappers: cp32_open(path), cp32_read(fd,buffer,count), cp32_lseek(fd,
offset,whence), cp32_close(fd). Buffered input can select a file descriptor;
reinitialize only when intentionally discarding unread buffered data.
Standalone cat copies sequential file operands or '-' stdin. Standalone wc
accepts -lwc, '--', filenames and '-' stdin, prints per-file names/counts and
aggregate totals, and continues after missing-file errors with exit=1.
The kernel builtins remain; use run /boot/cat and run /boot/wc to exercise apps.
hello --files checks independent offsets, seek/EOF, exhaustion, closed-handle
errors, missing/directory paths, and intentionally leaves three handles for
exit cleanup. Repeat it on hardware to prove subsequent launch reuse.

Memory: the first full implementation exceeded the fixed application boundary.
Coalescing embedded nonzero runs across zero gaps of at most four bytes avoids
redundant metadata; reconstruction remains byte-identical. Application linking
now uses --gc-sections so unreferenced library routines are omitted. No heap,
stack or application-slot reservation is reduced. Final _iram_end=403743d4,
_iram_ext_end=40387424, _runtime_stack_end=3fce6360, leaving 7328 bytes before
the application text data alias. Final hello is 5136 bytes; larger ELF loading
remains covered by the padded indirect-file integration test.

Validation: clean build, all 38 test scripts, ELF sections/segments and image
layout pass. Tests compile production FS and app wrappers/utilities together,
cover handle isolation/exhaustion/reuse, 40 leak/cleanup cycles, errno, cwd,
seek, single/double-indirect reads, request bounds, actual hello --files, cat/wc
multiple operands and missing-file recovery. Launch/reap tests check cleanup
on each of 20 launches. Sparse reconstruction and all four executable loading
checks pass. Hardware pending: [FEATURE APP-FILES 117]1 / [TEST APP-FILES 117].
Not flashed.

Hardware exercise:
hello --files (twice; File API OK and exit=0)
run /boot/cat readme boot/readme (file contents twice)
run /boot/wc readme boot/readme (2 8 61 per file, 4 16 122 total)
run /boot/wc -c readme boot/readme (61 per file, 122 total)
run /boot/cat missing readme (error, valid file still printed, exit=1)
cd boot; then run /boot/wc -c readme (61 readme), then cd /
run /boot/wc < readme (2 8 61), hello --errno, disk, mm
Enter each command separately; shell command chaining is not implemented.


## APP-DIRS 118 — application directory/metadata APIs and ls (2026-10-08)

Hardware image 117 confirms repeated hello --files, file operands and totals
in cat/wc, missing-file recovery, redirected stdin, disk and MM.
Capture: docs/hardware/app-files-v117.log.

MINIX 2 lib/posix/_fstat.c and fs/stadir.c obtain metadata from an open inode;
lib/posix/_readdir.c converts directory entries and skips deleted slots. CP32
preserves those portable semantics using its validated V2 directory decoder
and fixed ABI records, rather than exposing an in-kernel pointer or historical
host-sized struct stat. ABI v7 extends file operations without changing table
layout. cp32_opendir returns one of four owned fd handles, cp32_readdir returns
1/0/-1 for entry/EOF/error, cp32_fstat reads the opened inode, and cp32_stat
resolves a pathname without consuming a descriptor. cp32_close releases either
kind, and cp32_lseek(fd,0,0) rewinds directory iteration. Separate opens have
independent offsets; normal exit cleanup covers both kinds.

The parent validates exact record lengths and complete application address
spans before access. Path requests are bounded and terminated. Metadata and
entries are copied from aligned FS temporaries, allowing unaligned application
buffers; EOF/errors leave result records unchanged. stat/fstat return inode,
mode, link count, numeric uid/gid, size and three stored timestamps. Paths use
the launching shell cwd. Read-only directory opening requires read bits, while
path metadata lookup requires no extra descriptor or read-open permission.
This is still one trusted foreground client, not general FS credentials or a
protected multi-process server.

Add a fifth independent executable /boot/ls and package it in the MINIX image.
It lists current directory by default, supports -a (hidden entries), -l (mode,
links, UID, GID, byte size), -d (directory itself), '--', and multiple paths.
It reports a failed path and continues to later operands with exit=1. Listing
is streamed in disk order with bounded memory; sorting, recursion, dates and
owner names are not implemented. Bare ls remains the kernel builtin.
Application builds link the existing freestanding lib/ansi/memcpy.c for
compiler-generated aggregate copies; no hosted libc or ESP-IDF is introduced.

Validation: clean build and all 39 host scripts pass without compiler warnings.
New integration runs actual directory and metadata wrappers, production FS/IPC
request validation and ls over the demo disk, including boot/large's indirect
entry, deleted slots, independent offsets, rewind, closed/non-directory handles,
full handle tables, stat without spare fds, unchanged EOF/error results, short
record rejection, unaligned buffers, missing operands, default cwd, invalid
options, and output failure cleanup. All five real ELFs load through the FS
and image loader tests. ELF sections/segments and image layout pass.
_iram_end=403743d4, _iram_ext_end=403875ec, _runtime_stack_end=3fce6f40;
4288 bytes remain before the application text DRAM alias. ls.elf is 2616 bytes.

Hardware pending: [FEATURE APP-DIRS 118]1 / [TEST APP-DIRS 118]. Run:
run /boot/ls /boot
run /boot/ls -al /boot/large
run /boot/ls -l /readme
run /boot/ls -ld /boot
run /boot/ls missing /readme
hello --files
run /boot/wc readme boot/readme
disk
mm
For boot/large expect '.', '..', 'readme'; /readme long output is
-r--r--r-- 3 0 0 61 /readme. The missing-path command prints the valid operand
and exits 1; successful commands exit 0. Also try cd boot followed by
run /boot/ls and cd / as separate commands. Not flashed.

## Image 120: transactional foreground application exec

ABI v8 adds `cp32_execv(path, argv)` and `/boot/exec`. A running application
can replace its fixed-slot ELF while retaining its process slot, PID, parent
wait relationship and all four owned file/directory handles with their offsets.
Success starts at the replacement entry and never replies to the old SENDREC
buffer. The new context clears general/special registers and blocked-frame
bookkeeping, resets the application heap and BSS, and installs fresh argc/argv
and the fixed HOME/PATH/USER environment. Paths resolve against the shell cwd;
the process name is the executable basename.

MINIX reference: `minix-2.0.0/src/mm/exec.c` copies arguments before replacing
memory, installs a fresh context without allocating a new PID, and notifies FS
for close-on-exec processing. CP32 uses fixed Xtensa ELF addresses and its
existing internal SRAM instruction/data alias. The foreground FS coordinator
requests a temporary 32 KiB allocation from MM, loads and validates the complete
image and stack there, then copies into the blocked child's slot and publishes
the new context under the scheduler lock. It releases staging on both success
and failure. Unlike MINIX's early `new_mem` transition, no old image bytes are
changed before all payload reads succeed. No hardware mapping changes are made.

Missing/nonexecutable paths, malformed images, read errors and MM allocation
failure return errno through the existing file IPC callback; the old image,
registers, heap break and application handles remain intact. Loader descriptors
are separate from the application's four handles, so exec works with a full
application fd table. Arguments are bounded to nine entries and 256 string
bytes. This is trusted single-foreground execv, not general MM lifecycle
ownership, fork, execve environment inheritance or protected execution.
Close-on-exec flags are not exposed by this bootstrap application ABI.

Validation: `make clean && make`, all 40 `make tests` scripts, ELF section/segment
inspection and image-layout verification pass. No compiler warnings. The new
integration test runs production FS, ELF loading, stack construction and
replacement logic, modeling IPC and physical memory. It injects failures at
every disk-read boundary, checks exact rollback of image/process/heap state,
MM exhaustion, invalid executable/request/argument rejection, repeat staging
release and successful replacement with a full fd table and preserved offset.
All six generated application ELFs pass loader validation. Host tests do not
execute Xtensa replacement instructions.

ELF endpoints: `_iram_end=0x403743d4`, `_iram_ext_end=0x40387ba0`,
`_runtime_stack_end=0x3fce7c50`. The new helpers and literals are in extended
IRAM. Only 944 bytes remain before the application text's DRAM alias at
0x3fce8000; existing 128 KiB heap and stack reservations remain intact.

Hardware pending, not flashed: `[FEATURE APP-EXEC 120]1` and
`[TEST APP-EXEC 120]`. Run these commands separately:

```text
hello --exec-fail
hello --exec
run /boot/exec /boot/echo replaced successfully
run /boot/exec /missing
hello --files
run /boot/ls /boot
mm
disk
```

Expect `Exec failure recovery OK`, then `Exec PID and file OK`, both exit=0.
The exec-to-echo command prints `replaced successfully` and exits 0; missing
path prints `exec: cannot execute` and exits 1. Repeat `hello --exec` and check
that `mm` has no leaked staging allocation and the shell remains usable.
Invalid-image and injected read/allocation failures are host-verified only.

## Image 121: keep MM allocations inside the linker heap

Image 120 hardware FAILED during `hello --exec`, after `hello --exec-fail`
returned exit=0. User serial report: EXCCAUSE=0x1c, EXCVADDR=0,
EPC1=0x403d8002, PS=0x110. The expected instruction at that PC is `mov.n a6,a1`,
not a memory load; an intact image does not explain that fault there.

Root defect found in `mem_init`: `_heap_start=0x3fcb4c50` was rounded DOWN
to a 4 KiB click, yielding staging base 0x3fcb4000. The 3152 bytes before the
heap belong to kernel BSS, including part of the FS cache, its control fields,
and the MM allocation table at 0x3fcb4848. Clearing/loading the exec buffer
there overwrites these globals, and subsequent cache fills can overwrite
staged executable bytes. This is software memory overlap, not evidence of a
new Xtensa context or instruction-cache requirement. The earlier host exec
test modeled a separate staging array and therefore missed this integration
boundary; it also used 256-byte clicks instead of the target's 4096-byte clicks.

Fix: round the heap start UP and `_heap_end` DOWN to whole clicks, exposing
only the interior range. Empty/reversed/sub-click ranges expose zero pages.
Arithmetic rounds after division to avoid overflow at the address-space end.
The 128 KiB linker reservation is unchanged; this layout supplies 31 usable
4 KiB pages (124 KiB), enough for the 32 KiB exec staging allocation. MINIX's
whole-free-click allocation invariant is preserved without allocating BSS or
stack bytes. Correct the exec host model to use target-sized clicks.

Validation: clean ELF/bin build, all 41 host scripts and ELF sections/segments
and image-layout checks pass without compiler warnings. New regression runs
the production mem_init and allocator against image 120's exact addresses,
all 4096 alignment offsets, empty/reversed/address-limit ranges, whole-heap
exhaustion and release. The transactional exec regression still passes.
Image 121 endpoints: `_iram_end=0x403743d4`, `_iram_ext_end=0x40387bac`,
`_heap_start=0x3fcb4c60`, `_heap_end=0x3fcd4c60`,
`_runtime_stack_end=0x3fce7c60`; 928 bytes remain before the app reservation.
MM's usable interval is [0x3fcb5000, 0x3fcd4000).

Hardware retest pending, not flashed: `[FEATURE APP-EXEC 121]1` and
`[TEST APP-EXEC 121]`. Run `hello --exec-fail`, `hello --exec` twice,
`run /boot/exec /boot/echo replaced successfully`, then `mm` and `disk`.
Expect failure recovery, `Exec PID and file OK` with exit=0 on each successful
self-replacement, echo output and a usable shell without leaked MM staging.
Image 120 does not establish successful exec hardware validation.


## Image143: writable descriptors (ABI v9)

The service table layout is unchanged; file operations11/12/13 add a bounded
open request, write and dup. The open record contains a 256-byte terminated
path plus flags and mode. The kernel checks alignment, exact record size and
application data bounds before the shell resolves the path and opens an owned
handle. Writes copy at most64 bytes into a kernel buffer per request. The
runtime chunks larger writes and preserves confirmed progress on later errors.
MINIX open.c/read.c/filedes.c provide the access/append/shared-filp model;
CP32 retains its bounded inode-backed descriptions and serialized FS service.
The ESP32-S3 has no process MMU here: pointer checks and handle ownership remain
mandatory, and image staging/exec preserves the four owned file slots.

See README image143 for API behavior, limits and `hello --file` / `hello --check`
hardware acceptance. General credentials, unlink-while-open, mount and recovery
remain separate port milestones. ABI v6–8 read-only clients remain compatible.


## Image146: filesystem process lifecycle and ABI v10

The FS service selects an explicit `cp32_fproc` record; no request supplies a
PID or another process's namespace. The kernel binds the record to its allocated
child PID before publishing the initial runnable frame and checks the binding
for received child requests. Launch clones the shell's cwd, root inode, mask,
identity and owned references, including redirected stdin. The serialized
backend retains shared open descriptions; process fd tables map only their
owned handles. A failed clone unwinds child references without touching parent
references or offsets.

Successful exec invokes the FS CLOEXEC hook only after image/argv staging has
succeeded, before the new runnable frame is published. Any recoverable failure
leaves cwd, mask, descriptor flags and offsets intact. Exit releases owned
references and invalidates the process record. The shell's private loader and
maintenance handles are outside the app namespace. `cp32_fproc_file` handles
requests in src/fs/appfile.c; the kernel validates application spans through
`cp32_fproc_validate` before dispatch. FS code uses the existing size-optimized
extended-IRAM placement, retaining the call0 ABI and all kernel stacks.

ABI10 adds chdir/getcwd/umask and descriptor/status fcntl operations. See README
for the `hello --state` hardware test and current single-foreground limitations.
The clone hook models MINIX misc.c do_fork; it does not expose a general fork
syscall. Root inode is fixed1 and credential enforcement is not newly claimed.


## Image147: ABI v11 unlink

`cp32_unlink` uses a bounded terminated pathname request (operation21) and the
calling fproc cwd. The kernel checks the application span before dispatch.
Unlink leaves open references valid at link count0, including across exec and
process-state cloning. Final close, CLOEXEC and exit use the shared reclamation
path. Explicit close errors consume ownership; retrying that fd is invalid.
No orphan recovery after reboot or general mount/hotplug support is implied.
See README for the short `hello --unlink` acceptance test and failure limits.

## ABI12: mkdir

`CP32_APP_MKDIR` (22) accepts a `cp32_app_open_request`: a terminated path
in256 bytes, flags zero, and a mode. The IPC argument must equal the full
record size and the record must be aligned and within application memory.
The runtime `cp32_mkdir` returns0 or-1 with cp32_errno. Earlier service-table
prefixes are unchanged; older kernels return ENOSYS via the runtime guard.
Only rwx mode bits are stored, filtered by the application's umask. Empty paths
return ENOENT; duplicate names EEXIST; missing/non-directory parents retain
ordinary lookup errors. RAM builds return EROFS. No new descriptor is consumed.
`hello --dir` is the SD hardware acceptance command; see README for cleanup.

## ABI13: regular-file hard links

`cp32_link(source, destination)` uses operation23 (`CP32_APP_LINK`) and a
512-byte `cp32_app_link_request` containing two256-byte terminated paths. The
kernel validates the complete span, exact record size and both terminators
before dispatch. Byte arrays need no alignment. Empty paths return ENOENT;
duplicate destinations EEXIST; link-count exhaustion EMLINK; directory sources
EISDIR; RAM recovery EROFS. Other lookup/storage errors retain their normal
mapping. The runtime returns0 or-1 with cp32_errno and guards older kernels
with ENOSYS. No descriptor slot is consumed. Existing ABI prefixes are preserved.

The filesystem remains single-mount and trusted, without credential enforcement.
Hard links reuse inode metadata and mappings; only directory entries and link
counts change. `hello --link` exercises relative cross-directory linking and
open-file lifetime through unlink; successful runs leave no files behind.

## ABI14: chmod

Operation24 (`CP32_APP_CHMOD`) accepts `cp32_app_open_request` with a terminated
path, flags zero and mode0000..0777. Runtime `cp32_chmod` returns0 or-1 with
cp32_errno; older kernels are rejected with ENOSYS. Kernel validation checks
record size, alignment, address span and termination before filesystem dispatch.
Empty paths return ENOENT, unsupported mode bits or flags EINVAL, RAM writes
EROFS, and other lookup/I/O errors preserve their usual mappings.

The process cwd resolves relative names; umask is deliberately not applied.
Mode updates preserve the inode and all non-mode bytes. File and directory
metadata can be changed, but owner/group and directory search permission
checks remain unimplemented. Already-open descriptors retain their access;
new opens use the current aggregate rwx checks. `hello --mode` exercises the
production path with hard links, failed new opens and continued descriptor I/O.

## ABI15: empty-directory removal

`cp32_rmdir(path)` uses operation25 (`CP32_APP_RMDIR`), with the terminated
path length including NUL, bounded to256 bytes. The runtime returns0 or-1 with
cp32_errno and rejects older kernels with ENOSYS. The kernel validates the
complete span and final terminator before dispatch. ENOTEMPTY rejects a nonempty
directory; EBUSY protects root, open directories and either live client's cwd;
ENOTDIR rejects a regular file; terminal . or .. returns EINVAL; RAM returns
EROFS. Other lookup and I/O errors retain their ordinary mappings.

The production FS owner passes the suspended shell record to the rmdir helper,
which resolves the target against application cwd and compares protected inode
identities before mutation. This covers CP32's current two-client foreground
model, not a future arbitrary process table. The operation reuses validated
filesystem deletion and reclamation; uncertain writes may leak allocations.
`hello --rmdir` is the self-cleaning hardware acceptance command.

## ABI16: no-replacement regular-file rename

Operation26 (`CP32_APP_RENAME_NOREPLACE`) uses `cp32_app_link_request` with two
256-byte terminated paths. `cp32_rename_noreplace` returns0 or-1 with cp32_errno;
older kernels return ENOSYS. Kernel validation checks exact512-byte size, full
span and both terminators. The byte-array record does not require alignment.
Empty paths return ENOENT, directory sources EISDIR, a different destination
inode EEXIST, link-count exhaustion EMLINK and RAM recovery EROFS. Same-inode
source/destination pairs succeed without removing either name.

Both names resolve against the active application's cwd. The backend preserves
inode identity and existing descriptors. Cross-directory moves can leave both
names on uncertain I/O, and are not power-loss atomic. Full POSIX destination
replacement and directory renaming remain future features. `hello --move`
exercises this API and cleans up its private directory on success.

## ABI17: descriptor resizing

`cp32_ftruncate(fd, length)` uses operation27 (`CP32_APP_FTRUNCATE`), fd in the
usual descriptor argument, null buffer and byte length in the argument field.
The runtime returns0 or-1 with cp32_errno and guards older kernels with ENOSYS.
Negative lengths return EINVAL; sizes beyond263 KiB or the superblock maximum
return EFBIG. Invalid/read-only descriptors return EBADF; RAM returns EROFS.
No caller buffer is accessed.

File identity and shared offsets are unchanged, including when the retained
offset lies beyond the new EOF. Shrink releases storage using existing ordering;
growth leaves unmapped regions as holes and clears mapped bytes before publishing
the new size. Sparse growth allocates no blocks. Other descriptors observe the
new size on refresh. Open-but-unlinked files can be resized and are reclaimed
on final close. Failures may partially commit; rollback is not promised.
`hello --size` exercises shrink, growth, zero fill, duplicate and independent
opens, indirect blocks, unlink lifetime and bounds, with automatic cleanup.

## ABI18: descriptor replacement

`cp32_dup2(source, target)` uses operation28 (`CP32_APP_DUP2`), source in the fd
field, target in the unsigned argument and no buffer. Success returns the target
fd; failure returns-1 with cp32_errno. Older kernels return ENOSYS, invalid or
closed sources and targets outside3..6 return EBADF. Empty targets may return
EMFILE on backend exhaustion; occupied targets reuse their existing slot.

The fproc layer maps process-local descriptor numbers to backend handles and
validates before closing a target. Replacements share offsets and file status,
clear the target's CLOEXEC flag, and leave source CLOEXEC unchanged. Equal
source/target leaves all state unchanged. Final close of an unlinked target
uses existing reclamation; close errors are not reported by dup2. Exit and exec
continue to release the references through the normal process lifecycle.
This API currently excludes callback-backed standard streams0..2. `hello --dup`
provides a short hardware acceptance command with automatic cleanup.

## ABI19: F_DUPFD

`cp32_fcntl(fd, CP32_F_DUPFD, minimum)` uses operation29 (`CP32_APP_DUPFD`),
source in the fd field, minimum in the argument and no buffer. The runtime guards
older kernels with ENOSYS. Success returns the chosen local descriptor; failure
returns-1 with cp32_errno. Invalid/closed sources return EBADF, minimum outside
0..6 (including unsigned representations of negatives) EINVAL, and no free
local/backend descriptor EMFILE. Minimum0..2 searches from3 because standard
streams are reserved callback channels.

The process layer searches its own namespace, duplicates the source backend
reference, then publishes the new slot with CLOEXEC clear. Failed backend
allocation does not alter any local slot. The new descriptor shares offsets and
status flags while descriptor flags remain independent. Exit/exec cleanup uses
the existing process lifecycle. `hello --fcntl` provides the hardware test.

## ABI20: fchmod

`cp32_fchmod(fd, mode)` uses operation30 (`CP32_APP_FCHMOD`), descriptor in the
fd field, rwx mode in the argument and no buffer. Success returns0; failure
returns-1 with cp32_errno. Older kernels return ENOSYS, invalid descriptors EBADF,
unsupported mode bits EINVAL, RAM EROFS and failed writes EIO. The FS owner
supplies the mounted writer and resolves the backend descriptor's inode; no
pathname lookup or extra descriptor is needed.

Read-only handles may change metadata under the current trusted model. The
operation does not modify descriptor access rights, position, file contents or
inode identity. Changes remain visible through other references; zero-link
open files are supported until final close. Directory descriptors work too.
Path chmod and fchmod share the same inode update and failure handling.
`hello --fmode` exercises the new API and removes its test artifacts on success.

## ABI21: positional I/O

Operations31/32 (`CP32_APP_PREAD`, `CP32_APP_PWRITE`) use a72-byte
`cp32_app_position_request` containing offset, count and64 data bytes. The
kernel validates the aligned full record before reading fields, then checks
count<=64 and signed32-bit offset/range overflow. Read results are copied only
on positive progress. Error/EOF leaves application output unchanged. The runtime
rejects older kernels with ENOSYS and null nonempty buffers with EFAULT.

Invalid descriptors/access modes return EBADF; positional reads on directories
return EISDIR; invalid ranges EINVAL; file-size overflow EFBIG; RAM writes EROFS.
The mounted file backend preserves position and flags on success and failure.
pwrite explicitly bypasses APPEND without changing the flag. pread returns at
most64 bytes and may be short at zone boundaries; pwrite loops over chunks and
returns prior progress on a later error, retaining cp32_errno. Zero-count calls
still validate the descriptor and access mode. No standard-stream positional
I/O is exposed. `hello --at` provides self-cleaning hardware acceptance.

## ABI22: volume allocation counts

`cp32_fsinfo(struct cp32_app_fsinfo *)` uses operation33 (`CP32_APP_FSINFO`)
and a24-byte output record: zone_size, zones, free_zones, inodes, free_inodes,
name_max (six32-bit unsigned fields). The kernel validates exact record size
and the full span; unaligned output is supported through memcpy. On success the
runtime returns0, otherwise-1 with cp32_errno. Older kernels return ENOSYS;
null runtime buffers return EFAULT; unreadable/corrupt geometry uses normal FS
error mapping. Failed scans do not publish a partial output record.

Counts describe the sole mounted filesystem's allocatable data area. The scanner
ignores reserved bit0 and bitmap padding, converts disk byte order and uses the
validated zone size. Metadata space is excluded from zones; allocated indirect
blocks are included in used zones. No file descriptor is needed. The operation
also works with a full descriptor table or a read-only RAM recovery filesystem.
`df` formats these values; `hello --space` checks allocation and final-close reuse.


## ABI23: rename

`cp32_rename(source, destination)` supports file replacement and directory moves,
including replacement of an empty directory. Both names resolve in the caller's
cwd; shell mv uses the same service. Open replaced regular files survive until
final close. Source directory inode identity, open handles and live cwd paths
survive moves. Cycles and type mismatches are rejected. Open or current-working
destination directories are protected by the existing rmdir policy. The old
`cp32_rename_noreplace` keeps its ABI16 regular-file behavior.

Requests use the existing two bounded path strings and the new opcode34. Older
services return ENOSYS through the runtime. No on-disk format change is needed.
I/O failures can leave partial namespace changes; there is no rollback or
power-loss atomicity. Cwd rebasing commits only when the rename completes.
Hardware acceptance: update applications, run `hello --rename` twice, then
`pwd`, `df`, `ls`. Expect Rename OK/exit0, cwd /, unchanged free counts and no
ra/rb/rt/ru/rv leftovers. Requires three free KiB and three free inodes.

## ABI24: synchronization

`cp32_sync()` (operation35) and `cp32_fsync(fd)` (operation36) have no pointer
payload or size argument. They return0 or -1 with errno. fsync validates an
owned file/directory descriptor before the storage request; device errors map
to EIO. Earlier service versions return ENOSYS. The single root device is
synchronized for either operation; all writes are already synchronous and
verified. Directory handles, duplicates and unlinked-open files are supported.
Use `hello --sync` after updating applications. No on-disk format change.

## ABI25: time and explicit inode timestamps

TIME37 and STIME38 use one aligned 32-bit seconds record. UTIME39 uses a
268-byte record: path[256], atime, mtime, current (0 or 1). Seconds are bounded
to 0..2147483647. The kernel checks the complete application buffer before
FS dispatch; FS obtains wall-clock seconds through CLOCK GET_TIME/SET_TIME.
The application wrappers are cp32_time, cp32_stime and cp32_utime. They return
0 or -1 with cp32_errno; older services return ENOSYS. Successful time reads
publish the output only after a valid CLOCK reply. STIME requires effective
uid0. UTIME follows MINIX2 owner/root or current-time write-permission rules,
updates the V2 atime/mtime/ctime triple synchronously, and preserves inode size,
links, ownership, mode and data. Both disk byte orders are supported.

This does not supply a persistent RTC, automatic timestamps for ordinary file
operations, supplementary groups or full ancestor-directory authorization.
The clock resets each boot; inode timestamps persist on writable SD media.


## ABI26: bounded child-first fork/wait

`cp32_fork()` returns0 in the child and its PID in the parent. The parent is
suspended until child exit. The kernel backs up the complete32KiB fixed-address
image plus the parent's FS record in an MM-owned allocation, runs the child in
the live region, and restores the parent before delivering its fork reply.
Child exec can replace the live image without replacing that backing copy.

Image175 retains up to eight exited child results. The parent can fork again
before collecting earlier results. A third nested fork level and a full per-parent wait table return
-1/EAGAIN before allocation. Waiting frees one result slot; parent exit clears
all remaining results. Retained PIDs are excluded from PID allocation. `cp32_waitpid(pid,status,options)`
accepts a positive child PID or -1, NULL or a valid application status pointer,
and options0 or CP32_WNOHANG. It returns the child PID and stores the low8-bit
exit code shifted left8; wrong/no child gives ECHILD. Invalid options or pointers
do not consume status. Parent exit discards an uncollected result.

File descriptions and offsets are shared, while local descriptors, CLOEXEC,
cwd and umask are copied. Suspended-parent cwd state participates in directory
removal protection and rename rebasing. Closing the child's descriptors does
not close the parent's copies. Child usage accumulates in the actual parent.

This is a bootstrap fork policy, not general concurrent POSIX processes. No
parent progress occurs while the child is alive; signals, terminal job control,
arbitrary-depth trees and fault recovery are not provided. Image177 supports
two nested child-first fork levels; each parent retains its own wait table,
image and filesystem state. Exiting a child discards its uncollected descendant
results; this bootstrap policy does not provide INIT adoption. Image174 hardware validated fork continuation and child-exec restoration.
Image176 hardware validated multiple-result waits. Image177 nested forks passed hardware testing and a cold restart. See [cumulative checks](hardware/pending-tests.md).


## ABI27: process groups and group-selecting waits

`cp32_getpgrp()` returns the active application's group. Bootstrap launches
inherit shell group1. `cp32_setsid()` assigns the caller PID as its new group
and returns that ID; a caller already leading its group fails with EPERM,
matching MINIX2 mm/getset.c. CP32 has no controlling-terminal object to detach;
this does not implement terminal job control, setpgid, signals or a complete
POSIX session service. Groups inherit across fork, are private when a child
changes its group, and persist across exec. Parent-exit reset restores shell
group1 for the next launch.

waitpid(pid>0) and waitpid(-1) retain their existing meanings. waitpid(0) selects
only children in the caller's current group; waitpid(pid<-1) selects -pid.
A child's group is retained with its exit status. Nonmatching groups return
ECHILD without consuming another group's result. INT_MIN is rejected with
EINVAL to avoid negation overflow. WNOHANG remains supported; child-first
execution means the caller resumes only after child exit. PID allocation also
reserves group IDs retained in active or saved parent state and exit records.
Image178's extended hello --tree checks these paths on hardware; acceptance
is pending. The host suite covers group inheritance, leader rejection, wait
filters, parent restoration and actual IPC dispatch/runtime wrappers.


## ABI28: process CPU accounting (image179 PROC-TIMES)

`int cp32_times(struct cp32_app_times *out)` returns0 on success or -1 with
`cp32_errno`. The20-byte record contains unsigned32-bit `user`, `system`,
`children_user`, `children_system` and `uptime`. All are60Hz ticks
(`CP32_CLK_TCK`), wrapping modulo2^32; uptime is boot-relative and independent
of `stime`. Use unsigned subtraction for intervals. This is the bootstrap
record API, not a binary-compatible libc `times(struct tms *)` interface.

MINIX2 FS `do_tims` obtains five values through kernel `SYS_TIMES`. CP32's
trusted FS broker snapshots its actual active child under the scheduler lock;
the request cannot choose another PID. Null/outside/truncated writable-image
buffers return EFAULT; wrong size or nonzero descriptor return EINVAL. No
partial output is written on validation failure. The runtime rejects ABI<28.
Counters are sampled by the existing60Hz clock path. Child totals accrue when
the child exits (MINIX sys_xit behavior), including descendant totals exactly
once; wait only collects status. Fork starts new own/child counters at zero;
exec preserves the process counters. No clock or scheduling policy is changed.

`hello --time` checks CPU work, child totals and unchanged totals after wait.
The current shipped hello drops retired filesystem/allocator/duplicate I/O
checks, reducing text from16382 to4038 bytes including this new test. Historical
host checks live in tests/mm/legacy_hello.c and are excluded from firmware and
updater payloads. Current process/exec/line/identity/env/exit checks remain.
Hardware acceptance of image179 is pending; image178 group tests passed.


## Image180: default application fault termination (ABI28 unchanged)

MINIX2 kernel/exception.c converts user exceptions to signals; MM's default
signal action terminates and forkexit.c puts the signal in the low wait-status
bits. CP32 now implements that default termination slice for the current trusted
application broker. ESP32-S3 EXCCAUSE0/6/9/28/29 map to MINIX SIGILL4, SIGFPE8,
SIGBUS10 and SIGSEGV11. Values are documented in Espressif's ESP32-S3 Xtensa
core.h/corebits.h (locally available ESP-IDF5.5.3 source); no IDF runtime is used.

Both kernel/user exception vectors retain the existing IRQ/SYSCALL/probe paths.
For other exceptions, application text bounds are checked before any stack
write. A fresh aligned stack at the application's known top lets the classifier
run even with a damaged old SP. It accepts only the active broker endpoint,
matching current/selected owner, runnable state, outer exception, EXCM set,
INTLEVEL0 and the supported causes. It resets all call0/loop/SAR state and RFE
enters the ordinary application exit callback. No damaged instruction resumes.
The broker consumes the fault reason, closes descriptors, releases backing image
and process resources, restores a fork parent, and retains the encoded wait
status. Parent/child accounting continues through the existing exit path.

`CP32_WIFSIGNALED`/`CP32_WTERMSIG` distinguish signal death from normal
`CP32_WIFEXITED`/`CP32_WEXITSTATUS`. No core-dump or stopped status is produced.
Foreground shell status is128+signal (SIGILL gives132); normal exit still uses
the low8 bits, including negative input values. Fault diagnostics are in a
removable normal-broker function, first4 faults and every64th thereafter.

This is fault termination, not memory isolation or signal-handler delivery.
Kernel/callback text faults, unsupported causes, fetches outside the application
text slot, nested/double exceptions, and prior arbitrary kernel-memory corruption
are not recovered. Default termination hardware tests: hello --fault twice,
hello --crash (exit132), hello --fault, ps, mm and disk. ABI28 remains compatible,
but hello must be updated to install the commands. Hardware acceptance pending.


### Image181 PROC-YIELD — cooperative application image exchange (ABI29)

[x] Image180 updater hardware acceptance: three fault-recovery runs, foreground
crash status132, time/tree/wait/fork-exec, ps, MM and disk passed. See
`docs/hardware/app-fault-v180-updater-pass.log`. No normal-image or cold-restart
capture was supplied.

Implemented `cp32_yield()` for one first-level parent/child pair. The FS broker
swaps the complete32KiB fixed-address image with its MM backing while both
processes are blocked receiving FS. Only the restored image receives a reply.
Private stacks, heap breaks, cwd/umask, process groups and wait state survive;
inherited open-file descriptions retain shared offsets. No memory protection
is required or introduced. MINIX forkexit.c supplies the live-child wait and
exit semantics; the fixed-address Xtensa image requires cooperative exchange.

A matching live-child WNOHANG returns0 without changing status. Blocking wait
resumes the child and completes after exit. Parent exec retains the parked child.
Parent exit/fatal termination drains that child before completing parent exit;
a nonterminating child can delay this. This is not INIT orphan adoption.
Nested child-first forks remain supported, but only the first-level pair exchanges.
A further parent fork while a live child is parked returns EAGAIN. Yield without
an eligible peer returns0. Timer preemption, multiple independent runnable pairs,
general process trees, MM-owned lifecycle and signals remain pending in item1.

Host tests cover production image/FS state exchange, private memory and heap,
shared offsets, live waits, exact IPC reply ownership/order, parent exit/fault,
cleanup and real hello yield/exit/exec command paths. Hardware is unverified.
New commands: `hello --yield`, `hello --yexit`, `hello --yexec`.

Five clean builds pass: SD, updater, RAM, GPIO-SPI and GPIO-SPI updater. Normal
ELF: _iram_end=0x403743e0, _iram_ext_end=0x4038c3f0,
_runtime_stack_end=0x3fce7580 (2688-byte static margin). Other margins are
20064,3856,3632,21008 bytes respectively. Read-only boot/process invariant
validators use Os to retain SRAM headroom; scheduler policy is unchanged and
heap/stack reservations are unchanged. ELF sections/segments, compiled stack
budgets and fsck(clean) pass. Marker: [TEST PROC-YIELD 181], feature181,
SD/ROOT V181 and UPDATER181. No board or card was flashed.


### Image182 PROC-YTREE — nested cooperative process exchange

[x] Image181 updater hardware acceptance: apps/sync, three --yield runs,
--yexit, --yexec, ps with no application rows, MM and disk all pass. All CORE
checks pass and the SD root is100MiB. Capture:
`docs/hardware/proc-yield-v181-updater-pass.log`. No normal-image or cold-restart
capture was supplied, and the older regression commands were not in this log.

Image182 extends image181's cooperative exchange to the second supported fork
level. A child and grandchild can exchange private images and perform live waits
while the original parent stays blocked. After the grandchild exits, the child
can yield to its own parent. The broker's existing exit/fault/exec routing applies
at either level. PID/group reservation now scans both the parked child and all
suspended ancestors, without following the child's back-pointer in a cycle.

MINIX2 mm/forkexit.c keeps parent-child wait relationships regardless of tree
level. CP32 preserves those relationships using fixed-address image backing,
not address relocation or memory protection. Both participants must be blocked
receiving FS during exchange; only the restored image receives a reply. FS
ownership, private stacks/heap/cwd, shared file descriptions and accounting remain
intact. No allocation or disk I/O occurs in the locked image swap.

The new `hello --ytree` exercises nested private stack/data/heap and cwd/group
state, shared offsets, live WNOHANG, repeated yields and blocking wait, then
returns execution to the original parent and reaps the child. Expected output:
`Yield pair OK`, `Yield tree OK`, `Hello exit=0`. ABI29 is unchanged, but update
hello on SD. Markers: FEATURE/TEST PROC-YTREE182, SD/ROOT V182, UPDATER182.

This remains a bounded two-level tree with one deepest exchanging pair at a
time. A process with a parked live child yields toward that child, not its own
parent. Third-level forks and another fork by a parent with a parked child are
rejected. Parent exit drains descendants; no INIT adoption, timer-driven app
preemption or general independent process images are added. Plan item1 remains
incomplete. Hardware acceptance of182 is pending; no board/card was flashed.


### Image183 PROC-KILL — bounded default signal delivery (ABI30)

[x] Image182 updater hardware acceptance: application update, two --ytree runs,
--yield, ps with no remaining application rows, MM and disk pass. SD100MiB and
all CORE checks pass. Capture: docs/hardware/proc-ytree-v182-updater-pass.log.
The mistyped --ytre command was an ordinary hello invocation, not an acceptance
test. No normal-image/cold-restart capture or other regressions were supplied.

Implemented cp32_kill(pid,signal), positive PIDs only. Signal0 probes live
applications and the caller's retained, unreaped child results. SIGKILL9 and
SIGTERM15 terminate self or a parked direct child, at either supported fork
level. Other signals, nonpositive PIDs and pointer payloads return EINVAL;
missing targets return ESRCH. Terminating ancestors/other processes or probing
kernel/server slots returns EPERM. This is a bounded lifecycle policy, not full
MINIX UID-based signal permission or group selection. Retained exited children
can be probed but not signalled again; wait consumes their record.

MINIX2 mm/signal.c check_sig supplies the signal0, target selection and no-reply
on self-death behavior; forkexit.c supplies signal-status wait cleanup. CP32's
fixed-address parked child is restored solely for cleanup, never replied to or
made runnable. Parent memory/FS ownership are restored before its kill reply.
Self termination uses the existing exit/drain path; a parent with a live child
still drains that child before completing exit. Thus a nonterminating descendant
can delay parent termination, including SIGKILL: this remains a limitation of
the bounded broker, not general asynchronous signal delivery. No memory
protection, pending masks, handlers, signal return, group delivery or blocked-I/O
interruption is added. Item1 remains incomplete.

New hardware command: hello --kill. It probes self, rejects an invalid target,
terminates two parked children with SIGKILL/SIGTERM, tests child self-SIGTERM,
collects each signal wait status, checks ESRCH after reap, and verifies inherited
shared file offsets survive cleanup. Expected: Kill wait OK and Hello exit=0.
ABI30 requires an updated hello on SD. Existing older hello rejects newer ABI;
run the updater's apps command before application tests. No reformat is needed.
Markers: FEATURE/TEST PROC-KILL183, SD/ROOT V183, UPDATER183.
Hardware183 remains unverified. No board/card was flashed.


### Image184 PROC-MASK — SIGTERM masking and pending delivery (ABI31)

[x] Image183 updater hardware acceptance: apps update, two --kill runs, --ytree,
--yield, ps without application rows, MM and disk pass. All CORE checks pass.
Capture: docs/hardware/proc-kill-v183-updater-pass.log. Normal-image and cold
restart, and regressions not shown in this capture, remain unverified.

Added cp32_sigprocmask(how,set,old) and cp32_sigpending(set). BLOCK, UNBLOCK,
SETMASK and null-set inquiry follow MINIX2 mm/signal.c do_sigprocmask and
do_sigpending. Mask values are transferred by value over IPC, so no application
pointer is dereferenced by the broker. Only SIGTERM has a blocked/pending
implementation; its bit is1<<14. SIGKILL's bit1<<8 is accepted but removed from
the mask. Other bits/operations are rejected with EINVAL without mutation.

Blocked SIGTERM to self or a parked direct child records one coalesced pending
bit and returns success. A child remains live/waitable until delivery. UNBLOCK
or SETMASK exposing pending SIGTERM enters normal signal termination cleanup
without replying to the application. SIGKILL remains immediate within the
existing bounded delivery policy. Each private process state owns mask/pending;
fork inherits the mask and clears child pending, image exchange preserves both,
and exec leaves them intact. A fresh foreground launch clears both. Existing
parent-exit descendant draining remains unchanged.

hello --mask tests mask inheritance, SIGKILL exclusion, repeated TERM requests,
live WNOHANG, pending inquiry and unblocking termination. Host tests additionally
cover KILL bypass and clearing pending signals in forked children. Expected: Signal mask OK,
Hello exit=0. Update hello on SD before tests: ABI31 rejects the previous hello.
Markers: FEATURE/TEST PROC-MASK184, SD/ROOT V184, UPDATER184.

This is not general asynchronous signal delivery: handlers, sigreturn,
sigsuspend, group targeting, signals interrupting blocked I/O, independently
scheduled images and timer-driven application preemption remain pending. No
memory protection was added. Plan item1 remains incomplete. Hardware184 is
unverified; no board/card was flashed.


### Image185 PROC-IGNORE — default/ignore SIGTERM disposition (ABI32)

[x] Image184 updater hardware acceptance: two --mask runs, --kill, --ytree,
ps without application rows, MM and disk pass after updating apps. Capture:
docs/hardware/proc-mask-v184-updater-pass.log. Normal-image/cold-restart and
other regressions not present in this capture remain pending.

Added cp32_signal(signal,disposition), returning the previous disposition or -1.
Only SIGTERM and CP32_SIG_DFL/CP32_SIG_IGN are supported; SIGKILL, other signals,
handler addresses and invalid pointer payloads are rejected with EINVAL without
state mutation. This is a bounded disposition API, not POSIX signal handlers.
Following MINIX2 mm/signal.c do_sigaction, setting IGN discards pending TERM;
subsequent TERM is discarded even when masked. Fork inherits the disposition,
private image exchanges preserve it, exec keeps it, and a fresh foreground
launch resets it. SIGKILL remains unblockable/unignorable. Selecting DFL restores
the existing default termination behavior. General handlers, sigreturn, group
signals, asynchronous delivery and blocked-I/O interruption remain pending.

The hardware command hello --ignore creates pending TERM, discards it by setting
IGN, verifies empty pending state, forks, tests ignored self/direct-child TERM
and live WNOHANG, then execs hello --igexec in the child to check preservation.
The parent waits for normal exit and restores DFL. Expected: Signal ignore OK,
Hello exit=0. --igexec is an internal acceptance helper, not a standalone test
from the shell. Install updated hello: ABI32 requires updating the prior hello.
Markers: FEATURE/TEST PROC-IGNORE185, SD/ROOT V185, UPDATER185.

The broker now shares its existing reply path for signal operations instead of
duplicating send/continue blocks. To retain the RAM image margin, only ordinary
TTY character-processing routines (in_process, echo, rawecho, back_over, reprint,
out_process) use Os. Blocking task entry/resumption, hardware transport, IRQ
assembly, heap and stack reservations remain unchanged. This optimization needs
hardware line editing/echo regression alongside the signal test. No memory
protection or kernel functionality was removed. Item1 remains incomplete;
hardware185 is unverified and no board/card was flashed.


### Image186 APP-TEST — one-command hardware acceptance

[x] Image185 updater capture passes apps/sync, two --ignore runs, --mask, --kill,
--ytree, --time, line input, ps with no applications, MM and disk. Capture:
docs/hardware/proc-ignore-v185-updater-pass.log. The entered line was sdf; this
confirms input/echo but not the requested Backspace sequence. No normal-image
or cold-restart capture was supplied.

Added hello --t. The app explicitly requests the launcher suite; the launcher
loads a fresh foreground image for every case, preserving both fork levels.
A fork-based supervisor would consume one of those levels and invalidate tree
tests. Every case gets fresh BSS, runtime/FS/process state and normal cleanup.
The kernel and MM application lifecycle are unchanged. The same open executable
reader is reused with offset-based reads, then closed on success or failure.

The removable cp32_shell_app_tests cases list is the single place to update for
future feature acceptance. Current order: --ignore twice, --mask, --kill,
--ytree, --yield, --yexit, --yexec, --fault, --time, --tree, --wait, --fork-exec,
--exec-fail. Then ps prints, MM alloc/release and disk checks run automatically.
Each case prints [TEST option]; a load error/nonzero exit/service error stops
immediately with [TEST FAIL] and no all-pass marker. Only complete success
prints [TEST ALL PASS] followed by Hello exit=0. Stale hello without the explicit
request reports update hello, rather than pretending its generic greeting was
a test. Redirection is rejected. run /boot/hello --t is also supported.

Interactive --line remains manual; it cannot be a hands-free check. Foreground
--crash is omitted; --fault covers supported child faults without deliberately
terminating the test invocation. --igexec remains an internal --ignore helper.
ABI32 is unchanged; install updated hello before using --t. Markers:
FEATURE/TEST APP-TEST186, SD/ROOT V186, UPDATER186. No board/card was flashed;
hardware acceptance of the aggregate runner is pending.

To retain the existing RAM recovery margin, removable IPC/MM/TTY trace printers
and ordinary TTY transfer/attribute helpers use Os. Trace content/rate limits,
MM and TTY task entry loops, IRQ assembly and hardware transports are unchanged.
Heap and stack reservations are unchanged. These C helpers retain the same
MINIX line-discipline/transfer semantics; host TTY and compiled stack checks
cover them. Include line editing in hardware acceptance. No functionality was
removed. This is test-workflow work; plan item1 is still incomplete.


### Image187 PROC-SIGNALS — independent MINIX signal sets and USB-only suite

[x] Image186 updater hardware acceptance: apps/sync and the complete hello --t
sequence passed, including all14 application cases, ps with no application rows,
MM and disk, ending [TEST ALL PASS] / Hello exit=0. Capture:
docs/hardware/app-test-v186-updater-pass.log. Normal-image/cold restart remain
unverified; this capture does not add manual line-editing acceptance.

hello --t now suppresses LCD output during application tests, process listing,
service checks and the exit summary. Those messages remain on USB. Buffered
normal output is flushed before suppression; normal routing is restored on
success and failure. The display receives only the final [TEST ALL PASS] or
[TEST FAIL], followed by the normal shell prompt. A failed suite never displays
a pass marker. Standalone application/shell output is unchanged.

ABI33 extends explicit kill/default-ignore/mask/pending handling to MINIX2
signals1..15: HUP, INT, QUIT, ILL, TRAP, ABRT, EMT, FPE, KILL, USR1, SEGV, USR2,
PIPE, ALRM and TERM. SIGBUS10 is MINIX2's historical alias of SIGUSR1; the existing
Xtensa unaligned-fault mapping remains unchanged. Each signal has its own bit,
1U<<(signal-1). Independent ignore dispositions discard only that pending bit;
blocked delivery coalesces only that signal. Unblocking delivers the lowest
numbered pending signal first, matching MINIX2 mm/signal.c check_pending.
SIGKILL cannot be blocked or ignored. Fork/exec/private-image/reset behavior
extends to the full set. Out-of-range bits/signals remain EINVAL.

These are default fatal/ignore dispositions through the existing bounded self/
parked-direct-child delivery policy. No core dumps, user handlers, automatic
alarm/pipe/terminal producers, group selection, stop/continue, asynchronous
blocked-I/O interruption or timer-preempted images are claimed. Hardware faults
still force the supported default termination path rather than returning to a
faulting instruction. Plan item1 remains incomplete.

Added hello --signals at the start of --t. It queues USR1 and USR2 independently,
ignores just USR2, checks the remaining pending bit, unblocks USR1 and verifies
its wait status. Host tests cover default/ignore/mask behavior for every signal
1..15. Expected USB: Signals OK.
Update hello for ABI33 before tests. Markers: FEATURE/TEST PROC-SIGNALS187,
SD/ROOT V187, UPDATER187. No board/card was flashed; hardware187 is pending.

To retain RAM-recovery headroom without reducing heap/stacks, ordinary TTY
request handlers do_read/do_write/do_ioctl, handle_events and tty_init use Os.
Task entry/resumption loops and IRQ/hardware transport code remain unchanged.
Host TTY canonical/read/write/ioctl and compiled stack tests cover these helpers;
manual editing remains a hardware regression check.


Image188 TEST-DOTS preserves ABI33 and adds a new LCD row with one dot per
successful suite check (17 currently), then the final result on its own line.
Details remain USB-only. Firmware update alone suffices from image187.
