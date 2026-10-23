#ifndef C_AST_H
#define C_AST_H

#include "parser.h"

typedef struct CExpr CExpr;
typedef struct CStmt CStmt;
typedef struct CDecl CDecl;
typedef struct CFunction {
    CStmt *body;
    size_t line;
} CFunction;

typedef enum { C_INT, C_VAR, C_BINARY } CExprKind;
enum { C_COND_NONZERO = -1000 };
struct CExpr {
    CExprKind kind;
    size_t line;
    int64_t value;
    char name[MAX_NAME];
    int op;
    CExpr *left, *right;
};
typedef enum {
    C_BLOCK, C_DECL_STMT, C_ASSIGN_STMT, C_RETURN_STMT, C_IF_STMT,
    C_WHILE_STMT, C_FOR_STMT, C_DO_WHILE_STMT
} CStmtKind;
struct CDecl { char name[MAX_NAME]; CExpr *value; size_t line; CDecl *next; };
struct CStmt {
    CStmtKind kind;
    size_t line;
    char name[MAX_NAME];
    CExpr *expr;
    CDecl *decls;
    CStmt *items, *else_branch, *next;
    CStmt *loop_init, *loop_update;
};

CExpr *c_expr_int(int64_t value, size_t line);
CExpr *c_expr_var(const char *name, size_t line);
CExpr *c_expr_binary(int op, CExpr *left, CExpr *right, size_t line);
CDecl *c_decl(const char *name, CExpr *value, size_t line);
CStmt *c_stmt_block(CStmt *items, size_t line);
CStmt *c_stmt_decl(CDecl *decls, size_t line);
CStmt *c_stmt_assign(const char *name, CExpr *expr, size_t line);
CStmt *c_stmt_return(CExpr *expr, size_t line);
CStmt *c_stmt_if(CExpr *condition, CStmt *then_branch, CStmt *else_branch, size_t line);
CStmt *c_stmt_while(CExpr *condition, CStmt *body, size_t line);
CStmt *c_stmt_for(CStmt *init, CExpr *condition, CStmt *update, CStmt *body, size_t line);
CStmt *c_stmt_do_while(CStmt *body, CExpr *condition, size_t line);
void c_stmt_append(CStmt **list, CStmt *stmt);
void c_decl_append(CDecl **list, CDecl *decl);
void c_ast_free(CFunction *function);
void c_expr_destroy(CExpr *expr);
void c_decl_destroy(CDecl *decls);
void c_stmt_destroy(CStmt *stmts);
bool c_translate(CFunction *function, Program *out, char *err, size_t errcap);
void c_set_parse_error(size_t line, const char *message);

#endif
