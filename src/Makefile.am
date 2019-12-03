AM_YFLAGS = -d

COMMON_CPPFLAGS = -DBSD -DSHELL

AM_CPPFLAGS = $(COMMON_CPPFLAGS)
AM_CPPFLAGS_FOR_BUILD = $(COMMON_CPPFLAGS)

COMPILE_FOR_BUILD = \
	$(CC_FOR_BUILD) $(DEFAULT_INCLUDES) $(AM_CPPFLAGS_FOR_BUILD) \
	$(CPPFLAGS_FOR_BUILD) \
	$(AM_CFLAGS_FOR_BUILD) $(CFLAGS_FOR_BUILD) 

bin_PROGRAMS = gwsh

gwsh_CFILES = \
	alias.c arith_yacc.c arith_yylex.c cd.c error.c eval.c exec.c expand.c \
	histedit.c input.c jobs.c mail.c main.c memalloc.c miscbltin.c \
	mylocale.c mystring.c options.c parser.c priv.c redir.c show.c signames.c \
	trap.c output.c bltin/printf.c system.c bltin/test.c bltin/times.c var.c
gwsh_SOURCES = \
	$(gwsh_CFILES) \
	alias.h arith_yacc.h bltin/bltin.h cd.h error.h eval.h exec.h \
	expand.h \
	init.h input.h jobs.h machdep.h mail.h main.h memalloc.h miscbltin.h \
	myhistedit.h mylocale.h mystring.h options.h output.h parser.h priv.h \
	redir.h shell.h show.h system.h trap.h var.h
nodist_gwsh_SOURCES = builtins.c init.c nodes.c syntax.c

HELPERS = mkinit mksyntax mknodes

BUILT_SOURCES = builtins.h nodes.h syntax.h token.h token_vars.h
CLEANFILES = \
	builtins.def builtins.c builtins.h init.c nodes.c nodes.h \
	syntax.c syntax.h token.h token_vars.h $(HELPERS)

man_MANS = gwsh.1
EXTRA_DIST = \
	$(man_MANS) \
	mktokens mkbuiltins builtins.def.in mkinit.c \
	mknodes.c nodetypes nodes.c.pat mksyntax.c

token.h token_vars.h: mktokens
	$(AM_V_GEN)$(SHELL) $(srcdir)/mktokens

builtins.def: builtins.def.in $(top_builddir)/config.h
	$(AM_V_GEN)echo '#include "builtins.def.in"' > $@.c && $(CPP) $(DEFAULT_INCLUDES) $@.c > $@.tmp && rm $@.c && mv $@.tmp $@

builtins.c builtins.h: mkbuiltins builtins.def
	$(AM_V_GEN)$(SHELL) $(srcdir)/mkbuiltins builtins.def

init.c: mkinit $(gwsh_CFILES)
	$(AM_V_GEN)./mkinit `echo " $(gwsh_CFILES)" | sed -e 's: : $(srcdir)/:g'`

nodes.c nodes.h: mknodes nodetypes nodes.c.pat
	$(AM_V_GEN)./mknodes $(srcdir)/nodetypes $(srcdir)/nodes.c.pat

syntax.c syntax.h: mksyntax
	$(AM_V_GEN)./mksyntax

mksyntax: token.h

CCBUILD = $(AM_V_CC)$(COMPILE_FOR_BUILD) -o $@

mkinit: mkinit.c
	$(CCBUILD) $(srcdir)/mkinit.c
mknodes: mknodes.c
	$(CCBUILD) $(srcdir)/mknodes.c
mksyntax: mksyntax.c
	$(CCBUILD) $(srcdir)/mksyntax.c
