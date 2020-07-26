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
 */

#include "config.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#ifdef HAVE_GETPWNAM
#include <pwd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#ifdef WITH_LOCALE
#include <wchar.h>
#include <wctype.h>
#else
#include <ctype.h>
#endif
#include <stdbool.h>

/*
 * Routines to expand arguments to commands.  We have to deal with
 * backquotes, shell variables, and file metacharacters.
 */

#include "shell.h"
#include "main.h"
#include "nodes.h"
#include "eval.h"
#include "expand.h"
#include "syntax.h"
#include "parser.h"
#include "jobs.h"
#include "options.h"
#include "var.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "mylocale.h"
#include "mystring.h"
#include "show.h"
#include "system.h"

/*
 * pmatch() flags
 */
#define PM_MATCHMAX   0x01 /* Look for the longest match. Combine with PM_MATCHLEFT or PM_MATCHRIGHT. */
#define PM_MATCHRIGHT 0x02 /* Match a suffix, not the full string. Return the start of the suffix. */
#define PM_MATCHLEFT  0x04 /* Match a prefix, not the full string. Return the end of the prefix. */
#define PM_CTLESC     0x08 /* Skip over CTLESC characters. */

/* Add CTLESC when necessary. */
#define QUOTES_ESC	(EXP_FULL | EXP_CASE)
/* Do not skip NUL characters. */
#define QUOTES_KEEPNUL	EXP_TILDE

/*
 * Structure specifying which parts of the string should be searched
 * for IFS characters.
 */

struct ifsregion {
	struct ifsregion *next;	/* next region in list */
	int begoff;		/* offset of start of region */
	int endoff;		/* offset of end of region */
	int nulonly;		/* search for nul bytes only */
};

/* output of current string */
static char *expdest;
/* list of back quote expressions */
static struct nodelist *argbackq;
/* first struct in list of ifs regions */
static struct ifsregion ifsfirst;
/* last struct in list */
static struct ifsregion *ifslastp;
/* holds expanded arg list */
static struct arglist exparg;

STATIC char *argstr(char *, int);
STATIC char *exptilde(char *, char *, int);
STATIC char *expari(char *, int);
STATIC void expbackq(union node *, int);
STATIC const char *subevalvar(char *, char *, int, int, int, int, int);
STATIC char *evalvar(char *, int);
STATIC size_t strtodest(const char *, int);
STATIC void memtodest(const char *, size_t, int);
STATIC ssize_t varvalue(char *, int, int);
STATIC void expandmeta(struct strlist *, int);
STATIC void expmeta(char *);
STATIC struct strlist *expsort(struct strlist *);
STATIC struct strlist *msort(struct strlist *, int);
STATIC void addfname(char *);
STATIC int patmatch(char *, const char *);
STATIC const char *pmatch(char *, const char *, int);
STATIC int cvtnum(intmax_t, int);


/*
 * Prepare a pattern for a glob operation.
 *
 * Returns an stalloced string.
 */

STATIC inline char *
preglob(char *pattern) {
	return _rmescapes(pattern, 1);
}


static inline const char *getpwhome(const char *name)
{
#ifdef HAVE_GETPWNAM
	struct passwd *pw = getpwnam(name);
	return pw ? pw->pw_dir : 0;
#else
	return 0;
#endif
}


/*
 * Perform variable substitution and command substitution on an argument,
 * placing the resulting list of arguments in arglist.  If EXP_FULL is true,
 * perform splitting and file name expansion.  When arglist is NULL, perform
 * here document expansion.
 */

void
expandarg(union node *arg, struct arglist *arglist, int flag)
{
	struct strlist *sp;
	char *p;

	argbackq = arg->narg.backquote;
	STARTSTACKSTR(expdest);
	argstr(arg->narg.text, flag);
	if (arglist == NULL) {
		/* here document expanded */
		goto out;
	}
	p = grabstackstr(expdest);
	exparg.lastp = &exparg.list;
	/*
	 * TODO - EXP_REDIR
	 */
	if (flag & EXP_FULL) {
		ifsbreakup(p, -1, &exparg);
		*exparg.lastp = NULL;
		exparg.lastp = &exparg.list;
		expandmeta(exparg.list, flag);
	} else {
		sp = (struct strlist *)stalloc(sizeof (struct strlist));
		sp->text = p;
		*exparg.lastp = sp;
		exparg.lastp = &sp->next;
	}
	*exparg.lastp = NULL;
	if (exparg.list) {
		*arglist->lastp = exparg.list;
		arglist->lastp = exparg.lastp;
	}

out:
	ifsfree();
}



/*
 * Perform variable and command substitution.  If EXP_FULL is set, output CTLESC
 * characters to allow for further processing.  Otherwise treat
 * $@ like $* since no splitting will be performed.
 */

