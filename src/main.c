#include "optimizer.h"
#include "app.h"
#include "encoder.h"
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_operand(FILE *stream, Operand operand) {
    switch (operand.kind) {
    case OPER_REG: fprintf(stream, "R%d", operand.reg); break;
    case OPER_IMM: fprintf(stream, "#%" PRId64, operand.imm); break;
    case OPER_MEM:
    case OPER_LABEL: fputs(operand.name, stream); break;
    default: fputs("<invalid>", stream); break;
    }
}

static void print_program(FILE *stream, const Program *program) {
    for (size_t i = 0; i < program->count; i++) {
        const Instruction *instruction = &program->items[i];
        if (instruction->op == OP_LABEL) {
            fprintf(stream, "%s:\n", instruction->arg[0].name);
            continue;
        }
        fprintf(stream, "  %-4s", opcode_name(instruction->op));
        for (int arg = 0; arg < instruction->argc; arg++) {
            fprintf(stream, arg ? ", " : " ");
            print_operand(stream, instruction->arg[arg]);
        }
        fputc('\n', stream);
    }
}

static size_t instruction_count(const Program *program) {
    size_t count = 0;
    for (size_t i = 0; i < program->count; i++)
        if (program->items[i].op != OP_LABEL) count++;
    return count;
}

static size_t program_cost(const Program *program) {
    size_t total = 0;
    for (size_t i = 0; i < program->count; i++) {
        int item_cost = instruction_cost(&program->items[i]);
        if (item_cost > 0) total += (size_t)item_cost;
    }
    return total;
}

static bool parse_rules(const char *text, unsigned *mask) {
    static const struct { const char *name; unsigned bit; } rules[] = {
        {"load-after-store", RULE_LOAD_AFTER_STORE}, {"load-copy", RULE_LOAD_COPY},
        {"redundant-store", RULE_REDUNDANT_STORE}, {"algebra", RULE_ALGEBRA},
        {"mul-zero", RULE_MUL_ZERO}, {"self-move", RULE_SELF_MOVE},
        {"strength", RULE_STRENGTH}, {"fold", RULE_FOLD},
        {"next-jump", RULE_NEXT_JUMP}, {"jump-chain", RULE_JUMP_CHAIN},
        {"unreachable", RULE_UNREACHABLE}, {"dead-store", RULE_DEAD_STORE},
        {"const-prop", RULE_CONST_PROP}
    };
    if (!text || !*text || !mask) return false;
    *mask = 0;
    const char *part = text;
    for (;;) {
        const char *comma = strchr(part, ',');
        size_t length = comma ? (size_t)(comma - part) : strlen(part);
        if (length == 0) return false;
        if (length == 3 && memcmp(part, "all", 3) == 0) {
            *mask |= RULE_ALL;
        } else {
            bool found = false;
            for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
                if (strlen(rules[i].name) == length && memcmp(part, rules[i].name, length) == 0) {
                    *mask |= rules[i].bit;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (!comma) return true;
        part = comma + 1;
        if (!*part) return false;
    }
}

static void usage(FILE *stream) {
    fprintf(stream,
        "Usage: peephole [options] input.c|input.asm\n"
        "  -o FILE       write optimized target instructions to FILE\n"
        "  --binary FILE write optimized custom-ISA machine code to FILE\n"
        "  --hex FILE    write a hex dump of optimized custom-ISA code\n"
        "  --rules LIST  comma-separated rule names (default: all)\n"
        "  -h, --help    show this help\n"
        "Rules: all, load-after-store, load-copy, redundant-store, algebra,\n"
        "       mul-zero, self-move, strength, fold, next-jump, jump-chain,\n"
        "       unreachable, dead-store\n"
        "       const-prop\n"
        "Examples:\n"
        "  peephole program.asm\n"
        "  peephole samples/input.c\n"
        "  peephole -o optimized.asm program.asm\n"
        "  peephole --rules algebra,fold program.asm\n");
}

static bool write_output_file(const char *path, const char *input, const Program *program) {
    size_t path_len = strlen(path);
    static const size_t suffix_len = sizeof(".tmp.9999") - 1;
    if (path_len > SIZE_MAX - suffix_len - 1) {
        fprintf(stderr, "output path is too long\n");
        return false;
    }
    char *temporary = malloc(path_len + suffix_len + 1);
    if (!temporary) {
        fprintf(stderr, "out of memory while preparing output path\n");
        return false;
    }
    FILE *file = NULL;
    for (unsigned attempt = 0; attempt < 10000; attempt++) {
        snprintf(temporary, path_len + suffix_len + 1, "%s.tmp.%u", path, attempt);
        file = fopen(temporary, "wx");
        if (file) break;
        if (errno != EEXIST) {
            perror(temporary);
            free(temporary);
            return false;
        }
    }
    if (!file) {
        fprintf(stderr, "could not create a unique temporary output beside '%s'\n", path);
        free(temporary);
        return false;
    }

    fprintf(file, "; Optimized output from %s\n", input);
    print_program(file, program);
    bool ok = !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "failed writing temporary output for '%s'\n", path);
        remove(temporary);
        free(temporary);
        return false;
    }
    if (rename(temporary, path) != 0) {
        perror(path);
        remove(temporary);
        free(temporary);
        return false;
    }
    free(temporary);
    return true;
}

