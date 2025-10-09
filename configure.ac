AC_INIT([gwsh], m4_normalize(m4_include([VERSION])))
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/main.c])

AC_CONFIG_HEADERS([config.h])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_USE_SYSTEM_EXTENSIONS
AX_CFLAGS_WARN_ALL
AC_SYS_LARGEFILE

AX_C___ATTRIBUTE__
if test "$ax_cv___attribute__" = "yes"; then
	AC_DEFINE([attribute(x)], [__attribute__(x)], [Define to __attribute__(x) if the compiler supports GNU attribute syntax.])
else
	AC_DEFINE([attribute(x)], [])
fi
AC_CACHE_CHECK([for computed goto], [gwsh_cv_computed_goto],
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[void *l = &&label; goto *l; label:;])],
                   [gwsh_cv_computed_goto=yes],
                   [gwsh_cv_computed_goto=no])])
if test "$gwsh_cv_computed_goto" = "yes"; then
	AC_DEFINE_UNQUOTED([HAVE_COMPUTED_GOTO],[1],
	                   [Define to 1 if your compiler supports the GNU C computed goto extension.])
fi
AC_CACHE_CHECK([for _Static_assert], [gwsh_cv_static_assert],
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[_Static_assert(1, "")])],
                   [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[_Static_assert(0, "")])],
                                      [gwsh_cv_static_assert=no],
                                      [gwsh_cv_static_assert=yes])],
                   [gwsh_cv_static_assert=no])])
if test "$gwsh_cv_static_assert" = "yes"; then
	AC_DEFINE_UNQUOTED([HAVE_STATIC_ASSERT],[1],
	                   [Define to 1 if your compiler supports the C11 _Static_assert keyword.])
fi
AC_ARG_ENABLE([static], [AS_HELP_STRING([--enable-static], \
                                        [Build statical linked program])])
if test "$enable_static" = "yes"; then
	export LDFLAGS="-static -Wl,--fatal-warnings"
fi

AC_ARG_ENABLE([default-privileged],
              [AS_HELP_STRING([--enable-default-privileged], [Keep extra privileges when started with effective UID/GID unequal to real UID/GID.])])
if test "$enable_default_privileged" != "no"; then
	AC_DEFINE_UNQUOTED([ENABLE_DEFAULT_PRIVILEGED], [1],
	                   [Define to 1 to keep extra privileges when started with effective UID/GID unequal to real UID/GID.])
fi

dnl Checks for libraries.

dnl Checks for header files.
AC_CHECK_HEADERS([alloca.h paths.h sys/ioctl.h xlocale.h])

dnl Check for declarations
AC_CHECK_DECL([_PATH_BSHELL],,[AC_DEFINE_UNQUOTED([_PATH_BSHELL], ["/bin/sh"], [Define to system shell path])],[
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
])
AC_CHECK_DECL([_PATH_DEVNULL],,[AC_DEFINE_UNQUOTED([_PATH_DEVNULL], ["/dev/null"], [Define to devnull device node path])],[
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
])
AC_CHECK_DECL([_PATH_TTY],,[AC_DEFINE_UNQUOTED([_PATH_TTY], ["/dev/tty"], [Define to tty device node path])],[
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
])
AC_ARG_ENABLE([self-exec-path],
              [AS_HELP_STRING([--enable-self-exec-path[[=PATH]]], [Define to path to exec new instance of shell.])],,
              [enable_self_exec_path=auto])