STATIC char *
argstr(char *p, int flag)
{
	static const char spclchars[] = {
		'=',
		':',
		CTLQUOTEMARK,
		CTLENDVAR,
		CTLESC,
		CTLVAR,
		CTLENDVAR,
		CTLBACKQ,
		CTLARI,
		CTLENDARI,
		0
	};
	const char *reject = spclchars + 2;
	char prev;
	int c = 0;
	int breakall = (flag & (EXP_WORD | EXP_QUOTED)) == EXP_WORD;
	size_t length;
	int startloc;

	reject -= (flag & (EXP_VARTILDE | EXP_VARTILDE2)) / EXP_VARTILDE2;
	length = 0;
	if ((flag & (EXP_TILDE | EXP_DISCARD)) == EXP_TILDE) {
		char *q;

		flag &= ~EXP_TILDE;
tilde:
		q = p;
		if (*q == '~')
			p = exptilde(p, q, flag);
	}
start:
	startloc = expdest - (char *)stackblock();
	for (;;) {
		length += strcspn(p + length, reject);
		prev = c;
		c = (signed char)p[length];
		if (!((c - 1) & 0x80)) {
			/* c == '=' || c == ':' */
			length++;
		}
		if (length > 0 && !(flag & EXP_DISCARD)) {
			int newloc;
			expdest = stnputs(p, length, expdest);
			newloc = expdest - (char *)stackblock();
			if (breakall && !(flag & EXP_QUOTED) && newloc > startloc) {
				recordregion(startloc, newloc, 0);
			}
			startloc = newloc;
		}
		p += length + 1;
		length = 0;

		switch (c) {
			int dolatstrhack;
		case '\0':
		case CTLENDVAR:
		case CTLENDARI:
			if (!(flag & (EXP_WORD | EXP_DISCARD)))
				STPUTC('\0', expdest);
			return p;
		case '=':
			flag ^= EXP_VARTILDE | EXP_VARTILDE2;
			reject++;
			/* fall through */
		case ':':
			/*
			 * sort of a hack - expand tildes in variable
			 * assignments (after the first '=' and after ':'s).
			 */
			if (*--p == '~') {
				goto tilde;
			}
			continue;
		case CTLQUOTEMARK:
			flag ^= EXP_QUOTED;
addquote:
			if (flag & QUOTES_ESC) {
				p--;
				length++;
				startloc++;
			}
			break;
		case CTLESC:
			if (*p >= (char) CTL_FIRST && *p <= (char) CTL_LAST
			    && !(flag & EXP_QUOTED)) {
				length++;
				if (flag & QUOTES_ESC) {
					p--;
					length++;
				}
				break;
			}
			startloc++;
			length++;
			goto addquote;
		case CTLVAR:
			/* "$@" syntax adherence hack */
			dolatstrhack = p[1] == '@' && (*p & VSTYPE) != VSMINUS && (*p & VSTYPE) != VSLENGTH && !shellparam.nparam && flag & QUOTES_ESC && !(flag & EXP_DISCARD);
			p = evalvar(p, flag);
			if (dolatstrhack && prev == (char)CTLQUOTEMARK && *p == (char)CTLQUOTEMARK) {
				expdest--;
				flag ^= EXP_QUOTED;
				p++;
			}
			goto start;
		case CTLBACKQ:
			if (!(flag & EXP_DISCARD))
				expbackq(argbackq->n, flag);
			argbackq = argbackq->next;
			goto start;
		case CTLARI:
			p = expari(p, flag);
			goto start;
		}
	}
}

STATIC char *
exptilde(char *startp, char *p, int flag)
{
	signed char c;
	char *name;
	const char *home;
	int quotes = flag & QUOTES_ESC;

	name = p + 1;

	while ((c = *++p) != '\0') {
		switch(c) {
		case CTLESC:
			return (startp);
		case CTLQUOTEMARK:
			return (startp);
		case ':':
			if (flag & (EXP_VARTILDE | EXP_VARTILDE2))
				goto done;
			break;
		case '/':
		case CTLENDVAR:
			goto done;
		}
	}
done:
	*p = '\0';
	if (*name == '\0') {
		home = lookupvar(homestr);
	} else {
		home = getpwhome(name);
	}
	if (!home)
		goto lose;
	*p = c;
	if (quotes)
		STPUTC(CTLQUOTEMARK, expdest);
	strtodest(home, quotes | EXP_QUOTED);
	if (quotes)
		STPUTC(CTLQUOTEMARK, expdest);
	return (p);
lose:
	*p = c;
	return (startp);
}


