%{
#include "ast.h"
#include "xdrgen.h"

#include <stdio.h>

static void yyerror(char *s) {
        fprintf(stderr, "<stdin>:%u %s\n", get_line(), s);
        exit(1);
}

struct type *simple_type(enum type_type type) {
       struct type *t = zalloc(sizeof(*t));
       t->type = type;
       return t;
}

/* FIXME: rename */
static void list_move_(struct list *new_head, struct list *old_head)
{
        list_init(new_head);
        list_splice(new_head, old_head);
}

static void add_tail(struct list *head, struct list *item)
{
        list_add(head, item);
}
%}

%debug

%union {
       char *str;
       int i;
       struct list list;

       struct specification *spec;
       struct definition *def;
       struct typedef_ *tdef;
       struct const_def *cdef;
       struct decl *decl;
       struct union_detail *udetail;

       struct struct_detail *sdetail;
       struct enum_detail *edetail;
       struct case_entry *centry;
       struct type *ty;
       struct const_expr *cexpr;
};

%token BOOL CASE CONST DEFAULT DOUBLE QUADRUPLE ENUM FLOAT
%token HYPER INT OPAQUE STRING STRUCT SWITCH TYPEDEF UNION
%token UNSIGNED VOID

%token <i> INTEGER
%token <str> IDENTIFIER

%type <spec> specification
%type <list> definition_list
%type <def> definition
%type <tdef> typedef
%type <cdef> const_def
%type <decl> declaration
%type <udetail> union_detail
%type <sdetail> struct_detail
%type <edetail> enum_detail
%type <list> case_list
%type <centry> case
%type <decl> default_
%type <list> declaration_list
%type <list> enum_fields
%type <cdef> enum_field
%type <ty> type
%type <str> identifier
%type <cexpr> const_expr

%start specification

%%

specification : definition_list {
                $$ = zalloc(sizeof(*$$));
                list_move_(&$$->definitions, &$1);
                set_result($$);
              }
              ;


definition_list : /* empty */ {
                  list_init(&$$);
                }
                | definition_list definition {
                  list_move_(&$$, &$1);
                  add_tail(&$$, &$2->list);
                }
                ;

definition : typedef {
             $$ = zalloc(sizeof(*$$));
             $$->type = DEF_TYPEDEF;
             $$->u.ttypedef.td = $1;
           }
           | const_def {
             $$ = zalloc(sizeof(*$$));
             $$->type = DEF_CONSTANT;
             $$->u.tconst.cd = $1;
           }
           ;

typedef : TYPEDEF declaration ';' {
          if ($2->type != DECL_OTHER)
          	yyerror("you cannot use 'void' in this context");

          $$ = zalloc(sizeof(*$$));
          $$->identifier = $2->u.tother.identifier;
          $$->ti = zalloc(sizeof(*$$->ti));
          $$->ti->type = DEF_SIMPLE;
          $$->ti->u.tsimple.di = $2->u.tother.di;
        }
        | ENUM identifier enum_detail ';' {
          $$ = zalloc(sizeof(*$$));
          $$->identifier = $2;
          $$->ti = zalloc(sizeof(*$$->ti));
          $$->ti->type = DEF_ENUM;
          $$->ti->u.tenum.ed = $3;
        }
        | STRUCT identifier struct_detail ';' {
          $$ = zalloc(sizeof(*$$));
          $$->identifier = $2;
          $$->ti = zalloc(sizeof(*$$->ti));
          $$->ti->type = DEF_STRUCT;
          $$->ti->u.tstruct.sd = $3;
        }
        | UNION identifier union_detail ';' {
          $$ = zalloc(sizeof(*$$));
          $$->identifier = $2;
          $$->ti = zalloc(sizeof(*$$->ti));
          $$->ti->type = DEF_UNION;
          $$->ti->u.tunion.ud = $3;
        }
        ;

const_def : CONST identifier '=' const_expr ';' {
            $$ = zalloc(sizeof(*$$));
            list_init(&$$->list);
            $$->identifier = $2;
            $$->ce = $4;
          }
          ;

