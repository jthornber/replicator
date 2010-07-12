#include "var.h"
#include "xdrgen.h"
#include "emit.h"

/*
 * We need to keep track of a variable context.  This is a list of
 * identifiers, along with the whether they're a pointer or not.
 *
 *  eg, input->u.foo
 */
enum var_type {
        VT_TOP,
        VT_FIELD,
        VT_DEREF,
        VT_REF,
        VT_SUBSCRIPT
};

struct var {
        enum var_type t;
        struct var *v;
        const char *str;
};

var_t new_var()
{
        var_t v = zalloc(sizeof(*v));
        return v;
}

var_t top(const char *str)
{
        var_t r = new_var();
        r->t = VT_TOP;
        r->str = str;
        return r;
}

var_t field(var_t v, const char *str)
{
        var_t r = new_var();
        r->t = VT_FIELD;
        r->v = v;
        r->str = str;
        return r;
}

var_t deref(var_t v)
{
        var_t r = new_var();
        r->t = VT_DEREF;
        r->v = v;
        return r;
}

var_t ref(var_t v)
{
        if (v->t == VT_DEREF)
                return v->v;
        else {
                var_t r = new_var();
                r->t = VT_REF;
                r->v = v;
                return r;
        }
}

var_t subscript(var_t v, const char *str)
{
        var_t r = new_var();
        r->t = VT_SUBSCRIPT;
        r->v = v;
        r->str = str;
        return r;
}

void emit_var(var_t v)
{
        switch (v->t) {
        case VT_TOP:
                emit("%s", v->str);
                break;

        case VT_FIELD:
                emit("(");
                emit_var(v->v);
                emit(".%s", v->str);
                emit(")");
                break;

        case VT_DEREF:
                emit("(");
                emit("*");
                emit_var(v->v);
                emit(")");
                break;

        case VT_REF:
                emit("(");
                emit("&");
                emit_var(v->v);
                emit(")");
                break;

        case VT_SUBSCRIPT:
                emit("(");
                emit_var(v->v);
                emit("[%s]", v->str);
                emit(")");
                break;
        }
}

