#include "pretty_print.h"
#include "emit.h"

/*----------------------------------------------------------------*/

static void pp_type(struct type *t);
static void pp_enum_detail(struct enum_detail *ed);
static void pp_struct_detail(struct struct_detail *sd);
static void pp_union_detail(struct union_detail *ud);
static void pp_decl(struct decl *d);

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
        emit("%s", cd->identifier);
        if (cd->ce) {
                emit(" = ");
                pp_expr(cd->ce);
        }
}

static void pp_decl_internal(struct decl_internal *di)
{
        switch (di->type) {
        case DECL_SIMPLE:
                pp_type(di->u.simple.t);
                break;

        case DECL_ARRAY:
                pp_type(di->u.array.t);
                emit("[");
                pp_expr(di->u.array.e);
                emit("]");
                break;

        case DECL_VAR_ARRAY:
                pp_type(di->u.var_array.t);
                emit("<");
                if (di->u.var_array.e)
                        pp_expr(di->u.var_array.e);
                emit(">");
                break;

        case DECL_OPAQUE:
                emit("opaque[");
                pp_expr(di->u.opaque.e);
                emit("]");
                break;

        case DECL_VAR_OPAQUE:
                emit("opaque<");
                if (di->u.var_opaque.e)
                        pp_expr(di->u.var_opaque.e);
                emit(">");
                break;

        case DECL_STRING:
                emit("string<");
                if (di->u.string.e)
                        pp_expr(di->u.string.e);
                emit(">");
                break;

        case DECL_POINTER:
                pp_type(di->u.pointer.t);
                emit(" *");
                break;
        }
}

static void pp_typedef_internal(struct typedef_internal *ti)
{
        switch (ti->type) {
        case DEF_SIMPLE:
                pp_decl_internal(ti->u.tsimple.di);
                break;

        case DEF_ENUM:
                pp_enum_detail(ti->u.tenum.ed);
                break;

        case DEF_STRUCT:
                pp_struct_detail(ti->u.tstruct.sd);
                break;

        case DEF_UNION:
                pp_union_detail(ti->u.tunion.ud);
                break;
        }
}

static void pp_typedef(struct typedef_ *td)
{
        emit("typedef '%s'", td->identifier); push(); nl();
        pp_typedef_internal(td->ti);
        pop(); nl();
}

static void pp_type(struct type *t)
{
        switch (t->type) {
        case TINT:
                emit("int");
                break;

        case TUINT:
                emit("unsigned int");
                break;

        case THYPER:
                emit("hyper");
                break;

        case TUHYPER:
                emit("unsigned hyper");
                break;

        case TFLOAT:
                emit("float");
                break;

        case TDOUBLE:
                emit("double");
                break;

        case TBOOL:
                emit("bool");
                break;

        case TENUM:
                pp_enum_detail(t->u.tenum.ed);
                break;

        case TSTRUCT:
                pp_struct_detail(t->u.tstruct.sd);
                break;

        case TUNION:
                pp_union_detail(t->u.tunion.ud);
                break;

        case TTYPEDEF:
                emit("%s", t->u.ttypedef.t);
                break;
        }
}

static void pp_enum_detail(struct enum_detail *ed)
{
        struct const_def *cd;
        emit("enum"); push(); nl();
        list_iterate_items(cd, &ed->fields) {
                pp_const_def(cd);
                nl();
        }
        pop(); nl();
}

static void pp_struct_detail(struct struct_detail *sd)
{
        struct decl *d;

        emit("struct"); push(); nl();
        list_iterate_items(d, &sd->decls) {
                pp_decl(d);
                nl();
        }
        pop(); nl();
}

static void pp_decl(struct decl *d)
{
        switch(d->type) {
        case DECL_VOID:
                emit("void");
                break;

        case DECL_OTHER:
                pp_decl_internal(d->u.tother.di);
                emit(" %s", d->u.tother.identifier);
                break;
        }
}

static void pp_union_detail(struct union_detail *ud)
{
        struct case_entry *ce;

        emit("union switch(");
        pp_decl(ud->discriminator);
        emit(") {"); push(); nl();
        list_iterate_items(ce, &ud->cases) {
                emit("case ");
                pp_expr(ce->ce);
                emit(": ");
                push(); nl();
                pp_decl(ce->d);
                pop(); nl(); nl();
        }

        if (ud->default_case) {
                emit("default:");
                push(); nl();
                pp_decl(ud->default_case);
                pop(); nl();
        }
}

void pretty_print(struct specification *spec)
{
        struct definition *def;

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
