/*-
 * Copyright (c) 2018-2020
 *	Harald van Dijk <harald@gigawatt.nl>.  All rights reserved.
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

#include "config.h"

#include "mylocale.h"

#include "parser.h"

#ifdef WITH_LOCALE
char *
mbcget(const char *p, size_t len, int *c, int ctlesc)
{
	const char *pc = p + (ctlesc && *p == (char)CTLESC), *q = pc;
	int ch = (signed char) *q;
	if (ch >= 0)
		q++;
	else {
		wchar_t wc;
		mbstate_t mbs = { 0 };
		for (;;) {
			switch (mbrtowc(&wc, q++, 1, &mbs)) {
			case (size_t) -2:
				if (q - p < len) {
					q += ctlesc && *q == (char)CTLESC;
					continue;
				}
				/* fall through */
			case (size_t) -1:
				q = pc;
				ch = -(unsigned char) *q++;
				break;
			default:
				ch = wc;
				break;
			}
			break;
		}
	}
	if (c)
		*c = ch;
	return (char *) q;
}

size_t
mbccnt(const char *p)
{
	size_t count = 0;
	while (*p) {
		p = mbcget(p, -1, NULL, 0);
		count++;
	}
	return count;
}
#endif
