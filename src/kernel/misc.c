/* This file contains a collection of miscellaneous procedures:
 *	mem_init:	initialize memory tables.  Some memory is reported
 *			by the BIOS, some is guesstimated and checked later
 *	env_parse	parse environment variable.
 *	bad_assertion	for debugging
 *	bad_compare	for debugging
 */

#include "kernel.h"
#include "assert.h"
#include <stdlib.h>
#include <minix/com.h>

extern char _heap_start[];
extern char _heap_end[];

struct memory mem[3];
phys_clicks tot_mem_size;

/*=========================================================================*
 *				mem_init				   *
 *=========================================================================*/
CP32_IRAM_EXT PUBLIC void mem_init()
{
	phys_bytes start = (phys_bytes)_heap_start;
	phys_bytes end = (phys_bytes)_heap_end;
	phys_clicks first = (start >> CLICK_SHIFT) +
	                    ((start & (CLICK_SIZE - 1u)) != 0);
	phys_clicks limit = end >> CLICK_SHIFT;

	/* MINIX gives MM whole free clicks. The ESP32-S3 linker heap is only
	 * 8-byte aligned: round its start UP and end DOWN, never include BSS
	 * or stack bytes. Image 120 rounded down into the allocation table.
	 * Divide before rounding to avoid overflow near the address-space end.
	 */
	mem[0].base = 0;
	mem[0].size = 0;
	mem[1].base = first;
	mem[1].size = end > start && limit > first ? limit - first : 0;
	mem[2].base = 0;
	mem[2].size = 0;

	tot_mem_size = mem[0].size + mem[1].size + mem[2].size;
}

/*=========================================================================*
 *				env_parse				   *
 *=========================================================================*/
CP32_IRAM_EXT PUBLIC int env_parse(env, fmt, field, param, min, max)
char *env;		/* environment variable to inspect */
char *fmt;		/* template to parse it with */
int field;		/* field number of value to return */
long *param;		/* address of parameter to get */
long min, max;		/* minimum and maximum values for the parameter */
{
/* Parse an environment variable setting, something like "DPETH0=300:3".
 * Panic if the parsing fails.  Return EP_UNSET if the environment variable
 * is not set, EP_OFF if it is set to "off", EP_ON if set to "on" or a
 * field is left blank, or EP_SET if a field is given (return value through
 * *param).  Commas and colons may be used in the environment and format
 * string, fields in the environment string may be empty, and punctuation
 * may be missing to skip fields.  The format string contains characters
 * 'd', 'o', 'x' and 'c' to indicate that 10, 8, 16, or 0 is used as the
 * last argument to strtol.
 */

  char *val, *end;
  long newpar;
  int i = 0, radix, r;

  if ((val = k_getenv(env)) == NIL_PTR) return(EP_UNSET);
  if (strcmp(val, "off") == 0) return(EP_OFF);
  if (strcmp(val, "on") == 0) return(EP_ON);

  r = EP_ON;
  for (;;) {
	while (*val == ' ') val++;

	if (*val == 0) return(r);	/* the proper exit point */

	if (*fmt == 0) break;		/* too many values */

	if (*val == ',' || *val == ':') {
		/* Time to go to the next field. */
		if (*fmt == ',' || *fmt == ':') i++;
		if (*fmt++ == *val) val++;
	} else {
		/* Environment contains a value, get it. */
		switch (*fmt) {
		case 'd':	radix =   10;	break;
		case 'o':	radix =  010;	break;
		case 'x':	radix = 0x10;	break;
		case 'c':	radix =    0;	break;
		default:	goto badenv;
		}
		errno = 0;
		newpar = strtol(val, &end, radix);
		/* Library errno is positive; kernel error constants are negative.
		 * Reject any conversion error without relying on that sign. */
		if (errno) break;

		if (end == val) break;	/* not a number */
		val = end;

		if (i == field) {
			/* The field requested. */
			if (newpar < min || newpar > max) break;
			*param = newpar;
			r = EP_SET;
		}
	}
  }
badenv:
  printf("Bad environment setting: '%s = %s'\n", env, k_getenv(env));
  panic("", NO_NUM);
  /*NOTREACHED*/
  return EP_UNSET;
}

#if DEBUG
/*=========================================================================*
 *				bad_assertion				   *
 *=========================================================================*/
CP32_IRAM_EXT PUBLIC void bad_assertion(file, line, what)
char *file;
int line;
char *what;
{
  printf("panic at %s(%d): assertion \"%s\" failed\n", file, line, what);
  panic(NULL, NO_NUM);
}

/*=========================================================================*
 *				bad_compare				   *
 *=========================================================================*/
CP32_IRAM_EXT PUBLIC void bad_compare(file, line, lhs, what, rhs)
char *file;
int line;
int lhs;
char *what;
int rhs;
{
  printf("panic at %s(%d): compare (%d) %s (%d) failed\n",
	file, line, lhs, what, rhs);
  panic(NULL, NO_NUM);
}
#endif /* DEBUG */
