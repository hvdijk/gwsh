/*-
 * Copyright (c) 1993
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

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
/*
 * Editline and history functions (and glue).
 */
#include "shell.h"
#include "input.h"
#include "parser.h"
#include "var.h"
#include "options.h"
#include "main.h"
#include "output.h"
#include "mystring.h"
#include "error.h"
#ifndef SMALL
#include "mylocale.h"
#include "myhistedit.h"
#include "eval.h"
#include "expand.h"
#include "memalloc.h"
#include "nodes.h"
#include "system.h"

#define MAXHISTLOOPS	4	/* max recursions through fc */
#define DEFEDITOR	"ed"	/* default editor *should* be $EDITOR */

#ifdef mkinit
INCLUDE "myhistedit.h"
#endif

History *hist;	/* history cookie */
EditLine *el;	/* editline cookie */
int displayhist;
int histop = H_ENTER;
static FILE *el_in, *el_out;

STATIC const char *fc_replace(const char *, char *, char *);

#ifdef ENABLE_INTERNAL_COMPLETION
static unsigned char complete(EditLine *, int);
#endif

#ifdef DEBUG
extern FILE *tracefile;
#endif

/*
 * Set history and editing status.  Called whenever the status may
 * have changed (figures out what to do).
 */
void
histedit(void)
{
	FILE *el_err;

#define editing (Eflag || Vflag)

	if (iflag) {
		if (!hist) {
			/*
			 * turn history on
			 */
			INTOFF;
			hist = history_init();
			INTON;

			if (hist != NULL)
				sethistsize(histsizeval());
			else
				out2str("sh: can't initialize history\n");
		}
		if (editing && !el && isatty(0)) { /* && isatty(2) ??? */
			/*
			 * turn editing on
			 */
			INTOFF;
			if (el_in == NULL)
				el_in = fdopen(0, "r");
			if (el_out == NULL)
				el_out = fdopen(2, "w");
			if (el_in == NULL || el_out == NULL)
				goto bad;
			el_err = el_out;
#if DEBUG
			if (tracefile)
				el_err = tracefile;
#endif
			el = el_init(arg0, el_in, el_out, el_err);
			if (el != NULL) {
				if (hist)
					el_set(el, EL_HIST, history, hist);
#ifdef EL_PROMPT_ESC
				el_set(el, EL_PROMPT_ESC, getprompt, 1);
#else
				el_set(el, EL_PROMPT, getprompt, 1);
#endif
#if defined ENABLE_EXTERNAL_COMPLETION
#if defined(HAVE__EL_FN_SH_COMPLETE)
				el_set(el, EL_ADDFN, "sh-complete",
					"Filename completion",
					_el_fn_sh_complete);
#elif defined(HAVE__EL_FN_COMPLETE)
				el_set(el, EL_ADDFN, "sh-complete",
					"Filename completion",
					_el_fn_complete);
#endif
#elif defined ENABLE_INTERNAL_COMPLETION
				el_set(el, EL_ADDFN, "sh-complete",
					"Filename completion",
					complete);
#endif
			} else {
bad:
				out2str("sh: can't initialize editing\n");
			}
			INTON;
		} else if (!editing && el) {
			INTOFF;
			el_end(el);
			el = NULL;
			INTON;
		}
		if (el) {
			if (Vflag)
				el_set(el, EL_EDITOR, "vi");
			else if (Eflag)
				el_set(el, EL_EDITOR, "emacs");
#if ENABLE_COMPLETION
			el_set(el, EL_BIND, "^I", "sh-complete", NULL);
#endif
			el_source(el, NULL);
		}
	} else {
		INTOFF;
		if (el) {	/* no editing if not interactive */
			el_end(el);
			el = NULL;
		}
		if (hist) {
			history_end(hist);
			hist = NULL;
		}
		INTON;
	}
}

#ifdef ENABLE_INTERNAL_COMPLETION
extern int doprompt;
extern char *wordtext;
extern int wordflags;

#ifdef WITH_LOCALE
static int
str_width(const char *p)
{
	int result = 0;
	while (*p) {
		int c, w;
		GETC(c, p);
		if (c < 0)
			return -1;
		w = wcwidth(c);
		if (w < 0)
			return -1;
		result += w;
	}
	return result;
}
#endif

