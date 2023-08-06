/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2019, 2023
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

#include <stdlib.h>
#include "shell.h"
#include "input.h"
#include "output.h"
#include "error.h"
#include "memalloc.h"
#include "mystring.h"
#include "alias.h"
#include "options.h"	/* XXX for argptr (should remove?) */
#include "var.h"

#define ATABSIZE 39

struct alias *atab[ATABSIZE];

struct alias *aliasdone;

static void setalias(const char *, const char *);
static void freealias(struct alias ***);
static struct alias **__lookupalias(const char *);

static
void
setalias(const char *name, const char *val)
{
	struct alias *ap, **app;
	size_t namelen;

	app = __lookupalias(name);
	ap = *app;
	INTOFF;
	if (ap) {
		if (!(ap->flag & ALIASINUSE)) {
			ckfree(ap->name);
		}
		ap->flag &= ~ALIASDEAD;
	} else {
		/* not found */
		ap = ckmalloc(sizeof (struct alias));
		ap->flag = 0;
		ap->next = 0;
		*app = ap;
	}
	namelen = val - name;
	ap->name = savestr(name);
	ap->val = ap->name + namelen;
	INTON;
}

int
unalias(const char *name)
{
	struct alias **app;

	app = __lookupalias(name);

	if (*app) {
		INTOFF;
		freealias(&app);
		INTON;
		return (0);
	}

	return (1);
}

void
rmaliases(void)
{
	struct alias **app;
	int i;

	INTOFF;
	for (i = 0; i < ATABSIZE; i++) {
		app = &atab[i];
		while (*app)
			freealias(&app);
	}
	INTON;
}

struct alias *
lookupalias(const char *name, int check)
{
	struct alias *ap = *__lookupalias(name);

	if (check && ap && (ap->flag & ALIASINUSE))
		return (NULL);
	return (ap);
}

/*
 * TODO - sort output
 */
int
aliascmd(int argc, char **argv)
{
	char *n, *v;
	int ret = 0;
	struct alias *ap;

	nextopt(nullstr);
	argv = argptr;
	if (!*argv) {
		int i;

		for (i = 0; i < ATABSIZE; i++)
			for (ap = atab[i]; ap; ap = ap->next) {
				printalias(ap);
			}
		return (0);
	}
	while ((n = *argv) != NULL) {
		if (!*n || !(v = strchr(n+1, '='))) { /* n+1: funny ksh stuff */
			if ((ap = *__lookupalias(n)) == NULL) {
				outfmt(out2, "%s: %s not found\n", "alias", n);
				ret = 1;
			} else
				printalias(ap);
		} else {
			setalias(n, v + 1);
		}
		argv++;
	}

	return (ret);
}

int
unaliascmd(int argc, char **argv)
{
	int i;

	while ((i = nextopt("a")) != '\0') {
		if (i == 'a') {
			rmaliases();
			return (0);
		}
	}
	for (i = 0; *argptr; argptr++) {
		if (unalias(*argptr)) {
			outfmt(out2, "%s: %s not found\n", "unalias", *argptr);
			i = 1;
		}
	}

	return (i);
}

static void
freealias(struct alias ***appp) {
	struct alias **app = *appp, *ap = *app;

	if (ap->flag & ALIASINUSE) {
		ap->flag |= ALIASDEAD;
		*appp = &ap->next;
	} else {
		*app = ap->next;
		ckfree(ap->name);
		ckfree(ap);
	}
}

void
printalias(const struct alias *ap) {
	out1fmt("%s\n", shell_quote(ap->name, 0));
}


void
endaliasuse(void) {
	struct alias *ap = aliasdone;
	while (ap) {
		struct alias *nextdone = ap->nextdone;
		ap->flag &= ~ALIASINUSE;
		if (ap->flag & ALIASDEAD)
			unalias(ap->name);
		ap = nextdone;
	}
	aliasdone = NULL;
}


static struct alias **
__lookupalias(const char *name) {
	struct alias **app;

	app = &atab[hashval(name) % ATABSIZE];

	for (; *app; app = &(*app)->next) {
		if (varequal(name, (*app)->name)) {
			break;
		}
	}

	return app;
}
