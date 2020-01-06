#!/bin/sh

mkdir -p build-aux \
&& aclocal \
&& autoheader \
&& automake --add-missing --copy \
&& autoconf