static unsigned char
complete(EditLine *el, int ch)
{
	const LineInfo *li;
	HistEvent he;

	struct stackmark smark;
	struct parsefile *file_stop = parsefile;
	struct jmploc *savehandler = handler;
	struct jmploc jmploc;

	int saveprompt;

	unsigned char result;

	(void)ch;

	savehandler = handler;
	saveprompt = doprompt;
	if (!setjmp(jmploc.loc)) {
		handler = &jmploc;

		pushstackmark(&smark, stackblocksize());

		li = el_line(el);

		setinputmem(li->buffer, li->cursor - li->buffer);
		if (histop == H_APPEND && history(hist, &he, H_CURR) != -1)
			pushstring(he.str, strlen(he.str), NULL);
		parsefile->flags = PF_COMPLETING;

		doprompt = 0;
		errout.fd = -1;
		for (;;)
			parsecmd(0);
	}

	if (exception == EXEOF && !(wordflags & RT_NOCOMPLETE)) {
		char *p;
		int flags = EXP_FULL | EXP_TILDE | EXP_COMPLETE;
		size_t inlen, completelen, start;
		union node n;
		struct arglist arglist;
		struct strlist *strlist;
		int partial;
		if (checkkwd & CHKCMD)
			flags |= EXP_COMMAND | EXP_PATH;
		p = wordtext;
		if (!p)
			STARTSTACKSTR(p);
		inlen = p - (char *)stackblock();
		CHECKSTRSPACE(2, p);
		USTPUTC('\0', p);
		USTPUTC('\0', p);
		p = (char *)stackblock();
		grabstackblock(inlen + 2);
		n.narg.type = NARG;
		n.narg.next = NULL;
		n.narg.text = p;
		n.narg.backquote = NULL;
		arglist.lastp = &arglist.list;
		expandarg(&n, &arglist, flags);
		*arglist.lastp = NULL;
		if (!(strlist = arglist.list))
			goto beep;
		inlen = strlen(strlist->text);
		p = strrchr(strlist->text, '/');
		start = p ? p - strlist->text + 1 : 0;
		if (!(strlist = arglist.list = strlist->next))
			goto beep;
		if (strlist->next) {
			size_t maxwidth, screenwidth, colwidth, matches, cols;
			completelen = strlen(strlist->text);
#ifdef WITH_LOCALE
			maxwidth = str_width(&strlist->text[start]);
#else
			maxwidth = completelen;
#endif
			matches = 1;
			while ((strlist = strlist->next)) {
				size_t curwidth;
				p = strlist->text;
				for (;;) {
					char *q = p;
					int c;
					GETC(c, q);
					if (q > strlist->text + completelen ||
					    memcmp(arglist.list->text + (p - strlist->text),
					           p, q - p))
						break;
					p = q;
				}
				completelen = p - strlist->text;
#ifdef WITH_LOCALE
				curwidth = str_width(&strlist->text[start]);
#else
				curwidth = p + strlen(p) - strlist->text - start;
#endif
				if (maxwidth < curwidth)
					maxwidth = curwidth;
				matches++;
			}
			if (completelen < inlen)
				goto beep;
			if (completelen > inlen) {
				arglist.list->text[completelen] = '\0';
				partial = 1;
				goto docomplete;
			}
			fputc('\n', el_out);
			screenwidth = 0;
#if defined(TIOCGWINSZ)
			{
				struct winsize ws;
				if (ioctl(0, TIOCGWINSZ, &ws) != -1)
					screenwidth = ws.ws_col;
			}
#endif
#if defined(TIOCGSIZE)
			{
				struct ttysize ts;
				if (ioctl(0, TIOCGSIZE, &ts) != -1)
					screenwidth = ts.ts_cols;
			}
#endif
			colwidth = maxwidth + 1;
			cols = (screenwidth + 1) / colwidth;
			if (cols > matches)
				cols = matches;
			if ((screenwidth + 1) % colwidth >= cols - 1)
				colwidth++;
			if (cols > 1) {
				int rows = (matches - 1) / cols + 1;
				int row, col, i;
				strlist = arglist.list;
				for (row = 0; row < rows; ++row) {
					struct strlist *strlist_col = strlist;
					for (col = 0; col < cols; ++col) {
						const char *s = &strlist_col->text[start];
#ifdef WITH_LOCALE
						size_t curwidth = str_width(s);
						fprintf(el_out, "%s%*s", s,
						        col + 1 == cols ? 0 :
						        (int) (colwidth - curwidth),
						        "");
#else
						fprintf(el_out, "%-*s",
						        col + 1 == cols ? 0 :
						        (int) colwidth, s);
#endif
						for (i = 0; i < rows; ++i) {
							strlist_col = strlist_col->next;
							if (!strlist_col)
								goto endrow;
						}
					}
endrow:
					fputc('\n', el_out);
					strlist = strlist->next;
				}
			} else {
				for (strlist = arglist.list; strlist; strlist = strlist->next)
					fprintf(el_out, "%s\n", &strlist->text[start]);
			}
			el_set(el, EL_REFRESH);
			result = CC_NORM;
		} else {
			int style;
			char *startp, *endp;
			STATIC_ASSERT(RT_SQSYNTAX  >> 3 == 1);
			STATIC_ASSERT(RT_DQSYNTAX  >> 3 == 2);
			STATIC_ASSERT(RT_DSQSYNTAX >> 3 == 3);
			partial = 0;
docomplete:
			style = "\5\3\4\2"[(wordflags >> 3) & 3];
			_shell_quote(arglist.list->text + inlen, style, &startp, &endp);
			partial |= endp[-1] == '/';
			if (partial)
				*endp = '\0';
			el_insertstr(el, startp);
			if (!partial)
				el_insertstr(el, " ");
			result = CC_REFRESH;
		}
	} else {
beep:
		el_beep(el);
		result = CC_NORM;
	}

	unwindfiles(file_stop);
	popstackmark(&smark);

	doprompt = saveprompt;
	errout.fd = 2;
	handler = savehandler;

	FORCEINTON;

	return result;
}
#endif

