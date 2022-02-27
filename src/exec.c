/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2018, 2020-2022
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
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

/*
 * When commands are first encountered, they are entered in a hash table.
 * This ensures that a full path search will not have to be done for them
 * on each invocation.
 *
 * We should investigate converting to a linear search, even though that
 * would make the command name "hash" a misnomer.
 */

#include "shell.h"
#include "main.h"
#include "nodes.h"
#include "parser.h"
#include "redir.h"
#include "eval.h"
#include "exec.h"
#include "builtins.h"
#include "var.h"
#include "options.h"
#include "output.h"
#include "syntax.h"
#include "memalloc.h"
#include "error.h"
#include "init.h"
#include "mystring.h"
#include "show.h"
#include "jobs.h"
#include "alias.h"
#include "system.h"
#include "cd.h"


#define CMDTABLESIZE 31		/* should be prime */
#define ARB 1			/* actual size determined at run time */



struct tblentry {
	struct tblentry *next;	/* next entry in hash chain */
	union param param;	/* definition of builtin function */
	short cmdtype;		/* index identifying command */
	char rehash;		/* if set, cd done since entry created */
	char cmdname[ARB];	/* name of command */
};


STATIC struct tblentry *cmdtable[CMDTABLESIZE];

STATIC void tryexec(char *, char **, char **);
STATIC void printentry(struct tblentry *);
STATIC void clearcmdentry(void);
STATIC struct tblentry *cmdlookup(const char *, int);
STATIC void delete_cmd_entry(void);
STATIC void addcmdentry(char *, struct cmdentry *);
STATIC int describe_command(struct output *, char *, const char *, const char *, int);


/*
 * Exec a program.  Never returns.  If you change this routine, you may
 * have to change the find_command routine as well.
 */

void
shellexec(char **argv, const char *path, int idx)
{
	char *cmdname;
	const char *errmsg;
	int e;
	char **envp, **envpp;
	int exerrno;

	envp = environment();
	for (envpp = envp; *envpp; envpp++)
		*strchr(*envpp, '\0') = '=';
	if (strchr(argv[0], '/') != NULL) {
		tryexec(argv[0], argv, envp);
		e = errno;
		errmsg = errnomsg();
	} else {
		e = 0;
		errmsg = "not found";
		if (path && *path) {
			while (padvance(&path, NULL, argv[0]) >= 0) {
				cmdname = stackblock();
				if (--idx < 0) {
					tryexec(cmdname, argv, envp);
					if (errno != ENOENT && errno != ENOTDIR) {
						e = errno;
						errmsg = errnomsg();
					}
				}
			}
		}
	}

	/* Map to POSIX errors */
	switch (e) {
	default:
		exerrno = 126;
		break;
	case 0:
	case ELOOP:
	case ENAMETOOLONG:
	case ENOENT:
	case ENOTDIR:
		exerrno = 127;
		break;
	}
	exitstatus = exerrno;
	TRACE(("shellexec failed for %s, errno %d, suppressint %d\n",
		argv[0], e, suppressint ));
	exerror(EXEXIT, "%s: %s", argv[0], errmsg);
	/* NOTREACHED */
}


STATIC void
tryexec(char *cmd, char **argv, char **envp)
{
#ifdef SELF_EXEC_PATH
	char *const path_shell = SELF_EXEC_PATH;
#else
	char *const path_shell = _PATH_BSHELL;
#endif

repeat:
#ifdef SYSV
	do {
		execve(cmd, argv, envp);
	} while (errno == EINTR);
#else
	execve(cmd, argv, envp);
#endif
	if (cmd != path_shell && errno == ENOEXEC) {
		*argv-- = cmd;
		*argv-- = "-";
		*argv = "sh";
		cmd = path_shell;
		goto repeat;
	}
}



