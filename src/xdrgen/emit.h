#ifndef XDR_EMIT_H
#define XDR_EMIT_H

/*----------------------------------------------------------------*/

void push();
void pop();

void emit(const char *fmt, ...);
void nl();

/*----------------------------------------------------------------*/

#endif
