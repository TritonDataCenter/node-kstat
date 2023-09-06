#
# Copyright 2016 Joyent, Inc.
# Copyright 2023 MNX Cloud, Inc.
#

NAME =		node-kstat

#
# This is a standalone node module.
#
ENGBLD_SKIP_VALIDATE_BUILDENV =	true

#
# Tools
#
NODE :=		$(shell which node)
NPM :=		$(shell which npm)
TAPE :=		./node_modules/.bin/tape

#
# Ensure we have the eng submodule before attempting to include it.
#
ENGBLD_REQUIRE :=	$(shell git submodule update --init deps/eng)
include ./deps/eng/tools/mk/Makefile.defs
TOP ?=			$(error Unable to access eng.git submodule Makefiles.)

#
# Configuration used by Makefile.defs and Makefile.targ to generate
# "check" and "docs" targets.
#
JSON_FILES =		package.json
JS_FILES :=		$(shell find examples test -name '*.js')
JSL_CONF_NODE =		./deps/eng/tools/jsl.node.conf
JSL_FILES_NODE =	$(JS_FILES)
JSSTYLE_FILES =		$(JS_FILES)

CLEAN_FILES +=		build

include ./deps/eng/tools/mk/Makefile.node_modules.defs

#
# Repo-specific targets
#
.PHONY: all
all: $(STAMP_NODE_MODULES)

.PHONY: test
test: $(STAMP_NODE_MODULES)
	$(NODE) $(TAPE) test/*.js

include ./deps/eng/tools/mk/Makefile.deps
include ./deps/eng/tools/mk/Makefile.node_modules.targ
include ./deps/eng/tools/mk/Makefile.targ
