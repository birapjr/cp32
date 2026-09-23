/* Reference layout: minix-2.0.0/src/lib/ansi/memcpy.c.
 * CP32 freestanding implementation; behavior preserved from kernel/klib.c. */

void *memcpy(void *dst, const void *src, unsigned int n)
{
 	unsigned char *d = (unsigned char *) dst;
 	const unsigned char *s = (const unsigned char *) src;
 	while (n--) *d++ = *s++;
 	return dst;
}

