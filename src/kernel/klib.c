#include <stdint.h>

void *memcpy(void *dst, const void *src, unsigned int n)
{
	unsigned char *d = (unsigned char *) dst;
	const unsigned char *s = (const unsigned char *) src;
	while (n--) *d++ = *s++;
	return dst;
}

char *strcpy(char *dst, const char *src)
{
	char *ret = dst;
	while ((*dst++ = *src++) != '\0') { }
	return ret;
}

/* ── delay ────────────────────────────────────────────────────────────────────
 * Busy-wait loop calibrated for 240 MHz with -O0.
 * Each iteration is ~7 CPU cycles, so delay(200000) ≈ 5.8 ms. */
void delay(volatile uint32_t n) { while (n--); }

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}
	return (unsigned char)*s1 - (unsigned char)*s2;
}

static int isspace_local(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int digit_value(int c)
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
