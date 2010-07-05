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


# xdrgen
DIR:=src/xdrgen/src
XDRGEN_OBJECTS=\
	src/datastruct/src/list.o \
	src/mm/src/pool.o \
	src/log/src/log.o \
	$(DIR)/lex.yy.o \
	$(DIR)/xdrgen.tab.o \
	$(DIR)/xdrgen.o \
	$(DIR)/pretty_print.o \
	$(DIR)/emit.o \
	$(DIR)/pp_header.o \
	$(DIR)/pp_body.o \
	$(DIR)/main.o

bin/xdrgen: $(XDRGEN_OBJECTS)
	@echo "    [LD] "$@
	$(Q)$(CC) $(CFLAGS) $(XDRGEN_OBJECTS) -o $@

$(DIR)/lex.yy.c: $(DIR)/xdrgen.l
	@echo '   [LEX] '$@
	$(Q)flex -o $@ $(DIR)/xdrgen.l

$(DIR)/lex.yy.o: $(DIR)/xdrgen.tab.h

$(DIR)/xdrgen.tab.h $(DIR)/xdrgen.tab.c: $(DIR)/xdrgen.y
	@echo '    [YACC] '$@
	$(Q)bison --defines $(DIR)/xdrgen.y -o $@

Q=

.SUFFIXES:
.SUFFIXES: .c .o .l .y

.c.o:
	@echo "    [CC] " $@
	$(Q)$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@