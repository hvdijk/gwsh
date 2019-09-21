/*-
 * Copyright (c) 2018-2019
 *	Harald van Dijk <harald@gigawatt.nl>.  All rights reserved.
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

const char *const signal_names[] = {
	"EXIT",

	/* Signals which may just be aliases for other signals come first.
	 * If they turn out to be an alias, the later initialiser for the
	 * preferred signal name will override it. */
#ifdef SIGCLD
#if SIGCLD != SIGCHLD
	[SIGCLD]    = "CLD",
#endif
#endif
#ifdef SIGINFO
#if SIGINFO != SIGPWR
	[SIGINFO]   = "INFO",
#endif
#endif
#ifdef SIGIO
#if SIGIO != SIGPOLL
	[SIGIO]     = "IO",
#endif
#endif
#ifdef SIGIOT
#if SIGIOT != SIGABRT
	[SIGIOT]    = "IOT",
#endif
#endif

#ifdef SIGABRT
	[SIGABRT]   = "ABRT",
#endif
#ifdef SIGALRM
	[SIGALRM]   = "ALRM",
#endif
#ifdef SIGBUS
	[SIGBUS]    = "BUS",
#endif
#ifdef SIGCHLD
	[SIGCHLD]   = "CHLD",
#endif
#ifdef SIGCONT
	[SIGCONT]   = "CONT",
#endif
#ifdef SIGEMT
	[SIGEMT]    = "EMT",
#endif
#ifdef SIGFPE
	[SIGFPE]    = "FPE",
#endif
#ifdef SIGHUP
	[SIGHUP]    = "HUP",
#endif
#ifdef SIGILL
	[SIGILL]    = "ILL",
#endif
#ifdef SIGINT
	[SIGINT]    = "INT",
#endif
#ifdef SIGKILL
	[SIGKILL]   = "KILL",
#endif
#ifdef SIGLOST
	[SIGLOST]   = "LOST",
#endif
#ifdef SIGPIPE
	[SIGPIPE]   = "PIPE",
#endif
#ifdef SIGPOLL
	[SIGPOLL]   = "POLL",
#endif
#ifdef SIGPROF
	[SIGPROF]   = "PROF",
#endif
#ifdef SIGPWR
	[SIGPWR]    = "PWR",
#endif
#ifdef SIGQUIT
	[SIGQUIT]   = "QUIT",
#endif
#ifdef SIGSEGV
	[SIGSEGV]   = "SEGV",
#endif
#ifdef SIGSTKFLT
	[SIGSTKFLT] = "STKFLT",
#endif
#ifdef SIGSTOP
	[SIGSTOP]   = "STOP",
#endif
#ifdef SIGSYS
	[SIGSYS]    = "SYS",
#endif
#ifdef SIGTERM
	[SIGTERM]   = "TERM",
#endif
#ifdef SIGTRAP
	[SIGTRAP]   = "TRAP",
#endif
#ifdef SIGTSTP
	[SIGTSTP]   = "TSTP",
#endif
#ifdef SIGTTIN
	[SIGTTIN]   = "TTIN",
#endif
#ifdef SIGTTOU
	[SIGTTOU]   = "TTOU",
#endif
#ifdef SIGURG
	[SIGURG]    = "URG",
#endif
#ifdef SIGUSR1
	[SIGUSR1]   = "USR1",
#endif
#ifdef SIGUSR2
	[SIGUSR2]   = "USR2",
#endif
#ifdef SIGVTALRM
	[SIGVTALRM] = "VTALRM",
#endif
#ifdef SIGWINCH
	[SIGWINCH]  = "WINCH",
#endif
#ifdef SIGXCPU
	[SIGXCPU]   = "XCPU",
#endif
#ifdef SIGXFSZ
	[SIGXFSZ]   = "XFSZ",
#endif
};
const int signal_names_length = sizeof signal_names / sizeof *signal_names;
