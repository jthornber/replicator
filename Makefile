# FIXME: split this up into sub-makefiles and include (non-recursive
# build).

# FIXME: add dependency generation.

CC=gcc
CFLAGS=-Wall -g -Werror
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
	utility

LINK_INCLUDES:=$(shell scripts/mk_links $(UNITS))

# csp
CSP_DIR=src/csp/src
CSP_TEST=src/csp/tests
LIB_OBJECTS+=\
	$(CSP_DIR)/process.o

$(CSP_TEST)/process_t: $(CSP_TEST)/process_t.c lib/libreplicator.a
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -Llib -lreplicator

$(CSP_TEST)/process1_t: $(CSP_TEST)/process1_t.c lib/libreplicator.a
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< -Llib -lreplicator

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

# xdr
XDR_DIR=src/xdr/src
LIB_OBJECTS+=\
	$(XDR_DIR)/xdr.o

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
	$(Q)$(CC) $+ -o $@ -Llib -lreplicator

# utility
UTIL_DIR=src/utility/src
UTIL_OBJECTS=\
	$(UTIL_DIR)/dynamic_buffer.o
LIB_OBJECTS+=\
	$(UTIL_OBJECTS)

# xdrgen
XDRGEN_DIR=src/xdrgen/src
XDRGEN_OBJECTS=\
	$(XDRGEN_DIR)/lex.yy.o \
	$(XDRGEN_DIR)/xdrgen.tab.o \
	$(XDRGEN_DIR)/xdrgen.o \
	$(XDRGEN_DIR)/pretty_print.o \
	$(XDRGEN_DIR)/emit.o \
	$(XDRGEN_DIR)/pp_header.o \
	$(XDRGEN_DIR)/pp_body.o \
	$(XDRGEN_DIR)/main.o \
	$(XDRGEN_DIR)/var.o

bin/xdrgen: $(XDRGEN_OBJECTS) lib/libreplicator.a
	@echo "    [LD] "$@
	$(Q)$(CC) $(CFLAGS) $(XDRGEN_OBJECTS) -o $@ -Llib -lreplicator

$(XDRGEN_DIR)/lex.yy.c: $(XDRGEN_DIR)/xdrgen.l
	@echo '   [LEX] '$@
	$(Q)flex -o $@ $(XDRGEN_DIR)/xdrgen.l

$(XDRGEN_DIR)/lex.yy.o: $(XDRGEN_DIR)/xdrgen.tab.h

$(XDRGEN_DIR)/xdrgen.tab.h $(XDRGEN_DIR)/xdrgen.tab.c: $(XDRGEN_DIR)/xdrgen.y
	@echo '    [YACC] '$@
	$(Q)bison --defines $(XDRGEN_DIR)/xdrgen.y -o $@

lib/libreplicator.a: $(LIB_OBJECTS)
	@echo '    [AR] '$@
	$(Q)$(AR) -sr $@ $+

.PHONEY: test-programs
test-programs:

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

.PHONEY: unit-test ruby-test test-programs

unit-test: test-programs
	$(RUBY) report-generators/unit_test.rb $(shell find . -name TESTS)
	$(RUBY) report-generators/title_page.rb

memcheck: test-programs
	$(RUBY) report-generators/memcheck.rb $(shell find . -name TESTS)
	$(RUBY) report-generators/title_page.rb

ruby-test:
	$(RUBY) report-generators/test/ts.rb