void 
removerecordregions(int endoff)
{
	if (ifslastp == NULL)
		return;

	if (ifsfirst.endoff > endoff) {
		while (ifsfirst.next != NULL) {
			struct ifsregion *ifsp;
			INTOFF;
			ifsp = ifsfirst.next->next;
			ckfree(ifsfirst.next);
			ifsfirst.next = ifsp;
			INTON;
		}
		if (ifsfirst.begoff > endoff)
			ifslastp = NULL;
		else {
			ifslastp = &ifsfirst;
			ifsfirst.endoff = endoff;
		}
		return;
	}
	
	ifslastp = &ifsfirst;
	while (ifslastp->next && ifslastp->next->begoff < endoff)
		ifslastp=ifslastp->next;
	while (ifslastp->next != NULL) {
		struct ifsregion *ifsp;
		INTOFF;
		ifsp = ifslastp->next->next;
		ckfree(ifslastp->next);
		ifslastp->next = ifsp;
		INTON;
	}
	if (ifslastp->endoff > endoff)
		ifslastp->endoff = endoff;
}


/*
 * Expand arithmetic expression.
 */
STATIC char *
expari(char *start, int flag)
{
	struct stackmark sm;
	char *p;
	int begoff;
	int endoff;
	int len;
	intmax_t result;

	begoff = expdest - (char *) stackblock();
	p = argstr(start, (flag & EXP_DISCARD) | EXP_QUOTED);
	if (flag & EXP_DISCARD)
		goto out;

	endoff = expdest - (char *) stackblock();
	expdest = (char *) stackblock() + begoff;
	pushstackmark(&sm, endoff);
	result = arith(expdest);
	popstackmark(&sm);

	len = cvtnum(result, flag);

	if (likely(!(flag & EXP_QUOTED)))
		recordregion(begoff, begoff + len, 0);

out:
	return p;
}


/*
 * Expand stuff in backwards quotes.
 */

STATIC void
expbackq(union node *cmd, int flag)
{
	struct backcmd in;
	int i;
	char buf[128];
	char *p;
	char *dest;
	int startloc;
	struct stackmark smark;

	INTOFF;
	startloc = expdest - (char *)stackblock();
	pushstackmark(&smark, startloc);
	evalbackcmd(cmd, flag & EXP_XTRACE ? EV_XTRACE : 0, &in);
	popstackmark(&smark);

	p = in.buf;
	i = in.nleft;
	if (i == 0)
		goto read;
	for (;;) {
		memtodest(p, i, flag & (QUOTES_ESC | EXP_QUOTED));
read:
		if (in.fd < 0)
			break;
		do {
			i = read(in.fd, buf, sizeof buf);
		} while (i < 0 && errno == EINTR);
		TRACE(("expbackq: read returns %d\n", i));
		if (i <= 0)
			break;
		p = buf;
	}

	if (in.buf)
		ckfree(in.buf);
	if (in.fd >= 0) {
		close(in.fd);
		back_exitstatus = waitforjob(in.jp);
	}
	INTON;

	/* Eat all trailing newlines */
	dest = expdest;
	for (; dest > (char *)stackblock() + startloc && dest[-1] == '\n';)
		STUNPUTC(dest);
	expdest = dest;

	if (!(flag & EXP_QUOTED))
		recordregion(startloc, dest - (char *)stackblock(), 0);
	TRACE(("evalbackq: size=%d: \"%.*s\"\n",
		(dest - (char *)stackblock()) - startloc,
		(dest - (char *)stackblock()) - startloc,
		stackblock() + startloc));
}


STATIC const char *
subevalvar(char *p, char *str, int strloc, int subtype, int startloc, int varflags, int flag)
{
	int quotes = flag & QUOTES_ESC;
	char *startp;
	char *loc;
	struct nodelist *saveargbackq = argbackq;
	int amount;

	argstr(p, EXP_TILDE | (subtype != VSASSIGN && subtype != VSQUESTION ?
			       EXP_CASE : 0));
	argbackq = saveargbackq;
	startp = (char *) stackblock() + startloc;

	switch (subtype) {
	case VSASSIGN:
		setvar(str, startp, 0);
		amount = startp - expdest;
		STADJUST(amount, expdest);
		return startp;

	case VSQUESTION:
		varunset(p, str, startp, varflags);
		/* NOTREACHED */
	}

#ifdef DEBUG
	if (subtype < VSTRIMRIGHT || subtype > VSTRIMLEFTMAX)
		abort();
#endif

	str = (char *) stackblock() + strloc;
	preglob(str);

	loc = (char *)pmatch(str, startp, (quotes ? PM_CTLESC : 0) | (subtype & (PM_MATCHLEFT | PM_MATCHRIGHT | PM_MATCHMAX)));
	if (loc) {
		if (subtype & PM_MATCHLEFT) {
			memmove(startp, loc, str - loc);
			loc = startp + (str - loc) - 1;
		}
		*loc = '\0';
		amount = loc - expdest;
		STADJUST(amount, expdest);
	}
	return loc;
}


/*
 * Expand a variable, and return a pointer to the next character in the
 * input string.
 */
