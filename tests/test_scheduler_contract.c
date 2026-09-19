#include <stdio.h>

static int next_queue(int selected, int queues)
{
  return (selected + 1) % queues;
}

static int check(int value) { return value ? 0 : 1; }

int main(void)
{
  int failures = 0;
  failures += check(next_queue(0, 3) == 1);
  failures += check(next_queue(1, 3) == 2);
  failures += check(next_queue(2, 3) == 0);
  failures += check(next_queue(0, 1) == 0);
  return failures != 0;
}
