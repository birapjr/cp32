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

#if (CHIP == ESP32_S3)

extern char _heap_start[];
extern char _stack_bottom[];

#define CLICK_SHIFT 12
#define CLICK_SIZE   (1u << CLICK_SHIFT)

struct memory mem[3];
phys_clicks tot_mem_size;

static phys_clicks bytes_to_clicks(phys_bytes bytes)
{
	return (phys_clicks)((bytes + CLICK_SIZE - 1u) >> CLICK_SHIFT);
}

/*=========================================================================*
 *				mem_init				   *
 *=========================================================================*/
PUBLIC void mem_init()
{
	phys_bytes usable_bytes = (phys_bytes)(_stack_bottom - _heap_start);

	/* The ESP32-S3 build uses the linker script to carve one contiguous
	 * DRAM region for the kernel heap and stack.
	 */
	mem[0].base = 0;
	mem[0].size = 0;
	mem[1].base = (phys_clicks)((phys_bytes)_heap_start >> CLICK_SHIFT);
	mem[1].size = bytes_to_clicks(usable_bytes);
	mem[2].base = 0;
	mem[2].size = 0;

	tot_mem_size = mem[0].size + mem[1].size + mem[2].size;
}
#endif /* (CHIP == ESP32_S3) */

/*=========================================================================*
 *				env_parse				   *
 *=========================================================================*/
PUBLIC int env_parse(env, fmt, field, param, min, max)
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
		newpar = strtol(val, &end, radix);

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
}

#if DEBUG
/*=========================================================================*
 *				bad_assertion				   *
 *=========================================================================*/
PUBLIC void bad_assertion(file, line, what)
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
PUBLIC void bad_compare(file, line, lhs, what, rhs)
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
