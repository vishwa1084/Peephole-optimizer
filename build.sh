#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-gcc}
cd "$ROOT"
if command -v bison >/dev/null 2>&1 && command -v flex >/dev/null 2>&1; then
    bison -d -o src/grammar.tab.c src/grammar.y
    flex -o src/lexer.yy.c src/lexer.l
elif [ ! -f src/grammar.tab.c ] || [ ! -f src/lexer.yy.c ]; then
    printf 'build.sh: install flex and bison; generated parser sources are missing\n' >&2
    exit 1
fi
"$CC" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Isrc \
    main.c src/main.c src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c -o main
"$CC" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Isrc \
    simulator_main.c src/simulator.c src/isa.c -o simulator
printf 'Built %s/main and %s/simulator\n' "$ROOT" "$ROOT"
