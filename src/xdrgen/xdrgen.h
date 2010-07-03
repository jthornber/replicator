#ifndef XDRGEN_H
#define XDRGEN_H

#include <stdlib.h>

#include "ast.h"
#include "libdevmapper.h"

/*----------------------------------------------------------------*/

int yydebug;
int yyparse();
int yylex_destroy();
int yylex();

void init();
void fin();

void *zalloc(size_t s);
char *dup_string(const char *);

void inc_line();
unsigned get_line();

/* these allow the parser to return a value */
void set_result(struct specification *spec);
struct specification *get_result();

/*----------------------------------------------------------------*/

#endif
