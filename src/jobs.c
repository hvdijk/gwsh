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

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "shell.h"
#include <termios.h>
#include "eval.h"
#include "redir.h"
#include "show.h"
#include "main.h"
#include "parser.h"
#include "nodes.h"
#include "jobs.h"
#include "options.h"
#include "trap.h"
#include "syntax.h"
#include "input.h"
#include "output.h"
#include "memalloc.h"
#include "error.h"
#include "init.h"
#include "mystring.h"
#include "system.h"

/* mode flags for set_curjob */
#define CUR_DELETE 2
#define CUR_RUNNING 1
#define CUR_STOPPED 0

/* mode flags for dowait */
#define DOWAIT_NORMAL 0
#define DOWAIT_BLOCK 1
#define DOWAIT_WAITCMD 2

/* array of jobs */
static struct job *jobtab;
/* size of array */
static unsigned njobs;
/* pid of last background process */
pid_t backgndpid;

/* control terminal */
static int ttyfd = -1;
/* saved tty process group */
MKINIT pid_t ttypgrp;

/* current job */
static struct job *curjob;

STATIC void set_curjob(struct job *, unsigned);
STATIC int jobno(const struct job *);
STATIC int sprint_status(char *, int, int);
STATIC void freejob(struct job *);
STATIC struct job *getjob(const char *, int);
STATIC struct job *growjobtab(void);
STATIC void forkchild(struct job *, union node *, int);
STATIC void forkparent(struct job *, union node *, int, pid_t);
STATIC int dowait(int, struct job *);
STATIC int waitproc(int, int *);
STATIC char *commandtext(union node *);
STATIC void cmdtxt(union node *);
STATIC void cmdlist(union node *, int);
STATIC void cmdputs(const char *);
STATIC void showpipe(struct job *, struct output *);
STATIC int getstatus(struct job *);

static int restartjob(struct job *, int);
static void xtcsetpgrp(pid_t);

STATIC void
set_curjob(struct job *jp, unsigned mode)
{
	struct job *jp1;
	struct job **jpp, **curp;

	/* first remove from list */
	jpp = curp = &curjob;
	do {
		jp1 = *jpp;
		if (jp1 == jp)
			break;
		jpp = &jp1->prev_job;
	} while (1);
	*jpp = jp1->prev_job;

	/* Then re-insert in correct position */
	jpp = curp;
	switch (mode) {
	default:
#ifdef DEBUG
		abort();
#endif
	case CUR_DELETE:
		/* job being deleted */
		break;
	case CUR_RUNNING:
		/* newly created job or backgrounded job,
		   put after all stopped jobs. */
		do {
			jp1 = *jpp;
			if (!jp1 || jp1->state != JOBSTOPPED)
				break;
			jpp = &jp1->prev_job;
		} while (1);
		/* FALLTHROUGH */
	case CUR_STOPPED:
		/* newly stopped job - becomes curjob */
		jp->prev_job = *jpp;
		*jpp = jp;
		break;
	}
}

static int
gettty(int block)
{
	int fd = ttyfd;
	if (fd < 0) {
		fd = xopen(_PATH_TTY, O_RDWR);
		if (fd < 0)
			goto out;
		ttyfd = fd = savefd(fd, fd);
	}
	if (ttypgrp)
		goto out;
	for (;;) { /* while we are in the background */
		pid_t pgrp = tcgetpgrp(fd);
		if (pgrp < 0)
			break;
		if (pgrp == getpgrp()) {
			pid_t pid;
			ttypgrp = pgrp;
			pid = getpid();
			setpgid(pid, pid);
			xtcsetpgrp(pid);
			goto out;
		}
		if (!block)
			break;
		killpg(0, SIGTTIN);
	}
	fd = -1;
out:
	return fd;
}


void
releasetty(void)
{
	if (!ttypgrp)
		return;

	(void)tcsetpgrp(ttyfd, ttypgrp);
}


/*
 * Controls whether the shell is interactive or not.
 */

void
setinteractive(int on)
{
	static int interactive;
	if (on == interactive)
		return;
	interactive = on;
	if (on && gettty(1) < 0 && mflag)
		sh_warnx("can't access tty; job control limited");
	interactive = on;
	setsignal(SIGINT, 0);
	setsignal(SIGQUIT, 0);
	setsignal(SIGTERM, 0);
}


