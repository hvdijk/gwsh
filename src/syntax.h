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

#ifndef H_SYNTAX
#define H_SYNTAX 1

#include "config.h"

/* Syntax classes for is_ functions */
#define ISSPACE    0001 /* a space character */
#define ISALPHA    0002 /* a letter */
#define ISUNDER    0004 /* an underscore */
#define ISODIGIT   0010 /* an octal digit */
#define ISXDIGIT   0020 /* a hexadecimal digit */
#define ISSPECLDOL 0040 /* a character that is special after a dollar */
#define ISSPECLVAR 0100 /* the name of a special parameter */

#ifdef WITH_LOCALE
#define PMBW -258
#define PMBB -257
#endif
#define PEOF -256
#define SYNBASE 128

#define ctype(c)         ((unsigned char) is_type[(unsigned char)(c)])
#define is_odigit(c)     ((unsigned) ((c) - '0') <= 7)
#define is_digit(c)      ((unsigned) ((c) - '0') <= 9)
#define is_xdigit(c)     (ctype((c)) & ISXDIGIT)
#define is_alpha(c)      (ctype((c)) & ISALPHA)
#define is_alnum(c)      (ctype((c)) & (ISALPHA|ISXDIGIT))
#define is_name(c)       (ctype((c)) & (ISALPHA|ISUNDER))
#define is_in_name(c)    (ctype((c)) & (ISALPHA|ISUNDER|ISXDIGIT))
#define is_specialdol(c) (ctype((c)) & ISSPECLDOL)
#define is_specialvar(c) (ctype((c)) & ISSPECLVAR)
#define is_space(c)      (ctype((c)) & ISSPACE)
#define digit_val(c)     ((c) - '0')

extern const unsigned char is_type[];

#endif
