#include "pretty_print.h"
#include "emit.h"
#include "xdrgen.h"

/*----------------------------------------------------------------*/

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

typedef struct var *var_t;

static var_t new_var()
{
        var_t v = zalloc(sizeof(*v));
        return v;
}

static var_t top(const char *str)
{
        var_t r = new_var();
        r->t = VT_TOP;
        r->str = str;
        return r;
}

static var_t field(var_t v, const char *str)
{
        var_t r = new_var();
        r->t = VT_FIELD;
        r->v = v;
        r->str = str;
        return r;
}

static var_t deref(var_t v)
{
        var_t r = new_var();
        r->t = VT_DEREF;
        r->v = v;
        return r;
}

static var_t ref(var_t v)
{
        var_t r = new_var();
        r->t = VT_REF;
        r->v = v;
        return r;
}

static var_t subscript(var_t v, const char *str)
{
        var_t r = new_var();
        r->t = VT_SUBSCRIPT;
        r->v = v;
        r->str = str;
        return r;
}

static void emit_var(var_t v)
{
        emit("(");
        switch (v->t) {
        case VT_TOP:
                emit("%s", v->str);
                break;

        case VT_FIELD:
                emit_var(v->v);
                emit(".%s", v->str);
                break;

        case VT_DEREF:
                emit("*(");
                emit_var(v->v);
                emit(")");
                break;

        case VT_REF:
                emit("&(");
                emit_var(v->v);
                emit(")");
                break;

        case VT_SUBSCRIPT:
                emit_var(v->v);
                emit("[%s]", v->str);
                break;
        }
        emit(")");
}

/*----------------------------------------------------------------*/

static void pack_type(struct type *t, var_t v);
static void pack_enum_detail(struct enum_detail *ed, var_t v);
static void pack_struct_detail(struct struct_detail *sd, var_t v);
static void pack_union_detail(struct union_detail *ud, var_t v);
static void pack_decl(struct decl *d, var_t v);

/*----------------------------------------------------------------*/

static void pack(const char *fn, var_t v)
{
        emit("if (!xdr_pack_%s(buf, ", fn);
        emit_var(v);
        emit("))"); nl();
        push(); emit("return 0;"); pop();
}

static void pp_expr(struct const_expr *ce)
{
        switch (ce->type) {
        case CE_LITERAL:
                emit("%d", ce->u.literal);
                break;

        case CE_REFERENCE:
                emit("%s", ce->u.reference);
                break;
        }
}

static void pack_decl_internal(struct decl_internal *di, var_t v)
{
        switch (di->type) {
        case DECL_SIMPLE:
                pack_type(di->u.simple.t, v);
                break;

        case DECL_ARRAY:
                emit("{"); push(); nl();
                emit("unsigned int i;"); nl();
                emit("for (i = 0; i < ");
                pp_expr(di->u.array.e);
                emit("; i++) {"); push(); nl();
                {
                        var_t v2 = subscript(v, "i");
                        var_t v3 = ref(v2);
                        pack_type(di->u.array.t, v3);
                }
                pop(); nl(); emit("}");
                pop(); nl(); emit("}");
                break;

        case DECL_VAR_ARRAY:
                emit("{"); push(); nl();
                emit("unsigned int i;"); nl();
                // FIXME: check the return value
                emit("if (!xdr_pack_uint(buf, ");
                {
                        var_t v2 = field(v, "len");
                        emit_var(v2);
                        emit("))"); push(); nl();
                        emit("return 0;"); pop(); nl();
                        emit("for (i = 0; i < ");
                        emit_var(v2);
                }
                emit("; i++) {"); push(); nl();
                {
                        var_t v2 = ref(subscript(field(v, "array"), "i"));
                        pack_type(di->u.var_array.t, v2); nl();
                }
                pop(); emit("}"); pop(); nl();
                emit("}");
                break;

        case DECL_OPAQUE:
                emit("{"); push(); nl();
                emit("if (!xdr_buffer_write(buf, (void *) ");
                emit_var(v);
                emit(", ");
                pp_expr(di->u.opaque.e);
                emit("))"); push(); nl();
                emit("return 0;");
                nl(); pop();
                break;

        case DECL_VAR_OPAQUE:
                emit("{");
                push(); nl();

                emit("if (!xdr_pack_uint(buf, ");
                {
                        var_t v2 = field(v, "len");
                        emit_var(v2);
                }
                emit("))"); push(); nl();
                emit("return 0;"); pop(); nl();

                emit("if (!xdr_buffer_write(buf, (void *) ");
                {
                        var_t v2 = field(v, "data");
                        emit_var(v2);
                }

                emit(", ");

                {
                        var_t v2 = field(v, "len");
                        emit_var(v2);
                }

                emit("))"); push(); nl();
                emit("return 0;");
                nl(); pop();

                pop();
                emit("}");
                break;

        case DECL_STRING:
                emit("{"); push(); nl();
                emit("unsigned len = strlen(");
                emit_var(v);
                emit(");"); nl();

                emit("if (!xdr_pack_uint(buf, len))"); push(); nl();
                emit("return 0;");
                pop(); nl();

                emit("if (!xdr_buffer_write(buf, ");
                emit_var(v);
                emit(", len))"); push(); nl();
                emit("return 0;");
                pop(); nl();
                pop(); nl();
                emit("}");
                break;

        case DECL_POINTER:
                emit("if (!");
                emit_var(v);
                emit(")"); push(); nl();
                emit("if (!xdr_pack_uint(buf, 0))"); push(); nl();
                emit("return 0;");
                pop(); nl();
                pop(); nl();
                emit("else {"); push(); nl();
                emit("if (!xdr_pack_uint(buf, 1))"); push(); nl();
                emit("return 0;");
                pop(); nl();

                pack_type(di->u.pointer.t, v);

                pop(); nl();
                emit("}");
                break;
        }
}

