/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2022
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

#include <stdio.h>	/* defines BUFSIZ */
#ifdef WITH_LOCALE
#include <wchar.h>
#include <wctype.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/*
 * This file implements the input routines used by the parser.
 */

#include "eval.h"
#include "shell.h"
#include "redir.h"
#include "syntax.h"
#include "input.h"
#include "output.h"
#include "options.h"
#include "memalloc.h"
#include "error.h"
#include "alias.h"
#include "parser.h"
#include "main.h"
#include "system.h"
#include "var.h"
#ifndef SMALL
#include "myhistedit.h"
#endif

#define EOF_NLEFT -99		/* value of parsenleft when EOF pushed back */
#define IBUFSIZ (BUFSIZ + 1)


MKINIT struct parsefile basepf;	/* top level input file */
MKINIT char basebuf[IBUFSIZ];	/* buffer for top level input file */
struct parsefile *parsefile = &basepf;	/* current input file */
#ifdef WITH_PARSER_LOCALE
locale_t parselocale;		/* the locale to use during parsing */
#endif
int whichprompt;		/* 1 == PS1, 2 == PS2 */

static void pushfile(void);
static int preadfd(void);
static void setinputfd(int fd, int push);
static int preadbuffer(void);

#ifdef mkinit
INCLUDE <stdio.h>
INCLUDE "input.h"
INCLUDE "error.h"

INIT {
	basepf.p.nextc = basepf.buf = basebuf;
	basepf.linno = 1;
	basepf.p.flags = PF_LINENO | PF_NONUL
#ifndef SMALL
		| PF_HIST
#endif
	;
	basepf.p.backq = 1;
}

RESET {
	basepf.p.backq = 1;
	basepf.p.dqbackq = 0;
	popallfiles();
}
#endif


/*
 * Read a character from the script, returning PEOF on end of file.
 * Nul characters in the input are silently discarded.
 */

static int
pgetc2(void)
{
	if (--parsefile->p.nleft >= 0)
		return (signed char)*parsefile->p.nextc++;
	else
		return preadbuffer();
}

static int
pgetc1(void)
{
	int c;

#ifdef WITH_LOCALE
	mbstate_t mbs;
	wchar_t wc;
	char *p;
#endif

	c = pgetc2();
#ifdef WITH_LOCALE
	if (likely(!(c & 0x80)))
		goto out;

	memset(&mbs, 0, sizeof mbs);
	p = parsefile->p.mbc;

	for (;;) {
		*p = c;
		switch (mbrtowc(&wc, p, 1, &mbs)) {
		case (size_t)-2:
			p++;
			c = pgetc2();
			/* If c == PEOF, the next mbrtowc() is guaranteed to
			 * return -1. No special handling is needed. */
			STATIC_ASSERT(!(PEOF & 0xFF));
			continue;
		case (size_t)-1:
			break;
		default:
			/* If we have a blank other than space or tab, wrap
			 * it in PMBW/.../PMBW even if it is a single byte.
			 * This allows avoiding isblank() later. */
			if (iswblank(wc))
				c = PMBB;
			else if (p != parsefile->p.mbc)
				c = PMBW;
			else
				goto out;
			parsefile->p.mbt = c;
			parsefile->p.mbp = parsefile->p.mbc;
			*++p = '\0';
			goto out;
		}
		break;
	}

	if (p != parsefile->p.mbc) {
		/* Invalid multibyte character. As in the rest of the shell,
		 * treat the first byte as a single character, then allow
		 * the following characters to be interpreted as a multibyte
		 * character, unless it is PEOF or PEOA. */
		int sp = c != (signed char)c;
		parsefile->p.lastc[1] = sp ? c : parsefile->p.lastc[0];
		parsefile->p.lastc[parsefile->p.unget] = c = (signed char)parsefile->p.mbc[0];
		parsefile->p.unget += sp;
		pushstring(parsefile->p.mbc + 1, p - parsefile->p.mbc + sp, NULL);
		return c;
	}

out:
#endif

	return c;
}


int
pgetc(void)
{
	int c;

	if (parsefile->p.unget)
		return parsefile->p.lastc[--parsefile->p.unget];

#ifdef WITH_LOCALE
	if (parsefile->p.mbp) {
		c = (signed char)*parsefile->p.mbp++;
		if (!c) {
			parsefile->p.mbp = NULL;
			c = parsefile->p.mbt;
		}
		return c;
	}
#endif

	for (;;) {
		int len = 0;
		while ((c = pgetc1()) == '\\' && ++len < parsefile->p.backq)
			;
		if (!c)
			c = pgetc();
		else if (c == '`') {
			len++;
			goto eof;
		} else if (c == PEOF) {
eof:
			if (len == parsefile->p.backq)
				goto output;
			if (len == parsefile->p.backq >> 1 && (!len || c >= 0)) {
				c = PEOF;
				goto output;
			}
			lasttoken = TEOF;
			parsefile->p.backq = 1;
			parsefile->p.dqbackq = 0;
			synexpect(TENDBQUOTE);
		}
		if (!len)
			goto output;
		if (c == '\n') {
			nlprompt();
			continue;
		}
		if (c == '$' || c == '\\' ||
		    (c == '"' && len <= parsefile->p.dqbackq))
			goto output;
		goto escape;

output:
		parsefile->p.lastc[1] = parsefile->p.lastc[0];
		parsefile->p.lastc[0] = c;
		return c;

escape:
		parsefile->p.lastc[1] = '\\';
		parsefile->p.lastc[0] = c;
		parsefile->p.unget++;
		return '\\';
	}
}


