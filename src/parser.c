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

#if HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <stdlib.h>

#ifdef WITH_LOCALE
#include <wchar.h>
#endif

#include "shell.h"
#include "parser.h"
#include "nodes.h"
#include "expand.h"	/* defines rmescapes() */
#include "exec.h"	/* defines find_builtin() */
#include "syntax.h"
#include "options.h"
#include "input.h"
#include "output.h"
#include "var.h"
#include "error.h"
#include "memalloc.h"
#include "mystring.h"
#include "alias.h"
#include "show.h"
#include "builtins.h"
#include "system.h"
#ifndef SMALL
#include "myhistedit.h"
#endif

/*
 * Shell command parser.
 */

/* values returned by readtoken */
#include "token_vars.h"



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



struct heredoc {
	struct heredoc *next;	/* next here document in list */
	union node *here;		/* redirection node */
	char *eofmark;		/* string indicating end of input */
	int striptabs;		/* if set, strip leading tabs */
};



struct heredoc *heredoclist;	/* list of here documents to read */
int doprompt;			/* if set, prompt the user */
int needprompt;			/* true if interactive and at start of line */
#ifndef SMALL
const char *lastprompt;		/* the last prompt */
#endif
int lasttoken;			/* last token read */
int tokpushback;		/* last token pushed back */
char *wordtext;			/* text of last word returned by readtoken */
int checkkwd;
struct nodelist *backquotelist;
union node *redirnode;
struct heredoc *heredoc;
int quoteflag;			/* set if (part of) last token was quoted */


STATIC union node *list(int);
STATIC union node *andor(void);
STATIC union node *pipeline(void);
STATIC union node *command(void);
STATIC union node *simplecmd(void);
STATIC union node *makename(void);
STATIC void parsefname(void);
STATIC void parseheredoc(void);
STATIC int readtoken(void);
STATIC int xxreadtoken(void);
STATIC int pgetc_eatbnl(void);
STATIC int readtoken1(int, char *, int);
STATIC void synerror(const char *) attribute((noreturn));
STATIC void setprompt(int);


/*
 * Read and parse a command.  Returns NEOF on end of file.  (NULL is a
 * valid parse tree indicating a blank line.)
 */

union node *
parsecmd(int interact)
{
	union node *cmd;

#ifdef WITH_PARSER_LOCALE
	uselocale(parselocale);
#endif
	tokpushback = 0;
	checkkwd = 0;
	heredoclist = 0;
	doprompt = interact;
	if (doprompt) {
		plinno = 1;
		setprompt(doprompt);
	}
	needprompt = 0;
#ifndef SMALL
	if (histop == H_APPEND)
		histop = H_ENTER;
#endif
	cmd = list(1);
#ifdef WITH_PARSER_LOCALE
	uselocale(LC_GLOBAL_LOCALE);
#endif
	return cmd;
}


STATIC union node *
list(int nlflag)
{
	union node *n1, *n2, *n3;
	int tok;

	n1 = NULL;
	for (;;) {
		checkkwd = (nlflag & 1 ? 0 : CHKNL) | CHKKWD | CHKALIAS;
		switch (readtoken()) {
		case TNL:
			parseheredoc();
			return n1;

		case TEOF:
			if (nlflag & 1) {
				if (!n1)
					n1 = NEOF;
				parseheredoc();
			}
			tokpushback++;
			lasttoken = TEOF;
			return n1;
		}

		tokpushback++;
		if (nlflag == 2 && tokendlist[lasttoken])
			return n1;
		nlflag |= 2;

		n2 = andor();
		tok = readtoken();
		if (tok == TBACKGND) {
			if (n2->type == NPIPE) {
				n2->npipe.backgnd = 1;
			} else {
				if (n2->type != NREDIR) {
					n3 = stalloc(sizeof(struct nredir));
					n3->nredir.n = n2;
					n3->nredir.redirect = NULL;
					n2 = n3;
				}
				n2->type = NBACKGND;
			}
		}
		if (n1 == NULL) {
			n1 = n2;
		}
		else {
			n3 = (union node *)stalloc(sizeof (struct nbinary));
			n3->type = NSEMI;
			n3->nbinary.ch1 = n1;
			n3->nbinary.ch2 = n2;
			n1 = n3;
		}
		switch (tok) {
		case TNL:
		case TEOF:
			tokpushback++;
			/* fall through */
		case TBACKGND:
		case TSEMI:
			break;
		default:
			if ((nlflag & 1))
				synexpect(-1);
			tokpushback++;
			return n1;
		}
	}
}



STATIC union node *
andor(void)
{
	union node *n;
	union node **np = &n;

	for (;;) {
		int t;
		union node *n2;

		*np = pipeline();

		if ((t = readtoken()) == TAND)
			t = NAND;
		else if (t == TOR)
			t = NOR;
		else
			break;

		n2 = stalloc(sizeof (struct nbinary));
		n2->type = t;
		n2->nbinary.ch1 = n;
		n = n2;
		np = &n->nbinary.ch2;
		checkkwd = CHKNL | CHKKWD | CHKALIAS;
	}

	tokpushback++;
	return n;
}



