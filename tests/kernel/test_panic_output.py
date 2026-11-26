"""Exercise the kernel's fatal USB path with only the final halt substituted."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tests"))
from test_idle_handoff import extract_function

panic = extract_function(root / "src/kernel/main.c", "panic")
halt = 'for (;;) __asm__ volatile("nop");'
assert halt in panic
harness = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define PUBLIC
#define NO_NUM 0x8000
static jmp_buf halted;
static char output[256];
static int locks;
static int lock_save(void) { return locks++; }
static void usbj_print(const char *s) {
  assert(locks && strlen(output) + strlen(s) < sizeof(output));
  strcat(output, s);
}
static void usbj_print_u32(unsigned n) {
  char digits[16];
  snprintf(digits, sizeof(digits), "%u", n);
  usbj_print(digits);
}
'''
checks = r'''
#include <stdlib.h>
int main(int argc, char **argv) {
  assert(argc == 3);
  if (!setjmp(halted)) panic(strcmp(argv[1], "NULL") ? argv[1] : NULL, atoi(argv[2]));
  size_t emitted = strlen(output);
  assert(emitted && locks == 1);
  if (!setjmp(halted)) panic("secondary", 1);
  assert(emitted == strlen(output) && locks == 2);
  fputs(output, stdout);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "panic.c").write_text(harness + panic.replace(halt, "longjmp(halted, 1);") + checks)
    exe = str(path / "panic")
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", str(path / "panic.c"), "-o", exe], check=True)
    for reason, number, suffix in [
        ("MM app publish", 32768, " reason=MM app publish"),
        ("launch", -2147483648, " reason=launch n=-2147483648"),
        ("", 0, " n=0"), ("NULL", 32768, ""),
        ("z" * 60, 22, " reason=" + "z" * 48 + " n=22")
    ]:
        actual = subprocess.check_output([exe, reason, str(number)])
        assert actual == ("\r\n[PANIC V206" + suffix + "]\r\n").encode(), actual
print("panic USB: first reason, bounded length, signed number and terminal path pass")
