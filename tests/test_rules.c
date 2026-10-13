#include "../src/optimizer.h"
#include <stdio.h>
#include <string.h>

static int failures;

static Program parse(const char *source) {
    Program p = {0};
    char err[200];
    if (!parse_text(source, &p, err, sizeof(err))) {
        fprintf(stderr, "test input did not parse: %s\n", err);
        failures++;
    }
    return p;
}

static void expect_case(const char *name, const char *source, unsigned rule,
                        bool should_rewrite, size_t expected_count,
                        Opcode expected_op, size_t rewrite_line) {
    Program p = parse(source);
    RewriteLog log = {0};
    OptimizeStats stats = {0};
    char err[200];
    bool ok = optimize(&p, rule, &log, &stats, err, sizeof(err));
    if (!ok) {
        fprintf(stderr, "%s: optimizer failed: %s\n", name, err);
        failures++;
    }
    if (p.count != expected_count) {
        fprintf(stderr, "%s: expected %zu instructions, got %zu\n", name, expected_count, p.count);
        failures++;
    }
    if (p.count && expected_op != OP_INVALID && p.items[0].op != expected_op) {
        fprintf(stderr, "%s: expected first opcode %s, got %s\n", name,
                opcode_name(expected_op), opcode_name(p.items[0].op));
        failures++;
    }
    if (should_rewrite) {
        if (!log.count) {
            fprintf(stderr, "%s: expected a rewrite\n", name);
            failures++;
        } else if (rewrite_line && log.items[0].line != rewrite_line) {
            fprintf(stderr, "%s: expected rewrite source line %zu, got %zu\n",
                    name, rewrite_line, log.items[0].line);
            failures++;
        }
    } else if (log.count) {
        fprintf(stderr, "%s: unexpected rewrite (%s at line %zu)\n", name,
                log.items[0].rule, log.items[0].line);
        failures++;
    }
    program_free(&p);
    rewrite_log_free(&log);
}

static void test_redundant_load(void) {
    expect_case("load after store", "ST x, R1\nLD R1, x\n", RULE_LOAD_AFTER_STORE, true, 1, OP_ST, 2);
    expect_case("load after store register overwrite", "ST x, R1\nMOV R1, R2\nLD R1, x\n", RULE_LOAD_AFTER_STORE, false, 3, OP_ST, 0);
    expect_case("load after store label boundary", "ST x, R1\nblock:\nLD R1, x\n", RULE_LOAD_AFTER_STORE, false, 3, OP_ST, 0);
    expect_case("load after store different variable", "ST x, R1\nLD R1, y\n", RULE_LOAD_AFTER_STORE, false, 2, OP_ST, 0);
}

static void test_load_copy(void) {
    expect_case("load to copy", "LD R1, #4\nST x, R1\nLD R2, x\n", RULE_LOAD_COPY, true, 3, OP_LD, 3);
    expect_case("load copy label boundary", "ST x, R1\nblock:\nLD R2, x\n", RULE_LOAD_COPY, false, 3, OP_ST, 0);
    expect_case("load copy different variable", "ST x, R1\nLD R2, y\n", RULE_LOAD_COPY, false, 2, OP_ST, 0);
    expect_case("load copy separated by register overwrite", "ST x, R1\nMOV R1, R2\nLD R3, x\n", RULE_LOAD_COPY, false, 3, OP_ST, 0);
}

static void test_stores(void) {
    expect_case("redundant store", "ST x, R1\nST x, R1\n", RULE_REDUNDANT_STORE, true, 1, OP_ST, 2);
    expect_case("redundant store after register overwrite", "ST x, R1\nMOV R1, R2\nST x, R1\n", RULE_REDUNDANT_STORE, false, 3, OP_ST, 0);
    expect_case("redundant store across label", "ST x, R1\nblock:\nST x, R1\n", RULE_REDUNDANT_STORE, false, 3, OP_ST, 0);
    expect_case("different memory symbols", "ST x, R1\nST y, R1\n", RULE_REDUNDANT_STORE, false, 2, OP_ST, 0);

    expect_case("dead store", "ST x, R1\nST x, R2\n", RULE_DEAD_STORE, true, 1, OP_ST, 1);
    expect_case("dead store read before overwrite", "ST x, R1\nLD R3, x\nST x, R2\n", RULE_DEAD_STORE, false, 3, OP_ST, 0);
    expect_case("dead store label boundary", "ST x, R1\nblock:\nST x, R2\n", RULE_DEAD_STORE, false, 3, OP_ST, 0);
    expect_case("dead store branch boundary", "ST x, R1\nJZ R0, done\nST x, R2\ndone:\nHALT\n", RULE_DEAD_STORE, false, 5, OP_ST, 0);
    expect_case("dead store different symbol", "ST x, R1\nST y, R2\n", RULE_DEAD_STORE, false, 2, OP_ST, 0);

    Program p = parse("ST x, R1\nST x, R1\nST x, R2\n");
    RewriteLog log = {0}; OptimizeStats stats = {0}; char err[200];
    if (!optimize(&p, RULE_REDUNDANT_STORE | RULE_DEAD_STORE, &log, &stats, err, sizeof(err)) ||
        p.count != 1 || p.items[0].op != OP_ST || p.items[0].arg[1].reg != 2) {
        fprintf(stderr, "combined store rules must revisit shifted instructions safely\n"); failures++;
    }
    program_free(&p); rewrite_log_free(&log);
}