STATIC union node *
pipeline(void)
{
	union node *n;
	union node **np = &n;
	struct nodelist *lp = NULL, *prev;
	int negate;

	negate = 0;
	TRACE(("pipeline: entered\n"));
	if (readtoken() == TNOT) {
		negate = !negate;
		checkkwd = CHKKWD | CHKALIAS;
	} else
		tokpushback++;
	for (;;) {
		*np = command();
		if (readtoken() != TPIPE) {
			tokpushback++;
			break;
		}
		if (!lp) {
			union node *pipenode = stalloc(sizeof (struct npipe));
			pipenode->type = NPIPE;
			pipenode->npipe.backgnd = 0;
			lp = stalloc(sizeof (struct nodelist));
			pipenode->npipe.cmdlist = lp;
			lp->n = n;
			n = pipenode;
		}
		prev = lp;
		lp = stalloc(sizeof (struct nodelist));
		lp->next = NULL;
		prev->next = lp;
		np = &lp->n;
		checkkwd = CHKNL | CHKKWD | CHKALIAS;
	}
	if (negate) {
		union node *n2 = stalloc(sizeof (struct nnot));
		n2->type = NNOT;
		n2->nnot.com = n;
		n = n2;
	}
	return n;
}



STATIC union node *
command(void)
{
	union node *n1, *n2;
	union node *ap, **app;
	union node *cp, **cpp;
	union node *redir, **rpp;
	union node **rpp2;
	int t;
	int savelinno;

	redir = NULL;
	rpp2 = &redir;

	savelinno = plinno;

	switch (readtoken()) {
	default:
		synexpect(-1);
		/* NOTREACHED */
	case TIF:
		n1 = (union node *)stalloc(sizeof (struct nif));
		n1->type = NIF;
		n1->nif.test = list(0);
		if (readtoken() != TTHEN)
			synexpect(TTHEN);
		n1->nif.ifpart = list(0);
		n2 = n1;
		while (readtoken() == TELIF) {
			n2->nif.elsepart = (union node *)stalloc(sizeof (struct nif));
			n2 = n2->nif.elsepart;
			n2->type = NIF;
			n2->nif.test = list(0);
			if (readtoken() != TTHEN)
				synexpect(TTHEN);
			n2->nif.ifpart = list(0);
		}
		if (lasttoken == TELSE)
			n2->nif.elsepart = list(0);
		else {
			n2->nif.elsepart = NULL;
			tokpushback++;
		}
		t = TFI;
		break;
	case TWHILE:
	case TUNTIL: {
		int got;
		n1 = (union node *)stalloc(sizeof (struct nbinary));
		n1->type = (lasttoken == TWHILE)? NWHILE : NUNTIL;
		n1->nbinary.ch1 = list(0);
		if ((got=readtoken()) != TDO) {
TRACE(("expecting DO got %s %s\n", tokname[got], got == TWORD ? wordtext : ""));
			synexpect(TDO);
		}
		n1->nbinary.ch2 = list(0);
		t = TDONE;
		break;
	}
	case TFOR:
		if (readtoken() != TWORD || quoteflag || ! goodname(wordtext))
			synerror("Bad for loop variable");
		n1 = (union node *)stalloc(sizeof (struct nfor));
		n1->type = NFOR;
		n1->nfor.linno = savelinno;
		n1->nfor.var = wordtext;
		checkkwd = CHKNL | CHKKWD | CHKALIAS;
		if (readtoken() == TIN) {
			app = &ap;
			while (readtoken() == TWORD) {
				n2 = (union node *)stalloc(sizeof (struct narg));
				n2->type = NARG;
				n2->narg.text = wordtext;
				n2->narg.backquote = backquotelist;
				*app = n2;
				app = &n2->narg.next;
			}
			*app = NULL;
			n1->nfor.args = ap;
			if (lasttoken != TNL && lasttoken != TSEMI)
				synexpect(-1);
		} else {
			n2 = (union node *)stalloc(sizeof (struct narg));
			n2->type = NARG;
			n2->narg.text = (char *)dolatstr;
			n2->narg.backquote = NULL;
			n2->narg.next = NULL;
			n1->nfor.args = n2;
			/*
			 * Newline or semicolon here is optional (but note
			 * that the original Bourne shell only allowed NL).
			 */
			if (lasttoken != TSEMI)
				tokpushback++;
		}
		checkkwd = CHKNL | CHKKWD | CHKALIAS;
		if (readtoken() != TDO)
			synexpect(TDO);
		n1->nfor.body = list(0);
		t = TDONE;
		break;
	case TCASE:
		n1 = (union node *)stalloc(sizeof (struct ncase));
		n1->type = NCASE;
		n1->ncase.linno = savelinno;
		if (readtoken() != TWORD)
			synexpect(TWORD);
		n1->ncase.expr = n2 = (union node *)stalloc(sizeof (struct narg));
		n2->type = NARG;
		n2->narg.text = wordtext;
		n2->narg.backquote = backquotelist;
		n2->narg.next = NULL;
		checkkwd = CHKNL | CHKKWD | CHKALIAS;
		if (readtoken() != TIN)
			synexpect(TIN);
		cpp = &n1->ncase.cases;
next_case:
		checkkwd = CHKNL | CHKKWD;
		t = readtoken();
		switch (t) {
		case TLP:
			t = readtoken();
			/* fall through */
		default:
			if (t < TWORD)
need_word:
				synexpect(TWORD);
			*cpp = cp = (union node *)stalloc(sizeof (struct nclist));
			cp->type = NCLIST;
			app = &cp->nclist.pattern;
			for (;;) {
				*app = ap = (union node *)stalloc(sizeof (struct narg));
				ap->type = NARG;
				ap->narg.text = wordtext;
				ap->narg.backquote = backquotelist;
				t = readtoken();
				switch (t) {
				case TPIPE:
					app = &ap->narg.next;
					t = readtoken();
					if (t < TWORD)
						goto need_word;
					continue;
				default:
					synexpect(TRP);
				case TRP:
					break;
				}
				break;
			}
			ap->narg.next = NULL;
			cp->nclist.body = list(2);

			cpp = &cp->nclist.next;

			checkkwd = CHKNL | CHKKWD;
			t = readtoken();
			switch (t) {
			default:
				synexpect(TENDCASE);
			case TENDCASEFT:
				cp->type++;
			case TENDCASE:
				goto next_case;
			case TESAC:
				break;
			}
			break;
		case TESAC:
			break;
		}
		*cpp = NULL;
		goto redir;
	case TLP:
		n1 = (union node *)stalloc(sizeof (struct nredir));
		n1->type = NSUBSHELL;
		n1->nredir.linno = savelinno;
		n1->nredir.n = list(0);
		n1->nredir.redirect = NULL;
		t = TRP;
		break;
	case TBEGIN:
		n1 = list(0);
		t = TEND;
		break;
	case TWORD:
	case TREDIR:
		tokpushback++;
		return simplecmd();
	}

	if (readtoken() != t)
		synexpect(t);

redir:
	/* Now check for redirection which may follow command */
	checkkwd = CHKKWD | CHKALIAS;
	rpp = rpp2;
	while (readtoken() == TREDIR) {
		*rpp = n2 = redirnode;
		rpp = &n2->nfile.next;
		parsefname();
	}
	tokpushback++;
	*rpp = NULL;
	if (redir) {
		if (n1->type != NSUBSHELL || n1->nredir.redirect) {
			n2 = (union node *)stalloc(sizeof (struct nredir));
			n2->type = NREDIR;
			n2->nredir.linno = savelinno;
			n2->nredir.n = n1;
			n1 = n2;
		}
		n1->nredir.redirect = redir;
	}

	return n1;
}


