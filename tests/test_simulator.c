#include "../src/encoder.h"
#include "../src/optimizer.h"
#include "../src/simulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool encode_program(const Program *p, unsigned char **bytes, size_t *size) {
    FILE *f = tmpfile(); char err[256];
    if (!f || !encode_binary(p, f, err, sizeof err) || fseek(f, 0, SEEK_END)) {
        if (f) fclose(f);
        fprintf(stderr, "encoding for simulator test failed: %s\n", err);
        return false;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return false; }
    *bytes = malloc((size_t)n);
    if (!*bytes) { fclose(f); return false; }
    *size = (size_t)n;
    bool ok = fread(*bytes, 1, *size, f) == *size;
    fclose(f);
    if (!ok) { free(*bytes); *bytes = NULL; }
    return ok;
}

static bool has_memory(const SimResult *r, const char *name, int64_t value) {
    for (size_t i = 0; i < r->memory_count; ++i)
        if (!strcmp(r->memory[i].name, name)) return r->memory[i].value == value;
    return false;
}

static int check_source(const char *path, int64_t expected) {
    Program original = {0}, optimized = {0}; char err[256] = {0};
    RewriteLog log = {0}; OptimizeStats stats = {0};
    if (!parse_c_file(path, &original, err, sizeof err)) {
        fprintf(stderr, "simulator test parse failed for %s: %s\n", path, err); return 1;
    }
    optimized.items = malloc(original.count * sizeof *optimized.items);
    if (!optimized.items) { program_free(&original); return 1; }
    memcpy(optimized.items, original.items, original.count * sizeof *optimized.items);
    optimized.count = optimized.capacity = original.count;
    if (!optimize(&optimized, RULE_ALL, &log, &stats, err, sizeof err)) {
        fprintf(stderr, "simulator test optimize failed: %s\n", err); goto bad;
    }
    unsigned char *before_bytes = NULL, *after_bytes = NULL;
    size_t before_size = 0, after_size = 0;
    if (!encode_program(&original, &before_bytes, &before_size) ||
        !encode_program(&optimized, &after_bytes, &after_size)) goto bad;
    SimResult before = {0}, after = {0};
    bool before_ok = simulator_execute(before_bytes, before_size, 100000, &before, err, sizeof err);
    if (!before_ok) { fprintf(stderr, "original simulation failed: %s\n", err); goto sim_bad; }
    bool after_ok = simulator_execute(after_bytes, after_size, 100000, &after, err, sizeof err);
    if (!after_ok) { fprintf(stderr, "optimized simulation failed: %s\n", err); goto sim_bad; }
    if (!before.halted || !after.halted || before.registers[0] != expected ||
        after.registers[0] != expected || before.registers[0] != after.registers[0]) {
        fprintf(stderr, "%s: original/optimized return mismatch (%lld / %lld; expected %lld)\n",
                path, (long long)before.registers[0], (long long)after.registers[0], (long long)expected);
        goto sim_bad;
    }
    if (!strcmp(path, "samples/input.c") &&
        (!has_memory(&before, "v002", 200) || !has_memory(&after, "v002", 200))) {
        fprintf(stderr, "sample's result= a*b value was not preserved as 200\n"); goto sim_bad;
    }
    simulator_result_free(&before); simulator_result_free(&after);
    free(before_bytes); free(after_bytes); program_free(&original); program_free(&optimized); rewrite_log_free(&log);
    return 0;
sim_bad:
    simulator_result_free(&before); simulator_result_free(&after);
    free(before_bytes); free(after_bytes);
bad:
    program_free(&original); program_free(&optimized); rewrite_log_free(&log); return 1;
}

static int check_faults(void) {
    Program p = {0}; char err[256]; unsigned char *bytes = NULL; size_t size = 0;
    SimResult result = {0};
    if (!parse_text("LD R0, #1\nloop:\nJMP loop\n", &p, err, sizeof err) ||
        !encode_program(&p, &bytes, &size)) { fprintf(stderr, "loop test setup failed: %s\n", err); program_free(&p); return 1; }
    if (simulator_execute(bytes, size, 5, &result, err, sizeof err) ||
        !strstr(err, "instruction limit")) {
        fprintf(stderr, "infinite loop was not stopped by instruction limit\n"); free(bytes); program_free(&p); return 1;
    }
    free(bytes); bytes = NULL; simulator_result_free(&result); program_free(&p);

    if (!parse_text("LD R0, missing\nHALT\n", &p, err, sizeof err) ||
        !encode_program(&p, &bytes, &size)) { fprintf(stderr, "memory test setup failed: %s\n", err); program_free(&p); return 1; }
    if (simulator_execute(bytes, size, 10, &result, err, sizeof err) ||
        !strstr(err, "uninitialized location")) {
        fprintf(stderr, "uninitialized memory access was not detected\n"); free(bytes); program_free(&p); return 1;
    }
    free(bytes); simulator_result_free(&result); program_free(&p);

    if (!parse_text("LD R0, #10\nDIV R0, R0, #0\nHALT\n", &p, err, sizeof err) ||
        !encode_program(&p, &bytes, &size)) { fprintf(stderr, "divide test setup failed: %s\n", err); program_free(&p); return 1; }
    if (simulator_execute(bytes, size, 10, &result, err, sizeof err) || !strstr(err, "division by zero")) {
        fprintf(stderr, "division by zero was not detected\n"); free(bytes); program_free(&p); return 1;
    }
    free(bytes); simulator_result_free(&result); program_free(&p);

    if (!parse_text("JMP done\ndone:\nHALT\n", &p, err, sizeof err) ||
        !encode_program(&p, &bytes, &size)) {
        fprintf(stderr, "branch-target test setup failed: %s\n", err); program_free(&p); return 1;
    }
    /* A branch payload is a zero-based record index. Change the valid HALT
       index (1) into the out-of-range index 2. */
    bytes[19] = 2;
    if (simulator_execute(bytes, size, 10, &result, err, sizeof err) ||
        !strstr(err, "invalid branch target")) {
        fprintf(stderr, "invalid branch target was not detected: %s\n", err);
        free(bytes); program_free(&p); return 1;
    }
    free(bytes); simulator_result_free(&result); program_free(&p);

    if (!parse_text("HALT\n", &p, err, sizeof err) || !encode_program(&p, &bytes, &size)) {
        fprintf(stderr, "opcode test setup failed: %s\n", err); program_free(&p); return 1;
    }
    bytes[16] = 255;
    if (simulator_execute(bytes, size, 10, &result, err, sizeof err) || !strstr(err, "invalid opcode")) {
        fprintf(stderr, "invalid opcode was not detected\n"); free(bytes); program_free(&p); return 1;
    }
    free(bytes); simulator_result_free(&result); program_free(&p);
    return 0;
}

