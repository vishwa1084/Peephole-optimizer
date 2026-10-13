; Safe constant folds become immediate loads.
LD R0, #12
ADD R1, R0, #5               ; folds to R1 = 17
LD R2, #100
DIV R3, R2, #4                ; folds to R3 = 25

; Unsafe folds remain as operations: signed overflow and divide by zero.
LD R4, #9223372036854775807
ADD R5, R4, #1
LD R6, #8
DIV R7, R6, #0
HALT
