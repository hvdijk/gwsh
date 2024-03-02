/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2024
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifdef WITH_LOCALE
#include <locale.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#endif

/*
 * Shell variables.
 */

#include "shell.h"
#include "output.h"
#include "expand.h"
#include "nodes.h"	/* for other headers */
#include "eval.h"
#include "exec.h"
#include "syntax.h"
#include "options.h"
#include "mail.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "mystring.h"
#include "parser.h"
#include "show.h"
#ifndef SMALL
#include "myhistedit.h"
#endif
#include "system.h"


#define VTABSIZE 39


struct localvar_list {
	struct localvar_list *next;
	struct localvar *lv;
};

MKINIT struct localvar_list *localvar_stack;

const char defpathvar[] =
	"PATH\0/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0";
char defps1var[] = "PS1\0$ \0";
char defifsvar[] = "IFS\0 \t\n\0";
MKINIT char defoptindvar[] = "OPTIND\0001\0";

int lineno;
char linenovar[sizeof("LINENO=")+sizeof(int)*CHAR_BIT/3+1] = "LINENO";

#ifdef WITH_LOCALE
static void changelocale(const char *val);
#endif

struct var varinit[] = {
	{ 0,	VSTRFIXED|VTEXTFIXED,		defifsvar,	0 },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET,	"MAIL\0\0\1",	changemail },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET,	"MAILPATH\0\0\1",changemail },
	{ 0,	VSTRFIXED|VTEXTFIXED,		defpathvar,	changepath },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET,	"FPATH\0\0\1",	0 },
	{ 0,	VSTRFIXED|VTEXTFIXED,		defps1var,	0 },
	{ 0,	VSTRFIXED|VTEXTFIXED,		"PS2\0> ",	0 },
	{ 0,	VSTRFIXED|VTEXTFIXED,		"PS4\0+ ",	0 },
	{ 0,	VSTRFIXED|VTEXTFIXED|VLATEFUNC,	defoptindvar,	getoptsreset },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET,	"PWD\0\0\1",	0 },
#ifdef WITH_LINENO
	{ 0,	VSTRFIXED|VTEXTFIXED,		linenovar,	0 },
#endif
#ifdef WITH_LOCALE
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET|VLATEFUNC,	"LC_ALL\0\0\1",		changelocale },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET|VLATEFUNC,	"LC_COLLATE\0\0\1",	changelocale },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET|VLATEFUNC,	"LC_CTYPE\0\0\1",	changelocale },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET|VLATEFUNC,	"LC_MESSAGES\0\0\1",	changelocale },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET|VLATEFUNC,	"LC_NUMERIC\0\0\1",	changelocale },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET|VLATEFUNC,	"LANG\0\0\1",		changelocale },
#endif
#ifndef SMALL
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET,	"TERM\0\0\1",		0 },
	{ 0,	VSTRFIXED|VTEXTFIXED|VUNSET,	"HISTSIZE\0\0\1",	sethistsize },
#endif
};

static struct var *vartab[VTABSIZE];

static struct var **hashvar(const char *);
static struct var **findvar(struct var **, const char *);

#ifndef WITH_LOCALE
#define vpcmp pstrcmp
#else
static int vpcmp(const void *, const void *);
#endif

/*
 * Initialize the varable symbol tables and import the environment
 */

#ifdef mkinit
INCLUDE <unistd.h>
INCLUDE <sys/types.h>
INCLUDE <sys/stat.h>
INCLUDE "cd.h"
INCLUDE "input.h"
INCLUDE "output.h"
INCLUDE "var.h"
MKINIT char **environ;
INIT {
	char **envp;
	static char ppid[32] = "PPID";

	initvar();
	for (envp = environ ; *envp ; envp++) {
		const char *p = endofname(*envp);
		if (p != *envp && *p == '=') {
			setvareq(*envp, VEXPORT);
		}
	}

	setvareq(defifsvar, VTEXTFIXED);
	setvareq(defoptindvar, VTEXTFIXED);

	fmtstr(ppid + 5, sizeof(ppid) - 5, "%ld", (long) getppid());
	setvareq(ppid, VTEXTFIXED);

	setpwd(getpwd(0), 0);
	freepwd();
#ifdef WITH_PARSER_LOCALE
	parselocale = duplocale(LC_GLOBAL_LOCALE);
#endif
}

