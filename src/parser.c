#include "parser.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void program_free(Program *p) {
    if (!p) return;
    free(p->items);
    p->items = NULL;
    p->count = p->capacity = 0;
}

static bool program_push(Program *p, Instruction in) {
    if (p->count == p->capacity) {
        size_t cap = p->capacity ? p->capacity * 2 : 32;
        if (cap < p->capacity || cap > SIZE_MAX / sizeof(*p->items)) return false;
        Instruction *items = realloc(p->items, cap * sizeof(*items));
        if (!items) return false;
        p->items = items;
        p->capacity = cap;
    }
    p->items[p->count++] = in;
    return true;
}

static void set_error(char *err, size_t cap, size_t line, const char *message) {
    if (err && cap) {
        if (line) snprintf(err, cap, "line %zu: %s", line, message);
        else snprintf(err, cap, "%s", message);
    }
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_operand(char *text, Operand *out) {
    memset(out, 0, sizeof(*out));
    char *s = trim(text);
    if (!*s) return false;

    if (s[0] == '#') {
        char *end = NULL;
        errno = 0;
        long long value = strtoll(s + 1, &end, 0);
        if (s[1] == '\0' || errno == ERANGE || end == s + 1 || *end != '\0') return false;
        out->kind = OPER_IMM;
        out->imm = (int64_t)value;
        return true;
    }

    if (s[0] == 'R' && isdigit((unsigned char)s[1])) {
        char *end = NULL;
        errno = 0;
        long reg = strtol(s + 1, &end, 10);
        if (errno == ERANGE || end == s + 1 || *end != '\0' || reg < 0 || reg > 7) return false;
        out->kind = OPER_REG;
        out->reg = (int)reg;
        return true;
    }

    if (!isa_valid_name(s)) return false;
    memcpy(out->name, s, strlen(s) + 1);
    out->kind = OPER_MEM;
    return true;
}

static bool parse_line(char *line_text, size_t line_no, Program *out, char *err, size_t errcap) {
    char *s = trim(line_text);
    char *comment = strpbrk(s, ";!");
    if (comment) *comment = '\0';
    s = trim(s);
    if (!*s) return true;

    Instruction in = {0};
    in.line = line_no;
    size_t length = strlen(s);
    if (s[length - 1] == ':') {
        s[length - 1] = '\0';
        s = trim(s);
        if (!isa_valid_name(s)) {
            set_error(err, errcap, line_no, "invalid label syntax");
            return false;
        }
        in.op = OP_LABEL;
        in.argc = 1;
        in.arg[0].kind = OPER_LABEL;
        memcpy(in.arg[0].name, s, strlen(s) + 1);
    } else {
        char *op_end = s;
        while (*op_end && !isspace((unsigned char)*op_end)) op_end++;
        char *arguments = op_end;
        if (*op_end) *op_end++ = '\0';
        arguments = trim(op_end);
        for (char *c = s; *c; c++) *c = (char)toupper((unsigned char)*c);
        if (!opcode_parse(s, &in.op)) {
            set_error(err, errcap, line_no, "unknown opcode");
            return false;
        }

        while (*arguments) {
            if (in.argc == MAX_OPERANDS) {
                set_error(err, errcap, line_no, "too many operands");
                return false;
            }
            char *comma = strchr(arguments, ',');
            if (comma) *comma = '\0';
            char *token = trim(arguments);
            Operand *operand = &in.arg[in.argc];
            bool label_operand = (in.op == OP_JMP ||
                (isa_is_conditional_branch(in.op) && in.op != OP_JZ && in.op != OP_JNZ)) ||
                ((in.op == OP_JZ || in.op == OP_JNZ) && in.argc == 1);
            if (label_operand) {
                if (!isa_valid_name(token)) {
                    set_error(err, errcap, line_no, "invalid label operand");
                    return false;
                }
                operand->kind = OPER_LABEL;
                memcpy(operand->name, token, strlen(token) + 1);
            } else if (!parse_operand(token, operand)) {
                set_error(err, errcap, line_no, "malformed operand or invalid register");
                return false;
            }
            in.argc++;
            if (!comma) break;
            arguments = comma + 1;
            if (!*trim(arguments)) {
                set_error(err, errcap, line_no, "missing operand after comma");
                return false;
            }
        }

        char why[128];
        if (!isa_validate(&in, why, sizeof(why))) {
            set_error(err, errcap, line_no, why);
            return false;
        }
    }

    if (!program_push(out, in)) {
        set_error(err, errcap, line_no, "out of memory while parsing");
        return false;
    }
    return true;
}

bool parse_text(const char *text, Program *out, char *err, size_t errcap) {
    if (!out || !text) {
        set_error(err, errcap, 0, "invalid parser argument");
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (err && errcap) err[0] = '\0';
    size_t text_len = strlen(text);
    if (text_len == SIZE_MAX) {
        set_error(err, errcap, 0, "input is too large");
        return false;
    }
    char *buffer = malloc(text_len + 1);
    if (!buffer) {
        set_error(err, errcap, 0, "out of memory");
        return false;
    }
    memcpy(buffer, text, text_len + 1);

    size_t line_no = 1;
    char *line = buffer;
    bool ok = true;
    while (line && ok) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        ok = parse_line(line, line_no, out, err, errcap);
        line = next;
        line_no++;
    }
    free(buffer);
    if (ok) ok = validate_labels(out, err, errcap);
    if (!ok) program_free(out);
    return ok;
}

bool parse_file(const char *path, Program *out, char *err, size_t errcap) {
    if (!path || !out) {
        set_error(err, errcap, 0, "invalid input file argument");
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (err && errcap) snprintf(err, errcap, "cannot open '%s'", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(err, errcap, 0, "cannot seek input file");
        return false;
    }
    long file_size = ftell(file);
    if (file_size < 0 || (uintmax_t)file_size >= SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(err, errcap, 0, "cannot determine input size");
        return false;
    }
    size_t size = (size_t)file_size;
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        set_error(err, errcap, 0, "out of memory");
        return false;
    }
    size_t read_count = fread(buffer, 1, size, file);
    bool read_ok = read_count == size && !ferror(file);
    if (fclose(file) != 0) read_ok = false;
    if (!read_ok) {
        free(buffer);
        set_error(err, errcap, 0, "error reading input file");
        return false;
    }
    if (memchr(buffer, '\0', size) != NULL) {
        size_t line = 1;
        for (size_t i = 0; i < size && buffer[i] != '\0'; i++) if (buffer[i] == '\n') line++;
        free(buffer);
        set_error(err, errcap, line, "NUL byte in source file");
        return false;
    }
    buffer[size] = '\0';
    bool ok = parse_text(buffer, out, err, errcap);
    free(buffer);
    return ok;
}

/* Flex/Bison output is generated from lexer.l and grammar.y and included here so
   the documented direct-GCC build remains a single command. */
#include "grammar.tab.c"
#include "lexer.yy.c"
#include "c_ast.h"
#include <limits.h>

static char c_error[256];
static CExpr *new_expr(CExprKind kind, size_t line) {
    CExpr *e = calloc(1, sizeof(*e));
    if (e) { e->kind = kind; e->line = line; }
    else c_set_parse_error(line, "out of memory building C syntax tree");
    return e;
}
CExpr *c_expr_int(int64_t value, size_t line) { CExpr *e=new_expr(C_INT,line); if(e)e->value=value; return e; }
CExpr *c_expr_var(const char *name, size_t line) { CExpr *e=new_expr(C_VAR,line); if(e)snprintf(e->name,sizeof e->name,"%s",name); return e; }
CExpr *c_expr_binary(int op,CExpr *left,CExpr *right,size_t line) { CExpr *e=new_expr(C_BINARY,line); if(e){e->op=op;e->left=left;e->right=right;} return e; }
CDecl *c_decl(const char *name,CExpr *value,size_t line) { CDecl*d=calloc(1,sizeof(*d)); if(!d){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;} snprintf(d->name,sizeof d->name,"%s",name);d->value=value;d->line=line;return d; }
CStmt *c_stmt_block(CStmt *items,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_BLOCK;s->items=items;s->line=line;return s; }
CStmt *c_stmt_decl(CDecl *decls,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_DECL_STMT;s->decls=decls;s->line=line;return s; }
CStmt *c_stmt_assign(const char *name,CExpr *expr,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_ASSIGN_STMT;snprintf(s->name,sizeof s->name,"%s",name);s->expr=expr;s->line=line;return s; }
CStmt *c_stmt_return(CExpr *expr,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_RETURN_STMT;s->expr=expr;s->line=line;return s; }
CStmt *c_stmt_if(CExpr*condition,CStmt*then_branch,CStmt*else_branch,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_IF_STMT;s->expr=condition;s->items=then_branch;s->else_branch=else_branch;s->line=line;return s; }
CStmt *c_stmt_while(CExpr*condition,CStmt*body,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_WHILE_STMT;s->expr=condition;s->items=body;s->line=line;return s; }
CStmt *c_stmt_for(CStmt*init,CExpr*condition,CStmt*update,CStmt*body,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_FOR_STMT;s->loop_init=init;s->expr=condition;s->loop_update=update;s->items=body;s->line=line;return s; }
CStmt *c_stmt_do_while(CStmt*body,CExpr*condition,size_t line) { CStmt*s=calloc(1,sizeof(*s));if(!s){c_set_parse_error(line,"out of memory building C syntax tree");return NULL;}s->kind=C_DO_WHILE_STMT;s->items=body;s->expr=condition;s->line=line;return s; }
void c_stmt_append(CStmt **list,CStmt *stmt) { if(!list||!stmt)return;while(*list)list=&(*list)->next;*list=stmt; }
void c_decl_append(CDecl **list,CDecl *decl) { if(!list||!decl)return;while(*list)list=&(*list)->next;*list=decl; }
void c_expr_destroy(CExpr *e) { if(e){c_expr_destroy(e->left);c_expr_destroy(e->right);free(e);} }
void c_decl_destroy(CDecl*d){while(d){CDecl*n=d->next;c_expr_destroy(d->value);free(d);d=n;}}
void c_stmt_destroy(CStmt*s){while(s){CStmt*n=s->next;c_stmt_destroy(s->items);c_stmt_destroy(s->else_branch);c_stmt_destroy(s->loop_init);c_stmt_destroy(s->loop_update);c_decl_destroy(s->decls);c_expr_destroy(s->expr);free(s);s=n;}}
void c_ast_free(CFunction *f){if(f){c_stmt_destroy(f->body);free(f);}}
void c_set_parse_error(size_t line,const char *message){if(!c_error[0]){if(line)snprintf(c_error,sizeof c_error,"line %zu: %s",line,message);else snprintf(c_error,sizeof c_error,"%s",message);}}

typedef struct { char name[MAX_NAME]; char storage[MAX_NAME]; } CSymbol;
typedef struct { Program *p; CSymbol symbols[128]; size_t symbol_count, storage_count, label_count, scope_depth, scope_base[129]; char *err; size_t errcap; } CG;
static bool emit(CG*g,Opcode op,int argc,Operand a,Operand b,Operand c,size_t line){
    Instruction in={0};in.op=op;in.argc=argc;in.line=line;in.arg[0]=a;in.arg[1]=b;in.arg[2]=c;
    char why[128];if(!isa_validate(&in,why,sizeof why)){snprintf(g->err,g->errcap,"line %zu: generated invalid instruction: %s",line,why);return false;}
    if(g->p->count==g->p->capacity){size_t cap=g->p->capacity?g->p->capacity*2:32;Instruction*q=realloc(g->p->items,cap*sizeof(*q));if(!q){snprintf(g->err,g->errcap,"out of memory generating target instructions");return false;}g->p->items=q;g->p->capacity=cap;}
    g->p->items[g->p->count++]=in;return true;
}
static Operand regop(int r){Operand o={0};o.kind=OPER_REG;o.reg=r;return o;}
static Operand immop(int64_t n){Operand o={0};o.kind=OPER_IMM;o.imm=n;return o;}
static Operand memop(const char*n){Operand o={0};o.kind=OPER_MEM;snprintf(o.name,sizeof o.name,"%s",n);return o;}
static Operand labelop(const char*n){Operand o={0};o.kind=OPER_LABEL;snprintf(o.name,sizeof o.name,"%s",n);return o;}
static bool const_int(CExpr *e, int64_t *value) {
    if (!e) return false;
    if (e->kind == C_INT) { *value=e->value; return true; }
    if (e->kind != C_BINARY) return false;
    int64_t a,b,z;
    if (!const_int(e->left,&a) || !const_int(e->right,&b)) return false;
    switch(e->op) {
    case '+': if(__builtin_add_overflow(a,b,&z))return false; break;
    case '-': if(__builtin_sub_overflow(a,b,&z))return false; break;
    case '*': if(__builtin_mul_overflow(a,b,&z))return false; break;
    case '/': if(b==0 || (a==INT64_MIN&&b==-1))return false; z=a/b; break;
    default:return false;
    }
    *value=z; return true;
}
static CSymbol *find_symbol(CG*g,const char*n){for(size_t i=g->symbol_count;i>0;i--)if(strcmp(g->symbols[i-1].name,n)==0)return &g->symbols[i-1];return NULL;}
static bool add_symbol(CG*g,const char*n,size_t line){for(size_t i=g->symbol_count;i>g->scope_base[g->scope_depth];i--)if(strcmp(g->symbols[i-1].name,n)==0){snprintf(g->err,g->errcap,"line %zu: duplicate local variable '%s'",line,n);return false;}if(g->symbol_count==128||g->storage_count==128){snprintf(g->err,g->errcap,"line %zu: too many local variables (limit 128)",line);return false;}CSymbol *s=&g->symbols[g->symbol_count++];snprintf(s->name,MAX_NAME,"%s",n);snprintf(s->storage,MAX_NAME,"v%03zu",g->storage_count++);return true;}
static bool expression(CG*g,CExpr*e,int r,int depth){if(!e)return false;if(depth>=7){snprintf(g->err,g->errcap,"line %zu: expression is nested too deeply (maximum 7 registers)",e->line);return false;}
    if(e->kind==C_BINARY && e->op=='/') { int64_t denominator; if(const_int(e->right,&denominator)&&denominator==0){snprintf(g->err,g->errcap,"line %zu: division by zero in constant expression",e->line);return false;} }
    if(e->kind==C_INT)return emit(g,OP_LD,2,regop(r),immop(e->value),(Operand){0},e->line);
    if(e->kind==C_VAR){CSymbol*s=find_symbol(g,e->name);if(!s){snprintf(g->err,g->errcap,"line %zu: use of undeclared variable '%s'",e->line,e->name);return false;}return emit(g,OP_LD,2,regop(r),memop(s->storage),(Operand){0},e->line);}
    Opcode op=e->op=='+'?OP_ADD:e->op=='-'?OP_SUB:e->op=='*'?OP_MUL:OP_DIV;
    if(!expression(g,e->left,r,depth+1))return false;
    if(e->right && e->right->kind==C_INT)
        return emit(g,op,3,regop(r),regop(r),immop(e->right->value),e->line);
    if(!expression(g,e->right,r+1,depth+1))return false;
    return emit(g,op,3,regop(r),regop(r),regop(r+1),e->line);
}
static Opcode relation_branch(int op){
    switch(op){case '<':return OP_JLT;case '>':return OP_JGT;case EQ:return OP_JE;case NE:return OP_JNE;case LE:return OP_JLE;case GE:return OP_JGE;default:return OP_INVALID;}
}
static Opcode inverse_branch(Opcode op){
    switch(op){case OP_JLT:return OP_JGE;case OP_JGT:return OP_JLE;case OP_JLE:return OP_JGT;case OP_JGE:return OP_JLT;case OP_JE:return OP_JNE;case OP_JNE:return OP_JE;default:return OP_INVALID;}
}
static bool emit_condition_branch(CG*g,CExpr*condition,bool when_true,const char*destination,size_t line){
    if(!condition){snprintf(g->err,g->errcap,"line %zu: missing loop condition",line);return false;}
    if(!expression(g,condition->left,0,0))return false;
    if(condition->op==C_COND_NONZERO)
        return emit(g,when_true?OP_JNZ:OP_JZ,2,regop(0),labelop(destination),(Operand){0},line);
    if(!expression(g,condition->right,1,0)||!emit(g,OP_CMP,2,regop(0),regop(1),(Operand){0},line))return false;
    Opcode branch=relation_branch(condition->op);
    if(!when_true)branch=inverse_branch(branch);
    if(branch==OP_INVALID){snprintf(g->err,g->errcap,"line %zu: invalid conditional operator",line);return false;}
    return emit(g,branch,1,labelop(destination),(Operand){0},(Operand){0},line);
}
static bool begin_scope(CG*g,size_t line,size_t*saved){
    if(g->scope_depth>=128){snprintf(g->err,g->errcap,"line %zu: block nesting limit exceeded",line);return false;}
    *saved=g->symbol_count;g->scope_base[++g->scope_depth]=*saved;return true;
}
static void end_scope(CG*g,size_t saved){g->scope_depth--;g->symbol_count=saved;}
static bool statements(CG*g,CStmt*s,bool *returned);
static bool statement(CG*g,CStmt*s,bool *returned){
    if(s->kind==C_BLOCK){size_t saved;if(!begin_scope(g,s->line,&saved))return false;bool ok=statements(g,s->items,returned);end_scope(g,saved);return ok;}
    if(s->kind==C_DECL_STMT){for(CDecl*d=s->decls;d;d=d->next){if(!add_symbol(g,d->name,d->line))return false;CSymbol*sym=find_symbol(g,d->name);if(d->value&&(!expression(g,d->value,0,0)||!emit(g,OP_ST,2,memop(sym->storage),regop(0),(Operand){0},d->line)))return false;}return true;}
    if(s->kind==C_ASSIGN_STMT){CSymbol*sym=find_symbol(g,s->name);if(!sym){snprintf(g->err,g->errcap,"line %zu: assignment to undeclared variable '%s'",s->line,s->name);return false;}if(!expression(g,s->expr,0,0)||!emit(g,OP_ST,2,memop(sym->storage),regop(0),(Operand){0},s->line))return false;return true;}
    if(s->kind==C_RETURN_STMT){if(!expression(g,s->expr,0,0)||!emit(g,OP_HALT,0,(Operand){0},(Operand){0},(Operand){0},s->line))return false;*returned=true;return true;}
    if(s->kind==C_IF_STMT){
        char alternate[MAX_NAME],finish[MAX_NAME];size_t id=g->label_count++;
        snprintf(alternate,sizeof alternate,"L%zu_else",id);snprintf(finish,sizeof finish,"L%zu_end",id);
        if(!emit_condition_branch(g,s->expr,false,s->else_branch?alternate:finish,s->line))return false;
        if(!statement(g,s->items,returned))return false;
        if(s->else_branch){if(!emit(g,OP_JMP,1,labelop(finish),(Operand){0},(Operand){0},s->line))return false;if(!emit(g,OP_LABEL,1,labelop(alternate),(Operand){0},(Operand){0},s->line))return false;if(!statement(g,s->else_branch,returned))return false;}
        return emit(g,OP_LABEL,1,labelop(finish),(Operand){0},(Operand){0},s->line);
    }
    if(s->kind==C_WHILE_STMT){
        char test[MAX_NAME],finish[MAX_NAME];size_t id=g->label_count++;
        snprintf(test,sizeof test,"L%zu_while",id);snprintf(finish,sizeof finish,"L%zu_end",id);
        if(!emit(g,OP_LABEL,1,labelop(test),(Operand){0},(Operand){0},s->line)||
           !emit_condition_branch(g,s->expr,false,finish,s->line)||
           !statement(g,s->items,returned)||
           !emit(g,OP_JMP,1,labelop(test),(Operand){0},(Operand){0},s->line)||
           !emit(g,OP_LABEL,1,labelop(finish),(Operand){0},(Operand){0},s->line))return false;
        return true;
    }
    if(s->kind==C_FOR_STMT){
        size_t saved;if(!begin_scope(g,s->line,&saved))return false;
        if(s->loop_init&&!statement(g,s->loop_init,returned)){end_scope(g,saved);return false;}
        char test[MAX_NAME],finish[MAX_NAME];size_t id=g->label_count++;
        snprintf(test,sizeof test,"L%zu_for",id);snprintf(finish,sizeof finish,"L%zu_end",id);
        bool ok=emit(g,OP_LABEL,1,labelop(test),(Operand){0},(Operand){0},s->line);
        if(ok&&s->expr)ok=emit_condition_branch(g,s->expr,false,finish,s->line);
        if(ok)ok=statement(g,s->items,returned);
        if(ok&&s->loop_update)ok=statement(g,s->loop_update,returned);
        if(ok)ok=emit(g,OP_JMP,1,labelop(test),(Operand){0},(Operand){0},s->line);
        if(ok)ok=emit(g,OP_LABEL,1,labelop(finish),(Operand){0},(Operand){0},s->line);
        end_scope(g,saved);return ok;
    }
    if(s->kind==C_DO_WHILE_STMT){
        char body[MAX_NAME],finish[MAX_NAME];size_t id=g->label_count++;
        snprintf(body,sizeof body,"L%zu_do",id);snprintf(finish,sizeof finish,"L%zu_end",id);
        if(!emit(g,OP_LABEL,1,labelop(body),(Operand){0},(Operand){0},s->line)||
           !statement(g,s->items,returned)||
           !emit_condition_branch(g,s->expr,true,body,s->line)||
           !emit(g,OP_LABEL,1,labelop(finish),(Operand){0},(Operand){0},s->line))return false;
        return true;
    }
    return false;
}
static bool statements(CG*g,CStmt*s,bool *returned){for(;s;s=s->next)if(!statement(g,s,returned))return false;return true;}
static bool parse_c_text(const char*text,size_t length,Program*out,char*err,size_t errcap){
    memset(out,0,sizeof(*out));c_error[0]='\0';c_parse_result=NULL;yylineno=1;
    if(length>INT_MAX){snprintf(err,errcap,"C source file is too large");return false;}
    YY_BUFFER_STATE buffer=yy_scan_bytes(text,(int)length);if(!buffer){snprintf(err,errcap,"could not initialize C lexer");return false;}
    int status=yyparse();yy_delete_buffer(buffer);
    if(status||!c_parse_result){if(err&&errcap)snprintf(err,errcap,"%s",c_error[0]?c_error:"C syntax error");c_ast_free(c_parse_result);return false;}
    CG g={.p=out,.err=err,.errcap=errcap};bool returned=false;bool ok=statements(&g,c_parse_result->body,&returned);
    (void)returned;
    if(ok&&(out->count==0||out->items[out->count-1].op!=OP_HALT))ok=emit(&g,OP_HALT,0,(Operand){0},(Operand){0},(Operand){0},c_parse_result->line);
    c_ast_free(c_parse_result);c_parse_result=NULL;
    if(!ok)program_free(out);return ok;
}
bool parse_c_file(const char*path,Program*out,char*err,size_t errcap){
    if(!path||!out){if(err&&errcap)snprintf(err,errcap,"invalid C input file argument");return false;}
    FILE*f=fopen(path,"rb");if(!f){if(err&&errcap)snprintf(err,errcap,"cannot open '%s'",path);return false;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);if(err&&errcap)snprintf(err,errcap,"cannot seek input file");return false;}long n=ftell(f);if(n<0||(unsigned long)n>SIZE_MAX-1){fclose(f);if(err&&errcap)snprintf(err,errcap,"input file is too large");return false;}rewind(f);
    char*text=malloc((size_t)n+1);if(!text){fclose(f);if(err&&errcap)snprintf(err,errcap,"out of memory reading C source");return false;}size_t got=fread(text,1,(size_t)n,f);bool ioerr=ferror(f)!=0;fclose(f);if(ioerr||got!=(size_t)n){free(text);if(err&&errcap)snprintf(err,errcap,"failed reading C source");return false;}text[got]='\0';bool ok=parse_c_text(text,got,out,err,errcap);free(text);return ok;
}

bool validate_labels(const Program *p, char *err, size_t errcap) {
    if (!p) {
        set_error(err, errcap, 0, "invalid program");
        return false;
    }
    for (size_t i = 0; i < p->count; i++) {
        if (p->items[i].op != OP_LABEL) continue;
        for (size_t j = 0; j < i; j++) {
            if (p->items[j].op == OP_LABEL &&
                strcmp(p->items[i].arg[0].name, p->items[j].arg[0].name) == 0) {
                set_error(err, errcap, p->items[i].line, "duplicate label");
                return false;
            }
        }
    }
    for (size_t i = 0; i < p->count; i++) {
        for (int a = 0; a < p->items[i].argc; a++) {
            if (p->items[i].arg[a].kind != OPER_LABEL) continue;
            bool found = false;
            for (size_t j = 0; j < p->count; j++) {
                if (p->items[j].op == OP_LABEL &&
                    strcmp(p->items[j].arg[0].name, p->items[i].arg[a].name) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                set_error(err, errcap, p->items[i].line, "undefined label");
                return false;
            }
        }
    }
    return true;
}
