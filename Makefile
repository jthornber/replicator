# FIXME: split this up into sub-makefiles and include (non-recursive
# build).

# FIXME: add dependency generation.

CC=gcc
CFLAGS=-Wall -g -Werror -D_GNU_SOURCE
INCLUDES=\
	-Iinclude

LEX=flex
YACC=bison
XDRGEN=bin/xdrgen

# immediate evaluation used to force this to be performed early on
UNITS=\
	log \
	datastruct \
	mm \
	xdr \
	csp \
	utility \
	snapshots

LINK_INCLUDES:=$(shell scripts/mk_links $(UNITS))

include src/csp/src/Makefile
include src/csp/tests/Makefile

# datastruct
DS_DIR=src/datastruct/src
LIB_OBJECTS+=\
	$(DS_DIR)/list.o

# mm
MM_DIR=src/mm/src
LIB_OBJECTS+=\
	$(MM_DIR)/pool.o

# log
LOG_DIR=src/log/src
LIB_OBJECTS+=\
	$(LOG_DIR)/log.o
include src/log/test/Makefile

# xdr
XDR_DIR=src/xdr/src
LIB_OBJECTS+=\
	$(XDR_DIR)/xdr.o

include src/xdr/test/Makefile

# snapshot
include src/snapshots/src/Makefile
include src/snapshots/test/Makefile

# replicator
REP_DIR=src/replicator/src
REP_OBJECTS=\
	$(REP_DIR)/protocol.o \
	$(REP_DIR)/main.o

$(REP_DIR)/protocol.h: $(XDRGEN)
$(REP_DIR)/protocol.c: $(XDRGEN)
$(REP_DIR)/protocol.o: $(REP_DIR)/protocol.h

$(REP_DIR)/main.o: $(REP_DIR)/protocol.h

bin/replicator: $(REP_OBJECTS)
	@echo '    [LN] '$@
	$(Q)$(CC) $+ -o $@ -Llib -lreplicator -lrt

# utility
UTIL_DIR=src/utility/src
UTIL_OBJECTS=\
	$(UTIL_DIR)/dynamic_buffer.o
LIB_OBJECTS+=\
	$(UTIL_OBJECTS)

include src/xdrgen/src/Makefile

lib/libreplicator.a: $(LIB_OBJECTS)
	@echo '    [AR] '$@
	$(Q)$(AR) -sr $@ $+

.PHONEY: test-programs
test-programs: $(TEST_PROGRAMS)

Q=@

.SUFFIXES:
.SUFFIXES: .c .o .l .y .xdr .h

.c.o:
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.xdr.h:
	@echo '    [XDRGEN] '$@
	$(Q)$(XDRGEN) --format header -o $@ $<

.xdr.c:
	@echo '    [XDRGEN] '$@
	$(Q)$(XDRGEN) --format body -o $@ $<

RUBY=ruby1.9 -Ireport-generators/lib -Ireport-generators/test
RUBY-FT=ruby1.9 -Ireport-generators/lib -Ifunctional-tests/lib -Ifunctional-tests/tests

.PHONEY: unit-test ruby-test test-programs

unit-test: test-programs
	$(RUBY) report-generators/unit_test.rb $(shell find . -name TESTS)
	$(RUBY) report-generators/title_page.rb

memcheck: test-programs
	$(RUBY) report-generators/memcheck.rb $(shell find . -name TESTS)
	$(RUBY) report-generators/title_page.rb

ruby-test:
	$(RUBY) report-generators/test/ts.rb

.PHONEY: ft
ft: functional-tests/lib/protocol.rb bin/replicator
	$(RUBY-FT) functional-tests/tests/ts.rb

functional-tests/lib/protocol.rb: src/replicator/src/protocol.xdr bin/xdrgen
	@echo '    [XDRGEN] '$@
	$(Q)$(XDRGEN) --format ruby -o $@ src/replicator/src/protocol.xdr
