#!/bin/sh
# Run from the project root after building with run.sh, GCC, or Make.
set -eu

if [ -x ./main ]; then
  app=./main
elif [ -x ./peephole ]; then
  app=./peephole
else
  echo 'Build the application first with ./run.sh or the direct GCC command.' >&2
  exit 1
fi

printf '\n=== Arithmetic and redundancy ===\n'
"$app" samples/sample1.asm

printf '\n=== Constant folding and arithmetic guards ===\n'
"$app" samples/sample2.asm

printf '\n=== Control flow and loop ===\n'
"$app" samples/sample3.asm