void
setjobctl(int on)
{
	static int jobctl;
	if (on == jobctl)
		return;
	jobctl = on;
	setsignal(SIGTSTP, 0);
	setsignal(SIGTTIN, 0);
	setsignal(SIGTTOU, 0);
}


#ifdef mkinit
RESET {
	if (sub) {
		iflag = 0;
		mflag = 0;
		ttypgrp = 0;
#ifndef SMALL
		/* This belongs in histedit.c, but is here to make sure
		 * it executes before optschanged(). */
		el = NULL;
		hist = NULL;

#endif
		optschanged();
	}
}
#endif


int
killcmd(int argc, char *argv[])
{
	extern const char *signal_names[];
	extern int signal_names_length;
	int signo = -1;
	int list = 0;
	int i;
	pid_t pid;
	struct job *jp;

	signal_names[0] = "0";

	if (argc <= 1) {
usage:
		sh_error(
"Usage: kill [-s sigspec | -signum | -sigspec] [pid | job]... or\n"
"kill -l [exitstatus]"
		);
	}

	if (**++argv == '-') {
		signo = decode_signal(*argv + 1);
		if (signo < 0) {
			int c;

			while ((c = nextopt("ls:")) != '\0')
				switch (c) {
				default:
#ifdef DEBUG
					abort();
#endif
				case 'l':
					list = 1;
					break;
				case 's':
					signo = decode_signal(optionarg);
					if (signo < 0) {
						sh_error(
							"invalid signal number or name: %s",
							optionarg
						);
					}
		                        break;
				}
			argv = argptr;
		} else
			argv++;
	}

	if (!list && signo < 0)
		signo = SIGTERM;

	if ((signo < 0 || !*argv) ^ list) {
		goto usage;
	}

	if (list) {
		struct output *out;

		out = out1;
		if (!*argv) {
			for (i = 0; i < signal_names_length; i++) {
				if (signal_names[i])
					outfmt(out, snlfmt, signal_names[i]);
			}
			return 0;
		}
		signo = number(*argv);
		if (signo > 128)
			signo -= 128;
		if (0 <= signo && signo < signal_names_length && signal_names[signo])
			outfmt(out, snlfmt, signal_names[signo]);
		else
			sh_error("invalid signal number or exit status: %s",
				 *argv);
		return 0;
	}

	i = 0;
	do {
		if (**argv == '%') {
			jp = getjob(*argv, 0);
			if (jp->jobctl)
				pid = -jp->ps[0].pid;
			else {
				/* Send a signal to each process by its PID. Pay attention
				 * to processes which already exited: if we waited on them
				 * already, they are gone and we might accidentally be
				 * sending signals to some random other process that happens
				 * to have received the same PID. */
				struct procstat *ps = jp->ps;
				int n = jp->nprocs;
				for (; n; n--, ps++)
					if (ps->status && kill(ps->pid, signo) != 0)
						goto err;
				continue;
			}
		} else
			pid = **argv == '-' ?
				-number(*argv + 1) : number(*argv);
		if (kill(pid, signo) != 0) {
err:
			sh_warnx("%s", errnomsg());
			i = 1;
		}
	} while (*++argv);

	return i;
}

STATIC int
jobno(const struct job *jp)
{
	return jp - jobtab + 1;
}

int
fgcmd(int argc, char **argv)
{
	struct job *jp;
	struct output *out;
	int mode;
	int retval;

	if (!mflag)
		sh_error("job control disabled");

	mode = (**argv == 'f') ? FORK_FG : FORK_BG;
	nextopt(nullstr);
	argv = argptr;
	out = out1;
	do {
		jp = getjob(*argv, 1);
		if (mode == FORK_BG) {
			set_curjob(jp, CUR_RUNNING);
			outfmt(out, "[%d] ", jobno(jp));
		}
		outstr(jp->ps->cmd, out);
		showpipe(jp, out);
		flushall();
		retval = restartjob(jp, mode);
	} while (*argv && *++argv);
	return retval;
}


STATIC int
restartjob(struct job *jp, int mode)
{
	struct procstat *ps;
	int i;
	int status;
	pid_t pgid;

	INTOFF;
	if (jp->state == JOBDONE)
		goto out;
	jp->state = JOBRUNNING;
	pgid = jp->ps->pid;
	if (mode == FORK_FG)
		xtcsetpgrp(pgid);
	killpg(pgid, SIGCONT);
	ps = jp->ps;
	i = jp->nprocs;
	do {
		if (WIFSTOPPED(ps->status)) {
			ps->status = -1;
		}
	} while (ps++, --i);
out:
	status = (mode == FORK_FG) ? waitforjob(jp) : 0;
	INTON;
	return status;
}