STATIC char *
evalvar(char *p, int flag)
{
	int subtype;
	int varflags;
	char *var;
	int patloc;
	int startloc;
	ssize_t varlen;
	int easy;
	int quoted;

	varflags = *p++;
	subtype = varflags & VSTYPE;

	if (flag & EXP_DISCARD)
		goto discard;

	if (!subtype)
badsub:
		sh_error("Bad substitution");

	quoted = flag & EXP_QUOTED;
	var = p;
	easy = (!quoted || (*var == '@' && shellparam.nparam));
	startloc = expdest - (char *)stackblock();
	p = strchr(p, '=') + 1;
	if (subtype == VSLENGTH && *p != (char)CTLENDVAR)
		goto badsub;

again:
	varlen = varvalue(var, varflags, flag);
	if (varflags & VSNUL)
		varlen--;

	if (subtype == VSPLUS) {
		varlen = -1 - varlen;
		goto vsplus;
	}

	if (subtype == VSMINUS) {
vsplus:
		if (varlen < 0) {
			return argstr(p, flag | EXP_TILDE | EXP_WORD);
		}
		goto record;
	}

	if (subtype == VSASSIGN || subtype == VSQUESTION) {
		if (varlen >= 0)
			goto record;

		subevalvar(p, var, 0, subtype, startloc, varflags,
			   flag & ~QUOTES_ESC);
		varflags &= ~VSNUL;
		/* 
		 * Remove any recorded regions beyond 
		 * start of variable 
		 */
		removerecordregions(startloc);
		goto again;
	}

	if (varlen < 0 && uflag && (*var != '@' && *var != '*'))
		varunset(p, var, 0, 0);

	if (subtype == VSLENGTH) {
		cvtnum(varlen > 0 ? varlen : 0, flag);
		goto record;
	}

	if (subtype == VSNORMAL) {
record:
		if (easy)
			recordregion(startloc, expdest - (char *)stackblock(), quoted);
discard:
		if (subtype & ~VSNORMAL)
			return argstr(p, flag | EXP_DISCARD);
		return p;
	}

#ifdef DEBUG
	switch (subtype) {
	case VSTRIMLEFT:
	case VSTRIMLEFTMAX:
	case VSTRIMRIGHT:
	case VSTRIMRIGHTMAX:
		break;
	default:
		abort();
	}
#endif

	/*
	 * Terminate the string and start recording the pattern
	 * right after it
	 */
	STPUTC('\0', expdest);
	patloc = expdest - (char *)stackblock();
	if (subevalvar(p, NULL, patloc, subtype,
		       startloc, varflags, flag) == 0) {
		int amount = expdest - (
			(char *)stackblock() + patloc - 1
		);
		STADJUST(-amount, expdest);
	}
	/* Remove any recorded regions beyond start of variable */
	removerecordregions(startloc);
	goto record;
}


/*
 * Put a string on the stack.
 */

STATIC void
memtodest(const char *p, size_t len, int quotes) {
	char *q;

	if (unlikely(!len))
		return;

	q = makestrspace(len * 2, expdest);

	do {
		int c = (signed char)*p++;
		if (c) {
			if (quotes & QUOTES_ESC) {
				switch (c) {
					case '\\':
					case '!': case '*': case '?': case '[': case '=':
					case '~': case ':': case '/': case '-': case ']':
						if (quotes & EXP_QUOTED)
					case CTLCHARS:
							USTPUTC(CTLESC, q);
						break;
				}
			}
		} else if (!(quotes & QUOTES_KEEPNUL))
			continue;
		USTPUTC(c, q);
	} while (--len);

	expdest = q;
}


STATIC size_t
strtodest(const char *p, int quotes)
{
	size_t len = strlen(p);
	memtodest(p, len, quotes);
	return len;
}


/*
 * Add the value of a specialized variable to the stack string.
 */

STATIC ssize_t
varvalue(char *name, int varflags, int flags)
{
	int num;
	char *p;
	int i;
	const char *sep;
	char **ap;
	int subtype = varflags & VSTYPE;
	int discard = subtype == VSPLUS || subtype == VSLENGTH;
	int quotes = (flags & (EXP_QUOTED | (discard ? 0 : QUOTES_ESC))) | QUOTES_KEEPNUL;
	ssize_t len = 0;
#ifdef WITH_LOCALE
	ssize_t count = -1;
#endif

	flags &= EXP_QUOTED | EXP_FULL;

	switch (*name) {
	case '$':
		num = rootpid;
		goto numvar;
	case '?':
		num = exitstatus;
		goto numvar;
	case '#':
		num = shellparam.nparam;
		goto numvar;
	case '!':
		num = backgndpid;
		if (num == 0)
			return -1;
numvar:
		len = cvtnum(num, flags);
		break;
	case '-':
		p = makestrspace(NOPTS, expdest);
		for (i = NOPTS - 1; i >= 0; i--) {
			if (optlist[i] && optletters[i]) {
				USTPUTC(optletters[i], p);
				len++;
			}
		}
		expdest = p;
		break;
	case '@':
		flags &= EXP_FULL;
		/* fall through */
	case '*':
		if (flags == EXP_FULL) {
			sep = nullstr;
		} else {
			sep = ifsset() ? ifsval() : defifs;
			if (!*sep)
				sep = NULL;
		}
		if (!*(ap = shellparam.p))
			return -1;
		while ((p = *ap++)) {
			len += strtodest(p, quotes);

			if (*ap && sep) {
				len++;
#ifndef WITH_LOCALE
				memtodest(sep, 1, quotes);
#else
				memtodest(sep, mbcget(sep, -1, NULL, 0) - sep, quotes);
#endif
			}
		}
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		num = atoi(name);
		if (num < 0 || num > shellparam.nparam)
			return -1;
		p = num ? shellparam.p[num - 1] : arg0;
		goto value;
	default:
		p = lookupvar(name);
value:
		if (!p)
			return -1;

		len = strtodest(p, quotes);
#ifdef WITH_LOCALE
		if (subtype == VSLENGTH)
			count = mbccnt(p);
#endif
		break;
	}

	if (discard)
		STADJUST(-len, expdest);
#ifdef WITH_LOCALE
	if (subtype == VSLENGTH && count >= 0)
		return count;
#endif
	return len;
}



