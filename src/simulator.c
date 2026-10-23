#include "simulator.h"
#include "isa.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER_SIZE = 16, RECORD_SIZE = 32 };
static const unsigned char magic[8] = {'P','H','I','S','A',1,'\r','\n'};

typedef struct {
    Opcode op;
    unsigned argc;
    unsigned char kind[3];
    uint64_t payload[3];
    char memory_name[3][SIMULATOR_MEMORY_NAME_MAX + 1];
} Decoded;

static bool fail(char *err, size_t cap, const char *message) {
    if (cap) snprintf(err, cap, "%s", message);
    return false;
}

static uint64_t get64(const unsigned char *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8U * i);
    return v;
}
static uint32_t get32(const unsigned char *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8U * i);
    return v;
}
static int64_t signed_value(uint64_t bits) {
    int64_t v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

static bool decode(const unsigned char *image, size_t size, Decoded **out,
                   size_t *count_out, char *err, size_t cap) {
    if (!image || size < HEADER_SIZE) return fail(err, cap, "invalid PHISA binary: truncated header");
    if (memcmp(image, magic, 8)) return fail(err, cap, "invalid PHISA binary: bad magic or unsupported version");
    if (image[12] || image[13] || image[14] || image[15])
        return fail(err, cap, "invalid PHISA binary: reserved header bytes are nonzero");
    size_t count = (size_t)get32(image + 8);
    if (count > (SIZE_MAX - HEADER_SIZE) / RECORD_SIZE ||
        size != HEADER_SIZE + count * RECORD_SIZE)
        return fail(err, cap, "invalid PHISA binary: instruction count does not match file size");
    if (count > SIZE_MAX / sizeof(Decoded)) return fail(err, cap, "PHISA binary is too large");
    Decoded *items = count ? calloc(count, sizeof *items) : NULL;
    if (count && !items) return fail(err, cap, "out of memory decoding PHISA binary");
    for (size_t pc = 0; pc < count; ++pc) {
        const unsigned char *rec = image + HEADER_SIZE + pc * RECORD_SIZE;
        Decoded *in = &items[pc];
        in->op = (Opcode)rec[0]; in->argc = rec[1];
        if (in->op < OP_LD || in->op > OP_JNE || in->op == OP_LABEL) {
            free(items); return fail(err, cap, "invalid PHISA binary: invalid opcode");
        }
        if (in->argc > MAX_OPERANDS) {
            free(items); return fail(err, cap, "invalid PHISA binary: invalid operand count");
        }
        for (unsigned a = 0; a < 3; ++a) {
            const unsigned char *slot = rec + 2 + a * 10;
            if (a >= in->argc) {
                for (unsigned b = 0; b < 10; ++b) if (slot[b]) {
                    free(items); return fail(err, cap, "invalid PHISA binary: nonzero unused operand data");
                }
                continue;
            }
            in->kind[a] = slot[0]; in->payload[a] = get64(slot + 1);
            if (in->kind[a] < 1 || in->kind[a] > 4) {
                free(items); return fail(err, cap, "invalid PHISA binary: invalid operand kind");
            }
            if (in->kind[a] == 1 && in->payload[a] > 7) {
                free(items); return fail(err, cap, "invalid PHISA binary: register index out of range");
            }
            if (in->kind[a] == 3) {
                bool nul = false;
                for (unsigned b = 0; b < 8; ++b) {
                    unsigned char c = slot[1 + b];
                    if (!c) nul = true;
                    else if (nul || c > 0x7f) {
                        free(items); return fail(err, cap, "invalid PHISA binary: malformed memory name");
                    }
                    in->memory_name[a][b] = (char)c;
                }
                if (!in->memory_name[a][0]) {
                    free(items); return fail(err, cap, "invalid PHISA binary: empty memory name");
                }
            }
            if (in->kind[a] == 4 && (uint64_t)in->payload[a] >= (uint64_t)count) {
                free(items); return fail(err, cap, "invalid branch target");
            }
        }
        Instruction check = {0};
        check.op = in->op; check.argc = (int)in->argc;
        for (unsigned a = 0; a < in->argc; ++a) {
            Operand *o = &check.arg[a];
            switch (in->kind[a]) {
            case 1: o->kind = OPER_REG; o->reg = (int)in->payload[a]; break;
            case 2: o->kind = OPER_IMM; o->imm = signed_value(in->payload[a]); break;
            case 3: o->kind = OPER_MEM; memcpy(o->name, in->memory_name[a], sizeof in->memory_name[a]); break;
            case 4: o->kind = OPER_LABEL; memcpy(o->name, "target", sizeof "target"); break;
            default: break;
            }
        }
        char why[128];
        if (!isa_validate(&check, why, sizeof why)) {
            free(items); return fail(err, cap, "invalid PHISA binary: invalid operands for opcode");
        }
        for (unsigned a = 0; a < in->argc; ++a) {
            bool target = (in->op == OP_JMP && a == 0) ||
                ((in->op == OP_JZ || in->op == OP_JNZ) && a == 1) ||
                (in->op >= OP_JLT && in->op <= OP_JNE && a == 0);
            if ((in->kind[a] == 4) != target) {
                free(items); return fail(err, cap, "invalid PHISA binary: operand kind does not match opcode");
            }
        }
    }
    *out = items; *count_out = count;
    return true;
}

static bool memory_read(const SimResult *r, const char *name, int64_t *v) {
    for (size_t i = 0; i < r->memory_count; ++i)
        if (!strcmp(r->memory[i].name, name)) { *v = r->memory[i].value; return true; }
    return false;
}
static bool memory_write(SimResult *r, const char *name, int64_t value) {
    for (size_t i = 0; i < r->memory_count; ++i)
        if (!strcmp(r->memory[i].name, name)) { r->memory[i].value = value; return true; }
    if (r->memory_count >= SIZE_MAX / sizeof *r->memory) return false;
    void *p = realloc(r->memory, (r->memory_count + 1) * sizeof *r->memory);
    if (!p) return false;
    r->memory = p;
    memcpy(r->memory[r->memory_count].name, name, strlen(name) + 1);
    r->memory[r->memory_count].value = value;
    ++r->memory_count;
    return true;
}
static void runtime_error(char *err, size_t cap, size_t pc, const char *message) {
    if (cap) snprintf(err, cap, "instruction %zu: %s", pc, message);
}

bool simulator_execute(const unsigned char *image, size_t image_size,
                       uint64_t limit, SimResult *result, char *err, size_t cap) {
    if (!result) return fail(err, cap, "invalid simulator result argument");
    memset(result, 0, sizeof *result);
    if (!limit) return fail(err, cap, "instruction limit must be greater than zero");
    Decoded *program = NULL; size_t count = 0;
    if (!decode(image, image_size, &program, &count, err, cap)) return false;
    bool cmp_valid = false; int cmp = 0; size_t pc = 0;
    while (pc < count) {
        if (result->instructions_executed >= limit) {
            free(program); simulator_result_free(result);
            runtime_error(err, cap, pc, "instruction limit exceeded (possible infinite loop)");
            return false;
        }
        const Decoded *in = &program[pc]; size_t next = pc + 1;
        int64_t x = 0, y = 0; unsigned rd = (unsigned)in->payload[0];
        bool comparison_branch = in->op >= OP_JLT && in->op <= OP_JNE;
        if (cmp_valid && !comparison_branch && in->op != OP_CMP) cmp_valid = false;
        ++result->instructions_executed;
        switch (in->op) {
        case OP_LD:
            if (in->kind[1] == 2) result->registers[rd] = signed_value(in->payload[1]);
            else if (!memory_read(result, in->memory_name[1], &result->registers[rd])) {
                char msg[128]; snprintf(msg, sizeof msg, "invalid memory access: uninitialized location '%s'", in->memory_name[1]);
                free(program); simulator_result_free(result); runtime_error(err, cap, pc, msg); return false;
            }
            break;
        case OP_ST:
            if (!memory_write(result, in->memory_name[0], result->registers[in->payload[1]])) {
                free(program); simulator_result_free(result); runtime_error(err, cap, pc, "out of memory storing memory location"); return false;
            }
            break;
        case OP_MOV: result->registers[rd] = result->registers[in->payload[1]]; break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_SHL: case OP_SHR:
            x = in->kind[1] == 1 ? result->registers[in->payload[1]] : signed_value(in->payload[1]);
            y = in->kind[2] == 1 ? result->registers[in->payload[2]] : signed_value(in->payload[2]);
            if (in->op == OP_ADD && __builtin_add_overflow(x, y, &result->registers[rd])) goto overflow;
            if (in->op == OP_SUB && __builtin_sub_overflow(x, y, &result->registers[rd])) goto overflow;
            if (in->op == OP_MUL && __builtin_mul_overflow(x, y, &result->registers[rd])) goto overflow;
            if (in->op == OP_DIV) {
                if (!y) { free(program); simulator_result_free(result); runtime_error(err, cap, pc, "division by zero"); return false; }
                if (x == INT64_MIN && y == -1) goto overflow;
                result->registers[rd] = x / y;
            }
            if (in->op == OP_SHL || in->op == OP_SHR) {
                if (y < 0 || y > 62 || x < 0) {
                    free(program); simulator_result_free(result); runtime_error(err, cap, pc, "invalid shift value or count (expected nonnegative value and count 0..62)"); return false;
                }
                if (in->op == OP_SHL) {
                    if (x > (INT64_MAX >> y)) goto overflow;
                    result->registers[rd] = x << y;
                } else result->registers[rd] = x >> y;
            }
            break;
        case OP_CMP:
            x = result->registers[in->payload[0]]; y = result->registers[in->payload[1]];
            cmp = (x > y) - (x < y); cmp_valid = true; break;
        case OP_JMP: next = (size_t)in->payload[0]; break;
        case OP_JZ: if (!result->registers[in->payload[0]]) next = (size_t)in->payload[1]; break;
        case OP_JNZ: if (result->registers[in->payload[0]]) next = (size_t)in->payload[1]; break;
        case OP_JLT: case OP_JGT: case OP_JLE: case OP_JGE: case OP_JE: case OP_JNE:
            if (!cmp_valid) {
                free(program); simulator_result_free(result); runtime_error(err, cap, pc, "conditional comparison branch without a preceding CMP"); return false;
            }
            if ((in->op == OP_JLT && cmp < 0) || (in->op == OP_JGT && cmp > 0) ||
                (in->op == OP_JLE && cmp <= 0) || (in->op == OP_JGE && cmp >= 0) ||
                (in->op == OP_JE && cmp == 0) || (in->op == OP_JNE && cmp != 0))
                next = (size_t)in->payload[0];
            cmp_valid = false; break;
        case OP_NOP: break;
        case OP_HALT: result->halted = true; free(program); return true;
        default: free(program); simulator_result_free(result); runtime_error(err, cap, pc, "invalid opcode"); return false;
        }
        if (next > count) {
            free(program); simulator_result_free(result); runtime_error(err, cap, pc, "invalid branch target"); return false;
        }
        pc = next;
        continue;
overflow:
        free(program); simulator_result_free(result); runtime_error(err, cap, pc, "signed arithmetic overflow"); return false;
    }
    free(program);
    return fail(err, cap, "execution reached end of program without HALT");
}

bool simulator_execute_file(const char *path, uint64_t limit, SimResult *result,
                            char *err, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) { if (cap) snprintf(err, cap, "cannot open '%s'", path); return false; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return fail(err, cap, "cannot seek input binary"); }
    long length = ftell(f);
    if (length < 0 || (uintmax_t)length > SIZE_MAX) { fclose(f); return fail(err, cap, "binary file is too large"); }
    rewind(f);
    size_t size = (size_t)length;
    unsigned char *data = size ? malloc(size) : NULL;
    if (size && !data) { fclose(f); return fail(err, cap, "out of memory reading binary"); }
    bool ok = !size || fread(data, 1, size, f) == size;
    if (fclose(f) != 0) ok = false;
    if (!ok) { free(data); return fail(err, cap, "failed reading binary file"); }
    ok = simulator_execute(data, size, limit, result, err, cap);
    free(data); return ok;
}

void simulator_result_free(SimResult *result) {
    if (!result) return;
    free(result->memory); result->memory = NULL; result->memory_count = 0;
}
void simulator_print_result(FILE *out, const SimResult *result) {
    fprintf(out, "Execution complete: %llu instruction(s)\n", (unsigned long long)result->instructions_executed);
    for (unsigned i = 0; i < 8; ++i)
        fprintf(out, "R%u = %lld\n", i, (long long)result->registers[i]);
    fprintf(out, "Memory:\n");
    for (size_t i = 0; i < result->memory_count; ++i)
        fprintf(out, "  %s = %lld\n", result->memory[i].name, (long long)result->memory[i].value);
    fprintf(out, "Return value (R0): %lld\n", (long long)result->registers[0]);
}