STATIC int
sprint_status(char *s, int status, int sigonly)
{
	int col;
	int st;

	col = 0;
	st = WEXITSTATUS(status);
	if (!WIFEXITED(status)) {
		st = WSTOPSIG(status);
		if (!WIFSTOPPED(status))
			st = WTERMSIG(status);
		if (sigonly) {
			if (st == SIGINT || st == SIGPIPE)
				goto out;
			if (WIFSTOPPED(status))
				goto out;
		}
		col = fmtstr(s, 32, "%s", strsignal(st));
#ifdef WCOREDUMP
		if (WCOREDUMP(status)) {
			col += fmtstr(s + col, 16, " (core dumped)");
		}
#endif
	} else if (!sigonly) {
		if (st)
			col = fmtstr(s, 16, "Done(%d)", st);
		else
			col = fmtstr(s, 16, "Done");
	}

out:
	return col;
}

static void
showjob(struct output *out, struct job *jp, int mode)
{
	struct procstat *ps;
	struct procstat *psend;
	int col;
	int indent;
	char s[80];

	ps = jp->ps;

	if (mode & SHOW_PGID) {
		/* just output process (group) id of pipeline */
		outfmt(out, "%d\n", ps->pid);
		return;
	}

	col = fmtstr(s, 16, "[%d]   ", jobno(jp));
	indent = col;

	if (jp == curjob)
		s[col - 2] = '+';
	else if (curjob && jp == curjob->prev_job)
		s[col - 2] = '-';

	if (mode & SHOW_PID)
		col += fmtstr(s + col, 16, "%d ", ps->pid);

	psend = ps + jp->nprocs;

	if (jp->state == JOBRUNNING) {
		scopy("Running", s + col);
		col += strlen("Running");
	} else {
		int status = psend[-1].status;
		if (jp->state == JOBSTOPPED)
			status = jp->stopstatus;
		col += sprint_status(s + col, status, 0);
	}

	goto start;

	do {
		/* for each process */
		col = fmtstr(s, 48, " |\n%*c%d ", indent, ' ', ps->pid) - 3;

start:
		outfmt(
			out, "%s%*c%s",
			s, 33 - col >= 0 ? 33 - col : 0, ' ', ps->cmd
		);
		if (!(mode & SHOW_PID)) {
			showpipe(jp, out);
			break;
		}
		if (++ps == psend) {
			outcslow('\n', out);
			break;
		}
	} while (1);

	jp->changed = 0;

	if (jp->state == JOBDONE) {
		TRACE(("showjob: freeing job %d\n", jobno(jp)));
		freejob(jp);
	}
}


int
jobscmd(int argc, char **argv)
{
	int mode, m;
	struct output *out;

	mode = 0;
	while ((m = nextopt("lp")))
		if (m == 'l')
			mode = SHOW_PID;
		else
			mode = SHOW_PGID;

	out = out1;
	argv = argptr;
	if (*argv)
		do
			showjob(out, getjob(*argv,0), mode);
		while (*++argv);
	else
		showjobs(out, mode);

	return 0;
}


/*
 * Print a list of jobs.  If "change" is nonzero, only print jobs whose
 * statuses have changed since the last call to showjobs.
 */

void
showjobs(struct output *out, int mode)
{
	struct job *jp;

	TRACE(("showjobs(%x) called\n", mode));

	/* If not even one one job changed, there is nothing to do */
	while (dowait(DOWAIT_NORMAL, NULL) > 0)
		continue;

	for (jp = curjob; jp; jp = jp->prev_job) {
		if (!(mode & SHOW_CHANGED) || jp->changed)
			showjob(out, jp, mode);
	}
}

/*
 * Mark a job structure as unused.
 */

STATIC void
freejob(struct job *jp)
{
	struct procstat *ps;
	int i;

	INTOFF;
	for (i = jp->nprocs, ps = jp->ps ; --i >= 0 ; ps++) {
		if (ps->cmd != nullstr)
			ckfree(ps->cmd);
	}
	if (jp->ps != &jp->ps0)
		ckfree(jp->ps);
	jp->used = 0;
	set_curjob(jp, CUR_DELETE);
	INTON;
}