STATIC union node *
simplecmd(void) {
	union node *args, **app;
	union node *n = NULL;
	union node *vars, **vpp;
	union node **rpp, *redir;
	int savecheckkwd;
	int savelinno;

	args = NULL;
	app = &args;
	vars = NULL;
	vpp = &vars;
	redir = NULL;
	rpp = &redir;

	savecheckkwd = CHKALIAS;
	savelinno = plinno;
	for (;;) {
		switch (readtoken()) {
		case TWORD:
			n = (union node *)stalloc(sizeof (struct narg));
			n->type = NARG;
			n->narg.text = wordtext;
			n->narg.backquote = backquotelist;
			if (savecheckkwd && isassignment(wordtext)) {
				*vpp = n;
				vpp = &n->narg.next;
			} else {
				*app = n;
				app = &n->narg.next;
				savecheckkwd = 0;
			}
			break;
		case TREDIR:
			*rpp = n = redirnode;
			rpp = &n->nfile.next;
			parsefname();	/* read name of redirection file */
			break;
		case TLP:
			if (
				args && app == &args->narg.next &&
				!vars && !redir
			) {
				struct builtincmd *bcmd;
				const char *name;

				/* We have a function */
				if (readtoken() != TRP)
					synexpect(TRP);
				name = n->narg.text;
				if (
					!goodname(name) || (
						(bcmd = find_builtin(name)) &&
						bcmd->flags & BUILTIN_SPECIAL
					)
				)
					synerror("Bad function name");
				n->type = NDEFUN;
				checkkwd = CHKNL | CHKKWD | CHKALIAS;
				n->ndefun.text = n->narg.text;
				n->ndefun.linno = plinno;
				n->ndefun.body = command();
				return n;
			}
			/* fall through */
		default:
			tokpushback++;
			goto out;
		}
		checkkwd = savecheckkwd;
	}
out:
	*app = NULL;
	*vpp = NULL;
	*rpp = NULL;
	n = (union node *)stalloc(sizeof (struct ncmd));
	n->type = NCMD;
	n->ncmd.linno = savelinno;
	n->ncmd.args = args;
	n->ncmd.assign = vars;
	n->ncmd.redirect = redir;
	return n;
}

STATIC union node *
makename(void)
{
	union node *n;

	n = (union node *)stalloc(sizeof (struct narg));
	n->type = NARG;
	n->narg.next = NULL;
	n->narg.text = wordtext;
	n->narg.backquote = backquotelist;
	return n;
}

void fixredir(union node *n, const char *text, int err)
	{
	TRACE(("Fix redir %s %d\n", text, err));
	if (!err)
		n->ndup.vname = NULL;

	if (is_digit(text[0]) && text[1] == '\0')
		n->ndup.dupfd = digit_val(text[0]);
	else if (text[0] == '-' && text[1] == '\0')
		n->ndup.dupfd = -1;
	else {

		if (err)
			synerror("Bad fd number");
		else
			n->ndup.vname = makename();
	}
}


STATIC void
parsefname(void)
{
	union node *n = redirnode;

	if (n->type == NHERE)
		checkkwd = CHKEOFMARK;
	if (readtoken() != TWORD)
		synexpect(-1);
	if (n->type == NHERE) {
		struct heredoc *here = heredoc;
		struct heredoc *p;

		if (quoteflag == 0)
			n->type = NXHERE;
		TRACE(("Here document %d\n", n->type));
		rmescapes(wordtext);
		here->eofmark = wordtext;
		here->next = NULL;
		if (heredoclist == NULL)
			heredoclist = here;
		else {
			for (p = heredoclist ; p->next ; p = p->next);
			p->next = here;
		}
	} else if (n->type == NTOFD || n->type == NFROMFD) {
		fixredir(n, wordtext, 0);
	} else {
		n->nfile.fname = makename();
	}
}


/*
 * Input any here documents.
 */

STATIC void
parseheredoc(void)
{
	struct heredoc *here;
	union node *n;

	here = heredoclist;
	heredoclist = 0;

	while (here) {
		if (needprompt) {
			setprompt(2);
		}
		readtoken1(0, here->eofmark, here->striptabs | RT_HEREDOC | RT_CHECKEND | (here->here->type == NHERE ? RT_SQSYNTAX : RT_DQSYNTAX));
		endaliasuse();
		n = (union node *)stalloc(sizeof (struct narg));
		n->narg.type = NARG;
		n->narg.next = NULL;
		n->narg.text = wordtext;
		n->narg.backquote = backquotelist;
		here->here->nhere.doc = n;
		here = here->next;
	}
}