static int
readwrite_histfile(int cmd)
{
	const char *histfile;
	HistEvent he;

	if (pflag || !hist)
		return 0;
	histfile = lookupvar("HISTFILE");
	if (!histfile)
		histfile = expandstr("${HOME-}/.sh_history", 0);
	else if (!*histfile)
		return 0;
	history(hist, &he, cmd, histfile);
	return 1;
}

void
read_histfile(void)
{
	if (readwrite_histfile(H_LOAD))
		sethistsize(histsizeval());
}

void
write_histfile(void)
{
	readwrite_histfile(H_SAVE);
}

void
sethistsize(const char *hs)
{
	int histsize;
	HistEvent he;

	if (hist != NULL) {
		if (hs == NULL || *hs == '\0' ||
		   (histsize = atoi(hs)) < 0)
			histsize = 128;
		history(hist, &he, H_SETSIZE, histsize);
	}
}

void
setterm(const char *term)
{
	if (el != NULL && term != NULL)
		if (el_set(el, EL_TERMINAL, term) != 0) {
			outfmt(out2, "sh: Can't set terminal type %s\n", term);
			outfmt(out2, "sh: Using dumb terminal settings.\n");
		}
}

/*
 *  This command is provided since POSIX decided to standardize
 *  the Korn shell fc command.  Oh well...
 */