int
waitcmd(int argc, char **argv)
{
	struct job *job;
	int retval;
	struct job *jp;

	nextopt(nullstr);
	retval = 0;

	argv = argptr;
	if (!*argv) {
		/* wait for all jobs */
		for (;;) {
			jp = curjob;
			while (1) {
				if (!jp) {
					/* no running procs */
					goto out;
				}
				if (jp->state == JOBRUNNING)
					break;
				jp->waited = 1;
				jp = jp->prev_job;
			}
			if (dowait(DOWAIT_WAITCMD, 0) <= 0)
				goto sigout;
		}
	}

	retval = 127;
	do {
		if (**argv != '%') {
			pid_t pid = number(*argv);
			job = curjob;
			goto start;
			do {
				if (job->ps[job->nprocs - 1].pid == pid)
					break;
				job = job->prev_job;
start:
				if (!job)
					goto repeat;
			} while (1);
		} else
			job = getjob(*argv, 0);
		/* loop until process terminated or stopped */
		while (job->state == JOBRUNNING)
			if (dowait(DOWAIT_WAITCMD, 0) <= 0)
				goto sigout;
		job->waited = 1;
		retval = getstatus(job);
repeat:
		;
	} while (*++argv);

out:
	return retval;

sigout:
	retval = 128 + pending_sig;
	goto out;
}



/*
 * Convert a job name to a job structure.
 */

STATIC struct job *
getjob(const char *name, int getctl)
{
	struct job *jp;
	struct job *found;
	const char *err_msg = "No such job: %s";
	unsigned num;
	int c;
	const char *p;
	char *(*match)(const char *, const char *);

	jp = curjob;
	p = name;
	if (!p)
		goto currentjob;

	if (*p != '%')
		goto err;

	c = *++p;
	if (!c)
		goto currentjob;

	if (!p[1]) {
		if (c == '+' || c == '%') {
currentjob:
			err_msg = "No current job";
			goto check;
		} else if (c == '-') {
			if (jp)
				jp = jp->prev_job;
			err_msg = "No previous job";
check:
			if (!jp)
				goto err;
			goto gotit;
		}
	}

	if (is_number(p)) {
		num = atoi(p);
		if (num > 0 && num <= njobs) {
			jp = jobtab + num - 1;
			if (jp->used)
				goto gotit;
			goto err;
		}
	}

	match = prefix;
	if (*p == '?') {
		match = strstr;
		p++;
	}

	found = 0;
	while (jp) {
		if (match(jp->ps[0].cmd, p)) {
			if (found)
				goto err;
			found = jp;
			err_msg = "%s: ambiguous";
		}
		jp = jp->prev_job;
	}

	if (!found)
		goto err;
	jp = found;

gotit:
	return jp;
err:
	sh_error(err_msg, name);
}



/*
 * Return a new job structure.
 * Called with interrupts off.
 */

struct job *
makejob(union node *node, int nprocs)
{
	int i;
	struct job *jp;

	for (i = njobs, jp = jobtab ; ; jp++) {
		if (--i < 0) {
			jp = growjobtab();
			break;
		}
		if (jp->used == 0)
			break;
		if (jp->state != JOBDONE || !jp->waited)
			continue;
		if (mflag)
			continue;
		freejob(jp);
		break;
	}
	memset(jp, 0, sizeof(*jp));
	jp->pipefail = optpipefail;
	if (mflag)
		jp->jobctl = 1;
	jp->prev_job = curjob;
	curjob = jp;
	jp->used = 1;
	jp->ps = &jp->ps0;
	if (nprocs > 1) {
		jp->ps = ckmalloc(nprocs * sizeof (struct procstat));
	}
	TRACE(("makejob(0x%lx, %d) returns %%%d\n", (long)node, nprocs,
	    jobno(jp)));
	return jp;
}