/*
 * Do a path search.  The variable path (passed by reference) should be
 * set to the start of the path before the first call; padvance will update
 * this value as it proceeds.  Successive calls to padvance will return
 * the possible path expansions in sequence.  If pathopt is not NULL, then
 * if an option (indicated by a percent sign) appears in the path entry,
 * *pathopt will be set to point to it; otherwise *pathopt will be set to
 * NULL.
 */

int padvance(const char **path, const char **pathopt, const char *name)
{
	const char *p;
	char *q;
	const char *start;
	size_t len;

	if (*path == NULL)
		return -1;
	start = *path;
	for (p = start ; *p && *p != ':' && (!pathopt || *p != '%') ; p++);
	len = p - start + strlen(name) + 2;	/* "2" is for '/' and '\0' */
	while (stackblocksize() < len)
		growstackblock();
	q = stackblock();
	if (p != start) {
		memcpy(q, start, p - start);
		q += p - start;
		if (p[-1] != '/')
			*q++ = '/';
	}
	strcpy(q, name);
	if (pathopt) {
		*pathopt = NULL;
		if (*p == '%') {
			*pathopt = ++p;
			while (*p && *p != ':')  p++;
		}
	}
	if (*p == ':')
		*path = p + 1;
	else
		*path = NULL;
	return len;
}



/*** Command hashing code ***/


int
hashcmd(int argc, char **argv)
{
	struct tblentry **pp;
	struct tblentry *cmdp;
	int c;
	struct cmdentry entry;
	char *name;

	while (nextopt("r") != '\0') {
		clearcmdentry();
		return 0;
	}
	if (*argptr == NULL) {
		for (pp = cmdtable ; pp < &cmdtable[CMDTABLESIZE] ; pp++) {
			for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
				if (cmdp->cmdtype == CMDNORMAL)
					printentry(cmdp);
			}
		}
		return 0;
	}
	c = 0;
	while ((name = *argptr) != NULL) {
		if ((cmdp = cmdlookup(name, 0)) != NULL
		 && cmdp->cmdtype == CMDNORMAL)
			delete_cmd_entry();
		find_command(name, &entry, DO_ERR,
		             pathval(), fpathval());
		if (entry.cmdtype == CMDUNKNOWN)
			c = 1;
		argptr++;
	}
	return c;
}


STATIC void
printentry(struct tblentry *cmdp)
{
	int idx;
	const char *path;
	char *name;

	idx = cmdp->param.index;
	path = pathval();
	do {
		padvance(&path, NULL, cmdp->cmdname);
	} while (--idx >= 0);
	name = stackblock();
	out1str(name);
	out1fmt(snlfmt, cmdp->rehash ? "*" : nullstr);
}



/*
 * Resolve a command name.  If you change this routine, you may have to
 * change the shellexec routine as well.
 */

