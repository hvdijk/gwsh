/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018
 *	Harald van Dijk <harald@gigawatt.nl>.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * String functions.
 *
 *	equal(s1, s2)		Return true if strings are equal.
 *	scopy(from, to)		Copy a string.
 *	scopyn(from, to, n)	Like scopy, but checks for overflow.
 *	number(s)		Convert a string of digits to an integer.
 *	is_number(s)		Return true if s is a string of digits.
 */

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#ifdef WITH_LOCALE
#include <wctype.h>
#endif
#include "shell.h"
#include "syntax.h"
#include "error.h"
#include "mylocale.h"
#include "mystring.h"
#include "memalloc.h"
#include "parser.h"
#include "system.h"


char nullstr[1];		/* zero length string */
const char spcstr[] = " ";
const char snlfmt[] = "%s\n";
const char dolatstr[] = { CTLQUOTEMARK, CTLVAR, VSNORMAL, '@', '=',
			  CTLQUOTEMARK, '\0' };
const char qchars[] = { CTLESC, CTLQUOTEMARK, 0 };
const char illnum[] = "Illegal number: %s";
const char homestr[] = "HOME";

/*
 * equal - #defined in mystring.h
 */

/*
 * scopy - #defined in mystring.h
 */


#if 0
/*
 * scopyn - copy a string from "from" to "to", truncating the string
 *		if necessary.  "To" is always nul terminated, even if
 *		truncation is performed.  "Size" is the size of "to".
 */

void
scopyn(const char *from, char *to, int size)
{

	while (--size > 0) {
		if ((*to++ = *from++) == '\0')
			return;
	}
	*to = '\0';
}
#endif


/*
 * prefix -- see if pfx is a prefix of string.
 */

char *
prefix(const char *string, const char *pfx)
{
	while (*pfx) {
		if (*pfx++ != *string++)
			return 0;
	}
	return (char *) string;
}

void badnum(const char *s)
{
	sh_error(illnum, s);
}

/*
 * Convert a string into an integer of type intmax_t.  Alow trailing spaces.
 */
intmax_t atomax(const char *s, const char **end, int base)
{
	char *p;
	intmax_t r;

	errno = 0;
	r = strtoimax(s, &p, base);

	if (errno == ERANGE)
		badnum(s);

	/*
	 * Disallow completely blank strings in non-arithmetic (base != 0)
	 * contexts.
	 */
	if (p == s && base)
		badnum(s);

	while (is_space(*p))
	      p++;

	if (end)
		*end = p;
	else if (*p)
		badnum(s);

	return r;
}

intmax_t atomax10(const char *s)
{
	return atomax(s, NULL, 10);
}

/*
 * Convert a string of digits to an integer, printing an error message on
 * failure.
 */

int
number(const char *s)
{
	intmax_t n = atomax10(s);

	if (n < 0 || n > INT_MAX)
		badnum(s);

	return n;
}



/*
 * Check for a valid number.  This should be elsewhere.
 */

int
is_number(const char *p)
{
	do {
		if (! is_digit(*p))
			return 0;
	} while (*++p != '\0');
	return 1;
}


/*
 * Produce a possibly quoted string suitable as input to the shell.
 * If force is set, the result will definitely be quoted.
 * The return string is allocated on the stack.
 */

const char *
shell_quote(const char *s, int force) {
	const char *p, *q;
	char *r;
#ifdef WITH_LOCALE
	int c;
#else
	char c;
#endif
	// 0: unquoted
	// 1: single-quoted
	// 2: dollar-single-quoted
	int style = force || !*s;
	int bs = 0;

	const char *sqchars = " |&;<>()$`\\\"*?[#~=%";
#define ESCSEQCH "\\\'abefnrtv"
#define ESCCHARS "\\\'\a\b\e\f\n\r\t\v"
	const char *dqchars = ESCSEQCH "\0" ESCCHARS + sizeof ESCSEQCH + 1;

	STARTSTACKSTR(r);

	r = makestrspace(4, r);
	USTPUTC('$', r);
	USTPUTC('\'', r);

	for (p = s; *p; p = q) {
#ifdef WITH_LOCALE
		r = makestrspace((MB_LEN_MAX > 10 ? MB_LEN_MAX : 10) + 2, r);
#else
		r = makestrspace(6, r);
#endif
		c = *p;
		if (strchr(sqchars, c)) {
			style = 1;
			bs |= c == '\\';
		}
		if ((q = strchr(dqchars, c))) {
			c = q[-sizeof ESCSEQCH];
			USTPUTC('\\', r);
			USTPUTC(c, r);
			q = p + 1;
			goto dq;
		}
		q = p;
		GETC(c, q);
#ifdef WITH_LOCALE
		if (c < 0) {
			c = -c;
#else
		if (c < ' ' || c > '~') {
#endif
			r += sprintf(r, "\\%03o", (unsigned char) c);
			goto dq;
		} /* } */
#ifdef WITH_LOCALE
		if (!iswprint(c)) {
			r += sprintf(r, c >= 0x10000 ? "\\U%08x" : "\\u%04x", c);
			goto dq;
		}
		r = mempcpy(r, p, q - p);
#else
		USTPUTC(c, r);
#endif
		continue;

dq:
		if (style == 2)
			continue;
		style = 2;
		sqchars = nullstr;
		dqchars--;

		/* If any backslashes were seen before we committed
		 * to a dollar-quoted string, they have not been
		 * escaped yet. Restart from the beginning. */
		if (bs) {
			q = s;
			r = stackblock() + 2;
		}
	}

	if (style)
		USTPUTC('\'', r);
	USTPUTC(0, r);

	return stackblock() + 2 - style;
}

/*
 * Like strdup but works with the ash stack.
 */

char *
sstrdup(const char *p)
{
	size_t len = strlen(p) + 1;
	return memcpy(stalloc(len), p, len);
}

/*
 * Wrapper around strcmp for qsort/bsearch/...
 */
int
pstrcmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *) a, *(const char *const *) b);
}

/*
 * Find a string is in a sorted array.
 */
const char *const *
findstring(const char *s, const char *const *array, size_t nmemb)
{
	return bsearch(&s, array, nmemb, sizeof(const char *), pstrcmp);
}
