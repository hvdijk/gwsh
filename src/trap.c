/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018, 2020-2021
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

#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "main.h"
#include "nodes.h"	/* for other headers */
#include "eval.h"
#include "jobs.h"
#include "show.h"
#include "options.h"
#include "syntax.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "trap.h"
#include "mystring.h"

/*
 * Sigmode records the current value of the signal handlers for the various
 * modes.  A value of zero means that the current handler is not known.
 * S_HARD_IGN indicates that the signal was ignored on entry to the shell,
 */

#define S_DFL 1			/* default signal handling (SIG_DFL) */
#define S_CATCH 2		/* signal is caught */
#define S_IGN 3			/* signal is ignored (SIG_IGN) */
#define S_HARD_IGN 4		/* signal is ignored permenantly */
#define S_RESET 5		/* temporary - to reset a hard ignored sig */


/* trap handler commands */
static char *trap[NSIG];
/* number of non-null traps */
int trapcnt;
/* current value of signal */
char sigmode[NSIG - 1];
/* indicates specified signal received */
static char gotsig[NSIG - 1];
/* last pending signal */
volatile sig_atomic_t pending_sig;
/* received SIGCHLD */
int gotsigchld;

extern const char *signal_names[];
extern const int signal_names_length;

static int decode_signum(const char *);

sigset_t sigset_empty, sigset_full;

#ifdef mkinit
INCLUDE "trap.h"
INIT {
	sigemptyset(&sigset_empty);
	sigfillset(&sigset_full);
	sigprocmask(SIG_SETMASK, &sigset_empty, 0);
	sigmode[SIGCHLD - 1] = S_DFL;
	setsignal(SIGCHLD, 0);
}
#endif

/*
 * The trap builtin.
 */

int
trapcmd(int argc, char **argv)
{
	char *action;
	char **ap;
	int signo;
	int status = 0;

	signal_names[0] = "EXIT";

	nextopt(nullstr);
	ap = argptr;
	if (!*ap) {
		for (signo = 0 ; signo < NSIG ; signo++) {
			if (trap[signo] != NULL) {
				if (signo < signal_names_length && signal_names[signo])
					out1fmt(
						"trap -- %s %s\n",
						shell_quote(trap[signo], 0),
						signal_names[signo]);
				else
					out1fmt(
						"trap -- %s %d\n",
						shell_quote(trap[signo], 0),
						signo);
			}
		}
		return 0;
	}
	if (trapcnt < 0)
		clear_traps();
	if (!ap[1] || decode_signum(*ap) >= 0)
		action = NULL;
	else
		action = *ap++;
	while (*ap) {
		if ((signo = decode_signal(*ap)) < 0) {
			sh_warnx("%s: bad trap", *ap);
			status = 1;
		} else {
			sigprocmask(SIG_SETMASK, &sigset_full, 0);
			if (action) {
				if (action[0] == '-' && action[1] == '\0')
					action = NULL;
				else {
					if (*action)
						trapcnt++;
					action = savestr(action);
				}
			}
			if (trap[signo]) {
				if (*trap[signo])
					trapcnt--;
				ckfree(trap[signo]);
			}
			trap[signo] = action;
			if (signo != 0)
				setsignal(signo, 0);
			sigprocmask(SIG_SETMASK, &sigset_empty, 0);
		}
		ap++;
	}
	return status;
}



/*
 * Clear traps.
 */

void
clear_traps(void)
{
	char **tp;

	INTOFF;
	trapcnt = trapcnt <= 0 ? 0 : -1;
	for (tp = trap ; tp < &trap[NSIG] ; tp++) {
		if (*tp && **tp) {	/* trap not NULL or SIG_IGN */
			if (!trapcnt) {
				ckfree(*tp);
				*tp = NULL;
			} else if (tp != &trap[0])
				setsignal(tp - trap, 0);
		}
	}
	INTON;
}

#ifdef mkinit
RESET {
	if (sub)
		clear_traps();
}
#endif



/*
 * Set the signal handler for the specified signal.  The routine figures
 * out what it should be set to.
 */

