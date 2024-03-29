/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2019, 2021
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
 *	@(#)parser.h	8.3 (Berkeley) 5/4/95
 */

#ifndef H_PARSER
#define H_PARSER 1

#include "config.h"

#include "token.h"

/* control characters in argument strings */
#define CTL_FIRST -127		/* first 'special' character */
#define CTLESC -127		/* escape next character */
#define CTLVAR -126		/* variable defn */
#define CTLENDVAR -125
#define CTLBACKQ -124
#define	CTLARI -122		/* arithmetic expression */
#define	CTLENDARI -121
#define	CTLQUOTEMARK -120
#define	CTL_LAST -120		/* last 'special' character */

#define CTLCHARS \
	     CTLESC:      \
	case CTLVAR:      \
	case CTLENDVAR:   \
	case CTLBACKQ:    \
	case CTLARI:      \
	case CTLENDARI:   \
	case CTLQUOTEMARK

/* variable substitution byte (follows CTLVAR) */
#define VSTYPE	0x0f		/* type of variable substitution */
#define VSNUL	0x10		/* colon--treat the empty string as unset */

/* values of VSTYPE field */
#define VSNORMAL	0x1		/* normal variable:  $var or ${var} */
#define VSMINUS		0x2		/* ${var-text} */
#define VSPLUS		0x3		/* ${var+text} */
#define VSQUESTION	0x4		/* ${var?message} */
#define VSASSIGN	0x5		/* ${var=text} */
#define VSLENGTH	0x6		/* ${#var} */
#define VSTRIMRIGHT	0xa		/* ${var%pattern} */
#define VSTRIMRIGHTMAX	0xb		/* ${var%%pattern} */
#define VSTRIMLEFT	0xc		/* ${var#pattern} */
#define VSTRIMLEFTMAX	0xd		/* ${var##pattern} */

/* values of checkkwd variable */
#define CHKALIAS	0x1
#define CHKNL		0x2
#define CHKEOFMARK	0x4
#define CHKCMD		0x8
#define CHKKWD		-0x10
#define CHKKWDMASK(x)	(0x10 << ((x) - (KWDOFFSET)))

/* Flags for readtoken1(). */
#define RT_HEREDOC    0x01
#define RT_STRIPTABS  0x02
/* Reserved           0x04 */
#define RT_SQSYNTAX   0x08
#define RT_DQSYNTAX   0x10
#define RT_DSQSYNTAX  0x18
#define RT_QSYNTAX    0x18
#define RT_STRING     0x20
#define RT_VARNEST    0x40
#define RT_ARINEST    0x80
#define RT_ARIPAREN   0x100
#define RT_CHECKEND   0x200
#define RT_CTOGGLE1   0x400
#define RT_CTOGGLE2   0x800
#ifdef WITH_LOCALE
#define RT_ESCAPE     0x1000
#define RT_MBCHAR     0x2000
#else
#define RT_ESCAPE     0
#define RT_MBCHAR     0
#endif
#ifdef ENABLE_INTERNAL_COMPLETION
#define RT_NOCOMPLETE 0x4000
#else
#define RT_NOCOMPLETE 0
#endif


/*
 * NEOF is returned by parsecmd when it encounters an end of file.  It
 * must be distinct from NULL, so we use the address of a variable that
 * happens to be handy.
 */
extern int lasttoken;
extern int tokpushback;
#define NEOF ((union node *)&tokpushback)
extern int whichprompt;		/* 1 == PS1, 2 == PS2 */
extern int checkkwd;


union node *parsecmd(int);
void fixredir(union node *, const char *, int);
const char *getprompt(void *);
const char *const *findkwd(const char *);
char *endofname(const char *);
const char *expandstr(const char *, int);
void nlprompt(void);
void synexpect(int) attribute((noreturn));

static inline int
goodname(const char *p)
{
	return !*endofname(p);
}

static inline int
isassignment(const char *p)
{
	const char *q = endofname(p);
	if (p == q)
		return 0;
	return *q == '=';
}

static inline int parser_eof(void)
{
	return tokpushback > lasttoken;
}

#endif
