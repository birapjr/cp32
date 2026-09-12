#include <stdio.h>

#define SENDING   004
#define RECEIVING 010

static int check(int value) { return value ? 0 : 1; }

static int complete_receive(int flags, int frame_valid)
{
  if ((flags & RECEIVING) == 0 || !frame_valid) return -1;
  return flags & ~(SENDING | RECEIVING);
}

static int complete_send(int flags, int frame_valid)
{
  if ((flags & SENDING) == 0 || !frame_valid) return -1;
  return flags & ~(SENDING | RECEIVING);
}

static int bounded_queue_walk(int links, int limit)
{
  int hops;
  for (hops = 0; hops < links; ++hops)
    if (hops >= limit) return -1;
  return hops;
}

static int valid_endpoint(int endpoint, int first, int last)
{
  return endpoint >= first && endpoint <= last;
}

int main(void)
{
  int failures = 0;
  failures += check(complete_receive(RECEIVING, 1) == 0);
  failures += check(complete_receive(SENDING | RECEIVING, 1) == 0);
  failures += check(complete_receive(0, 1) < 0);
  failures += check(complete_receive(RECEIVING, 0) < 0);
  failures += check(complete_send(SENDING, 1) == 0);
  failures += check(complete_send(SENDING | RECEIVING, 1) == 0);
  failures += check(complete_send(0, 1) < 0);
  failures += check(complete_send(SENDING, 0) < 0);
  failures += check(bounded_queue_walk(3, 4) == 3);
  failures += check(bounded_queue_walk(5, 4) < 0);
  failures += check(valid_endpoint(3, 0, 7));
  failures += check(!valid_endpoint(-1, 0, 7));
  failures += check(!valid_endpoint(8, 0, 7));
  return failures != 0;
}
