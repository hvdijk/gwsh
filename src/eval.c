/*-
 * Copyright (c) 1993
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

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

/*
 * Evaluate a command.
 */

#include "shell.h"
#include "nodes.h"
#include "syntax.h"
#include "expand.h"
#include "parser.h"
#include "jobs.h"
#include "eval.h"
#include "builtins.h"
#include "options.h"
#include "exec.h"
#include "redir.h"
#include "input.h"
#include "output.h"
#include "trap.h"
#include "var.h"
#include "memalloc.h"
#include "error.h"
#include "init.h"
#include "show.h"
#include "mystring.h"
#include "system.h"
#ifndef SMALL
#include "myhistedit.h"
#endif


int evalskip;			/* set if we are skipping commands */
STATIC int skipcount;		/* number of levels to skip */
MKINIT int loopnest;		/* current loop nesting level */
int funcnest;			/* depth of function calls */


char *dotfile;
const char *commandname;
int exitstatus;			/* exit status of last command */
int back_exitstatus;		/* exit status of backquoted command */
int savestatus = -1;		/* exit status of last command outside traps */

STATIC void evaltreenr(union node *, int) attribute((noreturn));
STATIC int evalloop(union node *, int);
STATIC int evalfor(union node *, int);
STATIC int evalcase(union node *, int);
STATIC int evalsubshell(union node *, int);
STATIC void expredir(union node *);
STATIC int evalpipe(union node *, int);
STATIC int evalcommand(union node *, int);
STATIC int evalbltin(const struct builtincmd *, int, char **, int);
STATIC int evalfun(struct funcnode *, int, char **, int);
STATIC void prehash(union node *);
STATIC int eprintlist(struct output *, struct strlist *, int);
STATIC int nullcmd(int, char **);


#define EPL_START   0x01
#define EPL_ASSIGN  0x02
#define EPL_COMMAND 0x04


STATIC const struct builtincmd null = {
	.name = nullstr,
	.builtin = nullcmd,
};


/*
 * Called to reset things in subshells and after an exception.
 */

#ifdef mkinit
INCLUDE "eval.h"

RESET {
	evalskip = 0;
	loopnest = 0;
	savestatus = -1;
}
#endif



/*
 * The eval commmand.
 */

static int evalcmd(int argc, char **argv, int flags)
{
        char *p;
        char *concat;
        char **ap;

        if (argc > 1) {
                p = argv[1];
                if (argc > 2) {
                        STARTSTACKSTR(concat);
                        ap = argv + 2;
                        for (;;) {
                        	concat = stputs(p, concat);
                                if ((p = *ap++) == NULL)
                                        break;
                                STPUTC(' ', concat);
                        }
                        STPUTC('\0', concat);
                        p = grabstackstr(concat);
                }
                return evalstring(p, flags & ~EV_EXIT);
        }
        return 0;
}


/*
 * Execute a command or commands contained in a string.
 */

int
evalstring(const char *s, int flags)
{
	char *p;
	union node *n;
	struct stackmark smark;
	int status;

	p = sstrdup(s);
	setinputstring(p);
	if (flags & EV_LINENO) {
		parsefile->flags |= PF_LINENO;
		plinno = 1;
	}

	setstackmark(&smark);

	status = 0;
	for (; (n = parsecmd(0)) != NEOF; popstackmark(&smark)) {
		int i, eofmask;

		eofmask = -!parser_eof();
		tokpushback &= eofmask;
		i = evaltree(n, flags & ~(EV_EXIT & eofmask));
		if (n)
			status = i;

		if (evalskip)
			break;
	}
	popstackmark(&smark);
	popfile();
	stunalloc(p);

	return status;
}



/*
 * Evaluate a parse tree.  The value is left in the global variable
 * exitstatus.
 */

