/* Reference layout: minix-2.0.0/src/lib/ansi/strcpy.c.
 * CP32 freestanding implementation; behavior preserved from kernel/klib.c. */

char *strcpy(char *dst, const char *src)

{
	char *ret = dst;
	while ((*dst++ = *src++) != '\0') { }
	return ret;
}

