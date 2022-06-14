%option noyywrap
%option nounput
%option noinput

%{

#include <cstdlib>
#include <string>
#include "AST.hpp"

// 因为 Flex 会用到 Bison 中关于 token 的定义
// 所以需要 include Bison 生成的头文件
#include "sysy.tab.hpp"

using namespace std;

%}

/* 空白符和注释 */
WhiteSpace    [ \t\n\r]*
LineComment   "//".*$
CrossLineComment "/*"([^\*]|(\*+[^\*\/]))*\*\/

/* 标识符 */
Identifier    [a-zA-Z_][a-zA-Z0-9_]*

/* 整数字面量 */
Decimal       [1-9][0-9]*
Octal         0[0-7]*
Hexadecimal   0[xX][0-9a-fA-F]+

/* 操作符 */
LeqConst      "<="
GeqConst      ">="
EqConst       "=="
NeqConst      "!="
AndConst      "&&"
OrConst       "||"

%%

{WhiteSpace}    { /* 忽略, 不做任何操作 */ }
{LineComment}   { /* 忽略, 不做任何操作 */ }
{CrossLineComment}   { /* 忽略, 不做任何操作 */ }

"int"           { return INT; }
"return"        { return RETURN; }

{Identifier}    { yylval.str_val = new string(yytext); return IDENT; }

{Decimal}       { yylval.int_val = strtol(yytext, nullptr, 0); return INT_CONST; }
{Octal}         { yylval.int_val = strtol(yytext, nullptr, 0); return INT_CONST; }
{Hexadecimal}   { yylval.int_val = strtol(yytext, nullptr, 0); return INT_CONST; }

.               { return yytext[0]; }

{AndConst}      { return AND_CONST; }
{OrConst}      { return OR_CONST; }
{LeqConst}      { return LEQ_CONST; }
{EqConst}      { return EQ_CONST; }
{NeqConst}      { return NEQ_CONST; }
{GeqConst}      { return GEQ_CONST; }

{}

%%