STATIC struct job *
growjobtab(void)
{
	size_t len;
	ptrdiff_t offset;
	struct job *jp, *jq;

	len = njobs * sizeof(*jp);
	jq = jobtab;
	jp = ckrealloc(jq, len + 4 * sizeof(*jp));

	offset = (char *)jp - (char *)jq;
	if (offset) {
		/* Relocate pointers */
		size_t l = len;

		while (l) {
			l -= sizeof(*jp);
#define joff(p) ((struct job *)((char *)(p) + l))
#define jmove(p) (p) = (void *)((char *)(p) + offset)
			if (likely(joff(jp)->ps == &((struct job *)((char *)jq + l))->ps0))
				jmove(joff(jp)->ps);
			if (joff(jp)->prev_job)
				jmove(joff(jp)->prev_job);
		}
		if (curjob)
			jmove(curjob);
#undef joff
#undef jmove
	}

	njobs += 4;
	jobtab = jp;
	jp = (struct job *)((char *)jp + len);
	jq = jp + 3;
	do {
		jq->used = 0;
	} while (--jq >= jp);
	return jp;
}


/*
 * Fork off a subshell.  If we are doing job control, give the subshell its
 * own process group.  Jp is a job structure that the job is to be added to.
 * N is the command that will be evaluated by the child.  Both jp and n may
 * be NULL.  The mode parameter can be one of the following:
 *	FORK_FG - Fork off a foreground process.
 *	FORK_BG - Fork off a background process.
 *	FORK_NOJOB - Like FORK_FG, but don't give the process its own
 *		     process group even if job control is on.
 *
 * When job control is turned off, background processes have their standard
 * input redirected to /dev/null (except for the second and later processes
 * in a pipeline).
 *
 * Called with interrupts off.
 */

STATIC inline void
forkchild(struct job *jp, union node *n, int mode)
{
	TRACE(("Child shell %d\n", getpid()));

	if (mode != FORK_NOJOB && jp->jobctl) {
		pid_t pgrp;

		if (jp->nprocs == 0)
			pgrp = getpid();
		else
			pgrp = jp->ps[0].pid;
		/* This can fail because we are doing it in the parent also */
		(void)setpgid(0, pgrp);
		if (mode == FORK_FG)
			xtcsetpgrp(pgrp);
		setsignal(SIGTSTP, 1);
		setsignal(SIGTTIN, 1);
		setsignal(SIGTTOU, 1);
	} else if (mode == FORK_BG) {
		ignoresig(SIGINT);
		ignoresig(SIGQUIT);
		if (jp->nprocs == 0) {
			close(0);
			if (xopen(_PATH_DEVNULL, O_RDONLY) != 0)
				sh_error("Can't open %s", _PATH_DEVNULL);
		}
	}
	if (iflag) {
		setsignal(SIGINT, 1);
		setsignal(SIGQUIT, 1);
		setsignal(SIGTERM, 1);
	}
	reset(1);
}

STATIC inline void
forkparent(struct job *jp, union node *n, int mode, pid_t pid)
{
	TRACE(("In parent shell:  child = %d\n", pid));
	if (!jp)
		return;
	if (mode != FORK_NOJOB && jp->jobctl) {
		int pgrp;

		if (jp->nprocs == 0)
			pgrp = pid;
		else
			pgrp = jp->ps[0].pid;
		/* This can fail because we are doing it in the child also */
		(void)setpgid(pid, pgrp);
	}
	if (mode == FORK_BG) {
		backgndpid = pid;		/* set $! */
		set_curjob(jp, CUR_RUNNING);
	}
	if (jp) {
		struct procstat *ps = &jp->ps[jp->nprocs++];
		ps->pid = pid;
		ps->status = -1;
		ps->cmd = nullstr;
		if (n)
			ps->cmd = commandtext(n);
	}
}

int
forkshell(struct job *jp, union node *n, int mode)
{
	int pid;

	TRACE(("forkshell(%%%d, %p, %d) called\n", jobno(jp), n, mode));
	if (mode == FORK_FG && jp->jobctl)
		gettty(0);
	sigprocmask(SIG_SETMASK, &sigset_full, 0);
	pid = fork();
	if (pid < 0) {
		TRACE(("Fork failed, errno=%d", errno));
		if (jp)
			freejob(jp);
		sh_error("Cannot fork");
	}
	if (pid == 0)
		forkchild(jp, n, mode);
	else
		forkparent(jp, n, mode, pid);
	sigprocmask(SIG_SETMASK, &sigset_empty, 0);
	return pid;
}

