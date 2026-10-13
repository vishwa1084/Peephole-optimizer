; Arithmetic and redundancy: redundant load/store, identity, then safe strength reduction.
LD R1, #6
ST total, R1
LD R1, total                 ; redundant: R1 already holds the stored value
ST total, R1                 ; repeated store of an unchanged value
ST total, R1                 ; another repeated store
ADD R2, R1, #0               ; identity becomes MOV
MUL R3, R1, #8                ; known positive source: strength reduction, then folding
HALT
