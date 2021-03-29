/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2021
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

#include "config.h"

#include "shell.h"
#include "syntax.h"

/* character classification table */
const unsigned char is_type[256] = {
	['\t'] = ISSPACE,
	['\n'] = ISSPACE,
	['\v'] = ISSPACE,
	['\f'] = ISSPACE,
	['\r'] = ISSPACE,
	[' '] = ISSPACE,
	['!'] = ISSPECLDOL | ISSPECLVAR,
	['#'] = ISSPECLDOL | ISSPECLVAR,
	['$'] = ISSPECLDOL | ISSPECLVAR,
	['('] = ISSPECLDOL,
	['*'] = ISSPECLDOL | ISSPECLVAR,
	['-'] = ISSPECLDOL | ISSPECLVAR,
	['0'] = ISODIGIT | ISXDIGIT,
	['1'] = ISODIGIT | ISXDIGIT,
	['2'] = ISODIGIT | ISXDIGIT,
	['3'] = ISODIGIT | ISXDIGIT,
	['4'] = ISODIGIT | ISXDIGIT,
	['5'] = ISODIGIT | ISXDIGIT,
	['6'] = ISODIGIT | ISXDIGIT,
	['7'] = ISODIGIT | ISXDIGIT,
	['8'] = ISXDIGIT,
	['9'] = ISXDIGIT,
	['?'] = ISSPECLDOL | ISSPECLVAR,
	['@'] = ISSPECLDOL | ISSPECLVAR,
	['A'] = ISALPHA | ISXDIGIT,
	['B'] = ISALPHA | ISXDIGIT,
	['C'] = ISALPHA | ISXDIGIT,
	['D'] = ISALPHA | ISXDIGIT,
	['E'] = ISALPHA | ISXDIGIT,
	['F'] = ISALPHA | ISXDIGIT,
	['G'] = ISALPHA,
	['H'] = ISALPHA,
	['I'] = ISALPHA,
	['J'] = ISALPHA,
	['K'] = ISALPHA,
	['L'] = ISALPHA,
	['M'] = ISALPHA,
	['N'] = ISALPHA,
	['O'] = ISALPHA,
	['P'] = ISALPHA,
	['Q'] = ISALPHA,
	['R'] = ISALPHA,
	['S'] = ISALPHA,
	['T'] = ISALPHA,
	['U'] = ISALPHA,
	['V'] = ISALPHA,
	['W'] = ISALPHA,
	['X'] = ISALPHA,
	['Y'] = ISALPHA,
	['Z'] = ISALPHA,
	['_'] = ISUNDER,
	['a'] = ISALPHA | ISXDIGIT,
	['b'] = ISALPHA | ISXDIGIT,
	['c'] = ISALPHA | ISXDIGIT,
	['d'] = ISALPHA | ISXDIGIT,
	['e'] = ISALPHA | ISXDIGIT,
	['f'] = ISALPHA | ISXDIGIT,
	['g'] = ISALPHA,
	['h'] = ISALPHA,
	['i'] = ISALPHA,
	['j'] = ISALPHA,
	['k'] = ISALPHA,
	['l'] = ISALPHA,
	['m'] = ISALPHA,
	['n'] = ISALPHA,
	['o'] = ISALPHA,
	['p'] = ISALPHA,
	['q'] = ISALPHA,
	['r'] = ISALPHA,
	['s'] = ISALPHA,
	['t'] = ISALPHA,
	['u'] = ISALPHA,
	['v'] = ISALPHA,
	['w'] = ISALPHA,
	['x'] = ISALPHA,
	['y'] = ISALPHA,
	['z'] = ISALPHA,
	['{'] = ISSPECLDOL,
};
