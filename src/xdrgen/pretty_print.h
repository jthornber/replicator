#ifndef XDRGEN_PRETTY_PRINT_H
#define XDRGEN_PRETTY_PRINT_H

#include "ast.h"

/*----------------------------------------------------------------*/

void pretty_print(struct specification *spec);
void print_header(struct specification *spec);
void print_body(struct specification *spec);

/*----------------------------------------------------------------*/

#endif