int
find_command(char *name, struct cmdentry *entry, int act, const char *path, const char *fpath)
{
	struct tblentry *cmdp;
	int idx;
	int prev;
	char *fullname;
	struct stat statb;
	int e;
	int updatetbl;
	struct builtincmd *bcmd;
	int len;
	int checkexec;

	/* If name contains a slash, don't use PATH or hash table */
	if (strchr(name, '/') != NULL) {
		entry->u.index = -1;
		if (act & DO_ABS) {
			while (stat(name, &statb) < 0) {
				e = errno;
#ifdef SYSV
				if (e == EINTR)
					continue;
#endif
				entry->cmdtype = CMDUNKNOWN;
				return e;
			}
			if (!S_ISREG(statb.st_mode)
#ifdef HAVE_FACCESSAT
			    || !test_file_access(name, X_OK)
#else
			    || !test_access(&statb, X_OK)
#endif
			   ) {
				entry->cmdtype = CMDUNKNOWN;
				return EACCES;
			}
		}
		entry->cmdtype = CMDNORMAL;
		return 0;
	}

	updatetbl = !path || (path == pathval());
	if (!updatetbl)
		act |= DO_ALTPATH;

	/* If name is in the table, check answer will be ok */
	if ((cmdp = cmdlookup(name, 0)) != NULL) {
		int bit;

		switch (cmdp->cmdtype) {
		default:
#if DEBUG
			abort();
#endif
		case CMDNORMAL:
			bit = DO_ALTPATH;
			break;
		case CMDFUNCTION:
			bit = DO_NOFUNC;
			break;
		case CMDBUILTIN:
			bit = 0;
			break;
		}
		if (act & bit) {
			updatetbl = 0;
			cmdp = NULL;
		} else if (cmdp->rehash == 0)
			/* if not invalidated by cd, we're done */
			goto success;
	}

	/* Check for builtin next */
	bcmd = find_builtin(name);
	if (bcmd)
		goto builtin_success;

	/* We have to search path. */
	prev = -1;		/* where to start */
	if (cmdp && cmdp->rehash) {	/* doing a rehash */
		if (cmdp->cmdtype != CMDBUILTIN)
			prev = cmdp->param.index;
	}

	e = 0;
	idx = -1;
	checkexec = 1;
loop:
	if (path && *path) {
		while ((len = padvance(&path, NULL, name)) >= 0) {
			fullname = stackblock();
			idx++;
			/* if rehash, don't redo absolute path names */
			if (fullname[0] == '/' && idx <= prev) {
				if (idx < prev)
					continue;
				TRACE(("searchexec \"%s\": no change\n", name));
				goto success;
			}
			while (stat(fullname, &statb) < 0) {
#ifdef SYSV
				if (errno == EINTR)
					continue;
#endif
				if (errno != ENOENT && errno != ENOTDIR)
					e = errno;
				goto loop;
			}
			e = EACCES; /* if we fail, this will be the error */
			if (!S_ISREG(statb.st_mode))
				continue;
			if (!checkexec) { /* this is an FPATH directory */
				stalloc(len);
				readcmdfile(fullname);
				if ((cmdp = cmdlookup(name, 0)) == NULL ||
				    cmdp->cmdtype != CMDFUNCTION)
					sh_error("%s not defined in %s", name,
						 fullname);
				stunalloc(fullname);
				goto success;
			}
#ifdef HAVE_FACCESSAT
			if (!test_file_access(fullname, X_OK))
#else
			if (!test_access(&statb, X_OK))
#endif
				continue;
			TRACE(("searchexec \"%s\" returns \"%s\"\n", name, fullname));
			if (!updatetbl) {
				entry->cmdtype = CMDNORMAL;
				entry->u.index = idx;
				return 0;
			}
			INTOFF;
			cmdp = cmdlookup(name, 1);
			cmdp->cmdtype = CMDNORMAL;
			cmdp->param.index = idx;
			INTON;
			goto success;
		}
	}
	if (checkexec--) {
		path = fpath;
		goto loop;
	}

	/* We failed.  If there was an entry for this command, delete it */
	if (cmdp && updatetbl)
		delete_cmd_entry();
	if (act & DO_ERR)
		sh_warnx("%s: %s", name, errmsg(e));
	entry->cmdtype = CMDUNKNOWN;
	entry->u.index = idx;
	return e;

builtin_success:
	if (!updatetbl) {
		entry->cmdtype = CMDBUILTIN;
		entry->u.cmd = bcmd;
		return 0;
	}
	INTOFF;
	cmdp = cmdlookup(name, 1);
	cmdp->cmdtype = CMDBUILTIN;
	cmdp->param.cmd = bcmd;
	INTON;
success:
	cmdp->rehash = 0;
	entry->cmdtype = cmdp->cmdtype;
	entry->u = cmdp->param;
	return 0;
}



/*
 * Search the table of builtin commands.
 */

struct builtincmd *
find_builtin(const char *name)
{
	struct builtincmd *bp;

	bp = bsearch(
		&name, builtincmd, NUMBUILTINS, sizeof(struct builtincmd),
		pstrcmp
	);
	return bp;
}