AC_CACHE_CHECK([for path to exec new shell], [gwsh_cv_self_exec_path], [
case "$enable_self_exec_path" in
yes|no|/*) ;;
auto)
  if test "$cross_compiling" = no && test -x /proc/self/exe; then
    enable_self_exec_path=/proc/self/exe
  else
    enable_self_exec_path=no
  fi ;;
*) AC_MSG_ERROR([exec path must be absolute])
esac
gwsh_cv_self_exec_path=$enable_self_exec_path])
case "$gwsh_cv_self_exec_path" in
yes) AC_DEFINE_UNQUOTED([SELF_EXEC_PATH], [BINDIR "/gwsh"]) ;;
/*)  AC_DEFINE_UNQUOTED([SELF_EXEC_PATH], ["$gwsh_cv_self_exec_path"], [Define to path to gwsh.]) ;;
esac

dnl Some systems lack isblank
AC_CHECK_DECLS([isblank],,,[#include <ctype.h>])

dnl Check for sizes and widths of types
AC_CHECK_SIZEOF([size_t])
AC_CHECK_SIZEOF([intmax_t])
AC_CHECK_DECL([INTMAX_WIDTH],,
              [AC_DEFINE_UNQUOTED([INTMAX_WIDTH],[(sizeof(intmax_t)*CHAR_BIT)],
                                  [Define to the width of intmax_t if your <stdint.h> does not provide it.])],[
#include <stdint.h>
])

dnl Select a fallback format string for intmax_t in case we don't find PRIdMAX
if test "x$ac_cv_sizeof_intmax_t" = "x$ac_cv_sizeof_long_long_int"; then
  intmax_fstr="lld"
else
  intmax_fstr="jd"
fi

dnl Check for PRIdMAX and define it to a fallback if not found
AC_CHECK_DECL([PRIdMAX],,
	[AC_DEFINE_UNQUOTED([PRIdMAX], ["$intmax_fstr"],
	                    [Define to printf format string for intmax_t])],
	[
#include <inttypes.h>
])

dnl Checks for library functions.
AC_CHECK_FUNCS([bsearch faccessat getpwnam getrlimit isalpha killpg \
                mempcpy stpcpy strchrnul strsignal strtod \
                strtoimax strtoumax sysconf])

dnl Check whether it's worth working around FreeBSD PR kern/125009.
dnl The traditional behavior of access/faccessat is crazy, but
dnl POSIX.1-2008 explicitly allows those functions to misbehave.
dnl
dnl Unaffected kernels:
dnl
dnl - all versions of Linux
dnl - NetBSD sys/kern/vfs_subr.c 1.64, 1997-04-23
dnl - FreeBSD 9 (r212002), 2010-09-10
dnl - OpenBSD sys/kern/vfs_subr.c 1.166, 2008-06-09
dnl
dnl Also worked around in Debian's libc0.1 2.13-19 when using
dnl kFreeBSD 8.

AC_ARG_ENABLE([test-workaround], [AS_HELP_STRING([--enable-test-workaround], \
	[Guard against faccessat(2) that tells root all files are executable])],,
	[enable_test_workaround=auto])

if test "enable_test_workaround" = "auto" &&
   test "$ac_cv_func_faccessat" = yes; then
	case `uname -s 2>/dev/null` in
	GNU/kFreeBSD | \
	FreeBSD)
		enable_test_workaround=yes
	esac
fi
if test "$enable_test_workaround" = "yes"; then
	AC_DEFINE([HAVE_TRADITIONAL_FACCESSAT], [1],
	          [Define if your faccessat tells root all files are executable])
fi

dnl Check for klibc signal.
AC_CHECK_FUNC([signal])
if test "$ac_cv_func_signal" != yes; then
	AC_CHECK_FUNC([bsd_signal],
	              [AC_DEFINE([signal], [bsd_signal],
	                         [klibc has bsd_signal instead of signal])])
fi

AC_CHECK_MEMBER([struct stat.st_mtim], [
	AC_DEFINE([HAVE_ST_MTIM], [1], [Define to 1 if your `struct stat' has the member `st_mtim'.])])
AC_CHECK_MEMBER([struct stat.st_mtimespec], [
	AC_DEFINE([HAVE_ST_MTIMESPEC], [1], [Define to 1 if your `struct stat' has the member `st_mtimespec'.])])

AC_ARG_WITH([libedit], [AS_HELP_STRING([--with-libedit], [Compile with libedit support])])
use_libedit=
if test "$with_libedit" = "yes"; then
	AC_CHECK_LIB([edit], [history_init], [
		AC_CHECK_HEADER([histedit.h], [use_libedit="yes"],
				[AC_MSG_ERROR([Can't find required header files.])])], [
		AC_MSG_ERROR([Can't find libedit.])])
fi
if test "$use_libedit" != "yes"; then
	AC_DEFINE([SMALL], [1], [Define if you build with -DSMALL])
else
	export LIBS="$LIBS -ledit"
	AC_ARG_ENABLE([completion], [AS_HELP_STRING([--enable-completion[[=internal/external]]],
	                                            [Compile with completion support (default=external)])],
	              [], [enable_completion=auto])
	case $enable_completion in
	internal)
		AC_DEFINE([ENABLE_COMPLETION], [1], [Define if you build with completion support.])
		AC_DEFINE([ENABLE_INTERNAL_COMPLETION], [1], [Define if you build with libedit's completion support.])
		;;
	external|yes|auto)
		AC_CHECK_FUNCS([_el_fn_sh_complete _el_fn_complete], [break])
		if test "$ac_cv_func__el_fn_sh_complete:$ac_cv_func__el_fn_complete" = "no:no"; then
			if test "$enable_completion" != "auto"; then
				AC_MSG_ERROR([cannot find completion function in libedit])
			fi
		else
			AC_DEFINE([ENABLE_COMPLETION], [1])
			AC_DEFINE([ENABLE_EXTERNAL_COMPLETION], [1], [Define if you build with libedit's completion support.])
		fi
		;;
	no)
		;;
	esac
fi
AC_ARG_ENABLE([lineno], [AS_HELP_STRING([--disable-lineno], \
                                        [Disable LINENO support])])
if test "$enable_lineno" != "no"; then
	AC_DEFINE([WITH_LINENO], 1, [Define if you build with -DWITH_LINENO])
fi
AC_ARG_ENABLE([locale], [AS_HELP_STRING([--disable-locale], \
                                        [Disable locale support])])
if test "$enable_locale" != "no"; then
	AC_DEFINE([WITH_LOCALE], [1], [Define if you want locale support.])
	AC_ARG_ENABLE([parser-locale],
		      [AS_HELP_STRING([--disable-parser-locale],
		                      [Disable use of fixed locale during parsing.])])
	if test "$enable_parser_locale" != "no"; then
		AC_CHECK_FUNC([uselocale],
			[AC_DEFINE([WITH_PARSER_LOCALE], [1],
			          [Define if you want parsing to ignore LC_CTYPE changes after the shell starts.])],
			[if test "$enable_parser_locale" = "yes"; then
				AC_MSG_ERROR([--enable-parser-locale requires the uselocale() function.])
			fi])
	fi
fi
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