int
evaltree(union node *n, int flags)
{
	int checkexit = 0;
	int (*evalfn)(union node *, int);
	struct stackmark smark;
	unsigned isor;
	int status = 0;

	setstackmark(&smark);

	if (n == NULL || nflag) {
		if (n == NULL)
			TRACE(("evaltree(NULL) called\n"));
		goto out;
	}

	dotrap();

#ifndef SMALL
	displayhist = 1;	/* show history substitutions done with fc */
#endif
	TRACE(("pid %d, evaltree(%p: %d, %d) called\n",
	    getpid(), n, n->type, flags));
	switch (n->type) {
	default:
#ifdef DEBUG
		out1fmt("Node type = %d\n", n->type);
		flushall();
		break;
#endif
	case NNOT:
		status = evaltree(n->nnot.com,
				  (flags & ~EV_EXIT) | EV_TESTED);
		if (evalskip)
			break;
		status = !status;
		goto setstatus;
	case NREDIR:
		errlinno = lineno = n->nredir.linno;
		expredir(n->nredir.redirect);
		pushredir(n->nredir.redirect);
		status = redirectsafe(n->nredir.redirect, REDIR_PUSH);
		if (!status)
			status = evaltree(n->nredir.n, flags & ~EV_EXIT);
		if (n->nredir.redirect)
			popredir(0);
		checkexit = -!(flags & EV_TESTED);
		goto setstatus;
	case NCMD:
		evalfn = evalcommand;
checkexit:
		checkexit = -!(flags & EV_TESTED);
		goto calleval;
	case NFOR:
		evalfn = evalfor;
		goto calleval;
	case NWHILE:
	case NUNTIL:
		evalfn = evalloop;
		goto calleval;
	case NSUBSHELL:
	case NBACKGND:
		evalfn = evalsubshell;
		goto checkexit;
	case NPIPE:
		evalfn = evalpipe;
		goto checkexit;
	case NCASE:
		evalfn = evalcase;
		goto calleval;
	case NAND:
	case NOR:
	case NSEMI:;
		STATIC_ASSERT(NAND + 1 == NOR);
		STATIC_ASSERT(NOR + 1 == NSEMI);
		isor = n->type - NAND;
		status = evaltree(n->nbinary.ch1,
				  (flags & ~EV_EXIT) | (~isor & EV_TESTED));
		if ((!status) == isor || evalskip)
			break;
		n = n->nbinary.ch2;
evaln:
		evalfn = evaltree;
calleval:
		status = evalfn(n, flags);
		goto setstatus;
	case NIF:
		status = evaltree(n->nif.test,
				  (flags & ~EV_EXIT) | EV_TESTED);
		if (evalskip)
			break;
		if (!status) {
			n = n->nif.ifpart;
			goto evaln;
		} else if (n->nif.elsepart) {
			n = n->nif.elsepart;
			goto evaln;
		}
		status = 0;
		goto setstatus;
	case NDEFUN:
		defun(n);
setstatus:
		exitstatus = status;
		break;
	}
out:
	if (checkexit & status && eflag)
		goto exexit;

	waitforjob(NULL);
	dotrap();

	if (flags & EV_EXIT) {
exexit:
		exraise(EXEXIT);
	}

	popstackmark(&smark);

	return exitstatus;
}

STATIC void
evaltreenr(union node *n, int flags)
{
	evaltree(n, EV_EXIT | flags);
	abort();
}

static int skiploop(void)
{
	int skip = evalskip;

	switch (skip) {
	case 0:
		break;

	case SKIPBREAK:
	case SKIPCONT:
		if (likely(--skipcount <= 0)) {
			evalskip = 0;
			break;
		}

		skip = SKIPBREAK;
		break;
	}

	return skip;
}