/*
 * Wait for job to finish.
 *
 * Under job control we have the problem that while a child process is
 * running interrupts generated by the user are sent to the child but not
 * to the shell.  This means that an infinite loop started by an inter-
 * active user may be hard to kill.  With job control turned off, an
 * interactive user may place an interactive program inside a loop.  If
 * the interactive program catches interrupts, the user doesn't want
 * these interrupts to also abort the loop.  The approach we take here
 * is to have the shell ignore interrupt signals while waiting for a
 * forground process to terminate, and then send itself an interrupt
 * signal if the child process was terminated by an interrupt signal.
 * Unfortunately, some programs want to do a bit of cleanup and then
 * exit on interrupt; unless these processes terminate themselves by
 * sending a signal to themselves (instead of calling exit) they will
 * confuse this approach.
 *
 * Called with interrupts off.
 */

int
waitforjob(struct job *jp)
{
	int st;

	TRACE(("waitforjob(%%%d) called\n", jp ? jobno(jp) : 0));
	if (!jp) {
		int pid = gotsigchld;

		while (pid > 0)
			pid = dowait(DOWAIT_NORMAL, NULL);

		return exitstatus;
	}

	while (jp->state == JOBRUNNING)
		dowait(DOWAIT_BLOCK, jp);
	st = getstatus(jp);
	if (jp->jobctl) {
		xtcsetpgrp(getpid());
		/*
		 * This is truly gross.
		 * If we're doing job control, then we did a TIOCSPGRP which
		 * caused us (the shell) to no longer be in the controlling
		 * session -- so we wouldn't have seen any ^C/SIGINT.  So, we
		 * intuit from the subprocess exit status whether a SIGINT
		 * occurred, and if so interrupt ourselves.  Yuck.  - mycroft
		 */
		if (jp->sigint)
			raise(SIGINT);
	}
	if (jp->state == JOBDONE)
		freejob(jp);
	return st;
}



/*
 * Wait for a process to terminate.
 */

STATIC int
dowait(int block, struct job *job)
{
	int pid;
	int status;
	struct job *jp;
	struct job *thisjob = NULL;
	int state;

	INTOFF;
	TRACE(("dowait(%d) called\n", block));
	pid = waitproc(block, &status);
	TRACE(("wait returns pid %d, status=%d\n", pid, status));
	if (pid <= 0)
		goto out;

	for (jp = curjob; jp; jp = jp->prev_job) {
		struct procstat *sp;
		struct procstat *spend;
		if (jp->state == JOBDONE)
			continue;
		state = JOBDONE;
		spend = jp->ps + jp->nprocs;
		sp = jp->ps;
		do {
			if (sp->pid == pid) {
				TRACE(("Job %d: changing status of proc %d from 0x%x to 0x%x\n", jobno(jp), pid, sp->status, status));
				sp->status = status;
				thisjob = jp;
			}
			if (sp->status == -1)
				state = JOBRUNNING;
			if (state == JOBRUNNING)
				continue;
			if (WIFSTOPPED(sp->status)) {
				jp->stopstatus = sp->status;
				state = JOBSTOPPED;
			}
		} while (++sp < spend);
		if (thisjob)
			goto gotjob;
	}
	goto out;

gotjob:
	if (state != JOBRUNNING) {
		thisjob->changed = 1;

		if (thisjob->state != state) {
			TRACE(("Job %d: changing state from %d to %d\n", jobno(thisjob), thisjob->state, state));
			thisjob->state = state;
			if (state == JOBSTOPPED) {
				set_curjob(thisjob, CUR_STOPPED);
			}
		}
	}

out:
	INTON;

	if (thisjob && thisjob == job) {
		char s[48 + 1];
		int len;

		len = sprint_status(s, status, 1);
		if (len) {
			s[len] = '\n';
			s[len + 1] = 0;
			outstr(s, out2);
			flushall();
		}
	}

	return pid;
}



/*
 * Do a wait system call.  If job control is compiled in, we accept
 * stopped processes.  If block is zero, we return a value of zero
 * rather than blocking.
 */

STATIC int
waitproc(int block, int *status)
{
	int flags = block == DOWAIT_BLOCK ? 0 : WNOHANG;
	int err;

	if (mflag)
		flags |= WUNTRACED;

	do {
		gotsigchld = 0;
		err = wait3(status, flags, NULL);
		if (err || !block)
			break;

		block = 0;

		sigprocmask(SIG_SETMASK, &sigset_full, 0);

		while (!gotsigchld && !pending_sig && !intpending)
			sigsuspend(&sigset_empty);

		sigprocmask(SIG_SETMASK, &sigset_empty, 0);
	} while (gotsigchld);

	return err;
}

