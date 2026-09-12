#include <stdio.h>
#include "../src/kernel/irq_frame.h"

static int check(int value) { return value ? 0 : 1; }

int main(void)
{
  int failures = 0;
  failures += check(cp32_irq_handoff_allowed(1, 0, 1, 1, 1, 1));
  failures += check(!cp32_irq_handoff_allowed(0, 0, 1, 1, 1, 1));
  failures += check(!cp32_irq_handoff_allowed(1, 1, 1, 1, 1, 1));
  failures += check(!cp32_irq_handoff_allowed(1, 0, 1, 1, 0, 1));
  failures += check(!cp32_irq_handoff_allowed(1, 0, 1, 1, 1, 0));
  return failures != 0;
}
