#!/bin/sh
set -eu
tmp="${TMPDIR:-/tmp}/peephole-cli-$$"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

# Help succeeds and documents invocation and selectable rules.
./peephole --help > "$tmp/help.txt"
grep -q '^Usage: peephole' "$tmp/help.txt"
grep -q 'load-after-store' "$tmp/help.txt"
grep -q 'peephole --rules algebra,fold' "$tmp/help.txt"

# Standard run reports both programs, counts, costs, savings, passes and source lines.
./peephole samples/sample1.asm > "$tmp/report.txt"
grep -q 'Original code:' "$tmp/report.txt"
grep -q 'Optimized code:' "$tmp/report.txt"
grep -q 'Instruction count:' "$tmp/report.txt"
grep -q 'Estimated cost:' "$tmp/report.txt"
grep -q 'Cost savings: [0-9][0-9]* absolute (' "$tmp/report.txt"
grep -q 'Optimization passes:' "$tmp/report.txt"
grep -q 'Rewrites:' "$tmp/report.txt"
grep -q 'line 4' "$tmp/report.txt"

# Zero original cost reports a guarded percentage instead of dividing by zero.
: > "$tmp/empty.asm"
./peephole "$tmp/empty.asm" > "$tmp/empty-report.txt"
grep -q 'Instruction count: 0 -> 0' "$tmp/empty-report.txt"
grep -q 'Cost savings: 0 absolute (n/a; original cost is zero)' "$tmp/empty-report.txt"

# -o writes only assembly and installs it after a complete successful write.
./peephole -o "$tmp/out.asm" samples/sample1.asm > "$tmp/output-report.txt"
grep -q 'MOV  R2, R1' "$tmp/out.asm"
if grep -q 'Instruction count:' "$tmp/out.asm"; then
  echo 'report text leaked into assembly output' >&2
  exit 1
fi

# Rule selection applies only the requested rule.
printf 'MOV R0, R0\nADD R1, R2, #0\n' > "$tmp/rules.asm"
./peephole "$tmp/rules.asm" --rules self-move -o "$tmp/rules-out.asm" > "$tmp/rules-report.txt"
grep -q 'MOV *R0, R0' "$tmp/rules-report.txt"
grep -q 'Rewrites: 1' "$tmp/rules-report.txt"
grep -q 'ADD  R1, R2, #0' "$tmp/rules-out.asm"
if grep -q 'MOV  R0, R0' "$tmp/rules-out.asm"; then
  echo '--rules self-move did not select the requested rule' >&2
  exit 1
fi

# The input can also be the requested output; parsing completes before replacement.
cp "$tmp/rules.asm" "$tmp/in-place.asm"
./peephole -o "$tmp/in-place.asm" --rules self-move "$tmp/in-place.asm" > /dev/null
grep -q 'ADD  R1, R2, #0' "$tmp/in-place.asm"
if grep -q 'MOV  R0, R0' "$tmp/in-place.asm"; then
  echo 'in-place output was not optimized' >&2
  exit 1
fi

# Invalid assembly and malformed options return nonzero with useful diagnostics.
printf 'NOP\n\n; comment\nLD R9, x\n' > "$tmp/invalid.asm"
if ./peephole "$tmp/invalid.asm" > /dev/null 2> "$tmp/invalid.err"; then
  echo 'invalid assembly unexpectedly succeeded' >&2
  exit 1
fi
grep -q 'line 4:' "$tmp/invalid.err"
if ./peephole --rules self-move, "$tmp/rules.asm" > /dev/null 2>&1; then
  echo 'malformed rule list unexpectedly succeeded' >&2
  exit 1
fi
if ./peephole -o > /dev/null 2>&1; then
  echo 'missing output path unexpectedly succeeded' >&2
  exit 1
fi

# An unusable destination must fail without leaving an output file.
if ./peephole -o "$tmp/no-such-dir/out.asm" samples/sample1.asm > /dev/null 2>&1; then
  echo 'unwritable output path unexpectedly succeeded' >&2
  exit 1
fi

echo 'CLI integration tests passed'
