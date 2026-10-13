#!/bin/sh
# Run from the project root after building (or use `make demo`).
set -eu

printf '\n=== Arithmetic and redundancy ===\n'
./peephole samples/sample1.asm

printf '\n=== Constant folding and arithmetic guards ===\n'
./peephole samples/sample2.asm

printf '\n=== Control flow and loop ===\n'
./peephole samples/sample3.asm
