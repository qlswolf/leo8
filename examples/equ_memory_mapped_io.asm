; LEO-8 memory-mapped I/O using EQU constants

IO_OUT      EQU 0xF0
IO_IN       EQU 0xF1
IO_RANDOM   EQU 0xF2

    LDI R1, IO_RANDOM
    LDI R2, IO_OUT

    LD  R0, [R1]       ; read pseudo-random number
    ST  [R2], R0       ; write it to the output register

    HALT
