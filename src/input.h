/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2020
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
 *	@(#)input.h	8.2 (Berkeley) 5/4/95
 */

#ifndef H_INPUT
#define H_INPUT 1

#include "config.h"

#include <stddef.h>
#ifdef WITH_LOCALE
#include <limits.h>
#include <locale.h>
#endif
#if defined(WITH_PARSER_LOCALE) && defined(HAVE_XLOCALE_H)
#include <xlocale.h>
#endif

/* PEOF (the end of file marker) is defined in syntax.h */

enum {
	INPUT_PUSH_FILE = 1,
	INPUT_NOFILE_OK = 2,
};

struct alias;

struct parsefilepush {
	int nleft;		/* number of chars left in this line */
	const char *nextc;	/* next char in buffer */

#ifdef WITH_LOCALE
	int mbt;		/* multi-byte character type (word or blank) */
	char *mbp;		/* next byte in mbc to be returned */
	char mbc[MB_LEN_MAX+1];	/* null-terminated multibyte character */
#endif

	/* Remember last two characters for pungetc. */
	int lastc[2];

	/* Number of outstanding calls to pungetc. */
	int unget;

	int backq;		/* old-style cmdsubsts depth */
	int dqbackq;		/* whether each cmdsubst was double-quoted */
};

struct strpush {
	struct parsefilepush p;
	struct strpush *prev;	/* preceding string on stack */
	struct alias *ap;	/* if push was associated with an alias */
	const char *string;	/* remember the string since it may change */
};

/*
 * The parsefile structure pointed to by the global variable parsefile
 * contains information about the current file being read.
 */

struct parsefile {
	struct parsefilepush p;
	struct parsefile *prev;	/* preceding file on stack */
	int linno;		/* current line */
	int fd;			/* file descriptor (or -1 if string) */
	int lleft;		/* number of chars left in this buffer */
	char *buf;		/* input buffer */
	struct strpush *strpush; /* for pushing strings at this level */
	struct strpush basestrpush; /* so pushing one is fast */
	int flags;
};

#define PF_NONUL      0x01 /* do not allow NUL bytes on the first line */
#ifndef SMALL
#define PF_HIST       0x02 /* create history entries */
#ifdef ENABLE_INTERNAL_COMPLETION
#define PF_COMPLETING 0x04 /* processing input for tab completion */
#endif
#endif

extern struct parsefile *parsefile;
#ifdef WITH_PARSER_LOCALE
extern locale_t parselocale;
#endif

/*
 * The input line number.  Input.c just defines this variable, and saves
 * and restores it when files are pushed and popped.  The user of this
 * package must set its value.
 */
#define plinno (parsefile->linno)

int pgetc(void);
void pungetc(void);
void pushstring(const char *, size_t, void *);
void popstring(void);
int setinputfile(const char *, int);
void setinputstring(const char *);
void setinputmem(const char *, size_t);
void popfile(void);
void unwindfiles(struct parsefile *);
void popallfiles(void);
void closescript(void);

#endif
