#include "optimizer.h"
#include <stdio.h>

bool optimize(Program *p, unsigned mask, RewriteLog *log, OptimizeStats *stats,
              char *err, size_t errcap) {
    if (!p || !stats) {
        if (err && errcap) snprintf(err, errcap, "invalid optimizer argument");
        return false;
    }
    stats->passes = 0;
    stats->changes = 0;
    if (err && errcap) err[0] = '\0';
    if (!validate_labels(p, err, errcap)) return false;

    const size_t iteration_limit = 1000;
    for (size_t pass = 0; pass < iteration_limit; pass++) {
        size_t changes = 0;
        if (!apply_rules(p, mask, log, &changes)) {
            if (err && errcap && !err[0]) snprintf(err, errcap, "optimization pass failed");
            return false;
        }
        stats->passes++;
        stats->changes += changes;
        if (!validate_labels(p, err, errcap)) return false;
        if (changes == 0) return true;
    }
    if (err && errcap) snprintf(err, errcap, "optimization did not converge after %zu passes", iteration_limit);
    return false;
}
