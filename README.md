# Peephole Optimizer for a Simple Target Instruction Set (PHISA v1)

**Compiler Design Lab Project**  
A modular compiler frontend, fixed-point peephole optimizer, binary encoder, and virtual machine simulator for a custom teaching instruction set architecture (**PHISA v1**).

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Features](#2-features)
3. [Prerequisites](#3-prerequisites)
4. [Clone and Build](#4-clone-and-build)
5. [How to Run the Compiler](#5-how-to-run-the-compiler)
6. [How to Run the PHISA Simulator](#6-how-to-run-the-phisa-simulator)
7. [How to Run Tests](#7-how-to-run-tests)
8. [Project Structure](#8-project-structure)
9. [Architecture and Compilation Workflow](#9-architecture-and-compilation-workflow)
10. [End-to-End Example](#10-end-to-end-example)
11. [Limitations and Future Improvements](#11-limitations-and-future-improvements)

---

## 1. Project Overview

### What is a Peephole Optimizer?
A **peephole optimizer** is a compiler optimization technique that operates on a small, sliding window (a "peephole") of target instructions. Rather than performing heavyweight global dataflow analyses across an entire function or compilation unit, a peephole optimizer inspects short contiguous instruction sequences to identify and replace redundant, inefficient, or unneeded operations with equivalent, faster, or shorter instruction sequences.

### What This Project Implements
This project implements an end-to-end teaching compiler pipeline and optimizer written in C11:
1. **Frontend:** Parses target assembly directly or compiles a defined subset of C using Flex (lexer) and Bison (LALR parser).
2. **Intermediate/Target Representation:** Translates AST nodes or assembly text into an internal array of structured instruction records with explicit operand types and source line tracking.
3. **Peephole Optimizer:** A multi-pass, fixed-point rewrite engine applying 13 distinct optimization rules (algebraic identities, strength reduction, dead stores, redundant loads/stores, constant folding, constant propagation, and control-flow cleanup).
4. **Binary & Hex Encoder:** Serializes optimized instructions into **PHISA v1** custom bytecode or an annotated hexadecimal dump.
5. **Virtual Machine Simulator:** Decodes and executes the custom binary format, tracking register states, named scalar memory cells, and execution step limits.

### What is PHISA v1 and Why is it Used?
**PHISA v1** (*Peephole Instruction Set Architecture, Version 1*) is a simple 64-bit register-memory instruction set created specifically for teaching compiler backend construction:
- **Predictable Cost Model:** Every instruction has a deterministic, static cost formula based on its memory and immediate operand count.
- **Architectural Simplicity:** Eight 64-bit general-purpose registers (`R0` through `R7`), explicit 3-address arithmetic (`OP Rd, Rs, Rt/#imm`), condition-code comparison branches (`CMP` + `Jcc`), and distinct named scalar memory locations without complex addressing modes or aliasing.
- **Pedagogical Clarity:** Eliminates the immense complexity, legacy quirks, multi-byte variable-length encodings, calling conventions, and ABI requirements of real-world hardware (x86-64 / ARM64), allowing students to focus purely on the core algorithms of code generation and peephole rewriting.

### Difference Between Custom Bytecode and Native Machine Code
| Dimension | PHISA v1 Bytecode | Native Machine Code (x86-64 / ARM64) |
|---|---|---|
| **Target Execution** | Software interpreter / VM (`./simulator`) | Direct CPU hardware execution |
| **Instruction Encoding** | Fixed 32-byte record with 16-byte magic header | Variable-length (1–15 bytes x86) or 32-bit fixed (ARM) |
| **Memory Model** | Named scalar memory slots (e.g., `v000`, `total`) | Linear virtual address space (pointers, stack pointers, offsets) |
| **Host Executability** | Not an ELF/Mach-O/PE executable; cannot be run by the OS kernel | OS-native executable loaded directly by kernel |
| **Safety Guarantees** | Sandboxed in simulator with instruction count limits and bounds checking | Raw hardware access subject to OS page faults and signals |

---

## 2. Features

### Assembly Input Support
- Line-by-line assembly parsing with full whitespace, comment (`;` and `!`), and case tolerance.
- Strict validation of opcodes, register ranges (`R0`–`R7`), signed 64-bit immediates (`#imm`), valid symbol/label identifiers, duplicate labels, and undefined branch destinations.
- Accurate line-number tracking preserved through the entire compilation and rewrite pipeline for error diagnostics and reporting.

### Supported C Subset
The C frontend parses a valid single-function C translation unit (`int main() { ... }`):
- **Data Types:** 64-bit signed integers (`int`).
- **Statements:**
  - Local declarations with optional initializers (`int a;`, `int x = 10, y = 20;`).
  - Assignments (`a = b + 5;`).
  - Arithmetic expressions with operator precedence and parentheses (`+`, `-`, `*`, `/`, unary `-`, unary `+`).
  - Conditionals: `if (cond) stmt` and `if (cond) stmt else stmt` (including nested blocks).
  - Loops: `while (cond) stmt`, `do stmt while (cond);`, and `for (init; cond; update) stmt`.
  - Scoping: Block scoping (`{ ... }`) with lexical shadowing and unique target variable allocation (`v000`, `v001`, ...).
  - Relational conditions: `<`, `>`, `==`, `!=`, `<=`, `>=`, and truthy integer expressions (`if (x)`).
  - Return statements: `return expr;` (evaluates result into `R0` and emits `HALT`).
- **Semantic Error Reporting:** Reports undeclared variables, duplicate declarations in the same scope, constant divide-by-zero, deep expression nesting (>7 registers), and unsupported C constructs with clear source line numbers.

### Available Optimization Rules
The optimizer implements 13 distinct rules that can be enabled together or selectively:

| Rule Name | CLI Flag Identifier | Description / Transformation |
|---|---|---|
| **Redundant Load After Store** | `load-after-store` | Eliminates `LD R, x` immediately following `ST x, R` since `R` already holds the value. |
| **Load to Register Copy** | `load-copy` | Converts `ST x, R1` followed by `LD R2, x` into `MOV R2, R1`. |
| **Redundant Store** | `redundant-store` | Removes duplicate stores `ST x, R` when `x` already holds `R` and neither has changed. |
| **Algebraic Identity** | `algebra` | Simplifies identity operations: `ADD Rd, Rs, #0`, `SUB Rd, Rs, #0`, and `MUL Rd, Rs, #1` become `MOV Rd, Rs`. |
| **Multiply by Zero** | `mul-zero` | Replaces `MUL Rd, Rs, #0` with `LD Rd, #0`. |
| **Self-Move Elimination** | `self-move` | Removes no-op register copies: `MOV R, R`. |
| **Strength Reduction** | `strength` | Converts multiplication by positive powers of two (`MUL Rd, Rs, #2^k`) to arithmetic shifts (`SHL Rd, Rs, #k`) when proven safe against overflow. |
| **Constant Folding** | `fold` | Evaluates arithmetic on known immediate/register constants at compile time with overflow, divide-by-zero, and shift bounds protection. |
| **Jump to Next Label** | `next-jump` | Deletes `JMP L` when label `L:` immediately follows the jump. |
| **Jump Chaining** | `jump-chain` | Short-circuits jump trampolines (`JMP A` where `A: JMP B`) to jump directly to `B`, detecting and rejecting infinite cycles. |
| **Unreachable Code** | `unreachable` | Removes dead code following unconditional jumps (`JMP`, `HALT`) up to the next jump target. |
| **Dead Store Elimination** | `dead-store` | Eliminates `ST x, R1` when a subsequent `ST x, R2` overwrites `x` before any read or control-flow merge. |
| **Constant Propagation & Branch Folding** | `const-prop` | Propagates known constants across memory and registers within basic blocks, resolves static branches (`CMP` + `Jcc`), and prunes unreachable control-flow blocks. |

### Optimization Passes and Fixed-Point Behavior
- Optimization rules are executed iteratively in passes.
- A rewrite in one pass (such as an algebraic simplification to `MOV`) often unlocks further optimizations in the next pass (such as constant folding or dead-store elimination).
- The optimizer repeats passes until reaching a **fixed point** (a pass that produces zero rewrites).
- A hard guard limit of **1,000 passes** prevents infinite oscillation in the event of conflicting cyclic rules.
- Labels and branch targets are re-validated after every pass to guarantee code integrity.

### PHISA Binary and Hexadecimal Output
- **Binary (`--binary FILE`):** Serializes code into a compact, relocatable binary representation with an 8-byte magic header (`PHISA\x01\r\n`), 32-bit instruction count, and 32-byte fixed records per executable instruction.
- **Hex Dump (`--hex FILE`):** Generates human-readable, byte-for-byte hex representations with an explanatory header, suitable for inspection, diffing, and grading.

### Virtual Machine Simulator
- Reference execution environment (`./simulator`) for PHISA v1 binaries.
- Decodes binary records, executes instructions in an internal loop, and updates 8 virtual 64-bit registers.
- Dynamically allocates and updates named scalar memory cells.
- Tracks execution step limits (default: 1,000,000 instructions; configurable via `--limit N`).
- Traps on division by zero, signed 64-bit arithmetic overflow, negative shift counts, out-of-bounds branches, loads from uninitialized memory, or missing `HALT`.
- On completion, prints total instruction count, registers `R0`–`R7`, all memory values, and program return code (`R0`).

---

## 3. Prerequisites

The project is written in standard C11 and POSIX shell, with zero external runtime dependencies.

### Dependencies
- **GCC** (or Clang) supporting `-std=c11`.
- **GNU Make** (version 3.81 or later).
- **Flex** (version 2.6 or later, for generating lexer from `src/lexer.l`).
- **Bison** (version 3.0 or later, for generating parser from `src/grammar.y`).

> **Note on Flex/Bison:** The repository comes with pre-generated C snapshots (`src/lexer.yy.c`, `src/grammar.tab.c`, and `src/grammar.tab.h`). The project builds out-of-the-box with GCC alone even if Flex and Bison are not installed on the system.

### Installation on Ubuntu / Debian
```sh
sudo apt-get update
sudo apt-get install -y build-essential flex bison
```

---

## 4. Clone and Build

### Step 1: Clone the Repository
```sh
git clone https://github.com/vishwa1084/Peephole-optimizer.git
cd Peephole-optimizer
```

### Step 2: Build Using Make (Recommended)
Build both the compiler (`peephole`) and the simulator (`simulator`):
```sh
make
```

### Clean Rebuild
To remove all object files, compiled binaries, and temporary test builds:
```sh
make clean
make
```

### Alternative Build: Shell Script (`./build.sh`)
If `make` is not available, you can build directly using the standalone build script:
```sh
./build.sh
```
This builds `./main` (and copies/links to `./peephole`) and `./simulator`. If Flex and Bison are installed, it automatically regenerates the parser and lexer snapshots.

### Alternative Build: Direct GCC Command
You can build the entire project in two single GCC invocations without any build scripts:
```sh
# Build the compiler
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -Isrc \
  main.c src/main.c src/isa.c src/parser.c src/rules.c src/optimizer.c src/encoder.c \
  -o peephole

# Build the simulator
gcc -std=c11 -Wall -Wextra -Wpedantic -O2 -Isrc \
  simulator_main.c src/simulator.c src/isa.c \
  -o simulator
```

---

## 5. How to Run the Compiler

The compiler executable is `./peephole` (or `./main`).

### Command-Line Synopsis
```sh
./peephole [options] input.c|input.asm
```

### Available Options
| Option | Description |
|---|---|
| `-o FILE` | Write the optimized assembly instructions to `FILE`. (Atomic write via temporary file). |
| `--binary FILE` | Serialize and write the optimized instructions as a PHISA v1 binary file. |
| `--hex FILE` | Write a byte-for-byte formatted hexadecimal dump of the PHISA v1 binary. |
| `--rules LIST` | Comma-separated list of rule names to apply (default: `all`). |
| `-h`, `--help` | Display the usage message and list of available rules. |

### Running on Included Samples

#### 1. Run on an Assembly Sample
```sh
./peephole samples/sample1.asm
```
Prints the before-and-after assembly, instruction counts, static cost comparison, pass count, and a line-numbered rewrite log.

#### 2. Run on a C Program
```sh
./peephole samples/input.c
```
Parses `input.c`, translates the AST to PHISA instructions, runs the fixed-point optimizer, and reports results.

#### 3. Select Specific Optimization Rules
```sh
# Only apply algebraic identities and constant folding
./peephole --rules algebra,fold samples/sample2.asm

# Only apply constant propagation and branch elimination
./peephole --rules const-prop samples/input.c
```

#### 4. Save Optimized Assembly and Binary Encodings
```sh
./peephole samples/input.c \
  -o optimized.asm \
  --binary output.bin \
  --hex output.hex
```

---

## 6. How to Run the PHISA Simulator

The simulator (`./simulator`) executes PHISA v1 binary files produced by the compiler.

### Command-Line Synopsis
```sh
./simulator [--limit N] binary.bin
```

### Options
- `--limit N`: Maximum number of instructions to execute before terminating (default: `1000000`). Protects against infinite loops in user code.
- `-h`, `--help`: Show usage message.

### Execution Example
```sh
# 1. Compile C code to PHISA binary
./peephole samples/input.c --binary output.bin

# 2. Execute the binary in the simulator
./simulator output.bin
```

### Simulator Output Format
```text
Execution complete: 13 instruction(s)
R0 = 200
R1 = 20
R2 = 0
R3 = 0
R4 = 0
R5 = 0
R6 = 0
R7 = 0
Memory:
  v000 = 10
  v001 = 20
  v002 = 200
Return value (R0): 200
```
- `Execution complete`: Number of instructions retired.
- `R0`–`R7`: Final state of all eight 64-bit registers.
- `Memory`: Final values of all allocated scalar memory cells.
- `Return value (R0)`: Integer returned by `main()` via register `R0`.

---

## 7. How to Run Tests

The repository includes a comprehensive, multi-layer test suite covering the parser, individual optimization rules, optimizer driver, C frontend, simulator, and CLI integration.

### Run All Tests via Make
```sh
make test
```
This builds and executes all five unit test binaries and the CLI integration suite:
1. `test_parser`: Tests assembly syntax validation, duplicate labels, undefined labels, operand kinds, and diagnostics.
2. `test_rules`: Exhaustive positive and negative unit test cases for every one of the 13 peephole rules.
3. `test_optimizer`: Multi-pass convergence, jump chaining, loop detection, and fixed-point limit guards.
4. `test_c_frontend`: C grammar parsing, AST generation, semantic checks (duplicate declarations, undeclared variables, division by zero), loops, and scoping.
5. `test_simulator`: Instruction decoding, memory stores/loads, condition flag checks, overflow traps, and step limits.
6. `cli_test.sh`: End-to-end integration test validating CLI flags, error codes, and report contents.

### Run All Tests via Standalone Script (`tests/run_tests.sh`)
```sh
sh tests/run_tests.sh
```
This script creates a clean sandbox in `.test-build/`, compiles all test binaries, executes the entire unit suite, compiles and encodes sample programs, verifies hexadecimal vs binary consistency, executes programs in the simulator, checks return codes, and runs `cli_test.sh`.

### Memory Safety & Sanitizer Tests
To run all tests with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan):
```sh
make sanitize
```

### Verification Results
All tests in the repository pass with 0 errors:
```text
parser tests passed
positive and negative tests passed for all optimization rules
all C tests passed
C frontend tests passed
simulator tests passed
CLI integration tests passed
```

---

## 8. Project Structure

```text
peephole_optimizer/
├── Makefile                    # Primary build system (make, make test, make clean, make sanitize)
├── build.sh                    # Standalone GCC build script
├── run.sh                      # One-command compile-and-run wrapper
├── .gitignore                  # Git ignore rules for build artifacts and temp files
├── README.md                   # Complete project documentation
├── presentation_material.md    # Lab presentation slides and evaluation notes
│
├── main.c                      # Root entry point delegating to src/app.h
├── simulator_main.c            # Standalone CLI entry point for the PHISA simulator
│
├── src/                        # Core compiler and simulator implementation
│   ├── app.h                   # Application runner interface (peephole_run)
│   ├── isa.h / isa.c           # PHISA v1 instruction set, opcode definitions, cost model
│   ├── parser.h / parser.c     # Assembly parser, label validator, and AST lowering
│   ├── rules.h / rules.c       # 13 peephole optimization rules and rewrite log
│   ├── optimizer.h / optimizer.c # Fixed-point optimization driver loop
│   ├── encoder.h / encoder.c   # PHISA v1 binary and hexadecimal encoder
│   ├── simulator.h / simulator.c # Virtual machine interpreter and execution runtime
│   ├── c_ast.h                 # Abstract Syntax Tree nodes for the C frontend
│   ├── lexer.l                 # Flex lexer specification for C subset
│   ├── grammar.y               # Bison grammar specification for C subset
│   ├── lexer.yy.c              # Pre-generated Flex lexer snapshot
│   ├── grammar.tab.c           # Pre-generated Bison parser snapshot
│   └── grammar.tab.h           # Pre-generated Bison token definitions
│
├── samples/                    # Example programs for testing and demonstrations
│   ├── input.c                 # Primary C sample: conditionals, arithmetic, returns 200
│   ├── input2.c / input3.c     # Additional C samples
│   ├── while_loop.c            # C sample demonstrating while loop
│   ├── do_while_loop.c         # C sample demonstrating do-while loop
│   ├── for_loop.c              # C sample demonstrating for loop
│   ├── sample1.asm             # Assembly sample: redundant loads/stores, algebra, strength
│   ├── sample2.asm             # Assembly sample: constant folding and overflow guards
│   └── sample3.asm             # Assembly sample: control flow, jump trampolines, loops
│
├── scripts/
│   └── demo.sh                 # Demonstration script executing sample1, sample2, and sample3
│
└── tests/                      # Unit tests, integration tests, and test fixtures
    ├── run_tests.sh            # Complete end-to-end automated test runner
    ├── cli_test.sh             # Command-line integration test script
    ├── test_parser.c           # Unit tests for assembly parser
    ├── test_rules.c            # Unit tests for all 13 optimization rules
    ├── test_optimizer.c        # Unit tests for optimizer driver and fixed-point loop
    ├── test_c_frontend.c       # Unit tests for C lexer, parser, and code generation
    ├── test_simulator.c        # Unit tests for binary decoder and VM simulator
    └── *.c                     # 29 specific C frontend test fixtures (scoping, errors, loops)
```

---

## 9. Architecture and Compilation Workflow

The compilation pipeline transforms source code into simulated execution through a sequence of modular stages:

```
[ C Source (*.c) ]                     [ Assembly (*.asm) ]
        |                                       |
        v                                       |
   Flex Lexer (lexer.l)                         |
        |                                       |
        v                                       |
   Bison Parser (grammar.y)                     |
        |                                       |
        v                                       |
   C AST (c_ast.h)                              |
        |                                       |
        v                                       |
 Code Generation (parser.c)                     |
        |                                       |
        +------------------+--------------------+
                           |
                           v
              [ Raw Instruction List (Program) ]
                           |
                           v
              ISA Validator (isa.c)
                           |
                           v
              +----------------------------+
              |   Peephole Optimizer Loop  |<----+
              |   (optimizer.c / rules.c)  |     | Passes repeat
              |   - 13 Optimization Rules  |-----+ until fixed point
              +----------------------------+
                           |
             [ Optimized Instruction List ]
                           |
             +-------------+-------------+
             |                           |
             v                           v
     Text Formatter              Binary Encoder (encoder.c)
     (-o output.asm)             (--binary / --hex)
                                         |
                                         v
                                  [ PHISA v1 Binary ]
                                         |
                                         v
                                   VM Simulator (simulator.c)
                                   (Executes and returns R0)
```

### Module Responsibilities
1. **Frontend (`src/lexer.l`, `src/grammar.y`):**
   Converts C source text into an Abstract Syntax Tree (`CStmt`, `CExpr`, `CDecl`).
2. **Code Generator (`src/parser.c`):**
   Lowers AST nodes into three-address target instructions (`Instruction`). Allocates unique scalar memory symbols (`v000`, `v001`, ...) for variables and generates condition labels.
3. **Assembly Parser (`src/parser.c`):**
   Directly parses target assembly, tokenizing instructions, registers, immediates, and labels.
4. **ISA Specification & Cost Model (`src/isa.c`):**
   Validates instruction structures, operand combinations, register bounds (0–7), and computes the static cost metric ($1 + \text{immediates} + \text{memory operands}$).
5. **Optimizer (`src/optimizer.c`, `src/rules.c`):**
   Iterates through the instruction array, applying pattern rewrites until no changes occur. Tracks all rewrites with source line numbers in `RewriteLog`.
6. **Encoder (`src/encoder.c`):**
   Calculates executable instruction offsets, resolves label branch destinations, and writes the 16-byte header and 32-byte records.
7. **Simulator (`src/simulator.c`):**
   Decodes binary records and simulates an isolated CPU with registers `R0`–`R7`, a hash table of named memory cells, and flag registers.

---

## 10. End-to-End Example

Here is a verified walkthrough compiling, optimizing, encoding, and simulating the included sample `samples/input.c`.

### 1. View the C Source Code
```c
// samples/input.c
int main() {
    int a = 10;
    int b = 20;
    int c;
    if (a < b) {
        c = a * b;
    } else {
        c = a - b;
    }
    return c;
}
```

### 2. Compile and Optimize
Run `./peephole` to inspect the optimization report and write output files:
```sh
./peephole samples/input.c -o optimized.asm --binary output.bin --hex output.hex
```

### 3. Optimization Report Output
```text
Original code:
  LD   R0, #10
  ST   v000, R0
  LD   R0, #20
  ST   v001, R0
  LD   R0, v000
  LD   R1, v001
  CMP  R0, R1
  JGE  L0_else
  LD   R0, v000
  LD   R1, v001
  MUL  R0, R0, R1
  ST   v002, R0
  JMP  L0_end
L0_else:
  LD   R0, v000
  LD   R1, v001
  SUB  R0, R0, R1
  ST   v002, R0
L0_end:
  LD   R0, v002
  HALT

Optimized code:
  LD   R0, #10
  ST   v000, R0
  LD   R0, #20
  ST   v001, R0
  LD   R0, #10
  LD   R1, #20
  CMP  R0, R1
  LD   R0, #10
  LD   R1, #20
  LD   R0, #200
  ST   v002, R0
L0_end:
  LD   R0, v002
  HALT

Instruction count: 19 -> 13
Estimated cost: 32 -> 24
Cost savings: 8 absolute (25.00%)
Optimization passes: 3
Rewrites: 12
  constant-propagation (line 6): replaced a load with a value known from an earlier store
  constant-propagation (line 6): replaced a load with a value known from an earlier store
  constant-branch (line 6): removed a conditional branch proven not to be taken
  constant-propagation (line 7): replaced a load with a value known from an earlier store
  constant-propagation (line 7): replaced a load with a value known from an earlier store
  arithmetic-constant-folding (line 7): folded arithmetic using propagated constants with overflow and division checks
  unreachable-block (line 6): removed instruction outside every reachable control-flow path
  unreachable-block (line 9): removed instruction outside every reachable control-flow path
  unreachable-block (line 9): removed instruction outside every reachable control-flow path
  unreachable-block (line 9): removed instruction outside every reachable control-flow path
  unreachable-block (line 9): removed instruction outside every reachable control-flow path
  jump-to-next-label (line 6): removed jump to the immediately following label
```

### 4. Inspect the Hexadecimal Dump
```sh
head -n 5 output.hex
```
```text
# PHISA v1: 16-byte header, then 32-byte little-endian records
50 48 49 53 41 01 0d 0a 0d 00 00 00 00 00 00 00
02 02 01 00 00 00 00 00 00 00 00 00 02 0a 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
03 02 03 76 30 30 30 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
02 02 01 00 00 00 00 00 00 00 00 00 02 14 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
- Line 2: 16-byte header (`50 48 49 53 41 01 0d 0a` = magic, `0d 00 00 00` = 13 instructions).
- Line 3: 32-byte record for `LD R0, #10` (Opcode `02`, `argc=02`, operand 0 = `REG R0`, operand 1 = `IMM 10`).

### 5. Execute in the Simulator
```sh
./simulator output.bin
```
```text
Execution complete: 13 instruction(s)
R0 = 200
R1 = 20
R2 = 0
R3 = 0
R4 = 0
R5 = 0
R6 = 0
R7 = 0
Memory:
  v000 = 10
  v001 = 20
  v002 = 200
Return value (R0): 200
```
The program successfully executed and returned `200` ($10 \times 20$).

---

## 11. Limitations and Future Improvements

### Current Implementation Limitations
- **Single Function Only:** Exactly one `int main() { ... }` function is supported. Function definitions, arguments, function calls, and recursion are not implemented.
- **Type System:** Only signed 64-bit integer values (`int`). No support for floating-point (`float`, `double`), characters (`char`), strings, or aggregate types (`struct`, `union`).
- **Memory Model:** Scalar named variables only. There are no pointers (`*p`), addresses (`&x`), arrays (`arr[i]`), or heap allocations (`malloc`).
- **Control Flow Constructs:** Supports `if`, `if-else`, `while`, `do-while`, and `for`. Constructs such as `switch`, `case`, `break`, `continue`, and `goto` are explicitly diagnosed as unsupported.
- **Logical Operators:** Short-circuit logical operators (`&&`, `||`) and the ternary operator (`?:`) are not implemented.
- **Optimization Scope:** Peephole rewrites and constant propagation operate strictly within straight-line basic blocks and across simple branch trees. There is no global SSA (Static Single Assignment) form, loop-invariant code motion, or interprocedural analysis.
- **Host Execution:** PHISA v1 binary files are not native ELF binaries and cannot be executed directly by the host operating system.

### Potential Future Improvements
- **Global Dataflow Analysis:** Introduce control-flow graphs (CFG) with Dominator Trees and SSA form for global value numbering and loop invariant hoisting.
- **Register Allocation:** Implement graph coloring (Chaitin-Briggs) or linear scan register allocation to allow arbitrary expression complexity without spilling to named scalar memory.
- **Array and Pointer Support:** Extend the ISA with base + offset addressing (`LD Rd, [Rs + #offset]`) to support stack frames, arrays, and pointer dereferencing.
- **Native Code Generation:** Add an x86-64 or ARM64 assembly backend to compare custom PHISA bytecode performance against native machine execution.
