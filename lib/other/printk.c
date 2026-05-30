/*	printk() - kernel printf()			Author: Kees J. Bot
 *								15 Jan 1994
 *
 * CP32 adaptation: formatted output is preserved, but the final character
 * sink is usbj_print() instead of a low-level putk() implementation.
 */

#include "kernel.h"
#include <stdarg.h>
#include <limits.h>

#define nil 0
#define isdigit(c)	((unsigned) ((c) - '0') <  (unsigned) 10)

static void putk(int c)
{
	char ch[2];

	ch[0] = (char)c;
	ch[1] = '\0';
	usbj_print(ch);
}

void printk(const char *fmt, ...)
{
	int c;
	enum { LEFT, RIGHT } adjust;
	enum { LONG, INT } intsize;
	int fill;
	int width, max, len, base;
	static char X2C_tab[]= "0123456789ABCDEF";
	static char x2c_tab[]= "0123456789abcdef";
	char *x2c;
	char *p;
	long i;
	unsigned long u;
	char temp[8 * sizeof(long) / 3 + 2];

	va_list argp;

	va_start(argp, fmt);

	while ((c= *fmt++) != 0) {
		if (c != '%') {
			putk(c);
			continue;
		}

		c= *fmt++;
		adjust= RIGHT;
		if (c == '-') {
			adjust= LEFT;
			c= *fmt++;
		}

		fill= ' ';
		if (c == '0') {
			fill= '0';
			c= *fmt++;
		}

		width= 0;
		if (c == '*') {
			width= va_arg(argp, int);
			c= *fmt++;
		} else if (isdigit(c)) {
			do {
				width= width * 10 + (c - '0');
			} while (isdigit(c= *fmt++));
		}

		max= INT_MAX;
		if (c == '.') {
			if ((c= *fmt++) == '*') {
				max= va_arg(argp, int);
				c= *fmt++;
			} else if (isdigit(c)) {
				max= 0;
				do {
					max= max * 10 + (c - '0');
				} while (isdigit(c= *fmt++));
			}
		}

		x2c= x2c_tab;
		i= 0;
		base= 10;
		intsize= INT;
		if (c == 'l' || c == 'L') {
			intsize= LONG;
			c= *fmt++;
		}
		if (c == 0) break;

		switch (c) {
		case 'D':
			intsize= LONG;
		case 'd':
			i= intsize == LONG ? va_arg(argp, long)
						: va_arg(argp, int);
			u= i < 0 ? -i : i;
			goto int2ascii;

		case 'O':
			intsize= LONG;
		case 'o':
			base= 010;
			goto getint;

		case 'X':
			x2c= X2C_tab;
		case 'x':
			base= 0x10;
			goto getint;

		case 'U':
			intsize= LONG;
		case 'u':
		getint:
			u= intsize == LONG ? va_arg(argp, unsigned long)
						: va_arg(argp, unsigned int);
		int2ascii:
			p= temp + sizeof(temp)-1;
			*p= 0;
			do {
				*--p= x2c[u % base];
			} while ((u /= base) > 0);
			goto string_length;

		case 'c':
			p= temp;
			*p= va_arg(argp, int);
			len= 1;
			goto string_print;

		case '%':
			p= temp;
			*p= '%';
			len= 1;
			goto string_print;

		case 's':
			p= va_arg(argp, char *);

		string_length:
			for (len= 0; p[len] != 0 && len < max; len++) {}

		string_print:
			width -= len;
			if (i < 0) width--;
			if (fill == '0' && i < 0) putk('-');
			if (adjust == RIGHT) {
				while (width > 0) { putk(fill); width--; }
			}
			if (fill == ' ' && i < 0) putk('-');
			while (len > 0) { putk((unsigned char) *p++); len--; }
			while (width > 0) { putk(fill); width--; }
			break;

		default:
			putk('%');
			putk(c);
		}
	}

	putk(0);
	va_end(argp);
}
