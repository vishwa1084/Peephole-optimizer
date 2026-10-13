# Peephole Optimizer

**Compiler Design Lab project** — a small C11 peephole optimizer for a simple register and memory instruction set.

## Project goals

- Parse and validate assembly with useful source-line diagnostics.
- Apply local, conservative optimizations while retaining labels and control-flow boundaries.
- Repeat rewrites to a fixed point, with a pass limit to guard against cycles.
- Report the input and optimized assembly, an estimated cost comparison, and a line-numbered rewrite log.
- Keep the ISA, parser, rules, optimizer driver, and CLI in separate modules.

The reported cost is a static estimate from the project specification. It is not a runtime benchmark and does not prove that generated code runs faster on a physical processor.

## Architecture

```text
assembly file
     |
     v
 parser.c  -- syntax, operands, labels, source lines
     |
     v
 isa.c     -- instruction representation, validation, operand costs
     |
     v
 optimizer.c -- repeated scans until a pass makes no rewrites
     |             `-- rules.c applies the selected local transformations
     v
 main.c    -- command-line options, safe output, report and rewrite log
```

- `src/isa.h` defines `Opcode`, `OperandKind`, `Operand`, and `Instruction`. Each instruction retains its input line number.
- `src/parser.c` reads one instruction or label per line, checks its operand forms, and validates duplicate and undefined labels.
- `src/rules.c` applies the optimization rules and records each rewrite’s rule name, source line, and explanation.
- `src/optimizer.c` repeats the instruction-list scan until there are no changes, with a 1,000-pass limit.
- `src/main.c` implements the CLI and reports before/after program information.
- `tests/` contains parser, rule, optimizer, and CLI integration tests. `samples/` contains three valid example programs.

## Instruction-set syntax

There are eight registers: `R0` through `R7`. Immediates are written as `#n`; integer literals accept decimal and C-style base prefixes. A memory operand is a variable name. Labels start with a letter or underscore and may contain letters, digits, underscores, and dots after the first character. Names are case-sensitive. `;` and `!` start comments.

```text
LD Rd, variable       LD Rd, #n
ST variable, Rs
MOV Rd, Rs
ADD|SUB|MUL|DIV Rd, Rs, Rt-or-#n
SHL|SHR Rd, Rs, Rt-or-#n
JMP label
JZ|JNZ Rs, label
NOP
HALT
label:
```

For arithmetic, the second operand is the left input, the third operand is the right input, and the first operand receives the result. For example, `SUB R0, R1, #2` means `R0 = R1 - 2`.

Memory names represent distinct scalar locations: `x` and `y` do not alias. The ISA has no pointer or indirect-memory operands. The arithmetic model is signed 64-bit integer arithmetic when the result is representable. Operations that overflow are unsafe to fold. Division by zero and `INT64_MIN / -1` are not folded. Immediate shift counts from 0 through 62 are foldable when the value-specific checks pass; negative or larger counts are left unchanged. A fold is also skipped for negative values shifted left or right.

## Cost model and report

Each executable instruction costs:

```text
cost = 1 + number of immediate operands + number of memory operands
```

Registers and label operands do not add cost. Labels are excluded from instruction counts and total cost. Examples: `MOV R0, R1` costs 1, `LD R0, x` costs 2, and `ADD R0, R1, #3` costs 2.

The CLI prints the original and optimized assembly, instruction counts, estimated costs, absolute and percentage savings, number of passes, and each rewrite with its original source line. Percentage savings is `n/a` when the original estimated cost is zero.

Example report shape (the values below are from the current `samples/sample1.asm`):

```text
Original code:
  ...
Optimized code:
  ...
Instruction count: 8 -> 5
Estimated cost: 15 -> 8
Cost savings: 7 absolute (46.67%)
Optimization passes: 3
Rewrites: 6
  redundant-load-after-store (line 4): ...
  ...
```

## Implemented optimization rules

1. **Redundant load after store:** removes `LD R, x` directly after `ST x, R`.
2. **Load to register copy:** changes a load directly after a store to the same named variable into a `MOV` from the stored register.
3. **Redundant store:** removes a repeated store to the same location from an unchanged register, within the current basic block.
4. **Algebraic simplification:** changes `ADD Rd, Rs, #0` and `MUL Rd, Rs, #1` to `MOV Rd, Rs`.
5. **Multiply by zero:** changes `MUL Rd, Rs, #0` to `LD Rd, #0`.
6. **Self-move removal:** removes `MOV R, R`.
7. **Power-of-two strength reduction:** changes multiplication by a positive power of two to `SHL` only when a preceding immediate load proves that the source is nonnegative and the product fits in signed 64-bit range.
8. **Constant folding:** folds supported arithmetic using a register value established by an immediate load and an immediate right operand, within the same straight-line block.
9. **Jump to next label:** removes a jump when its target label immediately follows it.
10. **Jump chaining:** redirects a jump through a finite chain of unconditional jumps. Cyclic chains are left unchanged.
11. **Unreachable code removal:** removes instructions following an unconditional jump up to (but not including) the next label. Labels are retained.
12. **Dead-store elimination:** removes a store when a later store to the same named variable occurs before a read or control-flow boundary.

## Safety limitations