declaration : VOID {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_VOID;
            }
            | STRING identifier '<' '>' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_STRING;
            }
            | STRING identifier '<' const_expr '>' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_STRING;
              $$->u.tother.di->u.string.e = $4;
            }
            | OPAQUE identifier '[' const_expr ']' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_OPAQUE;
              $$->u.tother.di->u.opaque.e = $4;
            }
            | OPAQUE identifier '<' const_expr '>' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_VAR_OPAQUE;
              $$->u.tother.di->u.var_opaque.e = $4;
            }
            | OPAQUE identifier '<' '>' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_VAR_OPAQUE;
            }
            | type '*' identifier {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $3;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_POINTER;
              $$->u.tother.di->u.pointer.t = $1;
            }
            | type identifier '[' const_expr ']' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_ARRAY;
              $$->u.tother.di->u.array.t = $1;
              $$->u.tother.di->u.array.e = $4;
            }
            | type identifier '<' '>' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_VAR_ARRAY;
              $$->u.tother.di->u.var_array.t = $1;
            }
            | type identifier '<' const_expr '>' {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_VAR_ARRAY;
              $$->u.tother.di->u.var_array.t = $1;
              $$->u.tother.di->u.var_array.e = $4;
            }
            | type identifier {
              $$ = zalloc(sizeof(*$$));
              list_init(&$$->list);
              $$->type = DECL_OTHER;
              $$->u.tother.identifier = $2;
              $$->u.tother.di = zalloc(sizeof(struct decl_internal));
              $$->u.tother.di->type = DECL_SIMPLE;
              $$->u.tother.di->u.simple.t = $1;
            }
            ;

union_detail : SWITCH '(' declaration ')' '{' case_list default_ '}' {
               $$ = zalloc(sizeof(*$$));
               $$->discriminator = $3;
               list_move_(&$$->cases, &$6);
               $$->default_case = $7;
             }
             | SWITCH '(' declaration ')' '{' case_list '}' {
               $$ = zalloc(sizeof(*$$));
               $$->discriminator = $3;
               list_move_(&$$->cases, &$6);
             }
             ;

case_list : case {
            list_init(&$$);
            add_tail(&$$, &$1->list);
          }
          | case_list case {
            list_move_(&$$, &$1);
            add_tail(&$$, &$2->list);
          }
          ;

case : CASE const_expr ':' declaration ';' {
       $$ = zalloc(sizeof(*$$));
       list_init(&$$->list);
       $$->ce = $2;
       $$->d = $4;
     }
     ;

default_ : DEFAULT ':' declaration ';' {
           $$ = $3;
         }
         ;

struct_detail : '{' declaration_list '}' {
                $$ = zalloc(sizeof(*$$));
                list_move_(&$$->decls, &$2);
              }
              ;

declaration_list : declaration ';' {
                   list_init(&$$);
                   add_tail(&$$, &$1->list);
                 }
                 | declaration_list declaration ';' {
                   list_move_(&$$, &$1);
                   add_tail(&$$, &$2->list);
                 }
                 ;

enum_detail : '{' enum_fields '}' {
              $$ = zalloc(sizeof(*$$));
              list_move_(&$$->fields, &$2);
            }
            ;

enum_fields : enum_field {
              list_init(&$$);
              add_tail(&$$, &$1->list);
            }
            | enum_fields ',' enum_field {
              list_move_(&$$, &$1);
              add_tail(&$$, &$3->list);
            }
            ;

enum_field : identifier {
             $$ = zalloc(sizeof(*$$));
             list_init(&$$->list);
             $$->identifier = $1;
           }
           | identifier '=' const_expr {
             $$ = zalloc(sizeof(*$$));
             list_init(&$$->list);
             $$->identifier = $1;
             $$->ce = $3;
           }
           ;

type :
	UNSIGNED INT   { $$ = simple_type(TUINT); }
      | UNSIGNED HYPER { $$ = simple_type(TUHYPER); }
      | INT            { $$ = simple_type(TINT); }
      | HYPER          { $$ = simple_type(THYPER); }
      | FLOAT          { $$ = simple_type(TFLOAT); }
      | DOUBLE         { $$ = simple_type(TDOUBLE); }
      | BOOL           { $$ = simple_type(TBOOL); }

      | ENUM enum_detail
      {
              $$ = zalloc(sizeof(*$$));
              $$->type = TENUM;
              $$->u.tenum.ed = $2;
      }

      | STRUCT struct_detail
      {
              $$ = zalloc(sizeof(*$$));
              $$->type = TSTRUCT;
              $$->u.tstruct.sd = $2;
      }

      | UNION union_detail
      {
              $$ = zalloc(sizeof(*$$));
              $$->type = TUNION;
              $$->u.tunion.ud = $2;
      }
      | identifier
      {
              $$ = zalloc(sizeof(*$$));
              $$->type = TTYPEDEF;
              $$->u.ttypedef.t = $1;
      }
      ;

const_expr :
	INTEGER
        {
                $$ = zalloc(sizeof(*$$));
                $$->type = CE_LITERAL;
                $$->u.literal = $1;
        }
        | identifier
        {
                $$ = zalloc(sizeof(*$$));
                $$->type = CE_REFERENCE;
                $$->u.reference = $1;
        }
	;

identifier :
	IDENTIFIER
        ;
%%