RESET {
	unwindlocalvars(0, sub);
}
#endif


/*
 * This routine initializes the builtin variables.  It is called when the
 * shell is initialized.
 */

void
initvar(void)
{
	struct var *vp;
	struct var *end;
	struct var **vpp;

	vp = varinit;
	end = vp + sizeof(varinit) / sizeof(varinit[0]);
	do {
		vpp = hashvar(vp->text);
		vp->next = *vpp;
		*vpp = vp;
	} while (++vp < end);
}

/*
 * Set the value of a variable.  The flags argument is ored with the
 * flags of the variable.  If val is NULL, the variable is unset.
 */

struct var *setvar(const char *name, const char *val, int flags)
{
	char *p, *q;
	size_t namelen;
	char *nameeq;
	size_t vallen;
	struct var *vp;

	q = endofname(name);
	p = strchrnul(q, '=');
	namelen = p - name;
	if (!namelen || p != q)
		sh_error("%.*s: bad variable name", namelen, name);
	vallen = 0;
	if (val == NULL) {
		flags |= VUNSET;
	} else {
		vallen = strlen(val);
	}
	INTOFF;
	nameeq = ckmalloc(namelen + vallen + 3);
	p = mempcpy(nameeq, name, namelen);
	*p++ = '\0';
	if (val)
		p = mempcpy(p, val, vallen);
	*p++ = '\0';
	*p = !val;
	vp = setvareq(nameeq, flags | VNOSAVE);
	INTON;

	return vp;
}

/*
 * Set the given integer as the value of a variable.  The flags argument is
 * ored with the flags of the variable.
 */

intmax_t setvarint(const char *name, intmax_t val, int flags)
{
	int len = max_int_length(sizeof(val));
	char buf[len];

	fmtstr(buf, len, "%" PRIdMAX, val);
	setvar(name, buf, flags);
	return val;
}



/*
 * Same as setvar except that the variable and value are passed in
 * the first argument. The form of s depends on the provided flags:
 * if flags does not include any of VTEXTFIXED, VSTACK, or VNOSAVE,
 * s must be in the form name=value. If flags does include any of those,
 * s may alternatively be in the form name\0value, must regardless have
 * an extra byte after the value if there is a chance the value will be
 * printed by set, export -p, or readonly -p, and will be stored in the
 * table without making a copy so must not be a string that will go away.
 * Called with interrupts off.
 */

struct var *setvareq(char *s, int flags)
{
	struct var *vp, **vpp;
	int saveflags = flags;

	if (aflag && !(flags & VUNSET))
		flags |= VEXPORT;

