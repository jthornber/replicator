#include "pretty_print.h"
#include "emit.h"
#include "var.h"

#include <stdlib.h>

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

/*----------------------------------------------------------------*/

/* Packing */
static void pp_pack_type(struct type *t, var_t v);
static void pp_pack_enum_detail(struct enum_detail *ed, var_t v);
static void pp_pack_struct_detail(struct struct_detail *sd, var_t v);
static void pp_pack_union_detail(struct union_detail *ud, var_t v);
static void pp_pack_decl(struct decl *d, var_t v);

/*----------------------------------------------------------------*/

static void pp_pack_const_def(struct const_def *cd)
{
        emit_caps(cd->identifier);
        if (cd->ce) {
                emit(" = ");
                pp_expr(cd->ce);
        }
}

static void pp_pack_decl_internal(struct decl_internal *di, var_t v)
{
        switch (di->type) {
        case DECL_SIMPLE:
                pp_pack_type(di->u.simple.t, v);
                break;

        case DECL_ARRAY:
                emit("pack_array(");
                pp_expr(di->u.array.e);
                emit(", ");
                emit_var(v);
                emit(") {|v| ");
                pp_pack_type(di->u.array.t, top("v")); emit("}");
                break;

        case DECL_VAR_ARRAY:
                emit("pack_uint("); emit_var(v); emit(".length) + "); push(); nl();
                emit("pack_array("); emit_var(v); emit(".length, ");
                emit_var(v);
                emit(") {|v| ");
                pp_pack_type(di->u.array.t, top("v")); emit("}");
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
                pp_pack_type(di->u.pointer.t, v);
                pop(); nl();
                emit("end");
                break;
        }
}

static void pp_pack_typedef_internal(struct typedef_internal *ti, var_t v)
{
        switch (ti->type) {
        case DEF_SIMPLE:
                pp_pack_decl_internal(ti->u.tsimple.di, v); nl();
                break;

        case DEF_ENUM:
                pp_pack_enum_detail(ti->u.tenum.ed, v);
                break;

        case DEF_STRUCT:
                pp_pack_struct_detail(ti->u.tstruct.sd, v);
                break;

        case DEF_UNION:
                pp_pack_union_detail(ti->u.tunion.ud, v);
                break;
        }
}

static void pp_pack_typedef(struct typedef_ *td)
{
        emit("def pack_%s(v)", td->identifier); push(); nl();
        pp_pack_typedef_internal(td->ti, top("v"));
        nl(); pop(); emit("end");
}

static void pp_pack_type(struct type *t, var_t v)
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
                pp_pack_enum_detail(t->u.tenum.ed, v);
                break;

        case TSTRUCT:
                pp_pack_struct_detail(t->u.tstruct.sd, v);
                break;

        case TUNION:
                pp_pack_union_detail(t->u.tunion.ud, v);
                break;

        case TTYPEDEF:
                emit("pack_%s(", t->u.ttypedef.t); emit_var(v); emit(")");
                break;
        }
}

static void emit_enum_table(struct enum_detail *ed)
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
                        emit("table[:%s] = last", cd->identifier); nl();
                        emit("last = last + 1"); nl();
                }

                nl();
        }
        emit("table"); nl();
        pop(); emit("end"); nl();
}

static void pp_pack_enum_detail(struct enum_detail *ed, var_t v)
{
        emit("begin"); push(); nl();
        emit("table = "); emit_enum_table(ed);
        emit("pack_enum(table, "); emit_var(v); emit(")"); nl();
        pop(); emit("end"); nl();
}

static void pp_pack_struct_detail(struct struct_detail *sd, var_t v)
{
        struct decl *d;

        emit("["); push(); nl();
        list_iterate_items(d, &sd->decls) {
                pp_pack_decl(d, v); emit(", "); nl();
        }
        pop();
        emit("].join");
}

static void pp_pack_decl(struct decl *d, var_t v)
{
        switch(d->type) {
        case DECL_VOID:
                emit("''");
                break;

        case DECL_OTHER:
                pp_pack_decl_internal(d->u.tother.di, field(v, d->u.tother.identifier));
                break;
        }
}

