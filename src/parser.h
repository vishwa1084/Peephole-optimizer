#ifndef PARSER_H
#define PARSER_H
#include "isa.h"
typedef struct { Instruction *items; size_t count, capacity; } Program;
void program_free(Program *p);
bool parse_file(const char *path, Program *out, char *err, size_t errcap);
bool parse_text(const char *text, Program *out, char *err, size_t errcap);
bool validate_labels(const Program *p, char *err, size_t errcap);
#endif
