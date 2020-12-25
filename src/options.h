/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2019
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
 *	@(#)options.h	8.2 (Berkeley) 5/4/95
 */

#ifndef H_OPTIONS
#define H_OPTIONS 1

struct shparam {
	int nparam;		/* # of positional parameters (without $0) */
	unsigned char malloc;	/* if parameter list dynamically allocated */
	char **p;		/* parameter list */
	int optind;		/* next parameter to be processed by getopts */
	int optoff;		/* used by getopts */
};


enum {
	cflagind,
#define cflag (optlist[cflagind])
	lflagind,
#define lflag (optlist[lflagind])
	sflagind,
#define sflag (optlist[sflagind])
	FIRSTSETOPT,
	eflagind = FIRSTSETOPT,
#define eflag (optlist[eflagind])
	fflagind,
#define fflag (optlist[fflagind])
	Iflagind,
#define Iflag (optlist[Iflagind])
	iflagind,
#define iflag (optlist[iflagind])
	mflagind,
#define mflag (optlist[mflagind])
	nflagind,
#define nflag (optlist[nflagind])
	xflagind,
#define xflag (optlist[xflagind])
	vflagind,
#define vflag (optlist[vflagind])
	Vflagind,
#define Vflag (optlist[Vflagind])
	Eflagind,
#define Eflag (optlist[Eflagind])
	Cflagind,
#define Cflag (optlist[Cflagind])
	aflagind,
#define aflag (optlist[aflagind])
	bflagind,
#define bflag (optlist[bflagind])
	uflagind,
#define uflag (optlist[uflagind])
	pflagind,
#define pflag (optlist[pflagind])
	nologind,
#define nolog (optlist[nologind])
	debugind,
#define debug (optlist[debugind])
	optpipefailind,
#define optpipefail (optlist[optpipefailind])
	NOPTS
};

extern const char optletters[NOPTS];
extern char optlist[NOPTS];


extern char *minusc;		/* argument to -c option */
extern char *arg0;		/* $0 */
extern struct shparam shellparam;  /* $@ */
extern char **argptr;		/* argument list for builtin commands */
extern char *optionarg;		/* set by nextopt */
extern char *optptr;		/* used by nextopt */

int procargs(int, char **);
void optschanged(void);
void setparam(char **);
void freeparam(volatile struct shparam *);
int shiftcmd(int, char **);
int setcmd(int, char **);
int getoptscmd(int, char **);
int nextopt(const char *);
char *nextarg(int);
void endargs(void);
void getoptsreset(const char *);

#endif