STATIC int
readtoken(void)
{
	int t;
	int kwd = checkkwd;
#ifdef DEBUG
	int alreadyseen = tokpushback;
#endif

top:
	lasttoken = t = xxreadtoken();

	/*
	 * eat newlines
	 */
	if (kwd & CHKNL && t == TNL) {
		parseheredoc();
		checkkwd = 0;
		goto top;
	}

	if (t != TWORD || quoteflag) {
		goto out;
	}

	/*
	 * check for keywords
	 */
	if (kwd & CHKKWD) {
		const char *const *pp;

		if ((pp = findkwd(wordtext))) {
			lasttoken = t = pp - parsekwd + KWDOFFSET;
			TRACE(("keyword %s recognized\n", tokname[t]));
			goto out;
		}
	}

	if ((checkkwd | kwd) & CHKALIAS) {
		struct alias *ap;
		if ((ap = lookupalias(wordtext, 1)) != NULL) {
			if (*ap->val) {
				pushstring(ap->val, strlen(ap->val), ap);
			}
			goto top;
		}
	}
out:
	endaliasuse();
	checkkwd = 0;
#ifdef DEBUG
	if (!alreadyseen)
	    TRACE(("token %s %s\n", tokname[t], t == TWORD ? wordtext : ""));
	else
	    TRACE(("reread token %s %s\n", tokname[t], t == TWORD ? wordtext : ""));
#endif
	return (t);
}

void nlprompt(void)
{
	plinno++;
	if (doprompt)
		setprompt(2);
}

static void nlnoprompt(void)
{
	plinno++;
	needprompt = doprompt;
}


/*
 * Read the next input token.
 * If the token is a word, we set backquotelist to the list of cmds in
 *	backquotes.  We set quoteflag to true if any part of the word was
 *	quoted.
 * If the token is TREDIR, then we set redirnode to a structure containing
 *	the redirection.
 *
 * [Change comment:  here documents and internal procedures]
 * [Readtoken shouldn't have any arguments.  Perhaps we should make the
 *  word parsing code into a separate routine.  In this case, readtoken
 *  doesn't need to have any internal procedures, but parseword does.
 *  We could also make parseoperator in essence the main routine, and
 *  have parseword (readtoken1?) handle both words and redirection.]
 */

STATIC int
xxreadtoken(void)
{
	if (tokpushback) {
		tokpushback = 0;
		return lasttoken;
	}
	if (needprompt) {
		setprompt(2);
	}
	for (;;) {	/* until token or start of word found */
		int c = pgetc_eatbnl(), tok;
		switch (c) {
#ifdef WITH_LOCALE
		case PMBB:
			while (pgetc() != PMBB);
#endif
		case ' ': case '\t':
			endaliasuse();
			continue;
		case '#':
			while ((c = pgetc()) != '\n' && c != PEOF);
			pungetc();
			continue;
		case '\n':
			nlnoprompt();
			tok = TNL;
			break;
		case PEOF:
			tok = TEOF;
			break;
		case '&':
			if (pgetc_eatbnl() == '&') {
				tok = TAND;
				break;
			}
			pungetc();
			tok = TBACKGND;
			break;
		case '|':
			if (pgetc_eatbnl() == '|') {
				tok = TOR;
				break;
			}
			pungetc();
			tok = TPIPE;
			break;
		case ';':
			switch (pgetc_eatbnl()) {
			case '&':
				tok = TENDCASEFT;
				break;
			case ';':
				tok = TENDCASE;
				break;
			default:
				pungetc();
				tok = TSEMI;
				break;
			}
			break;
		case '(':
			tok = TLP;
			break;
		case ')':
			tok = TRP;
			break;
		default:
			return readtoken1(c, (char *)NULL, 0);
		}
		return tok;
	}
}

static int
pgetc_eatbnl(void)
{
	int c;

	while ((c = pgetc()) == '\\') {
		if (pgetc() != '\n') {
			pungetc();
			break;
		}

		nlprompt();
	}

	return c;
}



/*
 * If eofmark is NULL, read a word or a redirection symbol.  If eofmark
 * is not NULL, read a here document.  In the latter case, eofmark is the
 * word which marks the end of the document and striptabs is true if
 * leading tabs should be stripped from the document.  The argument firstc
 * is the first character of the input token or document.
 */

STATIC char *readtoken1_loop(char *, int, char *, int);
STATIC int readtoken1_endword(char *, char *);
STATIC void readtoken1_checkend(int *, char *, int);
STATIC void readtoken1_parseredir(char *, int);
STATIC char *readtoken1_parsesub(char *, int, char *, int);
STATIC char *readtoken1_parsebackq(char *, int, int);
STATIC char *readtoken1_parsearith(char *, char *, int);
STATIC char *readtoken1_parseheredoc(char *);

STATIC int
readtoken1(int firstc, char *eofmark, int flags)
{
	char *out;

	quoteflag = 0;
	backquotelist = NULL;
	STARTSTACKSTR(out);
	out = readtoken1_loop(out, firstc, eofmark, flags);
	return readtoken1_endword(out, eofmark);
}

