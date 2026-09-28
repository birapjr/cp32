/* MINIX lib/ansi/strtol.c: signed/unsigned conversion and ERANGE.
 * Use unsigned cutoff arithmetic for ESP32-S3's 32-bit long, without hosted
 * ctype or signed overflow. A prefix is consumed only with a following digit. */
#include <errno.h>
#include <limits.h>
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

CP32_IRAM_EXT static unsigned long string2long(const char *nptr,
    char **endptr, int base, int is_signed, int *negative, int *error)
{
    const char *s = nptr;
    unsigned long value = 0, limit, cutoff, remainder;
    int digit, any = 0, overflow = 0;
    *negative = 0;
    *error = 0;
    if (endptr) *endptr = (char *)nptr;
    if (base != 0 && (base < 2 || base > 36)) {
        *error = EINVAL;
        return 0;
    }
    while (isspace_local((unsigned char)*s)) s++;
    if (*s == '+' || *s == '-') {
        *negative = *s == '-';
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X') &&
        (digit = digit_value((unsigned char)s[2])) >= 0 && digit < 16) {
        base = 16;
        s += 2;
    }
    if (!base) base = *s == '0' ? 8 : 10;
    limit = is_signed ? (unsigned long)LONG_MAX + (unsigned)*negative
                      : (unsigned long)ULONG_MAX;
    cutoff = limit / (unsigned)base;
    remainder = limit % (unsigned)base;
    while ((digit = digit_value((unsigned char)*s)) >= 0 && digit < base) {
        any = 1;
        if (value > cutoff || (value == cutoff && (unsigned)digit > remainder))
            overflow = 1;
        else if (!overflow) value = value * (unsigned)base + (unsigned)digit;
        s++;
    }
    if (any && endptr) *endptr = (char *)s;
    if (overflow) {
        *error = ERANGE;
        return limit;
    }
    return value;
}

CP32_IRAM_EXT long strtol(const char *nptr, char **endptr, int base)
{
    int negative, error;
    unsigned long value = string2long(nptr, endptr, base, 1, &negative, &error);
    if (error) errno = error;
    if (!negative) return (long)value;
    if (value == (unsigned long)LONG_MAX + 1UL) return LONG_MIN;
    return -(long)value;
}

CP32_IRAM_EXT unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    int negative, error;
    unsigned long value = string2long(nptr, endptr, base, 0, &negative, &error);
    if (error) { errno = error; return value; }
    return negative ? (0UL - value) & (unsigned long)ULONG_MAX : value;
}