/*
 * Called when a cd is done.  Marks all commands so the next time they
 * are executed they will be rehashed.
 */

void
hashcd(void)
{
	struct tblentry **pp;
	struct tblentry *cmdp;

	for (pp = cmdtable ; pp < &cmdtable[CMDTABLESIZE] ; pp++) {
		for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
			if (cmdp->cmdtype == CMDNORMAL)
				cmdp->rehash = 1;
		}
	}
}



/*
 * Fix command hash table when PATH changed.
 * Called before PATH is changed.  The argument is the new value of PATH;
 * pathval() still returns the old value at this point.
 * Called with interrupts off.
 */

void
changepath(const char *newval)
{
	clearcmdentry();
}


/*
 * Clear out command entries.
 */

STATIC void
clearcmdentry(void)
{
	struct tblentry **tblp;
	struct tblentry **pp;
	struct tblentry *cmdp;

	INTOFF;
	for (tblp = cmdtable ; tblp < &cmdtable[CMDTABLESIZE] ; tblp++) {
		pp = tblp;
		while ((cmdp = *pp) != NULL) {
			if (cmdp->cmdtype == CMDNORMAL
			 || cmdp->cmdtype == CMDBUILTIN) {
				*pp = cmdp->next;
				ckfree(cmdp);
			} else {
				pp = &cmdp->next;
			}
		}
	}
	INTON;
}



/*
 * Locate a command in the command hash table.  If "add" is nonzero,
 * add the command to the table if it is not already present.  The
 * variable "lastcmdentry" is set to point to the address of the link
 * pointing to the entry, so that delete_cmd_entry can delete the
 * entry.
 *
 * Interrupts must be off if called with add != 0.
 */

struct tblentry **lastcmdentry;


STATIC struct tblentry *
cmdlookup(const char *name, int add)
{
	unsigned int hashval;
	const char *p;
	struct tblentry *cmdp;
	struct tblentry **pp;

	p = name;
	hashval = (unsigned char)*p << 4;
	while (*p)
		hashval += (unsigned char)*p++;
	hashval &= 0x7FFF;
	pp = &cmdtable[hashval % CMDTABLESIZE];
	for (cmdp = *pp ; cmdp ; cmdp = cmdp->next) {
		if (equal(cmdp->cmdname, name))
			break;
		pp = &cmdp->next;
	}
	if (add && cmdp == NULL) {
		cmdp = *pp = ckmalloc(sizeof (struct tblentry) - ARB
					+ strlen(name) + 1);
		cmdp->next = NULL;
		cmdp->cmdtype = CMDUNKNOWN;
		strcpy(cmdp->cmdname, name);
	}
	lastcmdentry = pp;
	return cmdp;
}

/*
 * Delete the command entry returned on the last lookup.
 */

STATIC void
delete_cmd_entry(void)
{
	struct tblentry *cmdp;

	INTOFF;
	cmdp = *lastcmdentry;
	*lastcmdentry = cmdp->next;
	if (cmdp->cmdtype == CMDFUNCTION)
		freefunc(cmdp->param.func);
	ckfree(cmdp);
	INTON;
}



#ifdef notdef
void
getcmdentry(char *name, struct cmdentry *entry)
{
	struct tblentry *cmdp = cmdlookup(name, 0);

	if (cmdp) {
		entry->u = cmdp->param;
		entry->cmdtype = cmdp->cmdtype;
	} else {
		entry->cmdtype = CMDUNKNOWN;
		entry->u.index = 0;
	}
}
#endif


/*
 * Add a new command entry, replacing any existing command entry for
 * the same name - except special builtins.
 */

STATIC void
addcmdentry(char *name, struct cmdentry *entry)
{
	struct tblentry *cmdp;

	cmdp = cmdlookup(name, 1);
	if (cmdp->cmdtype == CMDFUNCTION) {
		freefunc(cmdp->param.func);
	}
	cmdp->cmdtype = entry->cmdtype;
	cmdp->param = entry->u;
	cmdp->rehash = 0;
}


