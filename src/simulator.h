#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SIMULATOR_MEMORY_NAME_MAX 8

typedef struct {
    int64_t registers[8];
    struct {
        char name[SIMULATOR_MEMORY_NAME_MAX + 1];
        int64_t value;
    } *memory;
    size_t memory_count;
    uint64_t instructions_executed;
    bool halted;
} SimResult;

bool simulator_execute(const unsigned char *image, size_t image_size,
                       uint64_t instruction_limit, SimResult *result,
                       char *err, size_t errcap);
bool simulator_execute_file(const char *path, uint64_t instruction_limit,
                            SimResult *result, char *err, size_t errcap);
void simulator_result_free(SimResult *result);
void simulator_print_result(FILE *out, const SimResult *result);

#endif
