# Pending hardware acceptance — image188 TEST-DOTS

## Current quick procedure (supersedes the older per-image lists below)

On the computer, from src/: make updater, then make flash-updater. On hardware,
run apps and sync. Install normal firmware with make and make flash. Confirm
[TEST TEST-DOTS 188], then type:

```text
hello --t
```

Detailed progress, application output, ps and MM/disk checks go only to USB.
The display starts a new line and adds one dot per successful check (17 for
the current complete suite). The final [TEST ALL PASS] or [TEST FAIL] starts
on the next line, followed by the normal prompt. No detailed logs should appear
on the display. Expect Signals OK in the USB log and the
usual final Hello exit=0 there. --signals is included automatically. Ensure the
normal shell output works after the suite; repeated runs must not leave routing
suppressed. Save USB logs from a normal boot and a full cold restart.

For interactive TTY acceptance, separately run hello --line; type abc, erase c,
type d, then Enter. Expect abd and exit0; also check cursor blink and normal echo.
Repeat hello --t after a full cold restart and save both complete logs. The suite
list can change each image; no need to type the older individual command lists.
No SD reformat is needed. Image185 updater passed its supplied tests:
[log](proc-ignore-v185-updater-pass.log). Image186 aggregate acceptance passed: [log](app-test-v186-updater-pass.log). Image187 signals/output-routing acceptance is pending.

## Earlier acceptance records and detailed regression commands


Image176 hardware acceptance: two consecutive `hello --wait` runs and both
fork tests pass with exit0. The final `ps` shows no application rows and shell
children0/186. SD root and all CORE checks pass. Capture:
[proc-wait-v176-pass.log](proc-wait-v176-pass.log).
The image175 exit107 initialization defect is resolved. The sequence below is
now a repeatable regression checklist; cold-restart repetition and additional
service/exec checks not shown in this log remain pending.

Image177 updater, normal boot and cold-restart captures are saved. Repeated
tree and wait tests passed, followed by fork, ps, mm and disk in both normal
boots; fork-exec passed in the first normal boot only. This confirms nested
continuation and repeatability. Image178 also passed both tree runs, wait, fork,
fork-exec, ps, mm and disk. Capture: [proc-group-v178-pass.log](proc-group-v178-pass.log).
No cold restart was supplied for image178. Image179's updater boot passed apps/sync, two time tests, tree, wait, fork,
fork-exec, ps, mm and disk. Capture: [proc-times-v179-updater-pass.log](proc-times-v179-updater-pass.log).
Normal-image and cold-restart acceptance for179 were not supplied.

## Install firmware and updated applications

Image187 uses ABI33. Updated hello must be installed before application tests;
older hello lacks the suite request and will produce an update-required message. Do not reformat the existing card.
From `src/` on the development computer:

```sh
make updater
make flash-updater
```

On the Cardputer, confirm `[TEST TEST-DOTS 188]` and `[UPDATER 188 enabled]`, then:

```text
apps
sync
```

Save the updater output. Return to the computer and install the normal image:

```sh
make
make flash
```

Confirm `[TEST TEST-DOTS 188]` without the updater-enabled tag, successful SD
root initialization and all `[CORE ...=1]` checks. The root should still report
104857600 bytes if using the existing 100MiB image.

## 0. Startup regression

Cold boot twice with the existing SD card. Each boot must reach `[ROOT V180
backend=sd result=0 capacity=104857600]`, `[LCD clear-done]`, all CORE checks,
and `[TEST TEST-DOTS 188]` before `[TTY user-entry]`. Run `ls` and `disk`;
the prompt, cursor and keyboard must remain responsive. Save both complete
serial logs. No SD reformat is needed. Install image180 hello even if ABI28 applications are already installed.
Image175 hello fails before reaching the kernel; flashing firmware alone is insufficient.

## New: application fault termination

```text
hello --fault
hello --fault
hello --crash
hello --fault
ps
mm
disk
```

Each --fault invocation must print `Fault recovery OK` and `Hello exit=0`.
Four children deliberately execute an illegal instruction, a null load, a null
store and an illegal instruction after setting SP to0. The parent checks
signal-style wait status, private-memory restoration and shared file offsets.
Each child also leaves a file open to exercise exit cleanup. No files are created.

**The expected --crash result is `Hello exit=132`**, followed by the shell prompt.
This is a deliberate foreground SIGILL, not a test failure. The next --fault
must still pass. First-use diagnostics are `[CTX V180 fault ...]`; only the first
four faults and every64th are printed. No `!! CP32 EXCEPTION !!` or freeze is
expected for these tests. Save the complete log if either occurs.

Run the timing/tree/wait/fork regressions below and repeat this sequence after
a cold restart using the normal firmware. The getter and unsupported-fault
rejection paths have host tests; there are no general signal handlers or MMU
isolation. Do not inject faults into kernel code.

## Process CPU accounting regression

```text
hello --time
hello --time
ps
mm
disk
```

