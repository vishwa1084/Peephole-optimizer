%option noyywrap yylineno nounput noinput never-interactive
%{
#include "grammar.tab.h"
#include "c_ast.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define YY_USER_ACTION do { yylloc.first_line = yylloc.last_line = yylineno; yylloc.first_column = 1; yylloc.last_column = (int)yyleng; } while (0);
static char *lex_copy(const char *text) { size_t n=strlen(text)+1; char *p=malloc(n); if(p) memcpy(p,text,n); return p; }
%}
ID [A-Za-z_][A-Za-z0-9_]*
%%
[ \t\r\n]+                 ;
"//"[^\n]*                 ;
"/*"([^*]|\*+[^*/])*"*/"  ;
"int"                       return INT;
"return"                    return RETURN;
"if"                        return IF;
"else"                      return ELSE;
"for"                       return FOR;
"while"                     return WHILE;
"do"                        return DO;
"=="                        return EQ;
"!="                        return NE;
"<="                        return LE;
">="                        return GE;
"switch"|"case"|"break"|"continue"|"void"|"char"|"long"|"short"|"float"|"double"|"struct"|"typedef"|"const"|"unsigned"|"signed" {
    char message[96]; snprintf(message, sizeof message, "unsupported C construct '%s'", yytext);
    c_set_parse_error((size_t)yylineno, message); return UNSUPPORTED;
}
{ID}                        { if (yyleng >= MAX_NAME) { c_set_parse_error((size_t)yylineno,"identifier exceeds target name limit"); return UNSUPPORTED; } yylval.text = lex_copy(yytext); if (!yylval.text) { c_set_parse_error((size_t)yylineno, "out of memory"); return UNSUPPORTED; } return IDENTIFIER; }
0[xX][0-9A-Fa-f]+|[0-9]+   { errno=0; char *end=NULL; long long v=strtoll(yytext,&end,0); if(errno==ERANGE||!end||*end){c_set_parse_error((size_t)yylineno,"integer literal is outside signed 64-bit range");return UNSUPPORTED;} yylval.integer=(int64_t)v; return INTEGER; }
"&&"|"||"                 { c_set_parse_error((size_t)yylineno,"logical operators are not supported"); return UNSUPPORTED; }
[+*/(){};,=<>-]            { return (unsigned char)yytext[0]; }
.                           { char message[96]; snprintf(message,sizeof message,"unsupported character '%s'",yytext); c_set_parse_error((size_t)yylineno,message); return UNSUPPORTED; }
%%
