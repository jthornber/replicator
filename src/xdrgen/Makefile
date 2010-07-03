# FIXME: need to autoconf this

LEX=flex
YACC=bison

.PHONEY: all

all:
	bison --defines xdrgen.y
	flex xdrgen.l
	gcc -Wall -g -I../libdm -c lex.yy.c -o lex.yy.o
	gcc -Wall -g -I../libdm -c xdrgen.tab.c -o xdrgen.tab.o
	gcc -Wall -g -I../libdm -c xdrgen.c -o xdrgen.o
	gcc -Wall -g -I../libdm -c pretty_print.c -o pretty_print.o
	gcc -Wall -g -I../libdm -c emit.c -o emit.o
	gcc -Wall -g -I../libdm -c pp_header.c -o pp_header.o
	gcc -Wall -g -I../libdm -c pp_body.c -o pp_body.o
	gcc -Wall -g -I../libdm main.c xdrgen.tab.o lex.yy.o xdrgen.o pretty_print.o emit.o pp_header.o pp_body.o -o xdrgen -L../libdm -ldevmapper