/*
 * Record the fact that we have to scan this region of the
 * string for IFS characters.
 */

void
recordregion(int start, int end, int nulonly)
{
	struct ifsregion *ifsp;

	if (ifslastp == NULL) {
		ifsp = &ifsfirst;
	} else if (ifslastp->endoff == start && ifslastp->nulonly == nulonly) {
		ifslastp->endoff = end;
		return;
	} else {
		INTOFF;
		ifsp = (struct ifsregion *)ckmalloc(sizeof (struct ifsregion));
		ifsp->next = NULL;
		ifslastp->next = ifsp;
		INTON;
	}
	ifslastp = ifsp;
	ifslastp->begoff = start;
	ifslastp->endoff = end;
	ifslastp->nulonly = nulonly;
}



/*
 * Break the argument string into pieces based upon IFS and add the
 * strings to the argument list.  The regions of the string to be
 * searched for IFS characters have been stored by recordregion.
 * If maxargs is non-negative, at most maxargs arguments will be created, by
 * joining together the last arguments.
 */
void
ifsbreakup(char *string, int maxargs, struct arglist *arglist)
{
	struct ifsregion *ifsp;
	struct strlist *sp;
	char *start;
	char *p;
	char *q;
	char *r = NULL;
	const char *ifs, *realifs;
#ifdef WITH_LOCALE
	size_t realifslen;
#endif
	int ifsspc;
	int nulonly;


	start = string;
	if (ifslastp != NULL) {
		realifs = ifsset() ? ifsval() : defifs;
#ifdef WITH_LOCALE
		realifslen = strlen(realifs);
#endif
		ifsp = &ifsfirst;
		do {
			p = string + ifsp->begoff;
			nulonly = ifsp->nulonly;
#ifndef WITH_LOCALE
			ifs = nulonly ? nullstr : realifs;
#endif
			ifsspc = 0;
			while (p < string + ifsp->endoff) {
				int c;
				bool isifs;
				bool isdefifs;
#ifdef WITH_LOCALE
				int wc, wifs;
#endif

				q = p;

#ifdef WITH_LOCALE
				p = mbcget(p, string + ifsp->endoff - p, &wc, 1);
				c = wctob(wc);
				isifs = !wc;
				if (!isifs && !nulonly) {
					for (ifs = realifs; *ifs; ) {
						ifs = mbcget(ifs, realifs + realifslen - ifs, &wifs, 0);
						if (wc == wifs) {
							isifs = true;
							break;
						}
					}
				}
#else
				c = *p++;
				if (c == (char)CTLESC)
					c = *p++;

				isifs = strchr(ifs, c);
#endif
				isdefifs = false;
				if (isifs)
					isdefifs = strchr(defifs, c);

				/* If only reading one more argument:
				 * If we have exactly one field,
				 * read that field without its terminator.
				 * If we have more than one field,
				 * read all fields including their terminators,
				 * except for trailing IFS whitespace.
				 *
				 * This means that if we have only IFS
				 * characters left, and at most one
				 * of them is non-whitespace, we stop
				 * reading here.
				 * Otherwise, we read all the remaining
				 * characters except for trailing
				 * IFS whitespace.
				 *
				 * In any case, r indicates the start
				 * of the characters to remove, or NULL
				 * if no characters should be removed.
				 */
				if (!maxargs) {
					if (isdefifs) {
						if (!r)
							r = q;
						continue;
					}

					if (!(isifs && ifsspc))
						r = NULL;

					ifsspc = 0;
					continue;
				}

				if (ifsspc) {
					if (isifs)
						q = p;

					start = q;

					if (isdefifs)
						continue;

					isifs = false;
				}

				if (isifs) {
					if (!nulonly)
						ifsspc = isdefifs;
					/* Ignore IFS whitespace at start */
					if (q == start && ifsspc) {
						start = p;
						ifsspc = 0;
						continue;
					}
					if (maxargs > 0 && !--maxargs) {
						r = q;
						continue;
					}
					*q = '\0';
					sp = (struct strlist *)stalloc(sizeof *sp);
					sp->text = start;
					*arglist->lastp = sp;
					arglist->lastp = &sp->next;
					start = p;
					continue;
				}

				ifsspc = 0;
			}
		} while ((ifsp = ifsp->next) != NULL);
		if (nulonly)
			goto add;
	}

	if (r)
		*r = '\0';

	if (!*start)
		return;

add:
	sp = (struct strlist *)stalloc(sizeof *sp);
	sp->text = start;
	*arglist->lastp = sp;
	arglist->lastp = &sp->next;
}

