; LEO-8 example: countdown using a subroutine

        IN R0
        LDI R1, 1

loop:
        OUT R0
        CALL decrement
        CMP R0, R2
        BNE loop
        HALT

decrement:
        SUB R0, R1
        RET
