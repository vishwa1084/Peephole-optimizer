#include "../src/parser.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    Program p = {0};
    char err[200];
    if (!parse_text("entry:\nLD R0, #12\nJZ R0, done\ndone:\nHALT\n", &p, err, sizeof err)) {
        fprintf(stderr, "valid input rejected: %s\n", err); return 1;
    }
    if (p.count != 5) { fprintf(stderr, "unexpected parsed instruction count\n"); program_free(&p); return 1; }
    program_free(&p);
    const char *bad[] = {"LD R8, x\n", "LD R1,\n", "JMP missing\n", "again:\nagain:\n", "NOPE R1\n", "ST R1, x\n",
                         "bad-name:\n", "JMP bad-name\n", "LD R0, 12\n", "LD R0, #9223372036854775808\n",
                         "ADD R0, R1, R2, R3\n", "LD R0,, x\n"};
    for (size_t i=0;i<sizeof bad/sizeof bad[0];i++) {
        if (parse_text(bad[i], &p, err, sizeof err)) { fprintf(stderr, "invalid input accepted: %s\n", bad[i]); program_free(&p); return 1; }
        if (strstr(err,"line ")==NULL) { fprintf(stderr,"missing line number: %s\n",err); return 1; }
    }
    if (parse_text("NOP\n\n; comment\nLD R9, x\n", &p, err, sizeof err) || strstr(err, "line 4:") == NULL) {
        fprintf(stderr, "blank lines/comments must count toward diagnostic line numbers: %s\n", err);
        program_free(&p); return 1;
    }
    if (parse_text("JMP done\n\n; comment\ndone:\ndone:\n", &p, err, sizeof err) || strstr(err, "line 5:") == NULL) {
        fprintf(stderr, "duplicate label should report its source line: %s\n", err);
        program_free(&p); return 1;
    }
    Instruction cost_cases[4] = {0};
    cost_cases[0].op = OP_LD; cost_cases[0].argc = 2;
    cost_cases[0].arg[0].kind = OPER_REG; cost_cases[0].arg[1].kind = OPER_MEM;
    cost_cases[1].op = OP_LD; cost_cases[1].argc = 2;
    cost_cases[1].arg[0].kind = OPER_REG; cost_cases[1].arg[1].kind = OPER_IMM;
    cost_cases[2].op = OP_MOV; cost_cases[2].argc = 2;
    cost_cases[2].arg[0].kind = OPER_REG; cost_cases[2].arg[1].kind = OPER_REG;
    cost_cases[3].op = OP_LABEL;
    if (instruction_cost(&cost_cases[0]) != 2 || instruction_cost(&cost_cases[1]) != 2 ||
        instruction_cost(&cost_cases[2]) != 1 || instruction_cost(&cost_cases[3]) != 0) {
        fprintf(stderr, "ISA instruction cost classification is inconsistent\n"); return 1;
    }
    Instruction invalid = {.op=OP_MOV,.argc=2};
    invalid.arg[0].kind=OPER_REG; invalid.arg[0].reg=8;
    invalid.arg[1].kind=OPER_REG; invalid.arg[1].reg=1;
    if (isa_validate(&invalid, err, sizeof err)) { fprintf(stderr, "ISA accepted invalid register\n"); return 1; }
    puts("parser tests passed"); return 0;
}
