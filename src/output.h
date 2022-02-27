/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2019, 2022
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
 *
 *	@(#)output.h	8.2 (Berkeley) 5/4/95
 */

#ifndef H_OUTPUT
#define H_OUTPUT 1

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>

struct output {
	char *nextc;
	char *end;
	int fd;
	int error;
};

extern struct output output;
extern struct output errout;
extern struct output preverrout;
#define out1 (&output)
#define out2 (&errout)
#define IOBUFSIZE BUFSIZ
extern char iobuf[IOBUFSIZE];
#define IOBUFEND (iobuf+IOBUFSIZE)
extern struct output *iobufout;

void outmem(const char *, size_t, struct output *);
void outstr(const char *, struct output *);
void outcslow(int, struct output *);
void flushall(void);
void outfmt(struct output *, const char *, ...)
    attribute((format(printf,2,3)));
void out1fmt(const char *, ...)
    attribute((format(printf,1,2)));
int fmtstr(char *, size_t, const char *, ...)
    attribute((format(printf,3,4)));
int xasprintf(char **, const char *, ...);
int xvasprintf(char **, size_t, const char *, va_list);
int doformat(struct output *, const char *, va_list);
int xwrite(int, const void *, size_t);
int xopen(const char *, int);

static inline void
freestdout(void)
{
	iobufout = NULL;
	output.error = 0;
}

static inline void outc(int ch, struct output *file)
{
	if (file->nextc == file->end)
		outcslow(ch, file);
	else {
		*file->nextc = ch;
		file->nextc++;
	}
}
#define out1c(c)	outc((c), out1)
#define out2c(c)	outcslow((c), out2)
#define out1mem(s, l)	outmem((s), (l), out1)
#define out1str(s)	outstr((s), out1)
#define out2str(s)	outstr((s), out2)
#define outerr(f)	(f)->error

#endif
