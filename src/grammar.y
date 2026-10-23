%{
#include "c_ast.h"
#include <stdlib.h>
int yylex(void);
void yyerror(const char *message);
CFunction *c_parse_result;
%}
%code requires {
#include <stdint.h>
#include "c_ast.h"
}

%define parse.error detailed
%locations
%union { int64_t integer; char *text; CExpr *expr; CDecl *decl; CStmt *stmt; }
%token INT RETURN IF ELSE FOR WHILE DO EQ NE LE GE UNSUPPORTED
%token <text> IDENTIFIER
%token <integer> INTEGER
%type <expr> expression additive multiplicative unary primary optional_initializer condition optional_condition
%type <stmt> statement statements block for_initializer for_update
%type <decl> declarators declarator
%destructor { free($$); } <text>
%destructor { c_expr_destroy($$); } <expr>
%destructor { c_decl_destroy($$); } <decl>
%destructor { c_stmt_destroy($$); } <stmt>
%start translation_unit
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%
translation_unit:
    INT IDENTIFIER '(' ')' block {
        if ($2 && strcmp($2, "main") != 0) {
            c_set_parse_error((size_t)@2.first_line, "only a function named main is supported");
            free($2); c_stmt_destroy($5); YYABORT;
        }
        c_parse_result = malloc(sizeof(*c_parse_result));
        if (!c_parse_result) { c_set_parse_error((size_t)@1.first_line, "out of memory"); free($2); c_stmt_destroy($5); YYABORT; }
        c_parse_result->body = $5; c_parse_result->line = (size_t)@1.first_line;
        free($2);
    }
    | UNSUPPORTED { YYABORT; }
    ;

block: '{' statements '}' { $$ = c_stmt_block($2, (size_t)@1.first_line); }
    | '{' '}' { $$ = c_stmt_block(NULL, (size_t)@1.first_line); }
    ;
statements:
      statement { $$ = $1; }
    | statements statement { $$ = $1; c_stmt_append(&$$, $2); }
    ;
statement:
      INT declarators ';' { $$ = c_stmt_decl($2, (size_t)@1.first_line); }
    | IDENTIFIER '=' expression ';' { $$ = c_stmt_assign($1, $3, (size_t)@1.first_line); free($1); }
    | RETURN expression ';' { $$ = c_stmt_return($2, (size_t)@1.first_line); }
    | IF '(' condition ')' statement %prec LOWER_THAN_ELSE { $$ = c_stmt_if($3, $5, NULL, (size_t)@1.first_line); }
    | IF '(' condition ')' statement ELSE statement { $$ = c_stmt_if($3, $5, $7, (size_t)@1.first_line); }
    | WHILE '(' condition ')' statement { $$ = c_stmt_while($3, $5, (size_t)@1.first_line); }
    | DO statement WHILE '(' condition ')' ';' { $$ = c_stmt_do_while($2, $5, (size_t)@1.first_line); }
    | FOR '(' for_initializer ';' optional_condition ';' for_update ')' statement
        { $$ = c_stmt_for($3, $5, $7, $9, (size_t)@1.first_line); }
    | block { $$ = $1; }
    ;
for_initializer:
      %empty { $$ = NULL; }
    | INT declarators { $$ = c_stmt_decl($2, (size_t)@1.first_line); }
    | IDENTIFIER '=' expression { $$ = c_stmt_assign($1, $3, (size_t)@1.first_line); free($1); }
    ;
for_update:
      %empty { $$ = NULL; }
    | IDENTIFIER '=' expression { $$ = c_stmt_assign($1, $3, (size_t)@1.first_line); free($1); }
    ;
optional_condition:
      %empty { $$ = NULL; }
    | condition { $$ = $1; }
    ;
declarators:
      declarator { $$ = $1; }
    | declarators ',' declarator { $$ = $1; c_decl_append(&$$, $3); }
    ;
declarator:
      IDENTIFIER optional_initializer { $$ = c_decl($1, $2, (size_t)@1.first_line); free($1); }
    ;
optional_initializer:
      %empty { $$ = NULL; }
    | '=' expression { $$ = $2; }
    ;
expression: additive { $$ = $1; }
    ;
condition:
      expression { $$=c_expr_binary(C_COND_NONZERO,$1,NULL,(size_t)@1.first_line); }
    | expression '<' expression { $$=c_expr_binary('<',$1,$3,(size_t)@2.first_line); }
    | expression '>' expression { $$=c_expr_binary('>',$1,$3,(size_t)@2.first_line); }
    | expression EQ expression { $$=c_expr_binary(EQ,$1,$3,(size_t)@2.first_line); }
    | expression NE expression { $$=c_expr_binary(NE,$1,$3,(size_t)@2.first_line); }
    | expression LE expression { $$=c_expr_binary(LE,$1,$3,(size_t)@2.first_line); }
    | expression GE expression { $$=c_expr_binary(GE,$1,$3,(size_t)@2.first_line); }
    ;
additive:
      multiplicative { $$ = $1; }
    | additive '+' multiplicative { $$ = c_expr_binary('+', $1, $3, (size_t)@2.first_line); }
    | additive '-' multiplicative { $$ = c_expr_binary('-', $1, $3, (size_t)@2.first_line); }
    ;
multiplicative:
      unary { $$ = $1; }
    | multiplicative '*' unary { $$ = c_expr_binary('*', $1, $3, (size_t)@2.first_line); }
    | multiplicative '/' unary { $$ = c_expr_binary('/', $1, $3, (size_t)@2.first_line); }
    ;
unary:
      primary { $$ = $1; }
    | '-' unary { $$ = c_expr_binary('-', c_expr_int(0, (size_t)@1.first_line), $2, (size_t)@1.first_line); }
    | '+' unary { $$ = $2; }
    ;
primary:
      INTEGER { $$ = c_expr_int($1, (size_t)@1.first_line); }
    | IDENTIFIER { $$ = c_expr_var($1, (size_t)@1.first_line); free($1); }
    | '(' expression ')' { $$ = $2; }
    ;
%%

void yyerror(const char *message) {
    extern int yylineno;
    c_set_parse_error((size_t)yylineno, message);
}