void
setsignal(int signo, int subshell)
{
	int action;
	char *t, tsig;
	struct sigaction act;

	if ((t = trap[signo]) == NULL)
		action = S_DFL;
	else if (*t == '\0')
		action = S_IGN;
	else if (have_traps())
		action = S_CATCH;
	else
		action = S_DFL;
	if (action == S_DFL && !subshell) {
		switch (signo) {
		case SIGINT:
			if (iflag)
				action = S_CATCH;
			break;
		case SIGQUIT:
#ifdef DEBUG
			if (debug)
				break;
#endif
			/* FALLTHROUGH */
		case SIGTERM:
			if (iflag)
				action = S_IGN;
			break;
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			if (mflag)
				action = S_IGN;
			break;
		}
	}

	if (signo == SIGCHLD)
		action = S_CATCH;

	t = &sigmode[signo - 1];
	tsig = *t;
	if (tsig == 0) {
		/*
		 * current setting unknown
		 */
		if (sigaction(signo, 0, &act) == -1) {
			/*
			 * Pretend it worked; maybe we should give a warning
			 * here, but other shells don't. We don't alter
			 * sigmode, so that we retry every time.
			 */
			return;
		}
		if (act.sa_handler == SIG_IGN)
			tsig = S_HARD_IGN;
		else
			tsig = S_RESET;	/* force to be set */
	}
	if (tsig == S_HARD_IGN || tsig == action)
		return;
	switch (action) {
	case S_CATCH:
		act.sa_handler = onsig;
		break;
	case S_IGN:
		act.sa_handler = SIG_IGN;
		break;
	default:
		act.sa_handler = SIG_DFL;
	}
	*t = action;
	act.sa_flags = 0;
	sigfillset(&act.sa_mask);
	sigaction(signo, &act, 0);
}

/*
 * Ignore a signal.
 */

void
ignoresig(int signo)
{
	if (sigmode[signo - 1] != S_IGN && sigmode[signo - 1] != S_HARD_IGN) {
		signal(signo, SIG_IGN);
		sigmode[signo - 1] = S_IGN;
	}
}



/*
 * Signal handler.
 */

void
onsig(int signo)
{
	if (signo == SIGCHLD) {
		gotsigchld = 1;
		if (!have_traps() || !trap[SIGCHLD])
			return;
	}

	if (signo == SIGINT && (!have_traps() || !trap[SIGINT])) {
		if (!suppressint)
			onint();
		intpending = 1;
		return;
	}

	gotsig[signo - 1] = 1;
	pending_sig = signo;
}



/*
 * Called to execute a trap.  Perhaps we should avoid entering new trap
 * handlers while we are executing a trap handler.
 */

void dotrap(void)
{
	char *p;
	char *q;
	int i;
	int status;

	if (!pending_sig)
		return;

	status = savestatus;
	savestatus = exitstatus;

	pending_sig = 0;
	barrier();

	for (i = 0, q = gotsig; i < NSIG - 1; i++, q++) {
		if (!*q)
			continue;

		if (evalskip) {
			pending_sig = i + 1;
			break;
		}

		*q = 0;

		p = trap[i + 1];
		evalstring(p, 0);
		if (evalskip != SKIPFUNC)
			exitstatus = savestatus;
	}

	savestatus = status;
}



/*
 * Called to exit the shell.
 */

void
exitshell(void)
{
	struct jmploc loc;
	char *p;

	savestatus = exitstatus;
	TRACE(("pid %d, exitshell(%d)\n", getpid(), savestatus));
	if (setjmp(loc.loc)) {
		savestatus = exitstatus;
		goto out;
	}
	handler = &loc;
	if (have_traps()) {
		dotrap();
		if ((p = trap[0])) {
			evalskip = 0;
			evalstring(p, 0);
		}
	}
out:
	setjmp(loc.loc);
	flushall();
	releasetty();
	_exit(savestatus);
	/* NOTREACHED */
}

static int decode_signum(const char *string)
{
	int signo = -1;

	if (is_number(string)) {
		signo = atoi(string);
		if (signo >= NSIG)
			signo = -1;
	}

	return signo;
}

int decode_signal(const char *string)
{
	int signo;

	signo = decode_signum(string);
	if (signo >= 0)
		return signo;

	for (signo = 0; signo < signal_names_length; signo++) {
		if (signal_names[signo] && !strcasecmp(string, signal_names[signo])) {
			return signo;
		}
	}

	return -1;
}