/*
 * return 1 if there are stopped jobs, otherwise 0
 */
int job_warning;
int
stoppedjobs(void)
{
	struct job *jp;
	int retval;

	retval = 0;
	if (job_warning)
		goto out;

	while (dowait(DOWAIT_NORMAL, 0) > 0);

	jp = curjob;
	if (jp && jp->state == JOBSTOPPED) {
		out2str("You have stopped jobs.\n");
		job_warning = 2;
		retval++;
	}

out:
	return retval;
}

void
resetjobs(void)
{
	struct job *jp;

	for (jp = curjob; jp; jp = jp->prev_job)
		freejob(jp);
}

#ifdef mkinit
INCLUDE "jobs.h"
RESET {
	if (sub)
		resetjobs();
}
#endif

/*
 * Return a string identifying a command (to be printed by the
 * jobs command).
 */

STATIC char *cmdnextc;

STATIC char *
commandtext(union node *n)
{
	char *name;

	STARTSTACKSTR(cmdnextc);
	cmdtxt(n);
	name = stackblock();
	TRACE(("commandtext: name %p, end %p\n", name, cmdnextc));
	return savestr(name);
}


STATIC void
cmdtxt(union node *n)
{
	union node *np;
	struct nodelist *lp;
	const char *p;
	char s[2];

	if (!n)
		return;
	switch (n->type) {
	default:
#if DEBUG
		abort();
#endif
	case NPIPE:
		lp = n->npipe.cmdlist;
		for (;;) {
			cmdtxt(lp->n);
			lp = lp->next;
			if (!lp)
				break;
			cmdputs(" | ");
		}
		break;
	case NSEMI:
		p = "; ";
		goto binop;
	case NAND:
		p = " && ";
		goto binop;
	case NOR:
		p = " || ";
binop:
		cmdtxt(n->nbinary.ch1);
		cmdputs(p);
		n = n->nbinary.ch2;
		goto donode;
	case NREDIR:
	case NBACKGND:
		n = n->nredir.n;
		goto donode;
	case NNOT:
		cmdputs("!");
		n = n->nnot.com;
donode:
		cmdtxt(n);
		break;
	case NIF:
		cmdputs("if ");
		cmdtxt(n->nif.test);
		cmdputs("; then ");
		if (n->nif.elsepart) {
			cmdtxt(n->nif.ifpart);
			cmdputs("; else ");
			n = n->nif.elsepart;
		} else {
			n = n->nif.ifpart;
		}
		p = "; fi";
		goto dotail;
	case NSUBSHELL:
		cmdputs("(");
		n = n->nredir.n;
		p = ")";
		goto dotail;
	case NWHILE:
		p = "while ";
		goto until;
	case NUNTIL:
		p = "until ";
until:
		cmdputs(p);
		cmdtxt(n->nbinary.ch1);
		n = n->nbinary.ch2;
		p = "; done";
dodo:
		cmdputs("; do ");
dotail:
		cmdtxt(n);
		goto dotail2;
	case NFOR:
		cmdputs("for ");
		cmdputs(n->nfor.var);
		cmdputs(" in ");
		cmdlist(n->nfor.args, 1);
		n = n->nfor.body;
		p = "; done";
		goto dodo;
	case NDEFUN:
		cmdputs(n->ndefun.text);
		p = "() { ... }";
		goto dotail2;
	case NCMD:
		cmdlist(n->ncmd.args, 1);
		cmdlist(n->ncmd.redirect, 0);
		break;
	case NARG:
		p = n->narg.text;
dotail2:
		cmdputs(p);
		break;
	case NHERE:
	case NXHERE:
		p = "<<...";
		goto dotail2;
	case NCASE:
		cmdputs("case ");
		cmdputs(n->ncase.expr->narg.text);
		cmdputs(" in ");
		for (np = n->ncase.cases; np; np = np->nclist.next) {
			cmdtxt(np->nclist.pattern);
			cmdputs(") ");
			cmdtxt(np->nclist.body);
			cmdputs(";; ");
		}
		p = "esac";
		goto dotail2;
	case NTO:
		p = ">";
		goto redir;
	case NCLOBBER:
		p = ">|";
		goto redir;
	case NAPPEND:
		p = ">>";
		goto redir;
	case NTOFD:
		p = ">&";
		goto redir;
	case NFROM:
		p = "<";
		goto redir;
	case NFROMFD:
		p = "<&";
		goto redir;
	case NFROMTO:
		p = "<>";
redir:
		s[0] = n->nfile.fd + '0';
		s[1] = '\0';
		cmdputs(s);
		cmdputs(p);
		if (n->type == NTOFD || n->type == NFROMFD) {
			s[0] = n->ndup.dupfd + '0';
			p = s;
			goto dotail2;
		} else {
			n = n->nfile.fname;
			goto donode;
		}
	}
}