static void pp_pack_union_detail(struct union_detail *ud, var_t v)
{
        struct case_entry *ce;

        emit("["); push(); nl();
        {
                var_t discriminator = field(v, ud->discriminator->u.tother.identifier);
                pp_pack_decl(ud->discriminator, v); emit(","); nl();
                emit("case "); emit_var(discriminator); nl();
                list_iterate_items(ce, &ud->cases) {
                        emit("when "); pp_expr(ce->ce); push(); nl();
                        pp_pack_decl(ce->d, field(v, "u")); nl(); pop();
                }

                if (ud->default_case) {
                        emit("else"); push(); nl();
                        pp_pack_decl(ud->default_case, field(v, "u"));
                        pop(); nl();
                }
                emit("end"); nl();
        }
        pop();
        emit("].join");
}

/*----------------------------------------------------------------*/

/* Unpacking */
static void pp_unpack_type(struct type *t);
static void pp_unpack_enum_detail(struct enum_detail *ed);
static void pp_unpack_struct_detail(struct struct_detail *sd);
static void pp_unpack_union_detail(struct union_detail *ud);
static void pp_unpack_decl(struct decl *d);

/*----------------------------------------------------------------*/

static void unpacker(const char *t)
{
        emit("lambda {|txt| unpack_%s(txt)}", t);
}

static void pp_unpack_const_def(struct const_def *cd)
{
        /* the const definition from the pack routine will surfice */
}

static void pp_unpack_decl_internal(struct decl_internal *di)
{
        switch (di->type) {
        case DECL_SIMPLE:
                pp_unpack_type(di->u.simple.t);
                break;

        case DECL_ARRAY:
                emit("unpack_array_fn(");
                pp_expr(di->u.array.e);
                emit(", ");
                pp_unpack_type(di->u.array.t); emit(")");
                break;

        case DECL_VAR_ARRAY:
                emit("unpack_var_array_fn("); push(); nl();
                pp_unpack_type(di->u.array.t); emit(")");
                pop();
                break;

        case DECL_OPAQUE:
                emit("unpack_opaque_fn(");
                pp_expr(di->u.opaque.e);
                emit(")");
                break;

        case DECL_VAR_OPAQUE:
                emit("unpack_var_opaque_fn()");
                break;

        case DECL_STRING:
                unpacker("string");
                break;

        case DECL_POINTER:
                emit("unpack_pointer_fn("); push(); nl();
                pp_unpack_type(di->u.pointer.t);
                pop(); emit(")");
                break;
        }
}

static void pp_unpack_typedef_internal(struct typedef_internal *ti)
{
        switch (ti->type) {
        case DEF_SIMPLE:
                pp_unpack_decl_internal(ti->u.tsimple.di); nl();
                break;

        case DEF_ENUM:
                pp_unpack_enum_detail(ti->u.tenum.ed);
                break;

        case DEF_STRUCT:
                pp_unpack_struct_detail(ti->u.tstruct.sd);
                break;

        case DEF_UNION:
                pp_unpack_union_detail(ti->u.tunion.ud);
                break;
        }
}

static void pp_unpack_typedef(struct typedef_ *td)
{
        emit("def unpack_%s(txt)", td->identifier); push(); nl();
        emit("unpacker = ");
        pp_unpack_typedef_internal(td->ti); nl();
        emit("unpacker.call(txt)");
        nl(); pop(); emit("end");
}

static void pp_unpack_type(struct type *t)
{
        switch (t->type) {
        case TINT:
                unpacker("int");
                break;

        case TUINT:
                unpacker("uint");
                break;

        case THYPER:
                unpacker("hyper");
                break;

        case TUHYPER:
                unpacker("uhyper");
                break;

        case TFLOAT:
                unpacker("float");
                break;

        case TDOUBLE:
                unpacker("double");
                break;

        case TBOOL:
                unpacker("bool");
                break;

        case TENUM:
                pp_unpack_enum_detail(t->u.tenum.ed);
                break;

        case TSTRUCT:
                pp_unpack_struct_detail(t->u.tstruct.sd);
                break;

        case TUNION:
                pp_unpack_union_detail(t->u.tunion.ud);
                break;

        case TTYPEDEF:
                unpacker(t->u.ttypedef.t);
                break;
        }
}

