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
struct var_frame {
        struct dm_list list;

        const char *name;
        const char *subscript;
        int is_pointer;
};

typedef struct dm_list *var_t;

var_t new_var()
{
        var_t v = zalloc(sizeof(*v));
        dm_list_init(v);
        return v;
}

void push_frame(var_t v, const char *name, int is_pointer)
{
        struct var_frame *f = zalloc(sizeof(*f));
        f->name = dup_string(name);
        f->subscript = NULL;
        f->is_pointer = is_pointer;
        dm_list_add(v, &f->list);
}

void subscript_frame(var_t v, const char *subscript)
{
        struct var_frame *f = dm_list_item(dm_list_last(v), struct var_frame);
        f->subscript = dup_string(subscript);
}

void pop_frame(var_t v)
{
        dm_list_del(dm_list_last(v));
}

void emit_var(var_t v)
{
        int first = 1;
        int last_was_pointer = 0;
        struct var_frame *f;

        dm_list_iterate_items(f, v) {
                if (!first)
                        emit(last_was_pointer ? "->" : ".");
                else
                        first = 0;

                emit(f->name);
                if (f->subscript)
                        emit("[%s]", f->subscript);

                last_was_pointer = f->is_pointer;
        }
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

static void pack_const_def(struct const_def *cd)
{
        emit("%s", cd->identifier);
        if (cd->ce) {
                emit(" = ");
                pp_expr(cd->ce);
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
                emit("unsigned int i"); nl();
                emit("for (i = 0; i < ");
                pp_expr(di->u.array.e);
                emit("; i++) {"); push(); nl();
                subscript_frame(v, "i");
                pack_type(di->u.array.t, v);
                pop(); nl(); emit("}");
                pop(); nl(); emit("}");
                break;

        case DECL_VAR_ARRAY:
                emit("{");
                push(); nl();
                pack_type(di->u.var_array.t, v);
                emit(" *array;"); nl();
                emit("size_t len;"); nl();
                pop();
                emit("}");
                break;

        case DECL_OPAQUE:
        case DECL_VAR_OPAQUE:
                emit("struct {");
                push(); nl();
                emit("void *data;"); nl();
                emit("size_t len;"); nl();
                pop();
                emit("}");
                break;

        case DECL_STRING:
                emit("const char *");
                break;

        case DECL_POINTER:
                pack_type(di->u.pointer.t, v);
                emit(" *");
                break;
        }
}

static void pack_typedef_internal(struct typedef_internal *ti)
{
        var_t v = new_var();
        push_frame(v, "input", 1);
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
                pack(t->u.ttypedef.t, v);
                break;
        }
}

static void pack_enum_detail(struct enum_detail *ed, var_t v)
{
        pack("uint", v);
}

static void pack_struct_detail(struct struct_detail *sd, var_t v)
{
        struct decl *d;

        dm_list_iterate_items(d, &sd->decls) {
                pack_decl(d, v); nl();
        }
}

static void pack_decl(struct decl *d, var_t v)
{
        switch(d->type) {
        case DECL_VOID:
                break;

        case DECL_OTHER:
                push_frame(v, d->u.tother.identifier, 0);
                pack_decl_internal(d->u.tother.di, v);
                pop_frame(v);
                break;
        }
}

static void pack_union_detail(struct union_detail *ud, var_t v)
{
        struct case_entry *ce;

        emit("switch (input->");
        /* FIXME: emit decl name */
        emit(") {"); nl();
        push();
        {
                dm_list_iterate_items(ce, &ud->cases) {
                        emit("case ");
                        pp_expr(ce->ce);
                        emit(": {"); push(); nl();
                        pack_decl(ce->d, v); nl();
                        emit("break;"); nl();
                        pop();
                        emit("}");
                        nl(); nl();
                }

                if (ud->default_case) {
                        emit("default: {"); nl();
                        push();
                        pack_decl(ud->default_case, v);
                        emit("break;"); nl();
                        pop();
                        emit("}"); nl();
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

        emit("int xdr_unpack_%s(struct xdr_cursor *c, struct dm_pool *mem, %s **output)",
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

        dm_list_iterate_items(def, &spec->definitions) {
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
        emit("#include \"xdr.h\""); nl(); nl();
        sep();
        decls_(spec);
        sep();
        emit("#endif\n");
}

/*----------------------------------------------------------------*/
