#ifndef ISA_H
#define ISA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_OPERANDS 3
#define MAX_NAME 128

typedef enum { OP_INVALID, OP_LABEL, OP_LD, OP_ST, OP_MOV, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_SHL, OP_SHR, OP_JMP, OP_JZ, OP_JNZ, OP_NOP, OP_HALT, OP_CMP, OP_JLT, OP_JGT, OP_JLE, OP_JGE, OP_JE, OP_JNE } Opcode;
typedef enum { OPER_NONE, OPER_REG, OPER_IMM, OPER_MEM, OPER_LABEL } OperandKind;
typedef struct { OperandKind kind; int reg; int64_t imm; char name[MAX_NAME]; } Operand;
typedef struct { Opcode op; Operand arg[MAX_OPERANDS]; int argc; size_t line; } Instruction;
const char *opcode_name(Opcode op);
bool opcode_parse(const char *s, Opcode *op);
bool isa_valid_name(const char *name);
bool isa_validate(const Instruction *ins, char *err, size_t errcap);
bool isa_is_branch(Opcode op);
bool isa_is_conditional_branch(Opcode op);
int instruction_cost(const Instruction *ins);
bool instruction_writes_register(const Instruction *ins, int reg);
bool instruction_touches_memory(const Instruction *ins, const char *name, bool *reads, bool *writes);
#endif