static void pp_unpack_enum_detail(struct enum_detail *ed)
{
        struct const_def *cd;

        emit("begin"); push(); nl();
        emit("table = Array.new"); nl();
        emit("last = 0"); nl();
        list_iterate_items(cd, &ed->fields) {
                emit("table << EnumDetail.new(");
                if (cd->ce)
                        pp_expr(cd->ce);
                else
                        emit("last");
                emit(", ");
                emit(":%s)", cd->identifier); nl();

                if (cd->ce) {
                        emit("last = ");
                        pp_expr(cd->ce);
                } else
                        emit("last = last + 1");
                nl();
        }
        emit("unpack_enum_fn(table)"); nl();
        pop(); emit("end");
}

static void pp_unpack_struct_detail(struct struct_detail *sd)
{
        struct decl *d;

        emit("unpack_struct_fn("); push(); nl();
        list_iterate_items(d, &sd->decls) {
                emit("FieldDetail.new("); push(); nl();
                pp_unpack_decl(d); emit(","); nl();
                emit(":%s)", d->u.tother.identifier);
                if (d->list.n != &sd->decls)
                        emit(",");
                pop(); nl();
        }
        pop();
        emit(")");
}

static void pp_unpack_decl(struct decl *d)
{
        switch(d->type) {
        case DECL_VOID:
                emit("''");
                break;

        case DECL_OTHER:
                pp_unpack_decl_internal(d->u.tother.di);
                break;
        }
}

static void pp_unpack_union_detail(struct union_detail *ud)
{
        struct case_entry *ce;
        emit("unpack_union_fn("); push(); nl();

        /* Discriminator */
        emit("FieldDetail.new("); push(); nl();
        pp_unpack_decl(ud->discriminator); emit(","); nl();
        emit(":%s),", ud->discriminator->u.tother.identifier); pop(); nl();

        /* Cases */
        emit("["); push(); nl();
        list_iterate_items(ce, &ud->cases) {
                emit("CaseDetail.new("); push(); nl();
                pp_expr(ce->ce); emit(","); nl();
                pp_unpack_decl(ce->d); emit(","); nl();

                if (ce->d->type == DECL_VOID)
                        emit("unpack_void_fn()");
                else
                        emit(":%s", ce->d->u.tother.identifier);
                emit(")");

                if (ce->list.n != &ud->cases)
                        emit(",");

                pop(); nl();
        }
        pop(); emit("],"); nl();

        /* Default */
        if (ud->default_case) {
                emit(","); nl();
                emit("FieldDetail("); push(); nl();
                pp_unpack_decl(ud->default_case); emit(","); nl();
                if (ud->default_case->type == DECL_VOID)
                        emit("unpack_void_fn()");
                else
                        emit(":%s", ud->default_case->u.tother.identifier);
                pop(); nl();
        } else
                emit("nil");

        pop(); emit(")");
}


void print_ruby(struct specification *spec)
{
        struct definition *def;

        emit("require 'xdr'"); nl();
        emit("require 'xdr_utils'"); nl(); nl();
        emit("include XDR"); nl();
        emit("include XDRUtils"); nl(); nl();

        list_iterate_items(def, &spec->definitions) {
                switch (def->type) {
                case DEF_TYPEDEF:
                        pp_pack_typedef(def->u.ttypedef.td); nl(); nl();
                        pp_unpack_typedef(def->u.ttypedef.td); nl(); nl();
                        break;

                case DEF_CONSTANT:
                        pp_pack_const_def(def->u.tconst.cd); nl(); nl();
                        pp_unpack_const_def(def->u.tconst.cd); nl(); nl();
                }
                nl();
        }
}

/*----------------------------------------------------------------*/
