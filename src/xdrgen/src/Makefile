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
	$(XDRGEN_DIR)/pp_ruby.o \
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
