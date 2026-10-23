#include "isa.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *names[] = {"INVALID","LABEL","LD","ST","MOV","ADD","SUB","MUL","DIV","SHL","SHR","JMP","JZ","JNZ","NOP","HALT","CMP","JLT","JGT","JLE","JGE","JE","JNE"};
const char *opcode_name(Opcode op) { return op >= OP_INVALID && op <= OP_JNE ? names[op] : "INVALID"; }
bool opcode_parse(const char *s, Opcode *op) { for (int i=OP_LD;i<=OP_JNE;i++) if (strcmp(s,names[i])==0) {*op=(Opcode)i; return true;} return false; }
bool isa_is_conditional_branch(Opcode op) { return op==OP_JZ||op==OP_JNZ||op==OP_JLT||op==OP_JGT||op==OP_JLE||op==OP_JGE||op==OP_JE||op==OP_JNE; }
bool isa_is_branch(Opcode op) { return op==OP_JMP||isa_is_conditional_branch(op); }
bool isa_valid_name(const char *s) {
    if (!s || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    size_t n = 1;
    while (s[n]) {
        if (!(isalnum((unsigned char)s[n]) || s[n] == '_' || s[n] == '.')) return false;
        if (++n >= MAX_NAME) return false;
    }
    return true;
}
static bool regop(Operand o) { return o.kind==OPER_REG && o.reg>=0 && o.reg<=7; }
static void msg(char *e,size_t n,const char *s) { if(n) snprintf(e,n,"%s",s); }
bool isa_validate(const Instruction *i,char *e,size_t n) {
    if (!i) { msg(e,n,"null instruction"); return false; }
    if(i->op==OP_LABEL) { if(i->argc!=1||i->arg[0].kind!=OPER_LABEL||!isa_valid_name(i->arg[0].name)){msg(e,n,"label requires one valid name");return false;} return true; }
    switch(i->op) {
    case OP_LD: if(i->argc==2&&regop(i->arg[0])&&((i->arg[1].kind==OPER_MEM&&isa_valid_name(i->arg[1].name))||i->arg[1].kind==OPER_IMM)) return true; break;
    case OP_ST: if(i->argc==2&&i->arg[0].kind==OPER_MEM&&isa_valid_name(i->arg[0].name)&&regop(i->arg[1])) return true; break;
    case OP_MOV: if(i->argc==2&&regop(i->arg[0])&&regop(i->arg[1])) return true; break;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_SHL: case OP_SHR:
        if(i->argc==3&&regop(i->arg[0])&&regop(i->arg[1])&&(regop(i->arg[2])||i->arg[2].kind==OPER_IMM)) return true;
        break;
    case OP_CMP: if(i->argc==2&&regop(i->arg[0])&&regop(i->arg[1])) return true; break;
    case OP_JMP: if(i->argc==1&&i->arg[0].kind==OPER_LABEL&&isa_valid_name(i->arg[0].name))return true; break;
    case OP_JZ: case OP_JNZ: if(i->argc==2&&regop(i->arg[0])&&i->arg[1].kind==OPER_LABEL&&isa_valid_name(i->arg[1].name))return true; break;
    case OP_JLT: case OP_JGT: case OP_JLE: case OP_JGE: case OP_JE: case OP_JNE:
        if(i->argc==1&&i->arg[0].kind==OPER_LABEL&&isa_valid_name(i->arg[0].name)) return true;
        break;
    case OP_NOP: case OP_HALT: if(i->argc==0)return true; break;
    default: break;
    }
    msg(e,n,"invalid operand count or operand kind for instruction"); return false;
}
int instruction_cost(const Instruction *i) { if(!i||i->op==OP_LABEL)return 0; int c=1; for(int k=0;k<i->argc;k++) if(i->arg[k].kind==OPER_IMM||i->arg[k].kind==OPER_MEM)c++; return c; }
bool instruction_writes_register(const Instruction *i,int r) { return (i->op==OP_LD||i->op==OP_MOV||i->op==OP_ADD||i->op==OP_SUB||i->op==OP_MUL||i->op==OP_DIV||i->op==OP_SHL||i->op==OP_SHR)&&i->argc>0&&i->arg[0].kind==OPER_REG&&i->arg[0].reg==r; }
bool instruction_touches_memory(const Instruction *i,const char *name,bool *reads,bool *writes) {
    if (reads) *reads=false;
    if (writes) *writes=false;
    if (!i || !name || i->argc < 0) return false;
    if(i->op==OP_LD&&i->argc==2&&i->arg[1].kind==OPER_MEM&&strcmp(i->arg[1].name,name)==0){if(reads)*reads=true;return true;}
    if(i->op==OP_ST&&i->argc==2&&i->arg[0].kind==OPER_MEM&&strcmp(i->arg[0].name,name)==0){if(writes)*writes=true;return true;}
    return false;
}