STATIC int
evalloop(union node *n, int flags)
{
	int skip;
	int status;

	loopnest++;
	status = 0;
	flags &= ~EV_EXIT;
	do {
		int i;

		i = evaltree(n->nbinary.ch1,
			     (flags & ~EV_EXIT) | EV_TESTED);
		skip = skiploop();
		if (skip & SKIPFUNC)
			status = i;
		if (skip)
			continue;
		if (n->type != NWHILE)
			i = !i;
		if (i != 0)
			break;
		status = evaltree(n->nbinary.ch2, flags);
		skip = skiploop();
	} while (!(skip & ~SKIPCONT));
	loopnest--;

	return status;
}



STATIC int
evalfor(union node *n, int flags)
{
	struct arglist arglist;
	union node *argp;
	struct strlist *sp;
	int status;

	errlinno = lineno = n->nfor.linno;

	arglist.lastp = &arglist.list;
	for (argp = n->nfor.args ; argp ; argp = argp->narg.next) {
		expandarg(argp, &arglist, EXP_FULL | EXP_TILDE);
	}
	*arglist.lastp = NULL;

	status = 0;
	loopnest++;
	flags &= ~EV_EXIT;
	for (sp = arglist.list ; sp ; sp = sp->next) {
		setvar(n->nfor.var, sp->text, 0);
		status = evaltree(n->nfor.body, flags);
		if (skiploop() & ~SKIPCONT)
			break;
	}
	loopnest--;

	return status;
}



STATIC int
evalcase(union node *n, int flags)
{
	union node *cp;
	union node *patp;
	struct arglist arglist;
	int status = 0;

	errlinno = lineno = n->ncase.linno;

	arglist.lastp = &arglist.list;
	expandarg(n->ncase.expr, &arglist, EXP_TILDE);
	for (cp = n->ncase.cases ; cp ; cp = cp->nclist.next)
		for (patp = cp->nclist.pattern ; patp ; patp = patp->narg.next)
			if (casematch(patp, arglist.list->text))
				goto match;
	goto out;
match:
	for (;;) {
		/* Ensure body is non-empty as otherwise
		 * EV_EXIT may prevent us from setting the
		 * exit status.
		 */
		if (cp->nclist.body) {
			STATIC_ASSERT(NCLIST & EV_EXIT);
			STATIC_ASSERT(!(NCLISTFT & EV_EXIT));
			status = evaltree(cp->nclist.body, flags &
					  (~EV_EXIT | cp->type));
			if (evalskip)
				break;
		}
		if (cp->type != NCLISTFT || !(cp = cp->nclist.next))
			break;
	}
out:
	return status;
}



/*
 * Kick off a subshell to evaluate a tree.
 */

STATIC int
evalsubshell(union node *n, int flags)
{
	struct job *jp;
	int backgnd = (n->type == NBACKGND);
	int status;

	errlinno = lineno = n->nredir.linno;

	expredir(n->nredir.redirect);
	if (!backgnd && flags & EV_EXIT && !have_traps() && !mflag) {
		reset(1);
		goto nofork;
	}
	INTOFF;
	jp = makejob(n, 1);
	if (forkshell(jp, n, backgnd) == 0) {
		INTON;
		if (backgnd)
			flags &= ~EV_TESTED;
nofork:
		redirect(n->nredir.redirect, 0);
		evaltreenr(n->nredir.n, flags);
		/* never returns */
	}
	status = 0;
	if (! backgnd)
		status = waitforjob(jp);
	INTON;
	return status;
}



/*
 * Compute the names of the files in a redirection list.
 */

STATIC void
expredir(union node *n)
{
	union node *redir;

	for (redir = n ; redir ; redir = redir->nfile.next) {
		struct arglist fn;
		fn.lastp = &fn.list;
		switch (redir->type) {
		case NFROMTO:
		case NFROM:
		case NTO:
		case NCLOBBER:
		case NAPPEND:
			expandarg(redir->nfile.fname, &fn, EXP_TILDE | EXP_REDIR);
			redir->nfile.expfname = fn.list->text;
			break;
		case NFROMFD:
		case NTOFD:
			if (redir->ndup.vname) {
				expandarg(redir->ndup.vname, &fn, EXP_TILDE);
				fixredir(redir, fn.list->text, 1);
			}
			break;
		}
	}
}