Each timing test must print `Times OK (60 Hz)` and `Hello exit=0`. A child
performs CPU work until at least two user ticks are charged, then exits. The
parent checks child accounting and verifies wait does not charge it again.
The child stops with exit125 if uptime advances more than three seconds without
the expected user ticks. Save any nonzero exit and full log; do not treat it as a
pass. Tick values are sampled CPU usage, not milliseconds or Unix time.
Repeat this and the process regressions below after a full power-off/restart.

The retired hello filesystem/allocator demonstrations are no longer installed.
Their APIs and host regression tests remain. There is no SD format change.

## Process groups within nested forks (new in image178)

```text
hello --tree
hello --tree
hello --wait
hello --fork
hello --fork-exec
ps
mm
disk
```

Each tree run must print `Fork tree OK` and `Hello exit=0`. It forks a child
which forks a grandchild, checks each parent's stack variable after restoration,
collects exit9 at each level and checks rejection at the third level. In
image178 each child creates a new group with setsid. Its parent must get
ECHILD from waitpid(0), then collect it using waitpid(-child_pid). Repeating the
command checks release/reuse of both MM backing images and process slots.
The wait and existing fork tests must still pass, and ps must show no remaining
application rows. These commands create no files. Repeat after a cold restart.

Nested backing-image restoration, nested failure rollback, descendant CPU
accounting, separate wait tables, reserved ancestor PIDs and all-ancestor cwd
protection have host tests. Nested Xtensa continuation passed in image177; group-selecting waits passed image178; repeat on this image for regression.

## Multiple waitable children (new)

```text
hello --wait
hello --wait
ps
```

Each invocation must print `Wait children OK` and `Hello exit=0`. The test
creates eight children before waiting, checks EAGAIN on the ninth fork,
selectively collects a middle child, reuses the freed slot, collects all other
children with waitpid(-1), then intentionally leaves one result for parent-exit
cleanup. Running twice checks that cleanup. No test files are created. `ps`
should show no application rows; shell child usage may increase.

After a cold restart, repeat these three commands and both fork tests below.
Save the complete serial output from updater and normal firmware.

## 1. Idle and basic process accounting

Run `ps`, wait about ten seconds, then run `ps` again. Idle/task U counters
should advance; HARDWARE must not collect almost every timer tick. A few busy
rows can remain zero because the timer samples at60Hz. The cursor and keyboard
must stay responsive. PID0 and stopped reserved boot rows are expected.

## 2. Private fork, inherited files and wait

```text
hello --fork
ps
hello --fork-exec
ps
```

Both commands must print `Fork memory/file/wait OK` and `Hello exit=0`.
The exec variant also prints `Fork exec child` from echo. The embedded checks
exercise:

- Different child PID, correct parent PID and fork return values.
- Private global, stack and heap contents restored to the parent.
- Inherited descriptor with a shared offset; child close leaves parent open.
- Private cwd and umask restored after child changes them.
- The separate --tree command checks EAGAIN at the third fork level.
- Wrong-PID wait returning ECHILD without consuming the result.
- Wait status7 for ordinary child exit and status0 after child exec.
- A second wait returning ECHILD.
- Parent code/data/stack restoration after the child replaces its executable.

Repeat the four commands above three times. Exited child/parent application
rows should not remain in `ps`. Any `children U/S` totals below the shell should
persist and not decrease during the session. Very short programs may contribute
zero ticks, so an absent child-total line alone is not a failure.

A nonzero `Hello exit=N` means an assertion failed; retain the number and full
log. These tests do not create or remove files; they read `/readme`.

## 3. Earlier exec and lifecycle regression

```text
hello --pid
hello --pid
hello --exec
hello --exec-fail
ps
mm
sys
disk
ls
sync
```

Fresh launches should use different PIDs. Expect `Exec PID and file OK`,
`Exec failure recovery OK`, normal service replies and a working prompt.
There should be no panic, exception or permanently blocked application row.

## 4. Cold restart

Power-cycle after `sync`, then:

```text
ps
hello --fork
hello --fork-exec
ps
```

Repeat the same success checks. CPU and child totals reset on reboot; that is
expected. Save this second boot log separately.

## What these commands cannot prove

Malformed SYS_FORK authorization, slot exhaustion, forced MM/descriptor
allocation failures, invalid wait pointers and directory-move/rmdir protection
for the suspended parent's cwd have host tests but no manual hardware trigger.
The fork commands exercise the shared kernel clone through the FS broker, not
an MM-originated SYS_FORK message. GPIO-SPI and RAM recovery builds are host/build
checked; they require separate firmware runs for hardware acceptance.

This is bounded child-first fork: the parent remains suspended until child exit,
although its memory is private (unlike vfork). Up to eight exited child results
can remain uncollected; waiting frees one result slot. Parent/child concurrent execution, nesting beyond two fork levels,
terminal job control, general signal delivery, full memory-fault containment and
a general MM process tree remain unimplemented. Image180 only terminates the
recognized faults whose instruction PC lies inside active application text. Do not test programs that need the parent to run while the child
is alive. A child that never exits keeps the parent suspended.

