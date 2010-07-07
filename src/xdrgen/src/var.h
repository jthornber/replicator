#ifndef XDRGEN_VAR_H
#define XDRGEN_VAR_H

/*----------------------------------------------------------------*/

struct var;
typedef struct var *var_t;

var_t top(const char *str);
var_t field(var_t v, const char *str);
var_t deref(var_t v);
var_t ref(var_t v);
var_t subscript(var_t v, const char *str);
void emit_var(var_t v);

/*----------------------------------------------------------------*/

#endif
