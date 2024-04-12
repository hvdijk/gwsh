/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018-2021, 2024
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
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

/*
 * The cd and pwd commands.
 */

#include "shell.h"
#include "var.h"
#include "nodes.h"	/* for jobs.h */
#include "jobs.h"
#include "options.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "exec.h"
#include "redir.h"
#include "main.h"
#include "mystring.h"
#include "show.h"
#include "system.h"
#include "cd.h"

#define CD_PHYSICAL 1
#define CD_PRINT 2

static int docd(const char *, int);
static const char *updatepwd(const char *, const char *);
static int cdopt(void);

#ifdef __GLIBC__
static char *physdir = NULL; /* physical working directory */
#endif

static int
cdopt(void)
{
	int flags = 0;
	int i, j;

	j = 'L';
	while ((i = nextopt("LP"))) {
		if (i != j) {
			flags ^= CD_PHYSICAL;
			j = i;
		}
	}

	return flags;
}

int
cdcmd(int argc, char **argv)
{
	const char *dest;
	const char *path;
	const char *p;
	char c;
	struct stat statb;
	int flags;
	int len;

	flags = cdopt();
	dest = nextarg(0);
	endargs();
	if (!dest)
		dest = bltinlookup(homestr);
	else if (dest[0] == '-' && dest[1] == '\0') {
		dest = bltinlookup("OLDPWD");
		flags |= CD_PRINT;
	}
	if (!dest || !*dest) {
		dest = nullstr;
		flags |= CD_PHYSICAL; /* ensure error */
		goto step6;
	}
	if (*dest == '/')
		goto step6;
	if (*dest == '.') {
		c = dest[1];
dotdot:
		switch (c) {
		case '\0':
		case '/':
			goto step6;
		case '.':
			c = dest[2];
			if (c != '.')
				goto dotdot;
		}
	}
	path = bltinlookup("CDPATH");
	while (p = path, (len = padvance(&path, NULL, dest)) >= 0) {
		c = *p;
		p = stalloc(len);

		if (stat(p, &statb) >= 0 && S_ISDIR(statb.st_mode)) {
			if (c && c != ':')
				flags |= CD_PRINT;
docd:
			if (!docd(p, flags))
				goto out;
			goto err;
		}
	}

step6:
	p = dest;
	goto docd;

err:
	sh_error("%s: %s", dest, errnomsg());
	/* NOTREACHED */
out:
	if (flags & CD_PRINT)
		out1fmt(snlfmt, pwdval());
	return 0;
}


/*
 * Actually do the chdir.  We also call hashcd to let the routines in exec.c
 * know that the current directory has changed.
 */

static int
docd(const char *dest, int flags)
{
	const char *dir = 0;
	int err;

	TRACE(("docd(\"%s\", %d) called\n", dest, flags));

	INTOFF;
	if (!(flags & CD_PHYSICAL)) {
		const char *curdir = getpwd(flags);
		dir = updatepwd(curdir, dest);
		if (dir) {
			dest = dir;

			/* If curdir is a prefix of dest, turn it into a relative path. */
			if (curdir && *curdir == '/') {
				size_t n = strlen(curdir);
				if (n == 1)
					n = 0;
				if (strncmp(dir, curdir, n) == 0 && dir[n] == '/' && dir[n + 1])
					dest += n + 1;
			}
		}
	}
	err = chdir(dest);
	if (err)
		goto out;
	if (!dir)
		dir = getpwd(CD_PHYSICAL);
	setpwd(dir, 1);
	freepwd();
	hashcd();
out:
	INTON;
	return err;
}


/*
 * Update curdir (the name of the current directory) in response to a
 * cd command.
 */

static const char *
updatepwd(const char *curdir, const char *dir)
{
	char *new;
	char *p;
	const char *lim;

#ifdef __CYGWIN__
	/* On cygwin, thanks to drive letters, some absolute paths do
	   not begin with slash; but cygwin includes a function that
	   forces normalization to the posix form */
	char pathbuf[PATH_MAX];
	if (cygwin_conv_path(CCP_WIN_A_TO_POSIX | CCP_RELATIVE, dir, pathbuf,
			     sizeof(pathbuf)) < 0)
		sh_error("can't normalize %s", dir);
	dir = pathbuf;
#endif

	p = sstrdup(dir);
	STARTSTACKSTR(new);
	if (*p != '/') {
		if (!*curdir)
			return 0;
		new = stputs(curdir, new);
	}
	new = makestrspace(strlen(dir) + 2, new);
	lim = (char *)stackblock() + 1;
	if (*p == '/') {
		USTPUTC('/', new);
		p++;
		if (*p == '/' && p[1] != '/') {
			USTPUTC('/', new);
			p++;
			lim++;
		}
	}
	for (;;) {
		char *end = strchrnul(p, '/');
		if (end == p) {
			if (!*end)
				break;
			p++;
			continue;
		}
		switch (*p) {
		case '.':
			if (p[1] == '.' && end - p == 2) {
				struct stat statb;
				USTPUTC('/', new);
				USTPUTC('\0', new);
				if (stat(stackblock(), &statb) < 0)
					sh_error("%s: %s", dir, errnomsg());
				STUNPUTC(new);
				STUNPUTC(new);
				while (new > lim) {
					STUNPUTC(new);
					if (*new == '/')
						break;
				}
				break;
			} else if (end - p == 1)
				break;
			/* fall through */
		default:
			if (new != stackblock() && new[-1] != '/')
				USTPUTC('/', new);
			new = mempcpy(new, p, end - p);
		}
		if (!*end)
			break;
		p = end + 1;
	}
	*new = 0;
	return stackblock();
}


/*
 * Find out what the current directory is.
 */
const char *
getpwd(int flags)
{
	const char *dir;

	if (!(flags & CD_PHYSICAL)) {
		struct stat st1, st2;

		dir = pwdval();
		if (stat(dir, &st1) == 0 && stat(".", &st2) == 0
		    && st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino)
			return dir;
	}

#ifdef __GLIBC__
	freepwd();
	dir = physdir = getcwd(0, 0);
	if (dir)
		return dir;
#else
	static char buf[PATH_MAX];

	if (getcwd(buf, sizeof(buf)))
		return buf;
#endif

	sh_warnx("cannot determine the current directory: %s", errnomsg());
	return NULL;
}

int
pwdcmd(int argc, char **argv)
{
	int flags;
	const char *curdir;

	flags = cdopt();
	endargs();
	curdir = getpwd(flags);
	if (curdir)
		out1fmt(snlfmt, curdir);
	freepwd();
	return !curdir;
}

void
setpwd(const char *val, int setold)
{
	if (setold)
		setvar("OLDPWD", pwdval(), VEXPORT);
	setvar("PWD", val, VEXPORT);
}

void
freepwd(void)
{
#ifdef __GLIBC__
	free(physdir);
	physdir = NULL;
#endif
}
