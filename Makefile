# FIXME: split this up into sub-makefiles and include (non-recursive
# build).

# FIXME: add dependency generation.

CC=gcc
CFLAGS=-Wall -g
INCLUDES=\
	-Isrc/datastruct/src \
	-Isrc/mm/src \
	-Isrc/xdr/src \
	-Isrc/log/src

LEX=flex
YACC=bison

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

#	$(XDR_DIR)/xdr.o

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

bin/xdrgen: $(LIB_OBJECTS) $(XDRGEN_OBJECTS)
	@echo "    [LD] "$@
	$(Q)$(CC) $(CFLAGS) $+ -o $@

$(XDRGEN_DIR)/lex.yy.c: $(XDRGEN_DIR)/xdrgen.l
	@echo '   [LEX] '$@
	$(Q)flex -o $@ $(XDRGEN_DIR)/xdrgen.l

$(XDRGEN_DIR)/lex.yy.o: $(XDRGEN_DIR)/xdrgen.tab.h

$(XDRGEN_DIR)/xdrgen.tab.h $(XDRGEN_DIR)/xdrgen.tab.c: $(XDRGEN_DIR)/xdrgen.y
	@echo '    [YACC] '$@
	$(Q)bison --defines $(XDRGEN_DIR)/xdrgen.y -o $@

Q=

.SUFFIXES:
.SUFFIXES: .c .o .l .y

.c.o:
	@echo "    [CC] " $@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@