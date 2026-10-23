#ifndef ENCODER_H
#define ENCODER_H

#include "parser.h"
#include <stdio.h>

/* Version 1 uses a 16-byte header followed by fixed 32-byte instruction records. */
bool encode_binary(const Program *program, FILE *out, char *err, size_t errcap);
bool write_hex(const Program *program, FILE *out, char *err, size_t errcap);

#endif
