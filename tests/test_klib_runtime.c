#include <stdio.h>
#include "../src/kernel/klib.c"
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); failures++; } } while (0)
int main(void)
{
  char dst[16], copy[16], *end;
  memset(dst, 'x', sizeof(dst)); CHECK(dst[0] == 'x' && dst[15] == 'x');
  strcpy(dst, "cp32"); CHECK(strcmp(dst, "cp32") == 0);
  memcpy(copy, dst, 5); CHECK(strcmp(copy, "cp32") == 0);
  CHECK(strcmp("abc", "abd") < 0); CHECK(strcmp("abd", "abc") > 0);
  CHECK(strcmp("same", "same") == 0);
  CHECK(strtol("42", &end, 10) == 42 && *end == '\0');
  CHECK(strtol(" -17x", &end, 10) == -17 && *end == 'x');
  CHECK(strtol("0x2a", &end, 0) == 42 && *end == '\0');
  CHECK(strtol("077", &end, 0) == 63 && *end == '\0');
  CHECK(strtol("0Xff", &end, 16) == 255 && *end == '\0');
  CHECK(strtol("z", &end, 10) == 0 && *end == 'z');
  return failures != 0;
}