/*
 * Define a shell function.
 */

void
defun(union node *func)
{
	struct cmdentry entry;

	INTOFF;
	entry.cmdtype = CMDFUNCTION;
	entry.u.func = copyfunc(func);
	addcmdentry(func->ndefun.text, &entry);
	INTON;
}


/*
 * Delete a function if it exists.
 */

void
unsetfunc(const char *name)
{
	struct tblentry *cmdp;

	if ((cmdp = cmdlookup(name, 0)) != NULL &&
	    cmdp->cmdtype == CMDFUNCTION)
		delete_cmd_entry();
}

/*
 * Locate and print what a word is...
 */

int
typecmd(int argc, char **argv)
{
	int err = 0;

	nextopt(nullstr);
	argv = argptr;
	while (*argv) {
		err |= describe_command(out1, *argv++, NULL, NULL, 1);
	}
	return err;
}

STATIC int
describe_command(
	struct output *out, char *command, const char *path,
	const char *fpath, int verbose)
{
	struct cmdentry entry;
	struct tblentry *cmdp;
	const struct alias *ap;
	int e;

	/* First look at the keywords */
	if (findkwd(command)) {
		outstr(command, out);
		if (verbose) {
			outstr(" is a shell keyword", out);
		}
		goto out;
	}

	/* Then look at the aliases */
	if ((ap = lookupalias(command, 0)) != NULL) {
		if (verbose) {
			outfmt(out, "%s is an alias for %s", command, ap->val);
		} else {
			outstr("alias ", out);
			printalias(ap);
			return 0;
		}
		goto out;
	}

	/* Then if the standard search path is used, check if it is
	 * a tracked alias.
	 */
	if (path == NULL) {
		path = pathval();
		fpath = fpathval();
		cmdp = cmdlookup(command, 0);
	} else {
		cmdp = NULL;
	}

	if (cmdp != NULL) {
		entry.cmdtype = cmdp->cmdtype;
		entry.u = cmdp->param;
		e = 0;
	} else {
		/* Finally use brute force */
		e = find_command(command, &entry, DO_ABS, path, fpath);
	}

	switch (entry.cmdtype) {
	case CMDNORMAL: {
		int j = entry.u.index;
		const char *p;
		if (verbose) {
			outfmt(
				out, "%s is%s ",
				command,
				cmdp ? " a tracked alias for" : nullstr
			);
		}
		if (j == -1) {
			p = command;
		} else {
			do {
				padvance(&path, NULL, command);
			} while (--j >= 0);
			p = stackblock();
		}
		if (*p != '/') {
			char *d = getpwd();
			if (d) {
				outstr(d, out);
				if (strchr(d, '\0')[-1] != '/')
					outc('/', out);
				free(d);
			}
		}
		outstr(p, out);
		break;
	}

	case CMDFUNCTION:
		outstr(command, out);
		if (verbose) {
			outstr(" is a shell function", out);
		}
		break;

	case CMDBUILTIN:
		outstr(command, out);
		if (verbose) {
			outfmt(
				out, " is a %sshell builtin",
				entry.u.cmd->flags & BUILTIN_SPECIAL ?
					"special " : nullstr
			);
		}
		break;

	default:
		if (verbose) {
			const char *msg = e ? strerror(e) : "not found";
			outfmt(out2, "%s: %s\n", command, msg);
		}
		return 127;
	}

out:
	outc('\n', out);
	return 0;
}