/*
 * Evaluate a pipeline.  All the processes in the pipeline are children
 * of the process creating the pipeline.  (This differs from some versions
 * of the shell, which make the last process in a pipeline the parent
 * of all the rest.)
 */

STATIC int
evalpipe(union node *n, int flags)
{
	struct job *jp;
	struct nodelist *lp;
	int pipelen;
	int prevfd;
	int pip[2];
	int status = 0;

	TRACE(("evalpipe(0x%lx) called\n", (long)n));
	pipelen = 0;
	for (lp = n->npipe.cmdlist ; lp ; lp = lp->next)
		pipelen++;
	INTOFF;
	jp = makejob(n, pipelen);
	prevfd = -1;
	for (lp = n->npipe.cmdlist ; lp ; lp = lp->next) {
		prehash(lp->n);
		pip[0] = pip[1] = -1;
		if (lp->next) {
			if (pipe(pip) < 0) {
				if (prevfd >= 0)
					close(prevfd);
				sh_error("Pipe call failed");
			}
		}
		if (forkshell(jp, lp->n, n->npipe.backgnd) == 0) {
			INTON;
			if (pip[1] >= 0) {
				close(pip[0]);
			}
			if (prevfd > 0) {
				dup2(prevfd, 0);
				close(prevfd);
			}
			if (pip[1] > 1) {
				dup2(pip[1], 1);
				close(pip[1]);
			}
			evaltreenr(lp->n, flags);
			/* never returns */
		}
		if (prevfd >= 0)
			close(prevfd);
		prevfd = pip[0];
		if (pip[1] >= 0)
			close(pip[1]);
	}
	if (n->npipe.backgnd == 0) {
		status = waitforjob(jp);
		TRACE(("evalpipe:  job done exit status %d\n", status));
	}
	INTON;

	return status;
}



/*
 * Execute a command inside back quotes.  If it's a builtin command, we
 * want to save its output in a block obtained from malloc.  Otherwise
 * we fork off a subprocess and get the output of the command via a pipe.
 * Should be called with interrupts off.
 */

void
evalbackcmd(union node *n, int flags, struct backcmd *result)
{
	int pip[2];
	struct job *jp;

	result->fd = -1;
	result->buf = NULL;
	result->nleft = 0;
	result->jp = NULL;
	if (n == NULL) {
		goto out;
	}

	if (pipe(pip) < 0)
		sh_error("Pipe call failed");
	jp = makejob(n, 1);
	if (forkshell(jp, n, FORK_NOJOB) == 0) {
		FORCEINTON;
		close(pip[0]);
		if (pip[1] != 1) {
			dup2(pip[1], 1);
			close(pip[1]);
		}
		ifsfree();
		evaltreenr(n, flags);
		/* NOTREACHED */
	}
	close(pip[1]);
	result->fd = pip[0];
	result->jp = jp;

out:
	TRACE(("evalbackcmd done: fd=%d buf=0x%x nleft=%d jp=0x%x\n",
		result->fd, result->buf, result->nleft, result->jp));
}

static char **
parse_command_args(char **argv, const char **path)
{
	char *cp, c;

	for (;;) {
		cp = *++argv;
		if (!cp)
			return 0;
		if (*cp++ != '-')
			break;
		if (!(c = *cp++))
			break;
		if (c == '-' && !*cp) {
			if (!*++argv)
				return 0;
			break;
		}
		do {
			switch (c) {
			case 'p':
				*path = defpath;
				break;
			default:
				/* run 'typecmd' for other options */
				return 0;
			}
		} while ((c = *cp++));
	}
	return argv;
}



/*
 * Execute a simple command.
 */

