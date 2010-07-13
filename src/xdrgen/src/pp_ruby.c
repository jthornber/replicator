#include "pretty_print.h"
#include "emit.h"
#include "var.h"

#include <stdlib.h>

/*----------------------------------------------------------------*/

static void pp_type(struct type *t, var_t v);
static void pp_enum_detail(struct enum_detail *ed, var_t v);
static void pp_struct_detail(struct struct_detail *sd, var_t v);
static void pp_union_detail(struct union_detail *ud, var_t v);
static void pp_decl(struct decl *d, var_t v);

/*----------------------------------------------------------------*/

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

static void pp_const_def(struct const_def *cd)
{
        emit_caps(cd->identifier);
        if (cd->ce) {
                emit(" = ");
                pp_expr(cd->ce);
        }
}

static void pp_decl_internal(struct decl_internal *di, var_t v)
{
        switch (di->type) {
        case DECL_SIMPLE:
                pp_type(di->u.simple.t, v);
                break;

        case DECL_ARRAY:
                emit("pack_array(");
                pp_expr(di->u.array.e);
                emit(", ");
                emit_var(v);
                emit(") {|v| ");
                pp_type(di->u.array.t, top("v")); emit("}");
                break;

        case DECL_VAR_ARRAY:
                emit("pack_uint("); emit_var(v); emit(".length) + "); push(); nl();
                emit("pack_array("); emit_var(v); emit(".length, ");
                emit_var(v);
                emit(") {|v| ");
                pp_type(di->u.array.t, top("v")); emit("}");
                pop();
                break;

        case DECL_OPAQUE:
                emit("begin"); push(); nl();
                emit("if "); emit_var(v); emit(".length != "); pp_expr(di->u.opaque.e); push(); nl();
                emit("raise \"opaque is the wrong size\""); pop(); nl(); emit("end"); nl();

                emit("pack_raw("); emit_var(v); emit(")");
                pop(); nl(); emit("end");
                break;

        case DECL_VAR_OPAQUE:
                emit("begin"); push(); nl();
                emit("pack_uint("); emit_var(v); emit(".length) + "); push(); nl();
                emit("pack_raw("); emit_var(v); emit(")"); pop(); nl();
                pop(); nl(); emit("end");
                break;

        case DECL_STRING:
                emit("pack_string("); emit_var(v); emit(")");
                break;

        case DECL_POINTER:
                emit("if "); emit_var(v); emit(".nil?"); push(); nl();
                emit("pack_uint(0)");
                pop(); nl(); emit("else"); push(); nl();
                emit("pack_uint(1);"); nl();
                pp_type(di->u.pointer.t, v);
                pop(); nl();
                emit("end");
                break;
        }
}

static void pp_typedef_internal(struct typedef_internal *ti, var_t v)
{
        switch (ti->type) {
        case DEF_SIMPLE:
                pp_decl_internal(ti->u.tsimple.di, v); nl();
                break;

        case DEF_ENUM:
                pp_enum_detail(ti->u.tenum.ed, v);
                break;

        case DEF_STRUCT:
                pp_struct_detail(ti->u.tstruct.sd, v);
                break;

        case DEF_UNION:
                pp_union_detail(ti->u.tunion.ud, v);
                break;
        }
}

static void pp_typedef(struct typedef_ *td)
{
        emit("def pack_%s(v)", td->identifier); push(); nl();
        pp_typedef_internal(td->ti, top("v"));
        nl(); pop(); emit("end");
}

static void pp_type(struct type *t, var_t v)
{
        switch (t->type) {
        case TINT:
                emit("pack_int("); emit_var(v); emit(")");
                break;

        case TUINT:
                emit("pack_uint("); emit_var(v); emit(")");
                break;

        case THYPER:
                emit("pack_hyper("); emit_var(v); emit(")");
                break;

        case TUHYPER:
                emit("pack_uhyper("); emit_var(v); emit(")");
                break;

        case TFLOAT:
                emit("pack_float("); emit_var(v); emit(")");
                break;

        case TDOUBLE:
                emit("pack_double("); emit_var(v); emit(")");
                break;

        case TBOOL:
                emit("pack_bool("); emit_var(v); emit(")");
                break;

        case TENUM:
                pp_enum_detail(t->u.tenum.ed, v);
                break;

        case TSTRUCT:
                pp_struct_detail(t->u.tstruct.sd, v);
                break;

        case TUNION:
                pp_union_detail(t->u.tunion.ud, v);
                break;

        case TTYPEDEF:
                emit("pack_%s(v)", t->u.ttypedef.t);
                break;
        }
}

static void pp_enum_detail(struct enum_detail *ed, var_t v)
{
        struct const_def *cd;
        emit("begin"); push(); nl();
        emit("last = 0"); nl();
        emit("table = Hash.new"); nl();
        list_iterate_items(cd, &ed->fields) {
                if (cd->ce) {
                        emit("table[:%s] = ", cd->identifier);
                        pp_expr(cd->ce); nl();
                        emit("last = "); pp_expr(cd->ce); nl();
                } else {
                        emit("last = last + 1"); nl();
                        emit("table[:%s] = last", cd->identifier); nl();
                }

                nl();
        }
        emit("pack_enum(table, "); emit_var(v); emit(")"); nl();
        pop(); emit("end"); nl();
}

static void pp_struct_detail(struct struct_detail *sd, var_t v)
{
        struct decl *d;

        emit("["); push(); nl();
        list_iterate_items(d, &sd->decls) {
                pp_decl(d, v); emit(", "); nl();
        }
        pop();
        emit("].join");
}

static void pp_decl(struct decl *d, var_t v)
{
        switch(d->type) {
        case DECL_VOID:
                emit("''");
                break;

        case DECL_OTHER:
                pp_decl_internal(d->u.tother.di, field(v, d->u.tother.identifier));
                break;
        }
}

static void pp_union_detail(struct union_detail *ud, var_t v)
{
        struct case_entry *ce;

        emit("["); push(); nl();
        {
                var_t discriminator = field(v, ud->discriminator->u.tother.identifier);
                pp_decl(ud->discriminator, discriminator); emit(","); nl();
                emit("case "); emit_var(discriminator); nl();
                list_iterate_items(ce, &ud->cases) {
                        emit("when "); pp_expr(ce->ce); push(); nl();
                        pp_decl(ce->d, field(v, "u")); nl(); pop();
                }

                if (ud->default_case) {
                        emit("else"); push(); nl();
                        pp_decl(ud->default_case, field(v, "u"));
                        pop(); nl();
                }
        }
        pop();
        emit("].join");
}

void print_ruby(struct specification *spec)
{
        struct definition *def;

        emit("require 'xdr'"); nl(); nl();

        list_iterate_items(def, &spec->definitions) {
                switch (def->type) {
                case DEF_TYPEDEF:
                        pp_typedef(def->u.ttypedef.td);
                        break;

                case DEF_CONSTANT:
                        pp_const_def(def->u.tconst.cd);
                }
                nl();
        }
}

/*----------------------------------------------------------------*/
