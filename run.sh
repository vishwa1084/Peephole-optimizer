#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-gcc}

if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'run.sh: required compiler not found: %s\n' "$CC" >&2
    exit 127
fi

cd "$ROOT"

check_arguments() {
    has_input=0
    has_help=0
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -h|--help) has_help=1; shift ;;
            -o|--rules)
                if [ "$#" -lt 2 ]; then
                    printf 'run.sh: %s requires a value\n' "$1" >&2
                    return 2
                fi
                shift 2
                ;;
            -*) shift ;;
            *)
                if [ ! -f "$1" ]; then
                    printf 'run.sh: input file not found: %s\n' "$1" >&2
                    return 2
                fi
                has_input=1
                shift
                ;;
        esac
    done
    if [ "$has_input" -eq 0 ]; then
        [ "$has_help" -eq 1 ] && return 0
        printf 'Usage: ./run.sh [options] input.c|input.asm\n' >&2
        return 2
    fi
}

check_arguments "$@"

if [ ! -f src/grammar.tab.c ] || [ ! -f src/lexer.yy.c ]; then
    if command -v bison >/dev/null 2>&1 && command -v flex >/dev/null 2>&1; then
        bison -d -o src/grammar.tab.c src/grammar.y
        flex -o src/lexer.yy.c src/lexer.l
    else
        printf 'run.sh: generated C frontend sources are missing; install Flex and Bison to generate them\n' >&2
        exit 1
    fi
fi

"$CC" -std=c11 -Wall -Wextra -Wpedantic -Isrc \
    main.c src/main.c src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c \
    -o main
exec ./main "$@"
