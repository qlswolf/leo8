; Subtract two 16-bit values.
;
; R1:R0 = 0x1200
; R3:R2 = 0x0101
; Result  = 0x10FF

        LDI R0, 0x00
        LDI R1, 0x12
        LDI R2, 0x01
        LDI R3, 0x01

        SUB R0, R2
        SBC R1, R3

        OUT R1
        OUT R0
        HALT