	vpp = hashvar(s);
	vpp = findvar(vpp, s);
	vp = *vpp;
	if (vp) {
		if (vp->flags & VREADONLY) {
			const char *n;

			if (flags & VNOSAVE)
				free(s);
			n = vp->text;
			sh_error("%s: is read only", n);
		}

		if (flags & VNOSET)
			goto out;

		if (vp->func && (flags & VNOFUNC) == 0 && (vp->flags & VLATEFUNC) == 0)
			(*vp->func)(strchrnul(s, '=') + 1);

		if ((vp->flags & (VTEXTFIXED|VSTACK)) == 0)
			ckfree(vp->text);

		flags |= vp->flags & ~(VTEXTFIXED|VSTACK|VNOSAVE|VUNSET|VUSER1);

		if ((saveflags & (VEXPORT|VREADONLY|VUNSET)) == VUNSET) {
			if (!(flags & VSTRFIXED)) {
				*vpp = vp->next;
				ckfree(vp);
out_free:
				if ((flags & (VTEXTFIXED|VSTACK|VNOSAVE)) == VNOSAVE)
					ckfree(s);
				return 0;
			}
			flags &= ~(VEXPORT|VREADONLY);
		}
	} else {
		if (flags & VNOSET)
			goto out;
		if ((flags & (VEXPORT|VREADONLY|VSTRFIXED|VUNSET)) == VUNSET)
			goto out_free;
		/* not found */
		vp = ckmalloc(sizeof (*vp));
		vp->local = NULL;
		vp->next = *vpp;
		vp->func = NULL;
		*vpp = vp;
	}
	if (!(flags & (VTEXTFIXED|VSTACK|VNOSAVE))) {
		size_t len = strlen(s);
		char *d = ckmalloc(len + 2);
		*(char *) mempcpy(d, s, len + 1) = 0;
		s = d;
	}
	vp->text = s;
	vp->flags = flags;
	s = strchrnul(s, '=');
	*s = '\0';
	if (vp->func && flags & VLATEFUNC)
		(*vp->func)(s + 1);
out:
	return vp;
}



/*
 * Process a linked list of variable assignments.
 */

void
listsetvar(struct strlist *list, int flags)
{
	struct strlist *lp;

	lp = list;
	if (!lp)
		return;
	INTOFF;
	do {
		setvareq(lp->text, flags);
	} while ((lp = lp->next));
	INTON;
}


/*
 * Find the value of a variable.  Returns NULL if not set.
 */

char *
lookupvar(const char *name)
{
	struct var *v;

	if ((v = *findvar(hashvar(name), name)) && !(v->flags & VUNSET)) {
#ifdef WITH_LINENO
		if (v == &vlineno && v->text == linenovar) {
			fmtstr(linenovar+7, sizeof(linenovar)-7, "%d", lineno);
		}
#endif
		return strchr(v->text, '\0') + 1;
	}
	return NULL;
}

intmax_t lookupvarint(const char *name)
{
	const char *val = lookupvar(name);
	if (!val) {
		if (uflag)
			varunset(name, name, 0, 0);
		return 0;
	}
	return atomax(val, NULL, 0);
}



/*
 * Generate a list of variables satisfying the given conditions.
 */

char **
listvars(int on, int off, char ***end)
{
	struct var **vpp;
	struct var *vp;
	char **ep;
	int mask;

	STARTSTACKSTR(ep);
	vpp = vartab;
	mask = on | off;
	do {
		for (vp = *vpp ; vp ; vp = vp->next)
			if ((vp->flags & mask) == on) {
				if (ep == stackstrend())
					ep = growstackstr();
				*ep++ = (char *) vp->text;
			}
	} while (++vpp < vartab + VTABSIZE);
	if (ep == stackstrend())
		ep = growstackstr();
	if (end)
		*end = ep;
	*ep++ = NULL;
	return grabstackstr(ep);
}



/*
 * POSIX requires that 'set' (but not export or readonly) output the
 * variables in lexicographic order - by the locale's collating order (sigh).
 * Maybe we could keep them in an ordered balanced binary tree
 * instead of hashed lists.
 * For now just roll 'em through qsort for printing...
 */

int
showvars(const char *prefix, int on, int off)
{
	const char *sep;
	char **ep, **epend;

	ep = listvars(on, off, &epend);
	qsort(ep, epend - ep, sizeof(char *), vpcmp);

	sep = *prefix ? spcstr : prefix;

	for (; ep < epend; ep++) {
		const char *fmt;
		const char *val = strchr(*ep, '\0') + 1;
		if (!*val && val[1]) {
			fmt = "%s%s%s\n";
		} else {
			fmt = "%s%s%s=%s\n";
			val = shell_quote(val, 0);
		}
		out1fmt(fmt, prefix, sep, *ep, val);
	}

	return 0;
}



