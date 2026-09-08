// kernel/klib.h
#ifndef KLIB_H
#define KLIB_H

#include "kernel.h"
#include <stdint.h>

void delay(volatile uint32_t n);
int strcmp(const char *s1, const char *s2);
long strtol(const char *nptr, char **endptr, int base);
void phys_copy(phys_bytes source, phys_bytes destination, phys_bytes bytecount);

#endif
