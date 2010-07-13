#ifndef XDR_EMIT_H
#define XDR_EMIT_H

#include <stdio.h>

/*----------------------------------------------------------------*/

/* FIXME: horrible, stateful hack */
void set_output_file(FILE *f);

void push();
void pop();

void emit(const char *fmt, ...);
void nl();

void emit_caps(const char *str);

/*----------------------------------------------------------------*/

#endif
