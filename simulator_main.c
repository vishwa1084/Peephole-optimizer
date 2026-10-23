#include "src/simulator.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_LIMIT UINT64_C(1000000)

static void usage(FILE *out) {
    fprintf(out, "Usage: simulator [--limit N] output.bin\n");
}

int main(int argc, char **argv) {
    const char *path = NULL;
    uint64_t limit = DEFAULT_LIMIT;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout); return 0;
        } else if (!strcmp(argv[i], "--limit")) {
            if (++i >= argc || argv[i][0] == '-') { usage(stderr); return 2; }
            errno = 0; char *end = NULL;
            unsigned long long value = strtoull(argv[i], &end, 10);
            if (errno || !end || *end || value == 0) {
                fprintf(stderr, "invalid instruction limit: %s\n", argv[i]); return 2;
            }
            limit = (uint64_t)value;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]); usage(stderr); return 2;
        } else if (path) {
            fprintf(stderr, "only one binary file is allowed\n"); return 2;
        } else path = argv[i];
    }
    if (!path) { usage(stderr); return 2; }
    SimResult result = {0}; char err[256] = {0};
    if (!simulator_execute_file(path, limit, &result, err, sizeof err)) {
        fprintf(stderr, "simulator: %s\n", err);
        return 1;
    }
    simulator_print_result(stdout, &result);
    simulator_result_free(&result);
    return 0;
}