STATIC void
cmdlist(union node *np, int sep)
{
	for (; np; np = np->narg.next) {
		if (!sep)
			cmdputs(spcstr);
		cmdtxt(np);
		if (sep && np->narg.next)
			cmdputs(spcstr);
	}
}


STATIC void
cmdputs(const char *s)
{
	const char *p, *str;
	char cc[2] = " ";
	char *nextc;
	signed char c;
	int subtype = 0;
	int quoted = 0;
	static const char vstype[VSTYPE + 1][4] = {
		"", "}", "-", "+", "?", "=",
		"%", "%%", "#", "##",
	};

	nextc = makestrspace((strlen(s) + 1) * 8, cmdnextc);
	p = s;
	while ((c = *p++) != 0) {
		str = 0;
		switch (c) {
		case CTLESC:
			c = *p++;
			break;
		case CTLVAR:
			subtype = *p++;
			if ((subtype & VSTYPE) == VSLENGTH)
				str = "${#";
			else
				str = "${";
			goto dostr;
		case CTLENDVAR:
			str = &"\"}"[!(quoted & 1)];
			quoted >>= 1;
			subtype = 0;
			goto dostr;
		case CTLBACKQ:
			str = "$(...)";
			goto dostr;
		case CTLARI:
			str = "$((";
			goto dostr;
		case CTLENDARI:
			str = "))";
			goto dostr;
		case CTLQUOTEMARK:
			quoted ^= 1;
			c = '"';
			break;
		case '=':
			if (subtype == 0)
				break;
			if ((subtype & VSTYPE) != VSNORMAL)
				quoted <<= 1;
			str = vstype[subtype & VSTYPE];
			if (subtype & VSNUL)
				c = ':';
			else
				goto checkstr;
			break;
		case '\'':
		case '\\':
		case '"':
		case '$':
			/* These can only happen inside quotes */
			cc[0] = c;
			str = cc;
			c = '\\';
			break;
		default:
			break;
		}
		USTPUTC(c, nextc);
checkstr:
		if (!str)
			continue;
dostr:
		while ((c = *str++)) {
			USTPUTC(c, nextc);
		}
	}
	if (quoted & 1) {
		USTPUTC('"', nextc);
	}
	*nextc = 0;
	cmdnextc = nextc;
}


STATIC void
showpipe(struct job *jp, struct output *out)
{
	struct procstat *sp;
	struct procstat *spend;

	spend = jp->ps + jp->nprocs;
	for (sp = jp->ps + 1; sp < spend; sp++)
		outfmt(out, " | %s", sp->cmd);
	outcslow('\n', out);
}


STATIC void
xtcsetpgrp(pid_t pgrp)
{
	int err;

	if (!ttypgrp)
		return;

	sigprocmask(SIG_SETMASK, &sigset_full, 0);
	err = tcsetpgrp(ttyfd, pgrp);
	sigprocmask(SIG_SETMASK, &sigset_empty, 0);

	if (err)
		sh_error("Cannot set tty process group (%s)", errnomsg());
}


STATIC int
getstatus(struct job *job) {
	int status;
	int retval;
	int nproc = job->nprocs - 1;
	struct procstat *ps = job->ps + nproc;

	nproc &= -job->pipefail;

	for (;;) {
		status = ps->status;
		retval = WEXITSTATUS(status);
		if (!WIFEXITED(status)) {
			retval = WSTOPSIG(status);
			if (!WIFSTOPPED(status)) {
				/* XXX: limits number of signals */
				retval = WTERMSIG(status);
				if (retval == SIGINT)
					job->sigint = 1;
			}
			retval += 128;
		}
		TRACE(("getstatus: job %d, nproc %d, status %x, retval %x\n",
			jobno(job), job->nprocs, status, retval));
		if (retval || !nproc)
			return retval;

		nproc--;
		ps--;
	}
}