STATIC char *
readtoken1_loop(char *out, int c, char *eofmark, int flags)
{
	int qsyntax;

	if (!c)
		goto nextchar;

	for (;;) {
		if (eofmark && flags & RT_CHECKEND) {
			flags &= ~RT_CHECKEND;
			readtoken1_checkend(&c, eofmark, flags);	/* set c to PEOF if at end of here document */
		}

		CHECKSTRSPACE(4, out);	/* permit 4 calls to USTPUTC */

		switch (c) {
			int quotemark;
#ifdef WITH_LOCALE
		case PMBB:
			if (!flags)
				goto endword;
			/* fall through */
		case PMBW:;
			STATIC_ASSERT(RT_MBCHAR >> 1 == RT_ESCAPE);
			flags ^= RT_MBCHAR;
			flags &= ~RT_ESCAPE | (flags >> 1);
			goto nextchar;
#endif
		case '\n':
			if (!flags)
				goto endword;	/* exit outer loop */
			nlprompt();
			if (unlikely(flags & RT_HEREDOC && heredoclist))
				out = readtoken1_parseheredoc(out);
			flags |= RT_CHECKEND;
			/* fall through */
word:
		default:
			if (!(flags & (RT_ESCAPE | RT_CTOGGLE1 | RT_CTOGGLE2 | RT_MBCHAR)))
				goto output;
			/* fall through */
control:
		case '!': case '*': case '?': case '[': case '=':
		case '~': case ':': case '/': case '-': case ']':
#ifndef WITH_LOCALE
		case CTLCHARS:
#endif
			if ((flags & (RT_HEREDOC | RT_QSYNTAX)) == (RT_HEREDOC | RT_SQSYNTAX))
				goto output;
			if (flags & (RT_QSYNTAX | RT_ESCAPE)
#ifndef WITH_LOCALE
			    || (c >= CTL_FIRST && c <= CTL_LAST)
#endif
			) {
escape:
				flags &= ~RT_ESCAPE | (flags >> 1);
				USTPUTC(CTLESC, out);
			}
			while (flags & (RT_CTOGGLE1 | RT_CTOGGLE2)) {
				if (c >= 'a' && c <= 'z')
					c ^= 'A' ^ 'a';
				c ^= 0x40;
				flags -= RT_CTOGGLE1;
			}
			if (!(c & 0xFF)) {
				/* preadbuffer() removes physical null bytes,
				 * so we only get here in a $'...' string
				 * containing escape sequences such as \0 or
				 * toggle sequences such as \c@. All such
				 * sequences will be preceded by CTLESC. */
				int markloc = out - 1 - (char *)stackblock();
				readtoken1_loop(out, 0, eofmark, flags);
				return (char *)stackblock() + markloc;
			}
output:
			USTPUTC(c, out);
			break;
		case '\\':
			if ((flags & (RT_HEREDOC | RT_QSYNTAX)) == (RT_HEREDOC | RT_SQSYNTAX))
				goto word;
			if ((flags & (RT_SQSYNTAX | RT_ESCAPE | RT_MBCHAR))
			    && (flags & (RT_QSYNTAX | RT_ESCAPE | RT_MBCHAR)) != RT_DSQSYNTAX)
				goto control;
			quoteflag++;
			c = pgetc();
			if (c == PEOF) {
				pungetc();
				c = '\\';
			}
			if (flags & RT_SQSYNTAX) {
				switch (c) {
#ifdef WITH_LOCALE
					char buf[MB_LEN_MAX > 9 ? MB_LEN_MAX : 9];
#else
					char buf[4];
#endif
					int lenbase, cc;
					char *p;
					unsigned long val;
				default:
#define ESCSEQCH "\\\'abefnrtv"
#define ESCCHARS "\\\'\a\b\e\f\n\r\t\v"
					if ((p = strchr(ESCSEQCH "\0" ESCCHARS, c)))
						c = p[sizeof ESCSEQCH];
					break;
				case '0': case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
					do {
						pungetc();
						STATIC_ASSERT(ISODIGIT == 8);
						STATIC_ASSERT(ISXDIGIT == 16);
						lenbase = 0x0208; break;
				case 'x':	lenbase = 0x0110; break;
#ifdef WITH_LOCALE
				case 'u':	lenbase = 0x0310; break;
				case 'U':	lenbase = 0x0710; break;
#endif
					} while (0);
					p = buf;
					do {
						cc = pgetc();
						if (!(ctype(cc) & lenbase)) {
							pungetc();
							break;
						}
						*p++ = cc;
						lenbase -= 0x100;
					} while (lenbase >= 0);
					*p = '\0';
					val = atomax(buf, NULL, lenbase & 0xFF);
#ifdef WITH_LOCALE
					if (c == 'U' || c == 'u') {
						mbstate_t mbs = {0};
						size_t buflen = wcrtomb(buf, val, &mbs);
						if (buflen == (size_t) -1) {
							USTPUTC(CTLESC, out);
							USTPUTC(flags & RT_CTOGGLE1 ? '\\' ^ 0x40 : '\\', out);
							USTPUTC(c, out);
							buflen = p - buf;
							flags &= ~(RT_CTOGGLE1 | RT_CTOGGLE2);
						}
						if (!(flags & (RT_CTOGGLE1 | RT_CTOGGLE2))) {
							CHECKSTRSPACE(buflen * 2, out);
							p = buf;
							while (--buflen) {
								USTPUTC(CTLESC, out);
								USTPUTC(*p++, out);
							}
							val = *p;
						}
					}
#endif
					c = val;
#ifdef WITH_LOCALE
					flags |= RT_ESCAPE;
					break;
#else
					goto escape;
#endif
				case 'c':
					flags &= ~RT_CTOGGLE2;
					flags += RT_CTOGGLE1;
					goto nextchar;
				}
			} else if (flags & RT_DQSYNTAX) {
				if (
					c != '\\' && c != '`' &&
					c != '$' && (
						c != '"' ||
						flags & RT_HEREDOC
					) && (
						c != '}' ||
						!(flags & RT_VARNEST)
					)
				) {
					USTPUTC(CTLESC, out);
					USTPUTC('\\', out);
				}
			}
#ifdef WITH_LOCALE
			flags |= RT_ESCAPE;
			continue;
		case CTLCHARS:
			if ((flags & (RT_HEREDOC | RT_QSYNTAX)) == (RT_HEREDOC | RT_SQSYNTAX))
				goto output;
			if ((flags & (RT_QSYNTAX | RT_ESCAPE)) != RT_ESCAPE)
				goto escape;
			flags &= ~RT_ESCAPE | (flags >> 1);
#else
			if (flags & RT_QSYNTAX || c < CTL_FIRST || c > CTL_LAST)
				goto escape;
#endif
			USTPUTC(CTLQUOTEMARK, out);
			USTPUTC(CTLESC, out);
			USTPUTC(c, out);
			USTPUTC(CTLQUOTEMARK, out);
			break;
		case '$':
			if (flags & (RT_SQSYNTAX | RT_ESCAPE | RT_MBCHAR))
				goto word;
			c = pgetc_eatbnl();
			if (flags & RT_DQSYNTAX || c != '\'') {
				out = readtoken1_parsesub(out, c, eofmark, flags);	/* parse substitution */
				break;
			}
			qsyntax = RT_DSQSYNTAX;
			while (0) {
		case '\'':
				qsyntax = RT_SQSYNTAX;
				break;
		case '"':
				qsyntax = RT_DQSYNTAX;
				break;
			}
			if (flags & (~(flags << 1) | RT_ESCAPE | RT_MBCHAR) & (RT_HEREDOC | RT_QSYNTAX | RT_ESCAPE | RT_MBCHAR) & ~qsyntax)
				goto word;
			quotemark = 1;
			if (flags & qsyntax) {
				if (!(flags & RT_VARNEST)) {
					quoteflag++;
					return out;
				}
				quotemark = 0;
			}
			if (quotemark)
				USTPUTC(CTLQUOTEMARK, out);
			out = readtoken1_loop(out, 0, eofmark, (flags & RT_STRIPTABS) | RT_STRING | qsyntax);
			if (quotemark)
				USTPUTC(CTLQUOTEMARK, out);
			break;
		case '}':
			if ((flags ^ RT_VARNEST) & (RT_VARNEST | RT_ESCAPE | RT_MBCHAR))
				goto word;
			USTPUTC(CTLENDVAR, out);
			return out;
		case '(':
			if (!(flags & RT_ARINEST))
				goto special;
			USTPUTC(c, out);
			out = readtoken1_loop(out, 0, eofmark, flags | RT_ARIPAREN);
			break;
		case ')':
			if (!(flags & RT_ARINEST))
				goto special;
			if (flags & RT_ARIPAREN) {
				USTPUTC(c, out);
				return out;
			} else {
				if (pgetc_eatbnl() == ')') {
					USTPUTC(CTLENDARI, out);
					return out;
				} else {
					/*
					 * unbalanced parens
					 *  (don't 2nd guess - no error)
					 */
					pungetc();
					USTPUTC(')', out);
				}
			}
			break;
		case '`':
			if (flags & (RT_SQSYNTAX | RT_ESCAPE | RT_MBCHAR) || checkkwd & CHKEOFMARK)
				goto word;
			out = readtoken1_parsebackq(out, flags, 1);
			break;
		case PEOF:
			goto endword;		/* exit outer loop */
special:
		case '<': case '>': // case '(': case ')':
		case ';': case '&': case '|': case ' ': case '\t':
			if (!flags)
				goto endword;	/* exit outer loop */
			goto word;
		}
nextchar:
		c = flags & (RT_SQSYNTAX | RT_MBCHAR) ? pgetc() : pgetc_eatbnl();
	}
endword:
	if (flags & RT_ARINEST)
		synerror("Missing '))'");
	if (flags & RT_STRING)
		synerror("Unterminated quoted string");
	if (flags & RT_VARNEST) {
		/* { */
		synerror("Missing '}'");
	}
	pungetc();
	return out;
}