STATIC int
evalcommand(union node *cmd, int flags)
{
	struct localvar_list *localvar_stop;
	struct parsefile *file_stop;
	struct redirtab *redir_stop;
	union node *argp;
	struct arglist arglist;
	struct arglist varlist;
	char **argv;
	int argc;
	struct strlist *sp;
	struct cmdentry cmdentry;
	const struct builtincmd *bltin = COMMANDCMD;
	struct job *jp;
	char *lastarg;
	const char *path, *fpath;
	int spclbltin;
	int execcmd;
	int status;
	char **nargv;
	int cmdflags = 0;

	errlinno = lineno = cmd->ncmd.linno;

	/* First expand the arguments. */
	TRACE(("evalcommand(0x%lx, %d) called\n", (long)cmd, flags));
	localvar_stop = pushlocalvars();
	file_stop = parsefile;
	back_exitstatus = 0;

	cmdentry.cmdtype = CMDBUILTIN;
	cmdentry.u.cmd = &null;
	varlist.lastp = &varlist.list;
	*varlist.lastp = NULL;
	arglist.lastp = &arglist.list;
	*arglist.lastp = NULL;

	argc = 0;
	for (argp = cmd->ncmd.args; argp; argp = argp->narg.next) {
		struct strlist **spp;
		int eflags;

		spp = arglist.lastp;
		eflags = EXP_FULL | EXP_TILDE;
		if (cmdflags & BUILTIN_ASSIGN
			&& isassignment(argp->narg.text))
			eflags = EXP_VARTILDE;
		expandarg(argp, &arglist, eflags);
		for (sp = *spp; sp; sp = sp->next) {
			if (bltin == COMMANDCMD) {
				bltin = find_builtin(sp->text);
				cmdflags = bltin ? bltin->flags : 0;
			}
			argc++;
		}
	}

	/* Reserve two extra spots at the front for shellexec. */
	nargv = stalloc(sizeof (char *) * (argc + 3));
	argv = nargv += 2;
	for (sp = arglist.list ; sp ; sp = sp->next) {
		TRACE(("evalcommand arg: %s\n", sp->text));
		*nargv++ = sp->text;
	}
	*nargv = NULL;

	lastarg = NULL;
	if (iflag && funcnest == 0 && argc > 0)
		lastarg = nargv[-1];

	preverrout.fd = 2;
	expredir(cmd->ncmd.redirect);
	redir_stop = pushredir(cmd->ncmd.redirect);
	status = redirectsafe(cmd->ncmd.redirect, REDIR_PUSH);

	path = vpath.text;
	fpath = vfpath.text;
	for (argp = cmd->ncmd.assign; argp; argp = argp->narg.next) {
		struct strlist **spp;
		char *p;

		spp = varlist.lastp;
		expandarg(argp, &varlist, EXP_VARTILDE);

		mklocal((*spp)->text);

		/*
		 * Modify the command lookup path, if a PATH= assignment
		 * is present
		 */
		p = (*spp)->text;
		if (varequal(p, path))
			path = p;
		else if (varequal(p, fpath))
			fpath = p;
	}

	/* Print the command if xflag is set. */
	if (xflag && !(flags & EV_XTRACE)) {
		struct output *out;
		int sep;

		out = &preverrout;
		outstr(expandstr(ps4val(), EXP_XTRACE), out);
		sep = eprintlist(out, varlist.list, EPL_START | EPL_ASSIGN);
		eprintlist(out, arglist.list, sep | EPL_COMMAND);
		outcslow('\n', out);
		flushall();
	}

	execcmd = 0;
	spclbltin = -1;

	/* Now locate the command. */
	if (argc) {
		const char *oldpath;
		int cmd_flag = DO_ERR;

		path += 5;
		fpath += 6;
		oldpath = path;
		for (;;) {
			find_command(argv[0], &cmdentry, cmd_flag, path, fpath);
			if (cmdentry.cmdtype == CMDUNKNOWN) {
				status = 127;
				flushall();
				goto bail;
			}

			/* implement bltin and command here */
			if (cmdentry.cmdtype != CMDBUILTIN)
				break;
			if (spclbltin < 0)
				spclbltin = 
					cmdentry.u.cmd->flags &
					BUILTIN_SPECIAL
				;
			if (cmdentry.u.cmd == EXECCMD)
				execcmd++;
			if (cmdentry.u.cmd != COMMANDCMD)
				break;

			path = oldpath;
			nargv = parse_command_args(argv, &path);
			if (!nargv)
				break;
			argc -= nargv - argv;
			argv = nargv;
			cmd_flag |= DO_NOFUNC;
		}
	}

	if (status) {
bail:
		exitstatus = status;

		/* We have a redirection error. */
		if (spclbltin > 0)
			exraise(EXERROR);

		goto out;
	}

	jp = NULL;

	/* Execute the command. */
	switch (cmdentry.cmdtype) {
	default:
		/* Fork off a child process if necessary. */
		if (!(flags & EV_EXIT) || have_traps() || mflag) {
			INTOFF;
			jp = makejob(cmd, 1);
			if (forkshell(jp, cmd, FORK_FG) != 0)
				break;
			FORCEINTON;
		}
		listsetvar(varlist.list, VEXPORT|VSTACK);
		shellexec(argv, path, cmdentry.u.index);
		/* NOTREACHED */

	case CMDBUILTIN:
		if (spclbltin) {
			poplocalvars(1);
			if (execcmd && argc > 1)
				listsetvar(varlist.list, VEXPORT);
		} else if (cmdentry.u.cmd == LOCALCMD)
			poplocalvars(0);
		if (evalbltin(cmdentry.u.cmd, argc, argv, flags) &&
		    !(exception == EXERROR && spclbltin <= 0) && !iflag) {
			exception &= ~EXEXT;
raise:
			longjmp(*handler, 1);
		}
		break;

	case CMDFUNCTION:
		if (evalfun(cmdentry.u.func, argc, argv, flags))
			goto raise;
		break;
	}

	status = waitforjob(jp);
	FORCEINTON;

out:
	if (cmd->ncmd.redirect)
		popredir(execcmd);
	unwindredir(redir_stop, 0);
	unwindfiles(file_stop);
	unwindlocalvars(localvar_stop, 0);
	if (lastarg)
		/* dsl: I think this is intended to be used to support
		 * '_' in 'vi' command mode during line editing...
		 * However I implemented that within libedit itself.
		 */
		setvar("_", lastarg, 0);

	return status;
}

