/* Reference layout: minix-2.0.0/src/lib/ansi/strcmp.c.
 * CP32 freestanding implementation; behavior preserved from kernel/klib.c. */

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}
	return (unsigned char)*s1 - (unsigned char)*s2;
}