STATIC int
readtoken1_endword(char *out, char *eofmark)
{
	size_t len;
	int c;

	USTPUTC('\0', out);
	len = out - (char *)stackblock();
	out = stackblock();

	c = pgetc();
	if (eofmark == NULL) {
		if ((c == '>' || c == '<')
		 && quoteflag == 0
		 && len <= 2
		 && (*out == '\0' || is_digit(*out))) {
			readtoken1_parseredir(out, c);
			return TREDIR;
		} else {
			pungetc();
		}
	} else {
		if (c == '\n')
			nlnoprompt();
		else
			pungetc();
	}
	grabstackblock(len);
	wordtext = out;
	return TWORD;
}

/*
 * Check to see whether we are at the end of the here document.  When this
 * is called, c is set to the first character of the next input line.  If
 * we are at the end of the here document, this routine sets the c to PEOF.
 */

STATIC void
readtoken1_checkend(int *c, char *eofmark, int flags)
{
	char *p;

	if (flags & RT_STRIPTABS) {
		while (*c == '\t')
			*c = pgetc();
	}

	for (p = eofmark;; p++) {
#ifdef WITH_LOCALE
		while (*c < PEOF) {
			flags ^= RT_MBCHAR;
			*c = pgetc();
		}
#endif
		if (!*p)
			break;
		if (*c != *p) {
#ifdef WITH_LOCALE
			if (flags & RT_MBCHAR)
				p -= parsefile->p.mbp - parsefile->p.mbc - 1;
#endif
			goto more_heredoc;
		}

		*c = pgetc();
	}

	switch (*c) {
	case '\n':
	case PEOF:
		*c = PEOF;
		break;
	default:
more_heredoc:
#ifdef WITH_LOCALE
		if (flags & RT_MBCHAR) {
			parsefile->p.mbp = parsefile->p.mbc;
			*c = parsefile->p.lastc[0] = parsefile->p.mbt;
		}
#endif
		if (p != eofmark) {
			pungetc();
			pushstring(eofmark, p - eofmark, NULL);
			*c = pgetc();
		}
	}
}