STATIC int
evalbltin(const struct builtincmd *cmd, int argc, char **argv, int flags)
{
	const char *volatile savecmdname;
	jmp_buf *volatile savehandler;
	jmp_buf jmploc;
	struct parsefile *saveparsefile;
	int status, error;
	int i;

	savecmdname = commandname;
	savehandler = handler;
	saveparsefile = parsefile;
	if ((i = setjmp(jmploc))) {
		if (parsefile != saveparsefile)
			exception |= EXEXT;
		goto cmddone;
	}
	handler = &jmploc;
	commandname = argv[0];
	argptr = argv + 1;
	optptr = NULL;			/* initialize nextopt */
	if (cmd == EVALCMD)
		status = evalcmd(argc, argv, flags);
	else
		status = (*cmd->builtin)(argc, argv);
	flushall();
	if ((error = outerr(out1))) {
		sh_warnx("%s", strerror(error));
		flushall();
		status = 1;
	}
	exitstatus = status;
cmddone:
	freestdout();
	commandname = savecmdname;
	handler = savehandler;

	return i;
}

STATIC int
evalfun(struct funcnode *func, int argc, char **argv, int flags)
{
	volatile struct shparam saveparam;
	jmp_buf *volatile savehandler;
	jmp_buf jmploc;
	int e;
	int savefuncnest;
	int saveloopnest;

	saveparam = shellparam;
	savefuncnest = funcnest;
	saveloopnest = loopnest;
	savehandler = handler;
	if ((e = setjmp(jmploc))) {
		goto funcdone;
	}
	INTOFF;
	handler = &jmploc;
	shellparam.malloc = 0;
	func->count++;
	funcnest++;
	loopnest = 0;
	INTON;
	shellparam.nparam = argc - 1;
	shellparam.p = argv + 1;
	evaltree(func->n.ndefun.body, flags);
funcdone:
	INTOFF;
	loopnest = saveloopnest;
	funcnest = savefuncnest;
	freefunc(func);
	freeparam(&shellparam);
	shellparam = saveparam;
	handler = savehandler;
	INTON;
	evalskip &= ~SKIPFUNC;
	return e;
}


