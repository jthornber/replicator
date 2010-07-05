#ifndef XDRGEN_AST_H
#define XDRGEN_AST_H

#include "datastruct/list.h"

/*----------------------------------------------------------------*/

/*
 * This is a direct encoding of:
 * http://github.com/jthornber/xdrgen/blob/master/Data/XDR/AST.hs
 */

enum const_expr_type {
        CE_LITERAL,
        CE_REFERENCE
};

struct const_expr {
        enum const_expr_type type;
        union {
                int literal;
                char *reference;
        } u;
};

enum decl_internal_type {
        DECL_SIMPLE,
        DECL_ARRAY,
        DECL_VAR_ARRAY,
        DECL_OPAQUE,
        DECL_VAR_OPAQUE,
        DECL_STRING,
        DECL_POINTER
};

struct decl_internal {
        enum decl_internal_type type;
        union {
                struct {
                        struct type *t;
                } simple;

                struct {
                        struct type *t;
                        struct const_expr *e;
                } array;

                struct {
                        struct type *t;
                        struct const_expr *e;
                } var_array;

                struct {
                        struct const_expr *e;
                } opaque;

                struct {
                        struct const_expr *e;
                } var_opaque;

                struct {
                        struct const_expr *e;
                } string;

                struct {
                        struct type *t;
                } pointer;
        } u;
};

enum decl_type {
        DECL_VOID,
        DECL_OTHER
};

struct decl {
        struct list list;
        enum decl_type type;
        union {
                struct {
                        char *identifier;
                        struct decl_internal *di;
                } tother;
        } u;
};

enum type_type {
        TINT,
        TUINT,
        THYPER,
        TUHYPER,
        TFLOAT,
        TDOUBLE,
        TBOOL,
        TENUM,
        TSTRUCT,
        TUNION,
        TTYPEDEF
};

struct type {
        enum type_type type;
        union {
                struct {
                        struct enum_detail *ed;
                } tenum;

                struct {
                        struct struct_detail *sd;
                } tstruct;

                struct {
                        struct union_detail *ud;
                } tunion;

                struct {
                        char *t;
                } ttypedef;
        } u;
};

struct const_def {
        struct list list;
        char *identifier;
        struct const_expr *ce;
};

struct enum_detail {
        struct list fields;
};

struct struct_detail {
        struct list decls;
};

struct case_entry {
        struct list list;
        struct const_expr *ce;
        struct decl *d;
};

struct union_detail {
        struct decl *discriminator;
        struct list cases;
        struct decl *default_case;
};

enum typedef_internal_type {
        DEF_SIMPLE,
        DEF_ENUM,
        DEF_STRUCT,
        DEF_UNION
};

struct typedef_internal {
        enum typedef_internal_type type;
        union {
                struct {
                        struct decl_internal *di;
                } tsimple;

                struct {
                        struct enum_detail *ed;
                } tenum;

                struct {
                        struct struct_detail *sd;
                } tstruct;

                struct {
                        struct union_detail *ud;
                } tunion;
        } u;
};

struct typedef_ {
        char *identifier;
        struct typedef_internal *ti;
};

enum definition_type {
        DEF_TYPEDEF,
        DEF_CONSTANT
};

struct definition {
        struct list list;
        enum definition_type type;
        union {
                struct {
                        struct typedef_ *td;
                } ttypedef;

                struct {
                        struct const_def *cd;
                } tconst;
        } u;
};

struct specification {
        struct list definitions;
};

/*----------------------------------------------------------------*/

#endif
