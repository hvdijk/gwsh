/*-
 * Copyright (c) 2019-2020
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

#include "priv.h"
#include "error.h"
#include "options.h"
#include "shell.h"
#include "var.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

MKINIT int privileged;
#ifdef mkinit
INCLUDE "options.h"
INIT {
	uid_t euid = geteuid();
	if (!euid)
		defps1var[4] += '#' - '$';
	if (getuid() != euid || getgid() != getegid()) {
		privileged++;
#if ENABLE_DEFAULT_PRIVILEGED
		pflag++;
#endif
	}
}
#endif

void
setprivileged(int on)
{
	static char errfmt[] = "setregid: %s";
	uid_t uid;
	gid_t gid;

	if (!privileged || on)
		return;
	gid = getgid();
	if (setregid(gid, gid)) {
error:
		sh_error(errfmt, errnomsg());
	}
	uid = getuid();
	if (uid)
		defps1var[4] = '$';
	if (setreuid(uid, uid)) {
		errfmt[5] = 'u';
		goto error;
	}
	privileged = 0;
}