static bool clone_program(const Program *source, Program *copy) {
    memset(copy, 0, sizeof(*copy));
    if (!source->count) return true;
    if (source->count > SIZE_MAX / sizeof(*source->items)) return false;
    copy->items = malloc(source->count * sizeof(*source->items));
    if (!copy->items) return false;
    memcpy(copy->items, source->items, source->count * sizeof(*source->items));
    copy->count = copy->capacity = source->count;
    return true;
}

static bool report(FILE *stream, const Program *original, const Program *optimized,
                   const RewriteLog *log, const OptimizeStats *stats) {
    size_t before_cost = program_cost(original);
    size_t after_cost = program_cost(optimized);
    size_t saving = before_cost > after_cost ? before_cost - after_cost : 0;
    fprintf(stream, "Original code:\n");
    print_program(stream, original);
    fprintf(stream, "\nOptimized code:\n");
    print_program(stream, optimized);
    fprintf(stream, "\nInstruction count: %zu -> %zu\n",
            instruction_count(original), instruction_count(optimized));
    fprintf(stream, "Estimated cost: %zu -> %zu\n", before_cost, after_cost);
    fprintf(stream, "Cost savings: %zu absolute (", saving);
    if (before_cost) fprintf(stream, "%.2f%%)\n", 100.0 * (double)saving / (double)before_cost);
    else fprintf(stream, "n/a; original cost is zero)\n");
    fprintf(stream, "Optimization passes: %zu\nRewrites: %zu\n", stats->passes, log->count);
    for (size_t i = 0; i < log->count; i++)
        fprintf(stream, "  %s (line %zu): %s\n", log->items[i].rule,
                log->items[i].line, log->items[i].explanation);
    return !ferror(stream);
}

int peephole_run(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    const char *binary_output = NULL, *hex_output = NULL;
    unsigned rules = RULE_ALL;
    bool output_seen = false;
    bool rules_seen = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (++i == argc) { fprintf(stderr, "-o requires a file\n"); return 2; }
            if (output_seen) { fprintf(stderr, "-o may be specified only once\n"); return 2; }
            output = argv[i];
            output_seen = true;
        } else if (strcmp(argv[i], "--binary") == 0 || strcmp(argv[i], "--hex") == 0) {
            bool binary = strcmp(argv[i], "--binary") == 0;
            if (++i == argc) { fprintf(stderr, "%s requires a file\n", binary ? "--binary" : "--hex"); return 2; }
            const char **destination = binary ? &binary_output : &hex_output;
            if (*destination) { fprintf(stderr, "%s may be specified only once\n", binary ? "--binary" : "--hex"); return 2; }
            *destination = argv[i];
        } else if (strcmp(argv[i], "--rules") == 0) {
            if (rules_seen) { fprintf(stderr, "--rules may be specified only once\n"); return 2; }
            if (++i == argc || !parse_rules(argv[i], &rules)) {
                fprintf(stderr, "invalid --rules list\n"); usage(stderr); return 2;
            }
            rules_seen = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]); usage(stderr); return 2;
        } else if (input) {
            fprintf(stderr, "only one input file is allowed\n"); return 2;
        } else input = argv[i];
    }
    if (!input) { usage(stderr); return 2; }

    const char *extension = strrchr(input, '.');
    if (!extension || (strcmp(extension, ".asm") != 0 && strcmp(extension, ".c") != 0)) {
        fprintf(stderr, "unsupported input type: expected a .c or .asm file\n");
        return 2;
    }

    Program original = {0}, optimized = {0};
    RewriteLog log = {0};
    OptimizeStats stats = {0};
    char err[256];
    bool parsed = strcmp(extension, ".c") == 0
        ? parse_c_file(input, &optimized, err, sizeof(err))
        : parse_file(input, &optimized, err, sizeof(err));
    if (!parsed) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (!clone_program(&optimized, &original)) {
        fprintf(stderr, "out of memory while copying input program\n");
        program_free(&optimized);
        return 1;
    }
    if (!optimize(&optimized, rules, &log, &stats, err, sizeof(err))) {
        fprintf(stderr, "%s\n", err);
        program_free(&optimized); program_free(&original); rewrite_log_free(&log);
        return 1;
    }

    if (output && !write_output_file(output, input, &optimized)) {
        program_free(&optimized); program_free(&original); rewrite_log_free(&log);
        return 1;
    }
    if (binary_output || hex_output) {
        const char *paths[2] = {binary_output, hex_output};
        for (size_t k=0;k<2;k++) if(paths[k]) {
            FILE *f=fopen(paths[k], k==0 ? "wb" : "w");
            if(!f) { perror(paths[k]); program_free(&optimized); program_free(&original); rewrite_log_free(&log); return 1; }
            err[0]='\0';
            bool ok=k==0 ? encode_binary(&optimized,f,err,sizeof err) : write_hex(&optimized,f,err,sizeof err);
            if(fclose(f)!=0) ok=false;
            if(!ok) { fprintf(stderr,"%s\n",err[0]?err:"failed closing encoded output"); program_free(&optimized); program_free(&original); rewrite_log_free(&log); return 1; }
        }
    }
    bool report_ok = report(stdout, &original, &optimized, &log, &stats);
    if (!report_ok) fprintf(stderr, "failed writing report to standard output\n");
    program_free(&optimized);
    program_free(&original);
    rewrite_log_free(&log);
    return report_ok ? 0 : 1;
}