static void pack_typedef_internal(struct typedef_internal *ti)
{
        var_t v = deref(top("input"));
        switch (ti->type) {
        case DEF_SIMPLE:
                pack_decl_internal(ti->u.tsimple.di, v);
                break;

        case DEF_ENUM:
                pack_enum_detail(ti->u.tenum.ed, v);
                break;

        case DEF_STRUCT:
                pack_struct_detail(ti->u.tstruct.sd, v);
                break;

        case DEF_UNION:
                pack_union_detail(ti->u.tunion.ud, v);
                break;
        }
}

static void pack_type(struct type *t, var_t v)
{
        switch (t->type) {
        case TINT:
                pack("int", v);
                break;

        case TUINT:
                pack("uint", v);
                break;

        case THYPER:
                pack("hyper", v);
                break;

        case TUHYPER:
                pack("uhyper", v);
                break;

        case TFLOAT:
                pack("float", v);
                break;

        case TDOUBLE:
                pack("double", v);
                break;

        case TBOOL:
                pack("bool", v);
                break;

        case TENUM:
                pack_enum_detail(t->u.tenum.ed, v);
                break;

        case TSTRUCT:
                pack_struct_detail(t->u.tstruct.sd, v);
                break;

        case TUNION:
                pack_union_detail(t->u.tunion.ud, v);
                break;

        case TTYPEDEF:
                pack(t->u.ttypedef.t, ref(v));
                break;
        }
}

static void pack_enum_detail(struct enum_detail *ed, var_t v)
{
        emit("if (!xdr_pack_uint(buf, (uint) "); emit_var(v); emit("))"); push(); nl();
        emit("return 0;"); pop(); nl();
}

static void pack_struct_detail(struct struct_detail *sd, var_t v)
{
        struct decl *d;

        list_iterate_items(d, &sd->decls) {
                pack_decl(d, v); nl();
        }
}

static void pack_decl(struct decl *d, var_t v)
{
        switch(d->type) {
        case DECL_VOID:
                break;

        case DECL_OTHER:
        {
                var_t v2 = field(v, d->u.tother.identifier);
                pack_decl_internal(d->u.tother.di, v2);
                break;
        }
        }
}

static void pack_union_detail(struct union_detail *ud, var_t v)
{
        struct case_entry *ce;

        emit("switch (");
        {
                var_t v2 = field(v, ud->discriminator->u.tother.identifier);
                emit_var(v2);
        }
        emit(") {"); push(); nl();
        {
                var_t v2 = field(v, "u");
                {
                        list_iterate_items(ce, &ud->cases) {
                                emit("case ");
                                pp_expr(ce->ce);
                                emit(": {"); push(); nl();
                                pack_decl(ce->d, v2); nl();
                                emit("break;"); nl();
                                pop();
                                emit("}");
                                nl(); nl();
                        }

                        if (ud->default_case) {
                                emit("default: {"); nl();
                                push();
                                pack_decl(ud->default_case, v2); nl();
                                emit("break;"); nl();
                                pop();
                                emit("}"); nl();
                        }
                }
        }
        pop();
        emit("}"); nl();
}

static void sep()
{
        emit("/*----------------------------------------------------------------*/\n\n");
}

static void decl_(struct typedef_ *td)
{
        emit("int xdr_pack_%s(struct xdr_buffer *buf, %s *input)",
             td->identifier, td->identifier);
        nl();
        emit("{"); push(); nl();
        {
                pack_typedef_internal(td->ti);
                emit("return 1;");
        }
        pop(); nl(); emit("}"); nl(); nl();

        emit("int xdr_unpack_%s(struct xdr_cursor *c, struct pool *mem, %s **output)",
             td->identifier, td->identifier);
        nl();
        emit("{"); push(); nl();
        {
                emit("return 0;");
        }
        pop(); nl(); emit("}"); nl();
}

static void decls_(struct specification *spec)
{
        struct definition *def;

        list_iterate_items(def, &spec->definitions) {
                switch (def->type) {
                case DEF_TYPEDEF:
                        decl_(def->u.ttypedef.td); nl();
                        break;

                default:
                        break;
                }
        }
}

void print_body(struct specification *spec)
{
        /* FIXME: hack */
        emit("#include \"protocol.h\""); nl();
        emit("#include <string.h>"); nl(); nl();
        sep();
        decls_(spec);
        sep();
}

/*----------------------------------------------------------------*/
