; Control flow: trampoline jumps into a counted loop, then routes to done.
entry:
LD R0, #3
JMP trampoline

; The loop header is also the conditional branch target.
loop:
SUB R0, R0, #1
JNZ R0, loop
JMP exit_route

; Jump chaining can bypass these trampoline labels without deleting them.
trampoline:
JMP loop
exit_route:
JMP done
NOP                           ; unreachable after the unconditional jump
done:
HALT
