/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2020, 2022
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

#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * Code for dealing with input/output redirection.
 */

#include "main.h"
#include "shell.h"
#include "nodes.h"
#include "jobs.h"
#include "options.h"
#include "expand.h"
#include "redir.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"


#define EMPTY -2		/* marks an unused slot in redirtab */
#define CLOSED -1		/* fd opened for redir needs to be closed */

#ifndef PIPE_BUF
# define PIPESIZE 4096		/* amount of buffering in a pipe */
#else
# define PIPESIZE PIPE_BUF
#endif


MKINIT
struct redirtab {
	struct redirtab *next;
	int renamed[10];
};


MKINIT struct redirtab *redirlist;

STATIC int openredirect(union node *);
STATIC void dupredirect(union node *, int);
STATIC int openhere(union node *);


/*
 * Process a list of redirection commands.  If the REDIR_PUSH flag is set,
 * old file descriptors are stashed away so that the redirection can be
 * undone by calling popredir.  If the REDIR_BACKQ flag is set, then the
 * standard output, and the standard error if it becomes a duplicate of
 * stdout, is saved in memory.
 */

void
redirect(union node *redir, int flags)
{
	union node *n;
	struct redirtab *sv;
	int fd;
	int newfd;
	int *p;
	if (!redir)
		return;
	sv = NULL;
	INTOFF;
	if (likely(flags & REDIR_PUSH))
		sv = redirlist;
	n = redir;
	do {
		fd = n->nfile.fd;

		if (sv) {
			p = &sv->renamed[fd];
			if (likely(*p == EMPTY))
				*p = savefd(fd, -1);
		}

		newfd = openredirect(n);

		if (fd == newfd)
			continue;

		dupredirect(n, newfd);
	} while ((n = n->nfile.next));
	INTON;
	if (sv && sv->renamed[2] >= 0)
		preverrout.fd = sv->renamed[2];
}


STATIC int
openredirect(union node *redir)
{
	struct stat sb;
	char *fname;
	int f;

	switch (redir->nfile.type) {
	case NFROM:
		fname = redir->nfile.expfname;
		if ((f = xopen(fname, O_RDONLY)) < 0)
			goto eopen;
		break;
	case NFROMTO:
		fname = redir->nfile.expfname;
		if ((f = xopen(fname, O_RDWR|O_CREAT)) < 0)
			goto ecreate;
		break;
	case NTO:
		/* Take care of noclobber mode. */
		if (Cflag) {
			fname = redir->nfile.expfname;
			if (stat(fname, &sb) < 0) {
				if ((f = xopen(fname, O_WRONLY|O_CREAT|O_EXCL)) < 0)
					goto ecreate;
			} else if (!S_ISREG(sb.st_mode)) {
				if ((f = xopen(fname, O_WRONLY)) < 0)
					goto ecreate;
				if (!fstat(f, &sb) && S_ISREG(sb.st_mode)) {
					close(f);
					errno = EEXIST;
					goto ecreate;
				}
			} else {
				errno = EEXIST;
				goto ecreate;
			}
			break;
		}
		/* FALLTHROUGH */
	case NCLOBBER:
		fname = redir->nfile.expfname;
		if ((f = xopen(fname, O_WRONLY|O_CREAT|O_TRUNC)) < 0)
			goto ecreate;
		break;
	case NAPPEND:
		fname = redir->nfile.expfname;
		if ((f = xopen(fname, O_WRONLY|O_CREAT|O_APPEND)) < 0)
			goto ecreate;
		break;
	case NTOFD:
	case NFROMFD:
		f = redir->ndup.dupfd;
		if (f >= 0 && fcntl(f, F_GETFD) < 0)
			sh_error("%d: %s", f, errnomsg());
		break;
	default:
#ifdef DEBUG
		abort();
#endif
		/* Fall through to eliminate warning. */
	case NHERE:
	case NXHERE:
		f = openhere(redir);
		break;
	}

	return f;
ecreate:
	sh_error("cannot create %s: %s", fname, errnomsg());
eopen:
	sh_error("cannot open %s: %s", fname, errnomsg());
}