int
commandcmd(int argc, char *argv[])
{
	char *cmd;
	int c;
	enum {
		VERIFY_BRIEF = 1,
		VERIFY_VERBOSE = 2,
	} verify = 0;
	const char *path = NULL;

	while ((c = nextopt("pvV")) != '\0')
		if (c == 'V')
			verify |= VERIFY_VERBOSE;
		else if (c == 'v')
			verify |= VERIFY_BRIEF;
#ifdef DEBUG
		else if (c != 'p')
			abort();
#endif
		else
			path = defpath;

	cmd = *argptr;
	if (verify && cmd)
		return describe_command(out1, cmd, path, NULL, verify - VERIFY_BRIEF);

	return 0;
}

#ifdef HAVE_FACCESSAT
#ifdef HAVE_TRADITIONAL_FACCESSAT
static int
has_exec_bit_set(const char *path)
{
	struct stat st;

	if (stat(path, &st))
		return 0;
	return st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH);
}
#endif

int
test_file_access(const char *path, int mode)
{
#ifdef HAVE_TRADITIONAL_FACCESSAT
	if (mode == X_OK && geteuid() == 0 && !has_exec_bit_set(path))
		return 0;
#endif
	return !faccessat(AT_FDCWD, path, mode, AT_EACCESS);
}
#else	/* HAVE_FACCESSAT */
/*
 * The manual, and IEEE POSIX 1003.2, suggests this should check the mode bits,
 * not use access():
 *
 *	True shall indicate only that the write flag is on.  The file is not
 *	writable on a read-only file system even if this test indicates true.
 *
 * Unfortunately IEEE POSIX 1003.1-2001, as quoted in SuSv3, says only:
 *
 *	True shall indicate that permission to read from file will be granted,
 *	as defined in "File Read, Write, and Creation".
 *
 * and that section says:
 *
 *	When a file is to be read or written, the file shall be opened with an
 *	access mode corresponding to the operation to be performed.  If file
 *	access permissions deny access, the requested operation shall fail.
 *
 * and of course access permissions are described as one might expect:
 *
 *     * If a process has the appropriate privilege:
 *
 *        * If read, write, or directory search permission is requested,
 *          access shall be granted.
 *
 *        * If execute permission is requested, access shall be granted if
 *          execute permission is granted to at least one user by the file
 *          permission bits or by an alternate access control mechanism;
 *          otherwise, access shall be denied.
 *
 *   * Otherwise:
 *
 *        * The file permission bits of a file contain read, write, and
 *          execute/search permissions for the file owner class, file group
 *          class, and file other class.
 *
 *        * Access shall be granted if an alternate access control mechanism
 *          is not enabled and the requested access permission bit is set for
 *          the class (file owner class, file group class, or file other class)
 *          to which the process belongs, or if an alternate access control
 *          mechanism is enabled and it allows the requested access; otherwise,
 *          access shall be denied.
 *
 * and when I first read this I thought:  surely we can't go about using
 * open(O_WRONLY) to try this test!  However the POSIX 1003.1-2001 Rationale
 * section for test does in fact say:
 *
 *	On historical BSD systems, test -w directory always returned false
 *	because test tried to open the directory for writing, which always
 *	fails.
 *
 * and indeed this is in fact true for Seventh Edition UNIX, UNIX 32V, and UNIX
 * System III, and thus presumably also for BSD up to and including 4.3.
 *
 * Secondly I remembered why using open() and/or access() are bogus.  They
 * don't work right for detecting read and write permissions bits when called
 * by root.
 *
 * Interestingly the 'test' in 4.4BSD was closer to correct (as per
 * 1003.2-1992) and it was implemented efficiently with stat() instead of
 * open().
 *
 * This was apparently broken in NetBSD around about 1994/06/30 when the old
 * 4.4BSD implementation was replaced with a (arguably much better coded)
 * implementation derived from pdksh.
 *
 * Note that modern pdksh is yet different again, but still not correct, at
 * least not w.r.t. 1003.2-1992.
 *
 * As I think more about it and read more of the related IEEE docs I don't like
 * that wording about 'test -r' and 'test -w' in 1003.1-2001 at all.  I very
 * much prefer the original wording in 1003.2-1992.  It is much more useful,
 * and so that's what I've implemented.
 *
 * (Note that a strictly conforming implementation of 1003.1-2001 is in fact
 * totally useless for the case in question since its 'test -w' and 'test -r'
 * can never fail for root for any existing files, i.e. files for which 'test
 * -e' succeeds.)
 *
 * The rationale for 1003.1-2001 suggests that the wording was "clarified" in
 * 1003.1-2001 to align with the 1003.2b draft.  1003.2b Draft 12 (July 1999),
 * which is the latest copy I have, does carry the same suggested wording as is
 * in 1003.1-2001, with its rationale saying:
 *
 * 	This change is a clarification and is the result of interpretation
 * 	request PASC 1003.2-92 #23 submitted for IEEE Std 1003.2-1992.
 *
 * That interpretation can be found here:
 *
 *   http://www.pasc.org/interps/unofficial/db/p1003.2/pasc-1003.2-23.html
 *
 * Not terribly helpful, unfortunately.  I wonder who that fence sitter was.
 *
 * Worse, IMVNSHO, I think the authors of 1003.2b-D12 have mis-interpreted the
 * PASC interpretation and appear to be gone against at least one widely used
 * implementation (namely 4.4BSD).  The problem is that for file access by root
 * this means that if test '-r' and '-w' are to behave as if open() were called
 * then there's no way for a shell script running as root to check if a file
 * has certain access bits set other than by the grotty means of interpreting
 * the output of 'ls -l'.  This was widely considered to be a bug in V7's
 * "test" and is, I believe, one of the reasons why direct use of access() was
 * avoided in some more recent implementations!
 *
 * I have always interpreted '-r' to match '-w' and '-x' as per the original
 * wording in 1003.2-1992, not the other way around.  I think 1003.2b goes much
 * too far the wrong way without any valid rationale and that it's best if we
 * stick with 1003.2-1992 and test the flags, and not mimic the behaviour of
 * open() since we already know very well how it will work -- existance of the
 * file is all that matters to open() for root.
 *
 * Unfortunately the SVID is no help at all (which is, I guess, partly why
 * we're in this mess in the first place :-).
 *
 * The SysV implementation (at least in the 'test' builtin in /bin/sh) does use
 * access(name, 2) even though it also goes to much greater lengths for '-x'
 * matching the 1003.2-1992 definition (which is no doubt where that definition
 * came from).
 *
 * The ksh93 implementation uses access() for '-r' and '-w' if
 * (euid==uid&&egid==gid), but uses st_mode for '-x' iff running as root.
 * i.e. it does strictly conform to 1003.1-2001 (and presumably 1003.2b).
 */
int
test_access(const struct stat *sp, int stmode)
{
	gid_t *groups;
	register int n;
	uid_t euid;
	int maxgroups;

	/*
	 * I suppose we could use access() if not running as root and if we are
	 * running with ((euid == uid) && (egid == gid)), but we've already
	 * done the stat() so we might as well just test the permissions
	 * directly instead of asking the kernel to do it....
	 */
	euid = geteuid();
	if (euid == 0) {
		if (stmode != X_OK)
			return 1;

		/* any bit is good enough */
		stmode = (stmode << 6) | (stmode << 3) | stmode;
	} else if (sp->st_uid == euid)
		stmode <<= 6;
	else if (sp->st_gid == getegid())
		stmode <<= 3;
	else {
		/* XXX stolen almost verbatim from ksh93.... */
		/* on some systems you can be in several groups */
		maxgroups = getgroups(0, NULL);
		groups = stalloc(maxgroups * sizeof(*groups));
		n = getgroups(maxgroups, groups);
		while (--n >= 0) {
			if (groups[n] == sp->st_gid) {
				stmode <<= 3;
				break;
			}
		}
	}

	return sp->st_mode & stmode;
}
#endif	/* HAVE_FACCESSAT */