static int
preadfd(void)
{
	int nr;
	char *buf =  parsefile->buf;
	parsefile->p.nextc = buf;

retry:
#ifndef SMALL
	if (parsefile->fd == 0 && el) {
		static const char *rl_cp;
		static int el_len;

		if (rl_cp == NULL) {
#ifdef WITH_PARSER_LOCALE
			locale_t savelocale = uselocale(LC_GLOBAL_LOCALE);
#endif
			struct stackmark smark;
			pushstackmark(&smark, stackblocksize());
			rl_cp = el_gets(el, &el_len);
			popstackmark(&smark);
#ifdef WITH_PARSER_LOCALE
			uselocale(savelocale);
#endif
		}
		if (rl_cp == NULL)
			nr = 0;
		else {
			nr = el_len;
			if (nr > IBUFSIZ - 1)
				nr = IBUFSIZ - 1;
			memcpy(buf, rl_cp, nr);
			if (nr != el_len) {
				el_len -= nr;
				rl_cp += nr;
			} else
				rl_cp = 0;
		}

	} else
#endif
		nr = read(parsefile->fd, buf, parsefile->fd == 0 ? 1 : IBUFSIZ - 1);


	if (nr < 0) {
		if (errno == EINTR)
			goto retry;
		if (parsefile->fd == 0 && errno == EWOULDBLOCK) {
			int flags = fcntl(0, F_GETFL, 0);
			if (flags >= 0 && flags & O_NONBLOCK) {
				flags &=~ O_NONBLOCK;
				if (fcntl(0, F_SETFL, flags) >= 0) {
					out2str("sh: turning off NDELAY mode\n");
					goto retry;
				}
			}
		}
	}
	return nr;
}

/*
 * Refill the input buffer and return the next input character:
 *
 * 1) If a string was pushed back on the input, pop it;
 * 2) If an EOF was pushed back (parsenleft == EOF_NLEFT) or we are reading
 *    from a string so we can't refill the buffer, return EOF.
 * 3) If the is more stuff in this buffer, use it else call read to fill it.
 * 4) Process input up to the next newline, deleting nul characters.
 */

static int preadbuffer(void)
{
	char *q;
	int more;
#ifndef SMALL
	int something;
#endif
	char savec;

	if (unlikely(parsefile->strpush)) {
		popstring();
		return 0;
	}
	if (unlikely(parsefile->p.nleft == EOF_NLEFT ||
		     parsefile->buf == NULL)) {
#ifdef ENABLE_INTERNAL_COMPLETION
		if (parsefile->p.flags & PF_COMPLETING)
			exraise(EXEOF);
#endif
		return PEOF;
	}
	flushall();

	more = parsefile->lleft;
	if (more <= 0) {
again:
		if ((more = preadfd()) <= 0) {
			parsefile->lleft = parsefile->p.nleft = EOF_NLEFT;
			return PEOF;
		}
	}

	q = parsefile->buf + (parsefile->p.nextc - parsefile->buf);

	/* delete nul characters */
#ifndef SMALL
	something = histop == H_APPEND;
#endif
	for (;;) {
		int c;

		more--;
		c = *q;

		if (!c) {
			if (parsefile->p.flags & PF_NONUL) {
				sh_warnx("cannot execute binary file");
				flushall();
				_exit(126);
			}

			memmove(q, q + 1, more);
		} else {
			q++;

			if (c == '\n') {
				parsefile->p.nleft = q - parsefile->p.nextc - 1;
				break;
			}

#ifndef SMALL
			switch (c) {
			default:
				something = 1;
				/* fall through */
			case '\t':
			case ' ':
				break;
			}
#endif
		}

		if (more <= 0) {
			parsefile->p.nleft = q - parsefile->p.nextc - 1;
			if (parsefile->p.nleft < 0)
				goto again;
			break;
		}
	}
	parsefile->lleft = more;
	parsefile->p.flags &= ~PF_NONUL;

	savec = *q;
	*q = '\0';

#ifndef SMALL
	if (parsefile->p.flags & PF_HIST && hist && something) {
		HistEvent he;
		INTOFF;
		history(hist, &he, H_FIRST);
		history(hist, &he, histop, parsefile->p.nextc, (void *) NULL);
		histop = H_APPEND;
		INTON;
	}
#endif

	if (vflag) {
		out2str(parsefile->p.nextc);
		flushall();
	}

	*q = savec;

	return (signed char)*parsefile->p.nextc++;
}

/*
 * Undo a call to pgetc.  Only two characters may be pushed back.
 * PEOF may be pushed back.
 */

void
pungetc(void)
{
	parsefile->p.unget++;
}

/*
 * Push a string back onto the input at this current parsefile level.
 * We handle aliases this way.
 */