STATIC void
dupredirect(union node *redir, int f)
{
	int fd = redir->nfile.fd;
	const char *errmsg = NULL;

	if (redir->nfile.type == NTOFD || redir->nfile.type == NFROMFD) {
		/* if not ">&-" */
		if (f >= 0) {
			if (dup2(f, fd) < 0) {
				errmsg = errnomsg();
				goto err;
			}
			return;
		}
		f = fd;
	} else if (dup2(f, fd) < 0)
		errmsg = errnomsg();

	close(f);
	if (errmsg)
		goto err;

	return;

err:
	sh_error("%d: %s", fd, errmsg);
}


/*
 * Handle here documents.  Normally we fork off a process to write the
 * data to a pipe.  If the document is short, we can stuff the data in
 * the pipe without forking.
 */

STATIC int
openhere(union node *redir)
{
	char *p;
	int pip[2];
	size_t len = 0;

	if (pipe(pip) < 0)
		sh_error("Pipe call failed");

	p = redir->nhere.doc->narg.text;
	if (redir->type == NXHERE) {
		expandarg(redir->nhere.doc, NULL, EXP_QUOTED);
		p = stackblock();
	}

	len = strlen(p);
	if (len <= PIPESIZE) {
		xwrite(pip[1], p, len);
		goto out;
	}

	if (forkshell((struct job *)NULL, (union node *)NULL, FORK_NOJOB) == 0) {
		close(pip[0]);
		signal(SIGINT, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
#ifdef SIGTSTP
		signal(SIGTSTP, SIG_IGN);
#endif
		signal(SIGPIPE, SIG_DFL);
		xwrite(pip[1], p, len);
		_exit(0);
	}
out:
	close(pip[1]);
	return pip[0];
}



/*
 * Undo the effects of the last redirection.
 */

void
popredir(int drop)
{
	struct redirtab *rp;
	int i;

	INTOFF;
	rp = redirlist;
	for (i = 0 ; i < 10 ; i++) {
		switch (rp->renamed[i]) {
		case CLOSED:
			if (!drop)
				close(i);
			break;
		case EMPTY:
			break;
		default:
			if (!drop)
				dup2(rp->renamed[i], i);
			close(rp->renamed[i]);
			break;
		}
	}
	redirlist = rp->next;
	ckfree(rp);
	INTON;
}

/*
 * Undo all redirections.  Called on error or interrupt.
 */

#ifdef mkinit

INCLUDE "redir.h"

RESET {
	/*
	 * Discard all saved file descriptors.
	 */
	unwindredir(0, sub);
}

#endif



/*
 * Move a file descriptor to > 10.  Invokes sh_error on error unless
 * the original file dscriptor is not open.
 */

int
savefd(int from, int ofd)
{
	int newfd;
	int err;

#ifdef F_DUPFD_CLOEXEC
	newfd = fcntl(from, F_DUPFD_CLOEXEC, 10);
#else
	newfd = fcntl(from, F_DUPFD, 10);
#endif
	err = newfd < 0 ? errno : 0;
	if (err != EBADF) {
		close(ofd);
		if (err)
			sh_error("%d: %s", from, strerror(err));
#ifndef F_DUPFD_CLOEXEC
		else
			fcntl(newfd, F_SETFD, FD_CLOEXEC);
#endif
	}

	return newfd;
}


int
redirectsafe(union node *redir, int flags)
{
	int err;
	volatile int saveint;
	jmp_buf *volatile savehandler = handler;
	jmp_buf jmploc;

	SAVEINT(saveint);
	if (!(err = setjmp(jmploc) * 2)) {
		handler = &jmploc;
		redirect(redir, flags);
	}
	handler = savehandler;
	if (err && exception != EXERROR)
		longjmp(*handler, 1);
	RESTOREINT(saveint);
	return err;
}


void unwindredir(struct redirtab *stop, int drop)
{
	while (redirlist != stop)
		popredir(drop);
}


struct redirtab *pushredir(union node *redir)
{
	struct redirtab *sv;
	struct redirtab *q;
	int i;

	q = redirlist;
	if (!redir)
		goto out;

	sv = ckmalloc(sizeof (struct redirtab));
	sv->next = q;
	redirlist = sv;
	for (i = 0; i < 10; i++)
		sv->renamed[i] = EMPTY;

out:
	return q;
}
