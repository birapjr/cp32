/* Reference layout: minix-2.0.0/src/lib/ansi/strtol.c.
 * CP32 bounded bring-up implementation, not full ANSI range/error handling. */
#if defined(__XTENSA__)
#define CP32_IRAM_EXT __attribute__((section(".iram_ext.text")))
#else
#define CP32_IRAM_EXT
#endif

CP32_IRAM_EXT static int isspace_local(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

CP32_IRAM_EXT static int digit_value(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'z') return c - 'a' + 10;
	if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
	return -1;
}

long strtol(const char *nptr, char **endptr, int base)
{
	const char *s = nptr;
	long sign = 1;
	long acc = 0;
	int d;

	while (isspace_local((unsigned char)*s)) s++;
	if (*s == '+') {
		s++;
	} else if (*s == '-') {
		sign = -1;
		s++;
	}

	if (base == 0) {
		if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
			base = 16;
			s += 2;
		} else if (s[0] == '0') {
			base = 8;
			s++;
		} else {
			base = 10;
		}
	} else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
	}

	while ((d = digit_value((unsigned char)*s)) >= 0 && d < base) {
		acc = acc * base + d;
		s++;
	}

	if (endptr) *endptr = (char *)s;
	return sign * acc;
}
