; Add two 16-bit values.
;
; R1:R0 = 0x12FF
; R3:R2 = 0x0102
; Result  = 0x1401

LDI R0, 0xFF
LDI R1, 0x12
LDI R2, 0x02
LDI R3, 0x01

ADD R0, R2
ADC R1, R3

OUT R1
OUT R0
HALT
