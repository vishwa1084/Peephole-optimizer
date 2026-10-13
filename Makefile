CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
SRC = src/isa.c src/parser.c src/rules.c src/optimizer.c
OBJ = $(SRC:.c=.o)
TESTS = test_parser test_rules test_optimizer

.PHONY: all build clean test sanitize demo
all: peephole
build: peephole
peephole: src/main.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test_parser: tests/test_parser.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test_rules: tests/test_rules.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test_optimizer: tests/test_optimizer.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test: $(TESTS) peephole
	./test_parser
	./test_rules
	./test_optimizer
	sh ./tests/cli_test.sh
demo: peephole
	sh ./scripts/demo.sh
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' all $(TESTS)
	ASAN_OPTIONS=detect_leaks=0 ./test_parser
	ASAN_OPTIONS=detect_leaks=0 ./test_rules
	ASAN_OPTIONS=detect_leaks=0 ./test_optimizer
	ASAN_OPTIONS=detect_leaks=0 sh ./tests/cli_test.sh
clean:
	rm -f peephole $(TESTS) *.o src/*.o tests/*.o
