#!/usr/bin/env python3
"""Run the production idle/scheduler C bodies with modeled context transfer."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


def extract_function(path, name):
    source = path.read_text()
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", source)
    if match is None:
        raise RuntimeError(f"cannot find definition of {name} in {path}")
    start = source.rfind("\n", 0, match.start()) + 1
    opening = source.index("{", match.start())
    # Ignore braces inside comments and string/character literals.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'
                        r"'(?:\\.|[^'\\])*'|[{}]", re.S)
    depth = 0
    for token in tokens.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise RuntimeError(f"unterminated definition of {name}")


HARNESS = r'''
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define NIL_PROC ((struct proc *)0)
enum { IDLE = -7, FS_PROC_NR = 1, LOW_USER = 2 };
struct proc {
    int p_nr, p_flags;
    struct { uintptr_t pc, sp; } p_reg;
};
struct proc *proc_ptr, *current_proc;
volatile struct proc *cp32_last_selected_fs;
volatile int cp32_context_restore_gate;
volatile int k_reenter;
volatile uint32_t cp32_timer_irq_ticks;
static jmp_buf exit_idle;
static unsigned delays, restores;
static uintptr_t running_stack;
static struct proc *restore_owner, *restore_frame;

void unlock(void) {}
void status_line(const char *s, int state) { (void)s; (void)state; }
void wdt_feed_all(void) {}
void usbj_print(const char *s) { (void)s; }
void usbj_print_u32(uint32_t value) { (void)value; }
void delay(unsigned count)
{
    (void)count;
    /* Let one complete iteration run, including the former restore bypass. */
    if (++delays == 2) longjmp(exit_idle, 1);
}
void cp32_enter_initial_user(struct proc *frame)
{
    ++restores;
    restore_owner = proc_ptr;
    restore_frame = frame;
    running_stack = frame->p_reg.sp;
}

@IDLE_FUNCTION@

@SWITCH_FUNCTION@

static int idle_keeps_owner(int owner_number, int paused)
{
    struct proc owner = { owner_number, 0, { 0x40378000, 0x3FCD7000 } };
    struct proc fs = { FS_PROC_NR, 0, { 0x40378410, 0x3FCD6D50 } };
    if (paused) {
        /* A previous IRQ may have suspended FS inside a nested C call. */
        fs.p_reg.pc = 0x40371B76;
        fs.p_reg.sp = 0x3FCD6D20;
    }
    proc_ptr = current_proc = &owner;
    cp32_last_selected_fs = &fs; /* Retained from an earlier IRQ selection. */
    cp32_context_restore_gate = 1;
    k_reenter = 0;
    delays = restores = 0;
    restore_owner = restore_frame = NIL_PROC;
    running_stack = owner.p_reg.sp;
    if (setjmp(exit_idle) == 0) kernel_idle_loop();
    if (restores != 0 || proc_ptr != &owner || current_proc != &owner ||
        running_stack != owner.p_reg.sp) {
        fprintf(stderr, "idle handoff regression: owner=%d restores=%u "
                "restore owner=%d target=%d stack=%lx\n", owner_number, restores,
                restore_owner ? restore_owner->p_nr : 999,
                restore_frame ? restore_frame->p_nr : 999,
                (unsigned long)running_stack);
        return 1;
    }
    return 0;
}

static int selection_does_not_restore(void)
{
    struct proc owner = { LOW_USER, 0, { 0x40378000, 0x3FCD7000 } };
    struct proc fs = { FS_PROC_NR, 0, { 0x40378410, 0x3FCD6D50 } };
    proc_ptr = &fs; /* pick_proc() has published its choice. */
    current_proc = &owner;
    cp32_context_restore_gate = 1;
    k_reenter = 0;
    restores = 0;
    running_stack = owner.p_reg.sp;
    switch_to(&fs);
    if (current_proc != &fs || restores != 0 || running_stack != owner.p_reg.sp) {
        fprintf(stderr, "scheduler handoff regression: restores=%u stack=%lx\n",
                restores, (unsigned long)running_stack);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += idle_keeps_owner(LOW_USER, 0);
    failures += idle_keeps_owner(LOW_USER, 1);
    failures += idle_keeps_owner(IDLE, 0);
    failures += idle_keeps_owner(IDLE, 1);
    failures += selection_does_not_restore();
    return failures != 0;
}
'''


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
    source = HARNESS.replace("@IDLE_FUNCTION@", extract_function(
        root / "src/kernel/main.c", "kernel_idle_loop"))
    source = source.replace("@SWITCH_FUNCTION@", extract_function(
        root / "src/kernel/proc.c", "switch_to"))
    with tempfile.TemporaryDirectory(prefix="cp32-idle-handoff-") as folder:
        directory = Path(folder)
        path = directory / "test_idle_handoff.c"
        binary = directory / "test_idle_handoff"
        path.write_text(source)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        str(path), "-o", str(binary)], check=True)
        return subprocess.run([str(binary)]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