/*
 * Search for a command.  This is called before we fork so that the
 * location of the command will be available in the parent as well as
 * the child.  The check for "goodname" is an overly conservative
 * check that the name will not be subject to expansion.
 */

STATIC void
prehash(union node *n)
{
	struct cmdentry entry;

	if (n->type == NCMD && n->ncmd.args)
		if (goodname(n->ncmd.args->narg.text))
			find_command(n->ncmd.args->narg.text, &entry, 0,
			             pathval(), fpathval());
}



/*
 * Builtin commands.  Builtin commands whose functions are closely
 * tied to evaluation are implemented here.
 */

/*
 * No command given.
 */

STATIC int
nullcmd(int argc, char **argv)
{
	/*
	 * Preserve exitstatus of a previous possible redirection
	 * as POSIX mandates
	 */
	return back_exitstatus;
}


/*
 * Handle break and continue commands.  Break, continue, and return are
 * all handled by setting the evalskip flag.  The evaluation routines
 * above all check this flag, and if it is set they start skipping
 * commands rather than executing them.  The variable skipcount is
 * the number of loops to break/continue, or the number of function
 * levels to return.  (The latter is always 1.)  It should probably
 * be an error to break out of more loops than exist, but it isn't
 * in the standard shell so we don't make it one here.
 */

int
breakcmd(int argc, char **argv)
{
	const char *ns;
	int n;

	ns = nextarg(0);
	endargs();
	n = ns ? number(ns) : 1;

	if (n <= 0)
		badnum(ns);
	if (n > loopnest)
		n = loopnest;
	if (n > 0) {
		evalskip = (**argv == 'c')? SKIPCONT : SKIPBREAK;
		skipcount = n;
	}
	return 0;
}


/*
 * The return command.
 */

int
returncmd(int argc, char **argv)
{
	const char *ns;
	int status;

	ns = nextarg(0);
	endargs();

	/*
	 * If called outside a function, do what ksh does;
	 * skip the rest of the file.
	 */
	if (ns) {
		status = number(ns);
		evalskip = SKIPFUNCR;
	} else {
		status = exitstatus;
		evalskip = SKIPFUNCNR;
	}

	return status;
}


int
falsecmd(int argc, char **argv)
{
	return 1;
}


int
truecmd(int argc, char **argv)
{
	return 0;
}


int
execcmd(int argc, char **argv)
{
	if (argc > 1) {
		iflag = 0;		/* exit on error */
		mflag = 0;
		optschanged();
		shellexec(argv + 1, pathval(), 0);
	}
	return 0;
}


STATIC int
eprintlist(struct output *out, struct strlist *sp, int flags)
{
	while (sp) {
		const char *p, *q;
		int style;

		if (!(flags & EPL_START))
			outc(' ', out);
		p = sp->text;
		if (flags & EPL_ASSIGN) {
			q = p;
			p = strchr(p, '=') + 1;
			outmem(q, p - q, out);
		}
		style = flags & EPL_COMMAND && findkwd(p);
		outstr(shell_quote(p, style), out);
		sp = sp->next;
		flags &= ~(EPL_START | EPL_COMMAND);
	}

	return flags & EPL_START;
}