int
histcmd(int argc, char **argv)
{
	int ch;
	const char *editor = NULL;
	HistEvent he;
	int lflg = 0, nflg = 0, rflg = 0, sflg = 0;
	int i, retval;
	const char *firststr, *laststr;
	int first, last, direction;
	char *pat = NULL, *repl;	/* ksh "fc old=new" crap */
	static int active = 0;
	struct jmploc jmploc;
	struct jmploc *volatile savehandler;
	int saveactive;
	static char editfile[PATH_MAX + 1];
	FILE *efp;

	if (hist == NULL)
		sh_error("history not active");

	while (not_fcnumber(*argptr) &&
	       (ch = nextopt("e:lnrs")) != '\0')
		switch ((char)ch) {
		case 'e':
			editor = optionarg;
			break;
		case 'l':
			lflg = 1;
			break;
		case 'n':
			nflg = 1;
			break;
		case 'r':
			rflg = 1;
			break;
		case 's':
			sflg = 1;
			break;
		}
	argc -= argptr - argv;
	argv = argptr;

	saveactive = active;
	savehandler = handler;

	/*
	 * If executing...
	 */
	if (lflg == 0 || editor || sflg) {
		lflg = 0;	/* ignore */
		editfile[0] = '\0';
		/*
		 * Catch interrupts to reset active counter and
		 * cleanup temp files.
		 */
		if (setjmp(jmploc.loc)) {
			if (*editfile)
				unlink(editfile);
			active = saveactive;
			handler = savehandler;
			longjmp(handler->loc, 1);
		}
		handler = &jmploc;
		if (++active > MAXHISTLOOPS) {
			active = 0;
			displayhist = 0;
			sh_error("called recursively too many times");
		}
		/*
		 * Set editor.
		 */
		if (sflg == 0) {
			if (editor == NULL &&
			    ((editor = bltinlookup("FCEDIT")) == NULL ||
			     *editor == '\0'))
				editor = DEFEDITOR;
		}
	}

	/*
	 * If executing, parse [old=new] now
	 */
	if (lflg == 0 && sflg && argv[0] &&
	     ((repl = strchr(argv[0], '=')) != NULL)) {
		pat = argv[0];
		*repl++ = '\0';
		argc--, argv++;
	}
	/*
	 * determine [first] and [last]
	 */
	switch (argc) {
	case 0:
		firststr = lflg ? "-16" : "-1";
		laststr = "-1";
		break;
	case 1:
		firststr = argv[0];
		laststr = lflg ? "-1" : argv[0];
		break;
	case 2:
		if (!sflg) {
			firststr = argv[0];
			laststr = argv[1];
			break;
		}
	default:
		sh_error("too many args");
		/* NOTREACHED */
	}
	/*
	 * Turn into event numbers.
	 */
	first = str_to_event(firststr, 0);
	last = str_to_event(laststr, 1);

	if (rflg) {
		i = last;
		last = first;
		first = i;
	}
	/*
	 * XXX - this should not depend on the event numbers
	 * always increasing.  Add sequence numbers or offset
	 * to the history element in next (diskbased) release.
	 */
	direction = first < last ? H_PREV : H_NEXT;

	/*
	 * If editing, grab a temp file.
	 */
	if (editor) {
		int fd;
		INTOFF;		/* easier */
		sprintf(editfile, "%s_shXXXXXX", _PATH_TMP);
		if ((fd = mkstemp(editfile)) < 0)
			sh_error("can't create temporary file %s", editfile);
		if ((efp = fdopen(fd, "w")) == NULL) {
			close(fd);
			sh_error("can't allocate stdio buffer for temp");
		}
	}

	/*
	 * Loop through selected history events.  If listing or executing,
	 * do it now.  Otherwise, put into temp file and call the editor
	 * after.
	 *
	 * The history interface needs rethinking, as the following
	 * convolutions will demonstrate.
	 */
	history(hist, &he, H_FIRST);
	retval = history(hist, &he, H_NEXT_EVENT, first);
	for (;retval != -1; retval = history(hist, &he, direction)) {
		if (lflg) {
			if (!nflg)
				out1fmt("%5d ", he.num);
			out1str(he.str);
		} else {
			const char *s = pat ?
			   fc_replace(he.str, pat, repl) : he.str;

			if (sflg) {
				if (displayhist) {
					out2str(s);
				}

				history(hist, &he, H_FIRST);
				history(hist, &he, H_REPLACE, s, (void *) NULL);
				evalstring(s, 0);
				break;
			} else
				fputs(s, efp);
		}
		/*
		 * At end?  (if we were to lose last, we'd sure be
		 * messed up).
		 */
		if (he.num == last)
			break;
	}
	if (editor) {
		char *editcmd;

		fclose(efp);
		editcmd = stalloc(strlen(editor) + strlen(editfile) + 2);
		sprintf(editcmd, "%s %s", editor, editfile);
		/* XXX - should use no JC command */
		evalstring(editcmd, 0);
		INTON;
		setinputfile(editfile, INPUT_PUSH_FILE);
		unlink(editfile);
		*editfile = '\0';
		parsefile->flags |= PF_HIST;
		histop = H_REPLACE;
		cmdloop(0);
		popfile();
	}

	displayhist = 0;
	active = saveactive;
	handler = savehandler;
	return 0;
}

STATIC const char *
fc_replace(const char *s, char *p, char *r)
{
	char *dest;
	int plen = strlen(p);

	STARTSTACKSTR(dest);
	while (*s) {
		if (*s == *p && strncmp(s, p, plen) == 0) {
			while (*r)
				STPUTC(*r++, dest);
			s += plen;
			*p = '\0';	/* so no more matches */
		} else
			STPUTC(*s++, dest);
	}
	STACKSTRNUL(dest);
	dest = grabstackstr(dest);

	return (dest);
}

int
not_fcnumber(char *s)
{
	if (s == NULL)
		return 0;
        if (*s == '-')
                s++;
	return (!is_number(s));
}

int
str_to_event(const char *str, int last)
{
	HistEvent he;
	const char *s = str;
	int relative = 0;
	int i, retval;

	history(hist, &he, H_FIRST);
	retval = history(hist, &he, H_NEXT);
	if (retval == -1)
		sh_error("history empty");
	switch (*s) {
	case '-':
		relative = 1;
		/*FALLTHROUGH*/
	case '+':
		s++;
	}
	if (is_number(s)) {
		i = atoi(s);
		if (relative) {
			while (retval != -1 && --i)
				retval = history(hist, &he, H_NEXT);
			if (retval == -1)
				retval = history(hist, &he, H_LAST);
		} else {
			int savenum = he.num;
			retval = history(hist, &he, H_NEXT_EVENT, i);
			if (retval == -1) {
				/*
				 * the notion of first and last is
				 * backwards to that of the history package
				 */
				if (last)
					return savenum;
				retval = history(hist, &he, H_LAST);
			}
		}
#if DEBUG
		if (retval == -1)
			sh_error("history number %s not found (internal error)",
				 str);
#else
		(void) retval;
#endif
	} else {
		/*
		 * pattern
		 */
		retval = history(hist, &he, H_PREV_STR, str);
		if (retval == -1)
			sh_error("history pattern not found: %s", str);
	}
	return (he.num);
}
#endif
