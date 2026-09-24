/* Reference layout: minix-2.0.0/src/lib/ansi/memset.c.
 * CP32 freestanding implementation; behavior preserved from kernel/klib.c. */
#include <stddef.h>

void *memset(void *s, int c, size_t n)
{
 	unsigned char *p = s;
 	while (n--) {
 		*p++ = (unsigned char)c;
 	}
 	return s;
}

