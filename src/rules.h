#ifndef RULES_H
#define RULES_H
#include "parser.h"
#include <stdint.h>
typedef struct { char rule[48]; size_t line; char explanation[160]; } Rewrite;
typedef struct { Rewrite *items; size_t count, capacity; } RewriteLog;
bool apply_rules(Program *p, unsigned mask, RewriteLog *log, size_t *changes);
void rewrite_log_free(RewriteLog *l);
enum { RULE_LOAD_AFTER_STORE=1u<<0, RULE_LOAD_COPY=1u<<1, RULE_REDUNDANT_STORE=1u<<2, RULE_ALGEBRA=1u<<3, RULE_MUL_ZERO=1u<<4, RULE_SELF_MOVE=1u<<5, RULE_STRENGTH=1u<<6, RULE_FOLD=1u<<7, RULE_NEXT_JUMP=1u<<8, RULE_JUMP_CHAIN=1u<<9, RULE_UNREACHABLE=1u<<10, RULE_DEAD_STORE=1u<<11, RULE_CONST_PROP=1u<<12, RULE_ALL=(1u<<13)-1 };
#endif
