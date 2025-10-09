AM_YFLAGS = -d

COMMON_CPPFLAGS = -DBINDIR='"$(bindir)"'

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
	mylocale.c mystring.c options.c parser.c priv.c redir.c show.c \
	signames.c trap.c output.c syntax.c system.c var.c bltin/printf.c \
	bltin/test.c bltin/times.c
gwsh_SOURCES = \
	$(gwsh_CFILES) \
	alias.h arith_yacc.h bltin/bltin.h cd.h error.h eval.h exec.h \
	expand.h \
	init.h input.h jobs.h machdep.h mail.h main.h memalloc.h miscbltin.h \
	myhistedit.h mylocale.h mystring.h options.h output.h parser.h priv.h \
	redir.h shell.h show.h syntax.h system.h trap.h var.h
nodist_gwsh_SOURCES = builtins.c init.c nodes.c

BUILT_SOURCES = builtins.h nodes.h token.h token_vars.h
CLEANFILES = \
	builtins.def builtins.c builtins.h init.c nodes.c nodes.h \
	token.h token_vars.h $(HELPERS)

man_MANS = gwsh.1
EXTRA_DIST = \
	$(man_MANS) \
	mktokens mkbuiltins builtins.def.in mkinit.awk \
	mknodes.awk nodetypes nodes.c.pat nodes.h.pat

token.h token_vars.h: mktokens
	$(AM_V_GEN)$(SHELL) $(srcdir)/mktokens

builtins.def: builtins.def.in $(top_builddir)/config.h
	$(AM_V_GEN)echo '#include "builtins.def.in"' > $@.c && $(CPP) $(DEFAULT_INCLUDES) $@.c > $@.tmp && rm $@.c && mv $@.tmp $@

builtins.c builtins.h: mkbuiltins builtins.def
	$(AM_V_GEN)$(SHELL) $(srcdir)/mkbuiltins builtins.def

init.c: mkinit.awk $(gwsh_CFILES)
	$(AM_V_GEN)(cd $(srcdir) && awk -f mkinit.awk $(gwsh_CFILES)) > init.c \
		|| (rm -f init.c; exit 1)

nodes.c nodes.h: mknodes.awk nodetypes nodes.c.pat nodes.h.pat
	$(AM_V_GEN)awk -f $(srcdir)/mknodes.awk \
		-v nodes_c_pat=$(srcdir)/nodes.c.pat -v nodes_c=nodes.c \
		-v nodes_h_pat=$(srcdir)/nodes.h.pat -v nodes_h=nodes.h \
		$(srcdir)/nodetypes \
		|| (rm -f nodes.c nodes.h; exit 1)
