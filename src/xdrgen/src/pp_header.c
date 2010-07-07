#include "pretty_print.h"
#include "emit.h"

#include <stdlib.h>

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

static void pp_decl_internal(struct decl_internal *di, const char *identifier)
{
        switch (di->type) {
        case DECL_SIMPLE:
                pp_type(di->u.simple.t);
                emit(" %s", identifier);
                break;

        case DECL_ARRAY:
                pp_type(di->u.array.t);
                emit(" %s[", identifier);
                pp_expr(di->u.array.e);
                emit("]");
                break;

        case DECL_VAR_ARRAY:
                emit("struct {");
                push(); nl();
                pp_type(di->u.var_array.t);
                emit(" *array;"); nl();
                emit("size_t len;"); nl();
                pop();
                emit("} %s", identifier);
                break;

        case DECL_OPAQUE:
                emit("uint8_t %s[", identifier);
                pp_expr(di->u.opaque.e);
                emit("]");
                break;

        case DECL_VAR_OPAQUE:
                emit("struct {");
                push(); nl();
                emit("uint8_t *data;"); nl();
                emit("size_t len;"); nl();
                pop();
                emit("} %s", identifier);
                break;

        case DECL_STRING:
                emit("char *%s", identifier);
                break;

        case DECL_POINTER:
                pp_type(di->u.pointer.t);
                emit(" *%s", identifier);
                break;
        }
}

static void pp_typedef_internal(struct typedef_internal *ti, const char *identifier)
{
        switch (ti->type) {
        case DEF_SIMPLE:
                pp_decl_internal(ti->u.tsimple.di, identifier);
                break;

        case DEF_ENUM:
                pp_enum_detail(ti->u.tenum.ed);
                emit(" %s", identifier);
                break;

        case DEF_STRUCT:
                pp_struct_detail(ti->u.tstruct.sd);
                emit(" %s", identifier);
                break;

        case DEF_UNION:
                pp_union_detail(ti->u.tunion.ud);
                emit(" %s", identifier);
                break;
        }
}

static void pp_typedef(struct typedef_ *td)
{
        emit("typedef ");
        pp_typedef_internal(td->ti, td->identifier);
        emit(";"); nl();
}

static void pp_type(struct type *t)
{
        switch (t->type) {
        case TINT:
                emit("int32_t");
                break;

        case TUINT:
                emit("uint32_t");
                break;

        case THYPER:
                emit("int64_t");
                break;

        case TUHYPER:
                emit("uint64_t");
                break;

        case TFLOAT:
                emit("float");
                break;

        case TDOUBLE:
                emit("double");
                break;

        case TBOOL:
                emit("int");
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
        emit("enum {"); push(); nl();
        list_iterate_items(cd, &ed->fields) {
                pp_const_def(cd); emit(",");
                nl();
        }
        pop();
        emit("}");
}

static void pp_struct_detail(struct struct_detail *sd)
{
        struct decl *d;

        emit("struct {"); push(); nl();
        list_iterate_items(d, &sd->decls) {
                pp_decl(d); nl();
        }
        pop();
        emit("}");
}

static void pp_decl(struct decl *d)
{
        switch(d->type) {
        case DECL_VOID:
                break;

        case DECL_OTHER:
                pp_decl_internal(d->u.tother.di, d->u.tother.identifier);
                emit(";");
                break;
        }
}

static void pp_union_detail(struct union_detail *ud)
{
        struct case_entry *ce;

        emit("struct {");
        push(); nl();
        {
                pp_decl(ud->discriminator); nl();
                emit("union {");
                push(); nl();
                {
                        list_iterate_items(ce, &ud->cases) {
                                pp_decl(ce->d); nl();
                        }

                        if (ud->default_case) {
                                pp_decl(ud->default_case);
                        }
                }
                pop(); nl();
                emit("} u;"); nl();
        }
        pop();
        emit("}");
}

static void sep()
{
        emit("/*----------------------------------------------------------------*/\n\n");
}

static void datatypes_(struct specification *spec)
{
        struct definition *def;

        list_iterate_items(def, &spec->definitions) {
                switch (def->type) {
                case DEF_TYPEDEF:
                        pp_typedef(def->u.ttypedef.td);
                        break;

                case DEF_CONSTANT:
                        emit("enum { ");
                        pp_const_def(def->u.tconst.cd);
                        emit(" };");
                }
                nl();
        }
}

static void decl_(struct typedef_ *td)
{
        emit("int xdr_pack_%s(struct xdr_buffer *buf, %s *input);",
             td->identifier, td->identifier);
        nl();
        emit("int xdr_unpack_%s(struct xdr_cursor *c, struct pool *mem, %s **output);",
             td->identifier, td->identifier);
        nl();
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

void print_header(struct specification *spec)
{
        long n = random();

        emit("#ifndef XDR_OUTPUT_%ld_H\n#define XDR_OUTPUT_%ld_H", n, n); nl(); nl();
        emit("#include \"xdr/xdr.h\""); nl();
        emit("#include \"mm/pool.h\""); nl(); nl();
        sep();
        datatypes_(spec);
        sep();
        decls_(spec);
        sep();
        emit("#endif\n");
}

/*----------------------------------------------------------------*/
