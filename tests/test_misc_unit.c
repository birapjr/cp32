#include <stdio.h>
#include <setjmp.h>
#define main cp32_kernel_main

static const char *env_name;
static char *env_value;
static jmp_buf panic_jmp;

void printk(const char *fmt, ...)
{
  (void)fmt;
}

char *k_getenv(char *name)
{
  if (env_name != 0 && name != 0 && name[0] == env_name[0]) return env_value;
  return (char *)0;
}

void panic(const char *what, int num)
{
  (void)what; (void)num;
  longjmp(panic_jmp, 1);
}

char _heap_start[0x100];
char _stack_bottom[0x300];

#include "../src/kernel/misc.c"
#undef main

static int check(int condition) { return condition ? 0 : 1; }

int main(void)
{
  long value;
  int failures = 0;
  env_name = "TEST";

  env_value = "off";
  failures += check(env_parse("TEST", "d", 0, &value, 0, 99) == EP_OFF);
  env_value = "on";
  failures += check(env_parse("TEST", "d", 0, &value, 0, 99) == EP_ON);
  env_value = "12:34:0x2a";
  failures += check(env_parse("TEST", "d:d:x", 1, &value, 0, 99) == EP_SET && value == 34);
  env_value = "12::42";
  failures += check(env_parse("TEST", "d::d", 2, &value, 0, 99) == EP_SET && value == 42);
  env_value = "077";
  failures += check(env_parse("TEST", "o", 0, &value, 0, 99) == EP_SET && value == 63);
  env_value = "0x2a";
  failures += check(env_parse("TEST", "c", 0, &value, 0, 99) == EP_SET && value == 42);
  env_name = "NONE"; env_value = (char *)0;
  failures += check(env_parse("NONE", "d", 0, &value, 0, 99) == EP_UNSET);
  if (setjmp(panic_jmp) == 0) {
    env_name = "TEST"; env_value = "999";
    (void)env_parse("TEST", "d", 0, &value, 0, 10);
    failures++;
  }
  return failures != 0;
}
