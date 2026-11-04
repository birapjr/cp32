# Pending hardware acceptance — image176 WAIT-INIT

Image176 hardware acceptance: two consecutive `hello --wait` runs and both
fork tests pass with exit0. The final `ps` shows no application rows and shell
children0/186. SD root and all CORE checks pass. Capture:
[proc-wait-v176-pass.log](proc-wait-v176-pass.log).
The image175 exit107 initialization defect is resolved. The sequence below is
now a repeatable regression checklist; cold-restart repetition and additional
service/exec checks not shown in this log remain pending.

## Install firmware and updated applications

Image176 retains application ABI26. The new hello --wait test must be installed on
the SD card; old hello binaries reject ABI26. Do not reformat the existing card.
From `src/` on the development computer:

```sh
make updater
make flash-updater
```

On the Cardputer, confirm `[TEST WAIT-INIT 176]` and `[UPDATER 176 enabled]`, then:

```text
apps
sync
```

Save the updater output. Return to the computer and install the normal image:

```sh
make
make flash
```

Confirm `[TEST WAIT-INIT 176]` without the updater-enabled tag, successful SD
root initialization and all `[CORE ...=1]` checks. The root should still report
104857600 bytes if using the existing 100MiB image.

## 0. Startup regression

Cold boot twice with the existing SD card. Each boot must reach `[ROOT V176
backend=sd result=0 capacity=104857600]`, `[LCD clear-done]`, all CORE checks,
and `[TEST WAIT-INIT 176]` before `[TTY user-entry]`. Run `ls` and `disk`;
the prompt, cursor and keyboard must remain responsive. Save both complete
serial logs. No SD reformat is needed. The image176 hello is required even if ABI26 applications are already installed.
Image175 hello fails before reaching the kernel; flashing firmware alone is insufficient.

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
- Nested fork rejection with EAGAIN.
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
can remain uncollected; waiting frees one result slot. Parent/child concurrent execution, nested forks,
process-group waits, signals, fault recovery and a general MM process tree remain
unimplemented. Do not test programs that need the parent to run while the child
is alive. A child that never exits keeps the parent suspended.
