%{
#include "xdrgen.h"
#include "xdrgen.tab.h"
%}

LETTER	[A-Za-z]
ULETTER [A-Za-z_]

%x COMMENT
%option nounput noinput

%%

"/*"				BEGIN(COMMENT);
<COMMENT>[^*\n]*
<COMMENT>"*"+[^*/\n]*
<COMMENT>\n			inc_line();
<COMMENT>"*/"		   	BEGIN(INITIAL);

[ \t]+				/* ignore whitespace */
\n				inc_line();

[1-9][0-9]*		{ yylval.i = strtol(yytext, NULL, 10); return INTEGER; }
-[1-9][0-9]*		{ yylval.i = -strtol(yytext + 1, NULL, 10); return INTEGER; }
0x[0-9A-Fa-f]+		{ yylval.i = strtol(yytext + 2, NULL, 16); return INTEGER; }
0[0-9]*			{ yylval.i = strtol(yytext + 1, NULL, 8); return INTEGER; }

bool		return BOOL;
case		return CASE;
const		return CONST;
default 	return DEFAULT;
double  	return DOUBLE;
quadruple	return QUADRUPLE;
enum		return ENUM;
float		return FLOAT;
hyper		return HYPER;
int		return INT;
opaque		return OPAQUE;
string		return STRING;
struct		return STRUCT;
switch		return SWITCH;
typedef 	return TYPEDEF;
union		return UNION;
unsigned	return UNSIGNED;
void		return VOID;

[A-Za-z][A-Za-z0-9_]*	{ yylval.str = dup_string(yytext); return IDENTIFIER; }
.		return *yytext;

%%

int yywrap()
{
   return 1;
}