/*
 * The export and readonly commands.
 */

int
exportcmd(int argc, char **argv)
{
	struct var *vp;
	char *name;
	const char *p;
	char **aptr;
	int flag = argv[0][0] == 'r' ? VREADONLY : VEXPORT;
	int notp;

	notp = nextopt("p") - 'p';
	if (notp && ((name = *(aptr = argptr)))) {
		do {
			if ((p = strchr(name, '=')) != NULL) {
				p++;
			} else {
				if ((vp = *findvar(hashvar(name), name))) {
					vp->flags |= flag;
					continue;
				}
			}
			setvar(name, p, flag);
		} while ((name = *++aptr) != NULL);
	} else {
		showvars(argv[0], flag, 0);
	}
	return 0;
}


/*
 * The "local" command.
 */

int
localcmd(int argc, char **argv)
{
	char *name;

	if (!funcnest)
		sh_error("not in a function");

	nextopt(nullstr);
	argv = argptr;
	while ((name = *argv++) != NULL) {
		mklocal(name);
	}
	return 0;
}


/*
 * Make a variable a local variable.  When a variable is made local, it's
 * value and flags are saved in a localvar structure.  The saved values
 * will be restored when the shell function returns.  We handle the name
 * "-" as a special case.
 */

void
mklocal(char *name)
{
	struct var **vpp;
	struct var *vp;
	struct localvar_list *vpl;
	struct localvar *lvp;
	int opts, eq;

	INTOFF;

	opts = name[0] == '-' && name[1] == '\0';
	if (opts) {
		if (unlikely(!localvar_stack))
			goto out;
	} else {
		eq = strchr(name, '=') != NULL;
		vpp = hashvar(name);
		vpp = findvar(vpp, name);
		vp = *vpp;
		vpl = vp == NULL ? NULL : vp->local;

		if (unlikely(vpl == localvar_stack))
			goto setvar;
	}

	lvp = ckmalloc(sizeof (struct localvar));
	lvp->next = localvar_stack->lv;
	localvar_stack->lv = lvp;

	if (opts) {
		char *p;
		p = ckmalloc(sizeof(optlist));
		lvp->vp = NULL;
		lvp->text = memcpy(p, optlist, sizeof(optlist));
	} else if (vp == NULL) {
		lvp->flags = VUNSET;
		lvp->local = NULL;
		if (eq)
			vp = setvareq(name, VSTRFIXED);
		else
			vp = setvar(name, NULL, VSTRFIXED);
		lvp->vp = vp;
		vp->local = localvar_stack;
	} else {
		lvp->vp = vp;
		lvp->flags = vp->flags;
		lvp->text = vp->text;
		lvp->local = vp->local;
		vp->flags |= VSTRFIXED|VTEXTFIXED;
		vp->local = localvar_stack;
setvar:
		if (eq)
			setvareq(name, 0);
	}
out:
	INTON;
}


/*
 * Called after a function returns.
 * Interrupts must be off.
 */

