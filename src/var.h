/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2023
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
 *	@(#)var.h	8.2 (Berkeley) 5/4/95
 */

#ifndef H_VAR
#define H_VAR 1

#include "config.h"

#include <inttypes.h>

/*
 * Shell variables.
 */

/* flags */
#define VEXPORT		0x01	/* variable is exported */
#define VREADONLY	0x02	/* variable cannot be modified */
#define VSTRFIXED	0x04	/* variable struct is statically allocated */
#define VTEXTFIXED	0x08	/* text is statically allocated */
#define VSTACK		0x10	/* text is allocated on the stack */
#define VUNSET		0x20	/* the variable is not set */
#define VNOFUNC		0x40	/* don't call the callback function */
#define VNOSAVE		0x100	/* when text is on the heap before setvareq */
#define VLATEFUNC	0x200	/* call the callback function after the value has been changed */
#define VUSER1		0x400	/* special flag with per-variable meaning
				   OPTIND: extra data is stored after the option text */


struct var {
	struct var *next;		/* next entry in hash list */
	int flags;			/* flags are defined above */
	const char *text;		/* name=value */
	void (*func)(const char *);
					/* function to be called when  */
					/* the variable gets set/unset */
	struct localvar_list *local;
};


struct localvar {
	struct localvar *next;		/* next local variable in list */
	struct var *vp;			/* the variable that was made local */
	int flags;			/* saved flags */
	const char *text;		/* saved text */
	struct localvar_list *local;	/* saved localvar list */
};

struct localvar_list;


extern struct localvar *localvars;
extern struct var varinit[];

enum {
	VIFS,
	VMAIL,
	VMPATH,
	VPATH,
	VFPATH,
	VPS1,
	VPS2,
	VPS4,
	VOPTIND,
	VPWD,
#ifdef WITH_LINENO
	VLINENO,
#endif
#ifdef WITH_LOCALE
	VLC_ALL,
	VLC_COLLATE,
	VLC_CTYPE,
	VLC_MESSAGES,
	VLC_NUMERIC,
	VLANG,
#endif
#ifndef SMALL
	VTERM,
	VHISTSIZE,
#endif
};

#define vifs varinit[VIFS]
#define vmail varinit[VMAIL]
#define vmpath varinit[VMPATH]
#define vpath varinit[VPATH]
#define vfpath varinit[VFPATH]
#define vps1 varinit[VPS1]
#define vps2 varinit[VPS2]
#define vps4 varinit[VPS4]
#define voptind varinit[VOPTIND]
#define vpwd varinit[VPWD]
#ifdef WITH_LINENO
#define vlineno varinit[VLINENO]
#endif
#ifdef WITH_LOCALE
#define vlc_all varinit[VLC_ALL]
#define vlc_collate varinit[VLC_COLLATE]
#define vlc_ctype varinit[VLC_CTYPE]
#define vlc_messages varinit[VLC_MESSAGES]
#define vlc_numeric varinit[VLC_NUMERIC]
#define vlang varinit[VLANG]
#endif
#ifndef SMALL
#define vterm varinit[VTERM]
#define vhistsize varinit[VHISTSIZE]
#endif

extern char defifsvar[];
#define defifs (defifsvar + 4)
extern const char defpathvar[];
#define defpath (defpathvar + 36)
extern char defps1var[];

extern int lineno;
extern char linenovar[];

/*
 * The following macros access the values of the above variables.
 * They have to skip over the name.  They return the null string
 * for unset variables.
 */

#define ifsval()	(vifs.text + 4)
#define ifsset()	((vifs.flags & VUNSET) == 0)
#define mailval()	(vmail.text + 5)
#define mpathval()	(vmpath.text + 9)
#define mpathset()	((vmpath.flags & VUNSET) == 0)
#define pathval()	(vpath.text + 5)
#define pathset()	((vpath.flags & VUNSET) == 0)
#define fpathval()	(vfpath.text + 6)
#define fpathset()	((vfpath.flags & VUNSET) == 0)
#define ps1val()	(vps1.text + 4)
#define ps2val()	(vps2.text + 4)
#define ps4val()	(vps4.text + 4)
#define optindval()	(voptind.text + 7)
#define pwdval()	(vpwd.text + 4)
#ifdef WITH_LINENO
#define linenoval()	(vlineno.text + 7)
#endif
#ifdef WITH_LOCALE
#define lc_allval()	(vlc_all.text + 7)
#define lc_collateval()	(vlc_collate.text + 11)
#define lc_ctypeval()	(vlc_ctype.text + 9)
#define lc_messagesval()	(vlc_messages.text + 12)
#define lc_numericval()	(vlc_numeric.text + 11)
#define langval()	(vlang.text + 5)
#endif
#ifndef SMALL
#define histsizeval()	(vhistsize.text + 9)
#define termval()	(vterm.text + 5)
#endif

void initvar(void);
struct var *setvar(const char *name, const char *val, int flags);
intmax_t setvarint(const char *, intmax_t, int);
struct var *setvareq(char *s, int flags);
struct strlist;
void listsetvar(struct strlist *, int);
char *lookupvar(const char *);
intmax_t lookupvarint(const char *);
char **listvars(int, int, char ***);
#define environment() listvars(VEXPORT, VUNSET, 0)
int showvars(const char *, int, int);
int exportcmd(int, char **);
int localcmd(int, char **);
void mklocal(char *);
struct localvar_list *pushlocalvars(void);
void poplocalvars(int);
void unwindlocalvars(struct localvar_list *stop, int keep);
int unsetcmd(int, char **);
void unsetvar(const char *);
int varcmp(const char *, const char *);

static inline unsigned int
hashval(const char *p)
{
	unsigned int hashval;

	hashval = ((unsigned char) *p) << 4;
	while (*p) {
		hashval += (unsigned char) *p++;
		if (*p == '=')
			break;
	}

	return hashval;
}


static inline int
varequal(const char *a, const char *b)
{
	return !varcmp(a, b);
}

/*
 * Search the environment of a builtin command.
 */

static inline char *
bltinlookup(const char *name)
{
	return lookupvar(name);
}

#endif