/*
 * Parse a redirection operator.  The variable "out" points to a string
 * specifying the fd to be redirected.  The variable "c" contains the
 * first character of the redirection operator.
 */

STATIC void
readtoken1_parseredir(char *out, int c)
{
	char fd = *out;
	union node *np;

	np = (union node *)stalloc(sizeof (struct nfile));
	if (c == '>') {
		np->nfile.fd = 1;
		c = pgetc_eatbnl();
		if (c == '>')
			np->type = NAPPEND;
		else if (c == '|')
			np->type = NCLOBBER;
		else if (c == '&')
			np->type = NTOFD;
		else {
			np->type = NTO;
			pungetc();
		}
	} else {	/* c == '<' */
		np->nfile.fd = 0;
		switch (c = pgetc_eatbnl()) {
		case '<':
			if (sizeof (struct nfile) != sizeof (struct nhere)) {
				np = (union node *)stalloc(sizeof (struct nhere));
				np->nfile.fd = 0;
			}
			np->type = NHERE;
			heredoc = (struct heredoc *)stalloc(sizeof (struct heredoc));
			heredoc->here = np;
			if ((c = pgetc_eatbnl()) == '-') {
				heredoc->striptabs = RT_STRIPTABS;
			} else {
				heredoc->striptabs = 0;
				pungetc();
			}
			break;

		case '&':
			np->type = NFROMFD;
			break;

		case '>':
			np->type = NFROMTO;
			break;

		default:
			np->type = NFROM;
			pungetc();
			break;
		}
	}
	if (fd != '\0')
		np->nfile.fd = digit_val(fd);
	redirnode = np;
}


/*
 * Parse a substitution.  At this point, we have read the dollar sign
 * and nothing else.
 */

STATIC char *
readtoken1_parsesub(char *out, int c, char *eofmark, int flags)
{
	int subtype;
	int typeloc;
	char *p;
	int vsflags = flags;
	static const char types[] = "}-+?=";

	if (
		(checkkwd & CHKEOFMARK) ||
		(!is_in_name(c) && !is_specialdol(c))
	) {
		USTPUTC('$', out);
		pungetc();
	} else if (c == '(') {	/* $(command) or $((arith)) */
		if (pgetc_eatbnl() == '(') {
			out = readtoken1_parsearith(out, eofmark, flags);
		} else {
			pungetc();
			out = readtoken1_parsebackq(out, flags, 0);
		}
	} else {
		USTPUTC(CTLVAR, out);
		typeloc = out - (char *)stackblock();
		STADJUST(1, out);
		subtype = VSNORMAL;
		if (likely(c == '{')) {
			c = pgetc_eatbnl();
			subtype = 0;
		}
varname:
		if (is_digit(c)) {
			do {
				STPUTC(c, out);
				c = pgetc_eatbnl();
			} while (subtype != VSNORMAL && is_digit(c));
		} else if (is_name(c)) {
			do {
				STPUTC(c, out);
				c = pgetc_eatbnl();
			} while (is_in_name(c));
		} else if (is_specialvar(c)) {
			int cc = c;

			c = pgetc_eatbnl();

			if (!subtype && cc == '#') {
				if (is_in_name(c) || is_specialvar(c)) {
					subtype = VSLENGTH;
					goto varname;
				}
			}

			if (subtype == VSLENGTH && c != '}') {
				subtype = 0;
				pungetc();
				c = cc;
				cc = '#';
			}

			USTPUTC(cc, out);
		}
		else
			goto badsub;

		if (subtype == 0) {
			switch (c) {
			case ':':
				subtype = VSNUL;
				c = pgetc_eatbnl();
				/*FALLTHROUGH*/
			default:
				p = strchr(types, c);
				if (p == NULL)
					goto badsub;
				subtype |= p - types + VSNORMAL;
				break;
			case '%':
			case '#':
				{
					int cc = *((char *)stackblock() + typeloc + 1);
					if (cc == '@' || cc == '*')
						goto badsub;
					cc = c;
					subtype = c == '#' ? VSTRIMLEFT :
							     VSTRIMRIGHT;
					c = pgetc_eatbnl();
					if (c == cc)
						subtype++;
					else
						pungetc();
					vsflags &= ~RT_DQSYNTAX;
					break;
				}
			}
		} else {
badsub:
			pungetc();
		}
		*((char *)stackblock() + typeloc) = subtype;
		STPUTC('=', out);
		if (subtype != VSNORMAL) {
			out = readtoken1_loop(out, 0, eofmark, (vsflags & (RT_STRIPTABS | RT_DQSYNTAX)) | RT_VARNEST);
		}
	}
	return out;
}


/*
 * Called to parse command substitutions.  Newstyle is set if the command
 * is enclosed inside $(...); nlpp is a pointer to the head of the linked
 * list of commands (passed by reference), and savelen is the number of
 * characters on the top of the stack which must be preserved.
 */