void
pushstring(const char *s, size_t len, void *ap)
{
	struct strpush *sp;

	INTOFF;
/*dprintf("*** calling pushstring: %s, %d\n", s, len);*/
	if (parsefile->strpush) {
		sp = ckmalloc(sizeof (struct strpush));
		sp->prev = parsefile->strpush;
		parsefile->strpush = sp;
	} else
		sp = parsefile->strpush = &(parsefile->basestrpush);
	sp->p = parsefile->p;
	sp->ap = (struct alias *)ap;
	if (ap) {
		((struct alias *)ap)->flag |= ALIASINUSE;
		sp->string = s;
		parsefile->p.flags &= ~PF_LINENO;
	}
#ifdef WITH_LOCALE
	parsefile->p.mbp = NULL;
#endif
	parsefile->p.nextc = s;
	parsefile->p.nleft = len;
	parsefile->p.unget = 0;
	parsefile->p.backq = 1;
	parsefile->p.dqbackq = 0;
	INTON;
}

void
popstring(void)
{
	struct strpush *sp = parsefile->strpush;

	INTOFF;
	if (sp->ap) {
		if (
		    parsefile->p.lastc[0] == ' '
		    || parsefile->p.lastc[0] == '\t'
#ifdef WITH_LOCALE
		    || parsefile->p.lastc[0] == PMBB
#endif
		) {
			checkkwd |= CHKALIAS;
		}
		if (sp->string != sp->ap->val) {
			ckfree(sp->string);
		}
		sp->ap->nextdone = aliasdone;
		aliasdone = sp->ap;
	}
	if (unlikely(parsefile->p.backq)) {
		sp->p.dqbackq |= parsefile->p.dqbackq * sp->p.backq;
		sp->p.backq *= parsefile->p.backq;
	}
	parsefile->p = sp->p;
/*dprintf("*** calling popstring: restoring to '%s'\n", parsenextc);*/
	parsefile->strpush = sp->prev;
	if (sp != &(parsefile->basestrpush))
		ckfree(sp);
	INTON;
}

/*
 * Set the input to take input from a file.  If push is set, push the
 * old input onto the stack first.
 */

int
setinputfile(const char *fname, int flags)
{
	int fd;

	INTOFF;
	if ((fd = xopen(fname, O_RDONLY)) < 0) {
		if (flags & INPUT_NOFILE_OK)
			goto out;
		exitstatus = 127;
		exerror(EXERROR, "%s: %s", fname, errnomsg());
	}
	if (fd < 10)
		fd = savefd(fd, fd);
	setinputfd(fd, flags & INPUT_PUSH_FILE);
out:
	INTON;
	return fd;
}


/*
 * Like setinputfile, but takes an open file descriptor.  Call this with
 * interrupts off.
 */

static void
setinputfd(int fd, int push)
{
	if (push) {
		pushfile();
		parsefile->buf = 0;
	}
	parsefile->fd = fd;
	if (parsefile->buf == NULL)
		parsefile->buf = ckmalloc(IBUFSIZ);
	parsefile->lleft = parsefile->p.nleft = 0;
	parsefile->p.flags |= PF_LINENO;
	plinno = 1;
}


/*
 * Like setinputfile, but takes input from a string.
 */

void
setinputstring(const char *string)
{
	setinputmem(string, strlen(string));
}

void
setinputmem(const char *string, size_t len)
{
	INTOFF;
	pushfile();
	parsefile->p.nextc = string;
	parsefile->p.nleft = len;
	parsefile->buf = NULL;
	plinno = lineno;
	INTON;
}



/*
 * To handle the "." command, a stack of input files is used.  Pushfile
 * adds a new entry to the stack and popfile restores the previous level.
 */

static void
pushfile(void)
{
	struct parsefile *pf;

	pf = (struct parsefile *)ckmalloc(sizeof (struct parsefile));
	pf->prev = parsefile;
	pf->fd = -1;
	pf->strpush = NULL;
	pf->basestrpush.prev = NULL;
#ifdef WITH_LOCALE
	pf->p.mbp = NULL;
#endif
	pf->p.unget = 0;
	pf->p.backq = 1;
	pf->p.dqbackq = 0;
	pf->p.flags = 0;
	parsefile = pf;
}


void
popfile(void)
{
	struct parsefile *pf = parsefile;

	INTOFF;
	if (pf->fd >= 0)
		close(pf->fd);
	if (pf->buf)
		ckfree(pf->buf);
	while (pf->strpush)
		popstring();
	parsefile = pf->prev;
	ckfree(pf);
	INTON;
}


void unwindfiles(struct parsefile *stop)
{
	while (parsefile != stop)
		popfile();
}


/*
 * Return to top level.
 */

void
popallfiles(void)
{
	unwindfiles(&basepf);
}



/*
 * Close the file(s) that the shell is reading commands from.  Called
 * after a fork is done.
 */

void
closescript(void)
{
	popallfiles();
	if (parsefile->fd > 0) {
		close(parsefile->fd);
		parsefile->fd = 0;
	}
}

#ifdef mkinit
RESET {
	if (sub)
		closescript();
}
#endif