The getter and repeated-setsid EPERM behavior are covered by host/runtime tests;
the tree command exercises successful setsid and group-filtered waits. These
logs do not establish terminal job control or signal support.


## Image181 acceptance: cooperative process exchange

Image180 updater fault tests passed: [capture](app-fault-v180-updater-pass.log).
This does not establish normal/cold acceptance or image181 hardware behavior.
After installing updated apps and the normal firmware as above, run:

```text
hello --yield
hello --yield
hello --yexit
hello --yexec
hello --yield
hello --fault
hello --time
hello --tree
hello --wait
hello --fork-exec
ps
mm
disk
```

Expect `Yield pair OK` and exit0 for each --yield, `Yield exit OK` and exit0
for --yexit, and `Yield exec parent` followed by exit0 for --yexec. Existing
regressions should pass with exit0; ps must have no application rows after
completion, and MM/disk return0. All CORE checks must remain1. Repeat after a
cold restart and save both complete logs. No card reformat is needed.

--yield checks private stack/data/heap, separate cwd/group, shared file offsets,
live WNOHANG, fork rejection with a parked child, repeated exchanges and blocking
wait. --yexit/--yexec check parent termination/replacement while the child lives.
This does not test timer preemption: exchanges are explicitly cooperative.


## Image182 acceptance: nested cooperative exchange

Image181 updater passed its yield/exit/exec and cleanup tests:
[capture](proc-yield-v181-updater-pass.log). Normal and cold captures remain pending.
Install image182 and updated hello using the instructions above, then run:

```text
hello --ytree
hello --ytree
hello --yield
hello --yexit
hello --yexec
hello --fault
hello --time
hello --tree
hello --wait
hello --fork-exec
ps
mm
disk
```

Each --ytree must print `Yield pair OK`, `Yield tree OK` and `Hello exit=0`.
All other tests should pass with exit0. Afterward ps must have no application
rows, and MM/disk return0. Confirm all CORE checks1 and PROC-YTREE182. Repeat
after a full cold restart, saving both logs. No SD reformat is needed.


## Image183 acceptance: default signal delivery

Image182 updater passed nested/ordinary yield and cleanup:
[capture](proc-ytree-v182-updater-pass.log). Install image183 and updated hello
using the instructions above, then install the normal firmware. Run:

```text
hello --kill
hello --kill
hello --ytree
hello --yield
hello --yexit
hello --yexec
hello --fault
hello --time
hello --tree
hello --wait
hello --fork-exec
ps
mm
disk
```

Each --kill must print `Kill wait OK` and `Hello exit=0`. All other tests should
pass with exit0; ps must have no application rows, and MM/disk return0. Confirm
PROC-KILL183 and all CORE checks1. Repeat after a cold restart; save both logs.
The test covers direct child delivery and self-termination, not asynchronous
signals, group delivery, handlers or blocked-I/O interruption.


## Image184 acceptance: pending SIGTERM and masks

Image183 updater passed its kill/yield and cleanup tests:
[capture](proc-kill-v183-updater-pass.log). Install image184, run apps and sync,
then install normal firmware using the instructions above. Run:

```text
hello --mask
hello --mask
hello --kill
hello --ytree
hello --yield
hello --yexit
hello --yexec
hello --fault
hello --time
hello --tree
hello --wait
hello --fork-exec
ps
mm
disk
```

Each --mask must print `Signal mask OK` and `Hello exit=0`. Other regressions
should exit0; ps must contain no application rows and MM/disk must return0.
Confirm PROC-MASK184 and all CORE checks1. Repeat after a full cold restart and
save both complete logs. No SD reformat is needed. Asynchronous delivery and
interruption of blocked I/O are not part of this acceptance.


## Image185 acceptance: ignored SIGTERM and TTY regression

Image184 updater passed mask/kill/tree and cleanup tests:
[capture](proc-mask-v184-updater-pass.log). Install image185, run apps and sync,
then install normal firmware using the instructions above. Run:

```text
hello --ignore
hello --ignore
hello --mask
hello --kill
hello --ytree
hello --yield
hello --yexit
hello --yexec
hello --fault
hello --time
hello --tree
hello --wait
hello --fork-exec
hello --line
ps
mm
disk
```

Each --ignore must print `Signal ignore OK` and `Hello exit=0`. The test launches
its --igexec helper itself; do not run that helper separately. For --line, type
`abc`, erase `c` with Backspace, type `d`, then Enter; expect `abd` and exit0.
Check normal command echo, editing, newline and cursor blinking. These cover the
TTY size optimization as well as the signal feature. Other regressions should
exit0; ps must have no application rows, MM/disk return0, and all CORE checks1.
Confirm PROC-IGNORE185, repeat after a cold restart and save both logs. No SD
reformat is needed. General signal handlers/asynchronous delivery remain absent.

Image188 preserves ABI33 and the image187 hello binary. From an updated187 card,
only make and make flash are needed, then hello --t. Verify dots appear during
the run, not only at completion. Detailed USB output must still show each case.
