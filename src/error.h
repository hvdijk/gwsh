/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1997-2005
 *	Herbert Xu <herbert@gondor.apana.org.au>.  All rights reserved.
 * Copyright (c) 2019-2020
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
 *
 *	@(#)error.h	8.2 (Berkeley) 5/4/95
 */

#ifndef H_ERROR
#define H_ERROR 1

#include "config.h"

#include <setjmp.h>
#include <signal.h>

/*
 * The global variable handler contains the location to
 * jump to when an exception occurs, and the global variable exception
 * contains a code identifying the exeception.  To implement nested
 * exception handlers, the user should save the value of handler on entry
 * to an inner scope, set handler to point to a jmploc structure for the
 * inner scope, and restore handler on exit from the scope.
 */

extern jmp_buf *handler;
extern int exception;

/* exceptions */
#define EXINT 0		/* SIGINT received */
#define EXERROR 1	/* a generic error */
#define EXEXIT 4	/* exit the shell */
#define EXEXT 8		/* exception external to builtin */
#define EXEOF 16	/* hit EOF */


/*
 * These macros allow the user to suspend the handling of interrupt signals
 * over a period of time.  This is similar to SIGHOLD to or sigblock, but
 * much more efficient and portable.  (But hacking the kernel is so much
 * more fun than worrying about efficiency and portability. :-))
 */

extern int suppressint;
extern volatile sig_atomic_t intpending;

static inline void barrier(void) {
	__asm__ volatile ("": : :"memory");
}
#define INTOFF \
	( \
		suppressint++, \
		barrier() \
	)
#ifdef REALLY_SMALL
void __inton(void);
#define INTON __inton()
#else
#define INTON \
	( \
		barrier(), \
		--suppressint == 0 && intpending ? onint() : (void)0 \
	)
#endif
#define FORCEINTON \
	( \
		barrier(), \
		suppressint = 0, \
		intpending ? onint() : (void)0 \
	)
#define SAVEINT(v) ((v) = suppressint)
#define RESTOREINT(v) \
	( \
		barrier(), \
		((suppressint = (v)) == 0 && intpending) ? onint() : (void)0 \
	)
#define CLEAR_PENDING_INT intpending = 0
#define int_pending() intpending

void exraise(int) attribute((noreturn));
#ifdef USE_NORETURN
void onint(void) attribute((noreturn));
#else
void onint(void);
#endif
extern int errlinno;
void sh_error(const char *, ...) attribute((noreturn));
void exerror(int, const char *, ...) attribute((noreturn));
const char *errnomsg(void);
const char *errmsg(int);

void sh_warnx(const char *, ...);

#endif