void ifsfree(void)
{
	struct ifsregion *p = ifsfirst.next;

	if (!p)
		goto out;

	INTOFF;
	do {
		struct ifsregion *ifsp;
		ifsp = p->next;
		ckfree(p);
		p = ifsp;
	} while (p);
	ifsfirst.next = NULL;
	INTON;

out:
	ifslastp = NULL;
}



/*
 * Expand shell metacharacters.  At this point, the only control characters
 * should be escapes.  The results are stored in the list exparg.
 */

STATIC void
expandmeta(struct strlist *str, int flag)
{
	/* TODO - EXP_REDIR */

	while (str) {
		struct strlist **savelastp;
		struct strlist *sp;

		if (fflag)
			goto nometa;
		savelastp = exparg.lastp;

		INTOFF;
		preglob(str->text);
		expmeta(str->text);
		INTON;
		if (exparg.lastp == savelastp) {
			/*
			 * no matches
			 */
nometa:
			*exparg.lastp = str;
			rmescapes(str->text);
			exparg.lastp = &str->next;
		} else {
			*exparg.lastp = NULL;
			*savelastp = sp = expsort(*savelastp);
			while (sp->next != NULL)
				sp = sp->next;
			exparg.lastp = &sp->next;
		}
		str = str->next;
	}
}


/*
 * Do metacharacter (i.e. *, ?, [...]) expansion.
 */

