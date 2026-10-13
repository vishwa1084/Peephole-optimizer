#ifndef OPTIMIZER_H
#define OPTIMIZER_H
#include "rules.h"
typedef struct { size_t passes, changes; } OptimizeStats;
bool optimize(Program *p,unsigned mask,RewriteLog *log,OptimizeStats *stats,char *err,size_t errcap);
#endif
