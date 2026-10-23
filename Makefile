CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
SRC = src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c
OBJ = $(SRC:.c=.o)
TESTS = test_parser test_rules test_optimizer test_c_frontend test_simulator

.PHONY: all build clean test sanitize demo
all: peephole main simulator
build: peephole main simulator
peephole: main.o src/main.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
main: peephole
	cp -f peephole main
main.o: main.c src/app.h
src/main.o: src/app.h
test_parser: tests/test_parser.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test_rules: tests/test_rules.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test_optimizer: tests/test_optimizer.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test_c_frontend: tests/test_c_frontend.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
simulator: simulator_main.o src/simulator.o src/isa.o
	$(CC) $(LDFLAGS) -o $@ $^
test_simulator: tests/test_simulator.o src/simulator.o $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
test: $(TESTS) peephole simulator
	./test_parser
	./test_rules
	./test_optimizer
	./test_c_frontend
	./test_simulator
	sh ./tests/cli_test.sh
demo: peephole
	sh ./scripts/demo.sh
sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' all $(TESTS)
	ASAN_OPTIONS=detect_leaks=0 ./test_parser
	ASAN_OPTIONS=detect_leaks=0 ./test_rules
	ASAN_OPTIONS=detect_leaks=0 ./test_optimizer
	ASAN_OPTIONS=detect_leaks=0 ./test_c_frontend
	ASAN_OPTIONS=detect_leaks=0 ./test_simulator
	ASAN_OPTIONS=detect_leaks=0 sh ./tests/cli_test.sh
clean:
	rm -f main peephole simulator $(TESTS) *.o src/*.o tests/*.o
	rm -rf .test-build