STATIC void
expmeta1(char *expdir, char *enddir, char *name, int force)
{
	char *p;
	const char *cp;
	char *start;
	char *endname, saveendname, *startnext;
	int metaflag;
	struct stat statb;
	DIR *dirp;
	struct dirent *dp;
	int atend;
	int matchdot;

	metaflag = 0;
	start = name;
	for (p = name; *p;) {
		switch (*p) {
			int c;
			char *q, *r;
		case '\0':
			break;
		case '*':
		case '?':
			p++;
			metaflag = 1;
			continue;
		case '[':
			p++;
			if (!metaflag) {
				metaflag = -1;
				if (*p == '!')
					p++;
				if (*p == ']')
					p++;
			}
			continue;
		case ']':
			p++;
			if (metaflag)
				metaflag = 1;
			continue;
		default:
			do {
				q = p;
				break;
		case '\\':
				q = p + 1;
				force = 1;
				break;
			} while (0);
			r = q;
			if (*r == (char)CTLESC)
				r++;
			if (*r == '/') {
				if (metaflag > 0)
					break;
				start = p = r + 1;
				metaflag = 0;
				continue;
			}
			p = q;
			if (!*p)
				break;
			GETC_CTLESC(c, p, 1);
			continue;
		}
		break;
	}

	if (metaflag <= 0) { /* we've reached the end of the file name */
		if (force) {
			p = name;
			do {
				if (enddir == expdir + PATH_MAX)
					return;
				if (*p == '\\' && p[1] != '\0')
					p++;
				if (*p == (char)CTLESC)
					p++;
				*enddir++ = *p;
				} while (*p++);
			if (lstat(expdir, &statb) >= 0)
				addfname(expdir);
		}
		return;
	}

	endname = p;
	if (name < start) {
		p = name;
		do {
			if (enddir == expdir + PATH_MAX)
				return;
			if (*p == '\\')
				p++;
			if (*p == (char)CTLESC)
				p++;
			*enddir++ = *p++;
		} while (p < start);
	}
	if (enddir == expdir) {
		cp = ".";
	} else if (enddir == expdir + 1 && *expdir == '/') {
		cp = "/";
	} else {
		cp = expdir;
		enddir[-1] = '\0';
	}
	if ((dirp = opendir(cp)) == NULL)
		return;
	if (enddir != expdir)
		enddir[-1] = '/';
	if (*endname == 0) {
		atend = 1;
	} else {
		atend = 0;
		startnext = endname;
		if (*startnext == '\\')
			startnext++;
		if (*startnext == (char)CTLESC)
			startnext++;
		startnext++;
		saveendname = *endname;
		*endname = '\0';
	}
	matchdot = 0;
	p = start;
	if (*p == '\\')
		p++;
	if (*p == (char)CTLESC)
		p++;
	if (*p == '.')
		matchdot++;
	while (! int_pending() && (dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' && ! matchdot)
			continue;
		if (pmatch(start, dp->d_name, 0)) {
			p = enddir;
			cp = dp->d_name;
			for (;;) {
				if (p == expdir + PATH_MAX)
					goto toolong;
				if ((*p++ = *cp++) == '\0')
					break;
			}
			if (atend) {
				addfname(expdir);
			} else {
				p[-1] = '/';
				expmeta1(expdir, p, startnext, 1);
			}
		}
toolong: ;
	}
	closedir(dirp);
	if (! atend)
		*endname = saveendname;
}


STATIC void
expmeta(char *name)
{
	char expdir[PATH_MAX];
	expmeta1(expdir, expdir, name, 0);
}


/*
 * Add a file name to the list.
 */

STATIC void
addfname(char *name)
{
	struct strlist *sp;

	sp = (struct strlist *)stalloc(sizeof *sp);
	sp->text = sstrdup(name);
	*exparg.lastp = sp;
	exparg.lastp = &sp->next;
}


/*
 * Sort the results of file name expansion.  It calculates the number of
 * strings to sort and then calls msort (short for merge sort) to do the
 * work.
 */

STATIC struct strlist *
expsort(struct strlist *str)
{
	int len;
	struct strlist *sp;

	len = 0;
	for (sp = str ; sp ; sp = sp->next)
		len++;
	return msort(str, len);
}


STATIC struct strlist *
msort(struct strlist *list, int len)
{
	struct strlist *p, *q = NULL;
	struct strlist **lpp;
	int half;
	int n;

	if (len <= 1)
		return list;
	half = len >> 1;
	p = list;
	for (n = half ; --n >= 0 ; ) {
		q = p;
		p = p->next;
	}
	q->next = NULL;			/* terminate first half of list */
	q = msort(list, half);		/* sort first half of list */
	p = msort(p, len - half);		/* sort second half */
	lpp = &list;
	for (;;) {
#ifndef WITH_LOCALE
		int cmp = strcmp(p->text, q->text);
#else
		int cmp = strcoll(p->text, q->text);
#endif
		if (cmp < 0) {
			*lpp = p;
			lpp = &p->next;
			if ((p = *lpp) == NULL) {
				*lpp = q;
				break;
			}
		} else {
			*lpp = q;
			lpp = &q->next;
			if ((q = *lpp) == NULL) {
				*lpp = p;
				break;
			}
		}
	}
	return list;
}


/*
 * Returns true if the pattern matches the string.
 */

STATIC inline int
patmatch(char *pattern, const char *string)
{
	return !!pmatch(preglob(pattern), string, 0);
}


STATIC int
ccmatch(char *p, int chr, char **r)
{
	char *q;
#ifndef WITH_LOCALE
	static const struct class {
		char name[10];
		int (*fn)(int);
	} classes[] = {
		{ .name = "alnum:]",  .fn = isalnum  },
		{ .name = "cntrl:]",  .fn = iscntrl  },
		{ .name = "lower:]",  .fn = islower  },
		{ .name = "space:]",  .fn = isspace  },
		{ .name = "alpha:]",  .fn = isalpha  },
		{ .name = "digit:]",  .fn = isdigit  },
		{ .name = "print:]",  .fn = isprint  },
		{ .name = "upper:]",  .fn = isupper  },
		{ .name = "blank:]",  .fn = isblank  },
		{ .name = "graph:]",  .fn = isgraph  },
		{ .name = "punct:]",  .fn = ispunct  },
		{ .name = "xdigit:]", .fn = isxdigit },
	};
	const struct class *class, *end;
#else
	wctype_t class;
	int result;
#endif

	p++;
	if (*p++ != ':')
		return 0;

#ifndef WITH_LOCALE
	end = classes + sizeof(classes) / sizeof(classes[0]);
	for (class = classes; class < end; class++) {
		char *q;

		q = prefix(p, class->name);
		if (!q)
			continue;
		*r = q;
		return class->fn(chr);
	}
#endif

	q = p;
	for (;;) {
		int c;
		if (!*q)
			return 0;
		if (*q == ':' && q[1] == ']')
			break;
		GETC_CTLESC(c, q, 1);
	}
	*r = q + 2;
#ifndef WITH_LOCALE
	return 0;
#else
	*q = '\0';
	class = wctype(p);
	result = class && iswctype(chr, class);
	*q = ':';
	return result;
#endif
}

STATIC const char *
pmatch(char *pattern, const char *string, int flags)
{
	char *p;
	const char *q, *r, *s;
	char *ap;
	const char *aq;
#ifndef WITH_LOCALE
	char c, chr;
#else
	int c, chr;
#endif

	p = pattern;
	q = s = string;
	r = NULL;
	ap = NULL;
	aq = NULL;
	if (flags & PM_MATCHRIGHT)
		goto ast;
	for (;;) {
		switch (c = *p) {
		case '\0':
			p++;
			if (*q == '\0' || flags & PM_MATCHLEFT) {
				if (!(flags & (PM_MATCHRIGHT | PM_MATCHMAX)))
					return q;
				if (flags & PM_MATCHLEFT) {
					r = q;
					break;
				}
				if (flags & PM_MATCHMAX || ap == pattern)
					return s;
				r = s;
				GETC_CTLESC(c, s, flags & PM_CTLESC);
				if (!c)
					return r;
				p = pattern;
				q = s;
				goto ast;
			}
			break;
		case '\\':
			if (*++p)
				goto dft0;
			else
				goto dft1;
		case '?':
			p++;
			GETC_CTLESC(chr, q, flags & PM_CTLESC);
			if (chr == '\0')
				break;
			continue;
		case '*':
			p++;
ast:
			ap = p;
			aq = q;
			continue;
		case '[': {
			char *startp;
			int invert, found;

			p++;
			startp = p;
			invert = 0;
			if (*p == '!') {
				invert++;
				p++;
			}
			found = 0;
			GETC_CTLESC(chr, q, flags & PM_CTLESC);
			if (chr == '\0')
				break;
			do {
				if (!*p) {
					p = startp;
					c = '[';
					goto dft2;
				}
				if (*p == '[') {
					char *r = NULL;

					found |= !!ccmatch(p, chr, &r);
					if (r) {
						p = r;
						continue;
					}
				}
				if (*p == '\\')
					p++;
				GETC_CTLESC(c, p, 1);
				if (*p == '-' && p[1] != ']') {
#ifndef WITH_LOCALE
					char c2;
#else
					int c2;
#endif
					p++;
					if (*p == '\\')
						p++;
					GETC_CTLESC(c2, p, 1);
#ifdef WITH_LOCALE
					if (chr < 0 || c < 0 || c2 < 0)
						continue;
#endif
					if (chr >= c && chr <= c2)
						found = 1;
				} else {
					if (chr == c)
						found = 1;
				}
			} while (*p != ']');
			p++;
			if (found == invert)
				break;
			continue;
		}
		default:
dft0:
			GETC_CTLESC(c, p, 1);
dft1:
			GETC_CTLESC(chr, q, flags & PM_CTLESC);
dft2:
			if (chr != c)
				break;
			continue;
		}

		if (ap != NULL && *aq != '\0') {
			GETC_CTLESC(c, aq, flags & PM_CTLESC);
			p = ap;
			q = aq;
			if (ap == pattern)
				s = q;
			continue;
		}

		return r;
	}
}



/*
 * Remove CTLESC and CTLQUOTEMARK characters from a string.
 * If glob is set, only CTLESC characters known not to be needed for
 * globbing are removed.
 */

char *
_rmescapes(char *str, int glob)
{
	char *p, *q, c;
	for (p = str, q = p;;) {
		switch (c = *p) {
		case (char)CTLQUOTEMARK:
			p++;
			continue;
		case (char)CTLESC:
			p++;
			if (glob && !is_in_name(*p))
				*q++ = c;
			c = *p;
			/* fall through */
		default:
			p++;
			*q++ = c;
			if (!c)
				return str;
		}
	}
}



/*
 * See if a pattern matches in a case statement.
 */

int
casematch(union node *pattern, char *val)
{
	struct stackmark smark;
	int result;

	setstackmark(&smark);
	argbackq = pattern->narg.backquote;
	STARTSTACKSTR(expdest);
	argstr(pattern->narg.text, EXP_TILDE | EXP_CASE);
	ifsfree();
	result = patmatch(stackblock(), val);
	popstackmark(&smark);
	return result;
}

/*
 * Our own itoa().
 */

STATIC int
cvtnum(intmax_t num, int flag)
{
	int ctlesc = num < 0 && flag & QUOTES_ESC && flag & EXP_QUOTED;
	int len = max_int_length(sizeof(num));

	expdest = makestrspace(ctlesc + len, expdest);
	if (ctlesc)
		*expdest++ = CTLESC;
	len = fmtstr(expdest, len, "%" PRIdMAX, num);
	STADJUST(len, expdest);
	return len;
}

void
varunset(const char *end, const char *var, const char *umsg, int varflags)
{
	const char *msg;
	const char *tail;

	tail = nullstr;
	msg = "parameter not set";
	if (umsg) {
		if (*end == (char)CTLENDVAR) {
			if (varflags & VSNUL)
				tail = " or null";
		} else
			msg = umsg;
	}
	sh_error("%.*s: %s%s", end - var - 1, var, msg, tail);
}

#ifdef mkinit

INCLUDE "expand.h"

RESET {
	ifsfree();
}

#endif