void
poplocalvars(int keep)
{
	struct localvar_list *ll;
	struct localvar *lvp, *next;
	struct var *vp;

	INTOFF;
	ll = localvar_stack;
	localvar_stack = ll->next;

	next = ll->lv;
	ckfree(ll);

	while ((lvp = next) != NULL) {
		next = lvp->next;
		vp = lvp->vp;
		TRACE(("poplocalvar %s\n", vp ? vp->text : "-"));
		if (keep) {
			int bits = VSTRFIXED;

			if (lvp->flags != VUNSET) {
				if (vp->text == lvp->text)
					bits |= VTEXTFIXED;
				else if (!(lvp->flags & (VTEXTFIXED|VSTACK)))
					ckfree(lvp->text);
			}

			vp->local = lvp->local;
			vp->flags &= ~bits;
			vp->flags |= (lvp->flags & bits);

			if ((vp->flags &
			     (VEXPORT|VREADONLY|VSTRFIXED|VUNSET)) == VUNSET)
				unsetvar(vp->text);
		} else if (vp == NULL) {	/* $- saved */
			memcpy(optlist, lvp->text, sizeof(optlist));
			ckfree(lvp->text);
			optschanged();
		} else {
			vp->local = lvp->local;
			if (lvp->flags == VUNSET) {
				vp->flags &= ~(VSTRFIXED|VREADONLY);
				unsetvar(vp->text);
			} else {
				if (vp->func)
					(*vp->func)(strchr(lvp->text, '\0') + 1);
				if ((vp->flags & (VTEXTFIXED|VSTACK)) == 0)
					ckfree(vp->text);
				vp->flags = lvp->flags;
				vp->text = lvp->text;
				if (vp->func && vp->flags & VLATEFUNC)
					(*vp->func)(strchr(lvp->text, '\0') + 1);
			}
		}
		ckfree(lvp);
	}
	INTON;
}


/*
 * Create a new localvar environment.
 */
struct localvar_list *pushlocalvars(void)
{
	struct localvar_list *ll;

	INTOFF;
	ll = ckmalloc(sizeof(*ll));
	ll->lv = NULL;
	ll->next = localvar_stack;
	localvar_stack = ll;
	INTON;

	return ll->next;
}


void unwindlocalvars(struct localvar_list *stop, int keep)
{
	while (localvar_stack != stop)
		poplocalvars(keep);
}


/*
 * The unset builtin command.  We unset the function before we unset the
 * variable to allow a function to be unset when there is a readonly variable
 * with the same name.
 */

int
unsetcmd(int argc, char **argv)
{
	char **ap;
	int i;
	int flag = 0;

	while ((i = nextopt("vf")) != '\0') {
		flag = i;
	}

	for (ap = argptr; *ap ; ap++) {
		if (flag != 'f') {
			unsetvar(*ap);
			continue;
		}
		if (flag != 'v')
			unsetfunc(*ap);
	}
	return 0;
}


/*
 * Unset the specified variable.
 */

void unsetvar(const char *s)
{
	setvar(s, 0, 0);
}



/*
 * Find the appropriate entry in the hash table from the name.
 */

static struct var **
hashvar(const char *p)
{
	return &vartab[hashval(p) % VTABSIZE];
}



/*
 * Compares two strings up to the first = or '\0'.
 */

int
varcmp(const char *p, const char *q)
{
	int c = *p, d = *q;
	while (c == d) {
		if (!c)
			break;
		p++;
		q++;
		c = *p;
		d = *q;
		if (c == '=')
			c = '\0';
		if (d == '=')
			d = '\0';
	}
	return c - d;
}

#ifdef WITH_LOCALE
static int
vpcmp(const void *a, const void *b)
{
	const char *pa = *(const char **)a;
	const char *pb = *(const char **)b;
	return strcoll(pa, pb);
}
#endif

static struct var **
findvar(struct var **vpp, const char *name)
{
	for (; *vpp; vpp = &(*vpp)->next) {
		if (varequal((*vpp)->text, name)) {
			break;
		}
	}
	return vpp;
}


#ifdef WITH_LOCALE

static
void changelocale(const char *var) {
	setlocale(LC_CTYPE, *lc_allval() ? lc_allval() : *lc_ctypeval() ? lc_ctypeval() : langval());
	setlocale(LC_COLLATE, *lc_allval() ? lc_allval() : *lc_collateval() ? lc_collateval() : langval());
	setlocale(LC_NUMERIC, *lc_allval() ? lc_allval() : *lc_numericval() ? lc_numericval() : langval());
	setlocale(LC_MESSAGES, *lc_allval() ? lc_allval() : *lc_messagesval() ? lc_messagesval() : langval());
}

#endif