STATIC char *
readtoken1_parsebackq(char *out, int flags, int oldstyle)
{
	struct nodelist **nlpp;
	union node *n;
	char *str;
	size_t savelen;
	struct nodelist *savebqlist;
	struct heredoc *saveheredoclist;
	struct heredoc **here;

	str = NULL;
	savelen = out - (char *)stackblock();
	if (savelen > 0) {
		str = alloca(savelen);
		memcpy(str, stackblock(), savelen);
	}
	nlpp = &backquotelist;
	while (*nlpp)
		nlpp = &(*nlpp)->next;
	*nlpp = (struct nodelist *)stalloc(sizeof (struct nodelist));
	(*nlpp)->next = NULL;
	if (oldstyle) {
		/* We can reasonably assume we will not have >30 levels of
		 * nested old-style command substitutions: N levels
		 * requires (2**N)-1 consecutive backslashes. */
		if (flags & RT_DQSYNTAX)
			parsefile->p.dqbackq |= parsefile->p.backq;
		parsefile->p.backq <<= 1;
	}
	savebqlist = backquotelist;
	saveheredoclist = heredoclist;
	heredoclist = 0;
	n = list(2);
	backquotelist = savebqlist;
	for (here = &saveheredoclist; *here; here = &(*here)->next)
		;
	*here = heredoclist;
	heredoclist = saveheredoclist;
	(*nlpp)->n = n;
	if (oldstyle) {
		if (readtoken() != TEOF)
			synexpect(TENDBQUOTE);
		parsefile->p.backq >>= 1;
		parsefile->p.dqbackq &= ~parsefile->p.backq;
	} else {
		if (readtoken() != TRP)
			synexpect(TRP);
	}
	while (stackblocksize() <= savelen)
		growstackblock();
	STARTSTACKSTR(out);
	if (str) {
		memcpy(out, str, savelen);
		STADJUST(savelen, out);
	}
	USTPUTC(CTLBACKQ, out);
	return out;
}



/*
 * Parse an arithmetic expansion (indicate start of one and set state)
 */

STATIC char *
readtoken1_parsearith(char *out, char *eofmark, int flags)
{
	USTPUTC(CTLARI, out);
	return readtoken1_loop(out, 0, eofmark, (flags & RT_STRIPTABS) | RT_ARINEST);
}


/*
 * Parse a nested heredoc.
 */

STATIC char *
readtoken1_parseheredoc(char *out)
{
	char *str;
	size_t savelen;
	struct nodelist *savebqlist;

	str = NULL;
	savelen = out - (char *)stackblock();
	if (savelen) {
		str = alloca(savelen);
		memcpy(str, stackblock(), savelen);
	}
	savebqlist = backquotelist;
	parseheredoc();
	backquotelist = savebqlist;
	while (stackblocksize() <= savelen)
		growstackblock();
	STARTSTACKSTR(out);
	if (str) {
		memcpy(out, str, savelen);
		STADJUST(savelen, out);
	}
	return out;
}


#ifdef mkinit
INCLUDE "parser.h"
#endif


/*
 * Return of a legal variable name (a letter or underscore followed by zero or
 * more letters, underscores, and digits).
 */

char *
endofname(const char *name)
	{
	char *p;

	p = (char *) name;
	if (! is_name(*p))
		return p;
	while (*++p) {
		if (! is_in_name(*p))
			break;
	}
	return p;
}


/*
 * Called when an unexpected token is read during the parse.  The argument
 * is the token that is expected, or -1 if more than one type of token can
 * occur at this point.
 */

void
synexpect(int token)
{
	char msg[64];

	if (token >= 0) {
		fmtstr(msg, 64, "%s unexpected (expecting %s)",
			tokname[lasttoken], tokname[token]);
	} else {
		fmtstr(msg, 64, "%s unexpected", tokname[lasttoken]);
	}
	synerror(msg);
	/* NOTREACHED */
}


STATIC void
synerror(const char *msg)
{
#ifdef WITH_PARSER_LOCALE
	uselocale(LC_GLOBAL_LOCALE);
#endif
	errlinno = plinno;
	sh_error("Syntax error: %s", msg);
	/* NOTREACHED */
}

STATIC void
setprompt(int which)
{
	struct stackmark smark;
	int show;

	needprompt = 0;
	whichprompt = which;

#ifdef SMALL
	show = 1;
#else
	lastprompt = NULL;
	show = !el;
#endif
	if (show) {
		pushstackmark(&smark, stackblocksize());
		out2str(getprompt(NULL));
		popstackmark(&smark);
	}
}

const char *
expandstr(const char *ps, int flags)
{
	union node n;
	int saveprompt;

	setinputstring(ps);

	saveprompt = doprompt;
	doprompt = 0;

	readtoken1(0, NULL, RT_HEREDOC | RT_DQSYNTAX);

	doprompt = saveprompt;

	popfile();

	n.narg.type = NARG;
	n.narg.next = NULL;
	n.narg.text = wordtext;
	n.narg.backquote = backquotelist;

	expandarg(&n, NULL, flags | EXP_QUOTED);
	return stackblock();
}

/*
 * called by editline -- any expansions to the prompt
 *    should be added here.
 */
const char *
getprompt(void *unused)
{
	const char *prompt;
	struct nodelist *savebqlist;
	int savecheckkwd;

#ifndef SMALL
	if (lastprompt)
		return lastprompt;
#endif

	switch (whichprompt) {
	default:
#ifdef DEBUG
		return "<internal prompt error>";
#endif
	case 0:
		return nullstr;
	case 1:
		prompt = ps1val();
		break;
	case 2:
		prompt = ps2val();
		break;
	}

	savebqlist = backquotelist;
	savecheckkwd = checkkwd;
#ifdef WITH_PARSER_LOCALE
	uselocale(LC_GLOBAL_LOCALE);
#endif
	prompt = expandstr(prompt, 0);
#ifdef WITH_PARSER_LOCALE
	uselocale(parselocale);
#endif
	checkkwd = savecheckkwd;
	backquotelist = savebqlist;

#ifndef SMALL
	lastprompt = prompt;
#endif

	return prompt;
}

const char *const *
findkwd(const char *s)
{
	return findstring(
		s, parsekwd, sizeof(parsekwd) / sizeof(const char *)
	);
}
