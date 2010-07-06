# FIXME: split this up into sub-makefiles and include (non-recursive
# build).

# FIXME: add dependency generation.

CC=gcc
CFLAGS=-Wall -g
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
	csp
LINK_INCLUDES:=$(shell scripts/mk_links $(UNITS))

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
	$(REP_DIR)/main.o

$(REP_DIR)/main.o: $(REP_DIR)/protocol.h

bin/replicator: $(REP_OBJECTS)
	@echo '    [LN] '$@
	$(Q)$(CC) $+ -o $@ -Llib -lreplicator

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
	$(XDRGEN_DIR)/main.o

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

Q=

.SUFFIXES:
.SUFFIXES: .c .o .l .y .xdr .h

.c.o:
	@echo '    [CC] '$@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

.xdr.h:
	@echo '    [XDRGEN] '$@
	$(Q)$(XDRGEN) --format header < $< > $@

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