static int check_isa_operations(void) {
    const char *source =
        "LD R0, #8\nLD R1, #3\nADD R2, R0, R1\nSUB R3, R0, R1\n"
        "MUL R4, R0, R1\nDIV R5, R0, R1\nSHL R6, R1, #2\nSHR R7, R0, #1\n"
        "ST value, R2\nLD R0, value\nCMP R1, R3\nJLT less\n"
        "LD R0, #0\nJMP done\nless:\nLD R0, #77\ndone:\nHALT\n";
    Program p = {0}; char err[256] = {0}; unsigned char *bytes = NULL; size_t size = 0;
    if (!parse_text(source, &p, err, sizeof err) || !encode_program(&p, &bytes, &size)) {
        fprintf(stderr, "ISA operation test setup failed: %s\n", err); program_free(&p); return 1;
    }
    SimResult result = {0};
    if (!simulator_execute(bytes, size, 100, &result, err, sizeof err)) {
        fprintf(stderr, "ISA operation simulation failed: %s\n", err);
        free(bytes); program_free(&p); return 1;
    }
    bool ok = result.registers[0] == 77 && result.registers[2] == 11 &&
        result.registers[3] == 5 && result.registers[4] == 24 &&
        result.registers[5] == 2 && result.registers[6] == 12 &&
        result.registers[7] == 4 && has_memory(&result, "value", 11);
    if (!ok) fprintf(stderr, "arithmetic, memory, CMP/JLT execution result mismatch\n");
    simulator_result_free(&result); free(bytes); program_free(&p);
    return ok ? 0 : 1;
}

static int check_infinite_loop_limit(void) {
    Program original = {0}, optimized = {0}; char err[256] = {0};
    RewriteLog log = {0}; OptimizeStats stats = {0};
    if (!parse_c_file("tests/loop_infinite.c", &original, err, sizeof err)) {
        fprintf(stderr, "infinite C loop failed to parse: %s\n", err); return 1;
    }
    optimized.items = malloc(original.count * sizeof *optimized.items);
    if (!optimized.items) { program_free(&original); return 1; }
    memcpy(optimized.items, original.items, original.count * sizeof *optimized.items);
    optimized.count = optimized.capacity = original.count;
    if (!optimize(&optimized, RULE_ALL, &log, &stats, err, sizeof err)) {
        fprintf(stderr, "infinite C loop optimization failed: %s\n", err);
        program_free(&original); program_free(&optimized); rewrite_log_free(&log); return 1;
    }
    const Program *programs[2] = {&original, &optimized};
    for (size_t i = 0; i < 2; ++i) {
        unsigned char *bytes = NULL; size_t size = 0; SimResult result = {0};
        if (!encode_program(programs[i], &bytes, &size)) {
            program_free(&original); program_free(&optimized); rewrite_log_free(&log); return 1;
        }
        bool ok = simulator_execute(bytes, size, 100, &result, err, sizeof err);
        free(bytes); simulator_result_free(&result);
        if (ok || !strstr(err, "instruction limit")) {
            fprintf(stderr, "infinite C loop was not stopped in %s program: %s\n",
                    i ? "optimized" : "original", err);
            program_free(&original); program_free(&optimized); rewrite_log_free(&log); return 1;
        }
    }
    program_free(&original); program_free(&optimized); rewrite_log_free(&log);
    return 0;
}

int main(void) {
    if (check_source("samples/input.c", 200)) return 1;
    if (check_source("tests/comparisons.c", 7)) return 1;
    if (check_source("tests/if_unknown_after_merge.c", 2)) return 1;
    if (check_source("tests/if_true.c", 30)) return 1;
    if (check_source("tests/if_false.c", 7)) return 1;
    if (check_source("tests/if_else_false.c", -10)) return 1;
    if (check_source("tests/if_nested.c", 42)) return 1;
    if (check_source("tests/loop_while_zero.c", 9)) return 1;
    if (check_source("tests/loop_while_multiple.c", 10)) return 1;
    if (check_source("tests/loop_for_sum.c", 45)) return 1;
    if (check_source("tests/loop_do_once.c", 1)) return 1;
    if (check_source("tests/loop_nested.c", 6)) return 1;
    if (check_source("tests/loop_if_else.c", 5)) return 1;
    if (check_source("tests/loop_condition_exit.c", 4)) return 1;
    if (check_faults() || check_isa_operations() || check_infinite_loop_limit()) return 1;
    puts("simulator tests passed");
    return 0;
}
