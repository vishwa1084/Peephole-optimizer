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
            bool label_operand = in.op == OP_JMP ||
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
