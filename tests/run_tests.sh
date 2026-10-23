#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-gcc}
CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2}
CPPFLAGS=${CPPFLAGS:--D_POSIX_C_SOURCE=200809L}
BUILD_DIR=${BUILD_DIR:-"$ROOT/.test-build"}

mkdir -p "$BUILD_DIR"
cd "$ROOT"

# Keep checked-in generated sources synchronized with the Flex/Bison inputs.
if command -v bison >/dev/null 2>&1 && command -v flex >/dev/null 2>&1; then
  bison -d -o src/grammar.tab.c src/grammar.y
  flex -o src/lexer.yy.c src/lexer.l
fi

"$CC" $CPPFLAGS $CFLAGS -I src main.c src/main.c src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c -o main
"$CC" $CPPFLAGS $CFLAGS -I src simulator_main.c src/simulator.c src/isa.c -o simulator
cp main peephole
"$CC" $CPPFLAGS $CFLAGS -I src tests/test_parser.c src/isa.c src/parser.c -o "$BUILD_DIR/test_parser"
"$CC" $CPPFLAGS $CFLAGS -I src tests/test_rules.c src/isa.c src/parser.c src/rules.c src/optimizer.c -o "$BUILD_DIR/test_rules"
"$CC" $CPPFLAGS $CFLAGS -I src tests/test_optimizer.c src/isa.c src/parser.c src/rules.c src/optimizer.c -o "$BUILD_DIR/test_optimizer"
"$CC" $CPPFLAGS $CFLAGS -I src tests/test_c_frontend.c src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c -o "$BUILD_DIR/test_c_frontend"
"$CC" $CPPFLAGS $CFLAGS -I src tests/test_simulator.c src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c src/simulator.c -o "$BUILD_DIR/test_simulator"

"$BUILD_DIR/test_parser"
"$BUILD_DIR/test_rules"
"$BUILD_DIR/test_optimizer"
"$BUILD_DIR/test_c_frontend"
"$BUILD_DIR/test_simulator"
./main samples/input.c > /dev/null
./main samples/input.c --binary "$BUILD_DIR/sample.bin" --hex "$BUILD_DIR/sample.hex" > /dev/null
./simulator "$BUILD_DIR/sample.bin" > "$BUILD_DIR/sim-result.txt"
grep -q '^Return value (R0): 200$' "$BUILD_DIR/sim-result.txt"
[ "$(wc -c < "$BUILD_DIR/sample.bin")" -gt 16 ]
grep -q '^# PHISA v1' "$BUILD_DIR/sample.hex"
bin_bytes=$(od -An -tx1 -v "$BUILD_DIR/sample.bin" | tr -d ' \n')
hex_bytes=$(tail -n +2 "$BUILD_DIR/sample.hex" | tr -d ' \n')
[ "$bin_bytes" = "$hex_bytes" ]
./main samples/sample3.asm --binary "$BUILD_DIR/branches.bin" > /dev/null
[ "$(wc -c < "$BUILD_DIR/branches.bin")" -gt 16 ]
./simulator "$BUILD_DIR/branches.bin" > "$BUILD_DIR/branches-result.txt"
grep -q '^Return value (R0): 0$' "$BUILD_DIR/branches-result.txt"
sh ./tests/cli_test.sh
