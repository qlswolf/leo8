# LEO-8

A minimal 8-bit processor for learning computer architecture.

LEO-8 is a small educational processor designed for students who already know basic digital logic and the C programming language. Its purpose is not to imitate a modern CPU, but to make every important concept of a processor understandable.

The complete processor—including the assembler and emulator—can be studied in a relatively small amount of source code.

---

# Design Goals

LEO-8 was developed with the following goals in mind:

* Simple and readable instruction set
* Small C implementation
* Easy to understand assembler
* Clean separation between assembler and CPU
* Straightforward VHDL implementation
* Suitable for classroom use

The same assembly program can later be executed by both the C emulator and a hardware implementation.

---

# Architecture

| Feature           | Description                                   |
| ----------------- | --------------------------------------------- |
| Data width        | 8 bit                                         |
| Instruction width | 16 bit                                        |
| Registers         | 4 × 8-bit general purpose registers (R0...R3) |
| Program Counter   | 16 bit                                        |
| Stack Pointer     | 16 bit                                        |
| Flags             | Zero (Z), Carry (C)                           |
| Program format    | Fixed 16-bit instructions                     |

---

# Instruction Set

## Data transfer

* `LDI`
* `MOV`
* `LD`
* `ST`

## Arithmetic and logic

* `ADD`
* `ADC`
* `SUB`
* `SBC`
* `AND`
* `OR`
* `XOR`
* `CMP`

## Program flow

* `JMP`
* `BEQ`
* `BNE`
* `CALL`
* `RET`

## Stack

* `PUSH`
* `POP`

## Input / Output

* `IN`
* `OUT`

## System

* `NOP`
* `HALT`

---

# Project Structure

```text
main.c
    Command-line frontend

leo8_cpu.c / .h
    LEO-8 processor implementation

leo8_assembler.c / .h
    Two-pass assembler

leo8_io.c / .h
    Console input and output

leo8_isa.h
    Instruction definitions

examples/
    Example assembly programs
```

---

# Build

## GCC

```bash
gcc -std=c11 -Wall -Wextra -pedantic \
    main.c leo8_cpu.c leo8_assembler.c leo8_io.c \
    -o leo8
```

## Visual Studio

The project also compiles with Microsoft Visual Studio.

To suppress Microsoft's CRT deprecation warnings, define

```text
_CRT_SECURE_NO_WARNINGS
```

before including the standard C library or add it as a project preprocessor definition.

---

# Running a Program

Assemble and execute an assembly program:

```bash
leo8 countdown.asm
```

Enable instruction tracing:

```bash
leo8 countdown.asm --trace
```

The assembler automatically generates

```text
countdown.hex
countdown.lst
```

before the emulator starts.

---

# Example

```asm
LDI R0, 5
LDI R1, 1

loop:
    OUT R0
    SUB R0, R1
    BNE loop

HALT
```

---

# Development Flow

```text
Assembly source (.asm)
          │
          ▼
     Two-pass assembler
          │
          ▼
 Machine code (.hex)
          │
          ▼
     LEO-8 Emulator
          │
          ▼
   Program execution
```

The generated HEX file can later be used as the program memory for a VHDL implementation.

---

# Educational Philosophy

LEO-8 is intentionally small.

The goal is not to reproduce a commercial processor, but to help students understand how processors work.

The complete development flow can be explored:

```text
C
    ↓
Assembly
    ↓
Machine Code
    ↓
Processor
    ↓
Digital Hardware (VHDL)
```

This makes LEO-8 a bridge between software engineering and digital logic design.

---

# License

MIT License
