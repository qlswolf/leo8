LDI R0, 10
LDI R1, 10

CMP R0, R1
BEQ equal

LDI R2, 0
JMP done

equal:
LDI R2, 1

done:
OUT R2
HALT
