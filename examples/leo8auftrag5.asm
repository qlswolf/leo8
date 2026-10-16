; 1.
LDI R0, 15
LDI R1, 7
ADD R0, R1      ; 15 + 7, Output = 22
OUT R0

; 2.
LDI R0, 0x64
LDI R1, 0x25
SUB R0, R1      ; 0x64 - 0x25, Output = 0x3F
OUT R0

; 3.
LDI R0, 0xD7
LDI R1, 0xF0
AND R0, R1      ; 0xD7 && 0xF0, Output = 0xD0
OUT R0

; 4.
LDI R0, 0x35
LDI R1, 0xFF
XOR R0, R1      ; 0x35 ^ 0xFF, Output = 0xCA     
OUT R0

; 5.
IN R0
LDI R1, 0x22
OR R0, R1       ; Input || 0x22, Output = XX1XXX1X
OUT R0
HALT