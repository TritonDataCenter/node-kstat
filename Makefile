#
# Copyright (c) 2016 Joyent, Inc.  All rights reserved.
#

PREFIX_NODE :=	$(shell dirname $$(bash -c 'hash node; hash -t node'))/..
V8PLUS :=       $(shell $(PREFIX_NODE)/bin/node -e 'require("v8plus");')
BIT :=		$(shell file $$(which node) | awk '{ print $$3 }' | cut -d- -f1)

include $(V8PLUS)/Makefile.v8plus.defs

MODULE =	kstat
MODULE_DIR =	.

SRCS =	\
		kstat.c

ERRNO_JSON =	errno.json

CC =		/opt/local/bin/gcc
CXX =		/opt/local/bin/g++

#
# v8plus assumes the presence of ctfconvert and ctfmerge, which is about as
# practical as requiring a Sun Microsystems badge number; we set these to be
# /bin/true to override this behavior.
#
CTFCONVERT =	/bin/true
CTFMERGE =	/bin/true

CFLAGS +=	-pthread -D__EXTENSIONS__ -DV8PLUS_NEW_API -D_REENTRANT -m$(BIT)

CXXFLAGS +=	-m$(BIT)

LIBS +=		-lumem -lkstat -m$(BIT)

JSSTYLE = 	./tools/jsstyle
BASH_FILES =	./tools/multinode
JSSTYLE_FILES =	$(shell find test examples -name "*.js")

JSL_CONF_NODE =	./tools/jsl.node.conf
JSL_FILES_NODE = $(shell find test examples -name "*.js")

CSTYLE = 	./tools/cstyle.pl
CSTYLE_FILES =	$(SRCS)

include $(V8PLUS)/Makefile.v8plus.targ
include Makefile.check.targ

