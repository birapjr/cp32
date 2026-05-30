// kernel/klib.h
#ifndef KLIB_H
#define KLIB_H

#include <stdint.h>

void delay(volatile uint32_t n);
int strcmp(const char *s1, const char *s2);
long strtol(const char *nptr, char **endptr, int base);

#endif