static void test_arithmetic(void) {
    expect_case("add zero", "ADD R1, R2, #0\n", RULE_ALGEBRA, true, 1, OP_MOV, 1);
    expect_case("multiply one", "MUL R1, R2, #1\n", RULE_ALGEBRA, true, 1, OP_MOV, 1);
    expect_case("nonidentity add", "ADD R1, R2, #1\n", RULE_ALGEBRA, false, 1, OP_ADD, 0);
    expect_case("nonidentity multiply", "MUL R1, R2, #2\n", RULE_ALGEBRA, false, 1, OP_MUL, 0);
    expect_case("multiply zero", "MUL R1, R2, #0\n", RULE_MUL_ZERO, true, 1, OP_LD, 1);
    expect_case("multiply nonzero", "MUL R1, R2, #3\n", RULE_MUL_ZERO, false, 1, OP_MUL, 0);
    expect_case("self move", "MOV R1, R1\n", RULE_SELF_MOVE, true, 0, OP_INVALID, 1);
    expect_case("move between registers", "MOV R1, R2\n", RULE_SELF_MOVE, false, 1, OP_MOV, 0);

    expect_case("safe power of two", "LD R2, #2\nMUL R1, R2, #8\n", RULE_STRENGTH, true, 2, OP_LD, 2);
    expect_case("power of two unknown source", "MUL R1, R2, #8\n", RULE_STRENGTH, false, 1, OP_MUL, 0);
    expect_case("power of two negative source", "LD R2, #-2\nMUL R1, R2, #8\n", RULE_STRENGTH, false, 2, OP_LD, 0);
    expect_case("power of two overflowing source", "LD R2, #1152921504606846976\nMUL R1, R2, #8\n", RULE_STRENGTH, false, 2, OP_LD, 0);
    expect_case("nonpower multiplier", "LD R2, #2\nMUL R1, R2, #7\n", RULE_STRENGTH, false, 2, OP_LD, 0);
    expect_case("register overwritten before strength reduction", "LD R2, #2\nMOV R2, R3\nMUL R1, R2, #8\n", RULE_STRENGTH, false, 3, OP_LD, 0);

    expect_case("constant fold", "LD R2, #6\nADD R1, R2, #3\n", RULE_FOLD, true, 2, OP_LD, 2);
    expect_case("constant fold register overwrite", "LD R2, #6\nMOV R2, R3\nADD R1, R2, #3\n", RULE_FOLD, false, 3, OP_LD, 0);
    expect_case("constant fold across label", "LD R2, #6\nblock:\nADD R1, R2, #3\n", RULE_FOLD, false, 3, OP_LD, 0);
    expect_case("constant fold overflow", "LD R2, #9223372036854775807\nADD R1, R2, #1\n", RULE_FOLD, false, 2, OP_LD, 0);
    expect_case("constant fold divide zero", "LD R2, #8\nDIV R1, R2, #0\n", RULE_FOLD, false, 2, OP_LD, 0);
    expect_case("constant fold signed division overflow", "LD R2, #-9223372036854775808\nDIV R1, R2, #-1\n", RULE_FOLD, false, 2, OP_LD, 0);
    expect_case("constant fold negative shift", "LD R2, #8\nSHL R1, R2, #-1\n", RULE_FOLD, false, 2, OP_LD, 0);
    expect_case("constant fold oversized shift", "LD R2, #8\nSHL R1, R2, #63\n", RULE_FOLD, false, 2, OP_LD, 0);
    expect_case("constant fold negative left operand", "LD R2, #-2\nSHL R1, R2, #1\n", RULE_FOLD, false, 2, OP_LD, 0);
    expect_case("constant fold negative right operand", "LD R2, #-2\nSHR R1, R2, #1\n", RULE_FOLD, false, 2, OP_LD, 0);
}

static void test_control_flow(void) {
    expect_case("jump to next label", "JMP L\nL:\nHALT\n", RULE_NEXT_JUMP, true, 2, OP_LABEL, 1);
    expect_case("jump with intervening instruction", "JMP L\nNOP\nL:\nHALT\n", RULE_NEXT_JUMP, false, 4, OP_JMP, 0);
    expect_case("jump chain", "JMP A\nA:\nJMP B\nB:\nHALT\n", RULE_JUMP_CHAIN, true, 5, OP_JMP, 1);
    expect_case("jump chain loop", "JMP A\nA:\nJMP B\nB:\nJMP A\n", RULE_JUMP_CHAIN, false, 5, OP_JMP, 0);
    expect_case("unreachable code", "JMP L\nNOP\nADD R1, R2, #1\nL:\nHALT\n", RULE_UNREACHABLE, true, 3, OP_JMP, 2);
    expect_case("conditional branch keeps following code", "JZ R0, L\nADD R1, R2, #1\nL:\nHALT\n", RULE_UNREACHABLE, false, 4, OP_JZ, 0);

    Program p = {0}; RewriteLog log = {0}; OptimizeStats stats = {0}; char err[200];
    p = parse("JMP L\nNOP\nL:\nHALT\n");
    if (!optimize(&p, RULE_UNREACHABLE, &log, &stats, err, sizeof(err)) ||
        p.count != 3 || p.items[1].op != OP_LABEL || strcmp(p.items[1].arg[0].name, "L") != 0) {
        fprintf(stderr, "unreachable removal must preserve the branch target label\n"); failures++;
    }
    if (!log.count || log.items[0].line != 2) {
        fprintf(stderr, "unreachable rewrite should retain removed source line\n"); failures++;
    }
    program_free(&p); rewrite_log_free(&log);
}

int main(void) {
    test_redundant_load();
    test_load_copy();
    test_stores();
    test_arithmetic();
    test_control_flow();
    if (failures) { fprintf(stderr, "%d rule test(s) failed\n", failures); return 1; }
    puts("positive and negative tests passed for all optimization rules");
    return 0;
}
