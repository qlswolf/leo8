# LEO-8 assembler: EQU support

## Changed

- Added `NAME EQU value` constants to the two-pass assembler.
- Labels and constants now share one case-insensitive symbol table.
- `EQU` lines do not occupy program memory and produce no HEX/LST instruction.
- `LDI` accepts either an 8-bit number or an `EQU` symbol.
- Existing jump and branch label resolution remains compatible.
- Duplicate names are rejected, including collisions between labels and constants.
- Added an example for memory-mapped I/O.

## Intentionally not added

- `INCLUDE`
- `ORG`
- `.byte`
- Arithmetic expressions in `EQU`