- Rules use local patterns and conservative straight-line scans; this is not a global dataflow or control-flow optimizer.
- Labels stop dataflow-sensitive scans. Branches stop store scans and constant reasoning. The optimizer does not remove labels, including branch targets.
- Distinct memory names are assumed to be distinct scalar locations. A target machine with aliases, memory-mapped I/O, or observable store side effects would need a different memory model.
- Constant folding does not infer constants through arbitrary arithmetic, memory loads, or branches. If it cannot prove a register value, it leaves the operation unchanged.
- Strength reduction is intentionally skipped for unknown or negative source values and when the shifted result could overflow.
- Potentially overflowing expressions, division by zero, signed division overflow, invalid shift counts, and shifts of negative values remain unchanged.
- The iteration limit prevents an optimizer cycle from running forever; reaching it is reported as an error. A successful run is not a formal proof of semantic equivalence.
- Cost reduction is only a model-based estimate. In particular, a fold may preserve cost (as in the constants sample), and this project does not measure hardware execution time.

## Prerequisites

- GCC with C11 support (the Makefile defaults to `gcc`).
- GNU Make.
- Standard C library only; no third-party libraries are required.
- AddressSanitizer and UndefinedBehaviorSanitizer support is needed only for `make sanitize`.

## Build, test, and clean

Run these commands from the project root:

```sh
make                 # build ./peephole
make build           # same explicit build target
make test            # parser, rule, optimizer, and CLI integration tests
make sanitize        # rebuild and run tests with ASan and UBSan
make clean           # remove executable, test binaries, and object files
```

The default build uses `-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow`. The sanitizer target disables LeakSanitizer (`detect_leaks=0`) because it is unsupported in some constrained execution environments; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.

## CLI usage

```sh
./peephole [options] input.asm
```

- `input.asm`: required positional input path. Options can appear before or after it.
- `-o FILE`: write optimized assembly to `FILE`. Output is staged beside the destination and renamed into place after a successful write; using the same input and output path is supported.
- `--rules LIST`: comma-separated rule names from the list above, or `all`. If omitted, every rule is enabled.
- `-h`, `--help`: print help and examples.

Examples:

```sh
./peephole samples/sample1.asm
./peephole -o optimized.asm samples/sample1.asm
./peephole samples/sample2.asm --rules fold
./peephole --rules algebra,fold samples/sample2.asm -o constants-optimized.asm
./peephole --help
```

Invalid assembly and failed file operations produce an error on standard error and a nonzero exit status. Command-line usage errors also return nonzero.

## Sample programs and demo

- `samples/sample1.asm` demonstrates redundant loads/stores, an algebraic identity, strength reduction, and folding.
- `samples/sample2.asm` demonstrates safe folds and leaves overflow and division-by-zero cases unchanged.
- `samples/sample3.asm` demonstrates a conditional loop, jump chaining, fall-through, and unreachable-code removal.

Run an individual sample or display all three reports:

```sh
./peephole samples/sample1.asm
./peephole samples/sample2.asm
./peephole samples/sample3.asm
make demo
# equivalent direct command: sh scripts/demo.sh
```

The sample report measurements are produced by the optimizer itself:

| Sample | Instructions before → after | Cost before → after | Absolute cost saved | Percent saved |
|---|---:|---:|---:|---:|
| Arithmetic/redundancy | 8 → 5 | 15 → 8 | 7 | 46.67% |
| Constants | 9 → 9 | 17 → 17 | 0 | 0.00% |
| Control flow/loop | 9 → 6 | 11 → 8 | 3 | 27.27% |

These are static cost-model results, not runtime speed measurements. The constants sample folds two expressions but does not lower this cost model’s total because the replacement immediate loads have the same estimated cost as the original immediate arithmetic instructions.

## Viva preparation

### Parser and instruction representation

The parser reads the input line by line, preserving each source line number. It tokenizes the opcode and comma-separated operands, maps operands to explicit kinds (`register`, `immediate`, `memory`, or `label`), and asks the ISA validator to enforce the operand format. After parsing, it checks label uniqueness and verifies every branch target exists. The optimizer works on an array of `Instruction` structures rather than raw text, so rules can compare opcode and operand kinds directly.

### Sliding-window scan and fixed point

Each pass scans the instruction array and applies local peephole patterns around the current instruction. Some rules compare adjacent instructions; store and constant checks scan only within the current straight-line block and stop at labels or control flow. A rewrite may expose a new pattern, so the driver runs another pass when a pass reports changes. It stops after a no-change pass or reports an error at the 1,000-pass limit.

### Label and fall-through safety

Labels are separate pseudo-instructions and are never deleted by the rules. Jump-to-next-label removal is safe because normal fall-through reaches that exact target. Unreachable removal stops before the next label so a branch entry point remains intact. Jump chaining changes a jump’s destination but leaves the intermediate labels in place; it declines to rewrite cyclic chains.

### Correctness conditions

An optimization must preserve the values visible to later instructions and the path taken through control flow. For example, a redundant store is removed only if the source register is unchanged and no intervening write or control-flow boundary invalidates the comparison. Constant folding checks overflow, divide-by-zero, the signed division corner case, and shift constraints before calculating a replacement. If a rule cannot establish its preconditions, it leaves the instruction as written.

### Complexity

Parsing and local rewrites are linear in common cases. Label validation and some backward/forward rule searches can scan the instruction list repeatedly, so a pass has a conservative worst-case time of O(n²) for n instructions. The optimizer is limited to 1,000 passes; storage is O(n) for instructions plus the rewrite log. This bound favors a small lab input and simple implementation over a global control-flow analysis.

### Cost savings

The report sums the stated cost for every non-label instruction before and after optimization. Absolute savings is `original cost - optimized cost`; percentage savings is `100 × absolute savings / original cost`. When original cost is zero, percentage savings is printed as `n/a`. This compares the project’s estimated instruction costs only; it does not establish real processor speedup or formal equivalence.
