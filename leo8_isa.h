/******************************************************************************
 * @file        leo8_isa.h
 * @brief       Binary instruction encoding shared by assembler and CPU.
 *
 * The assembler uses these values to create opcodes. The CPU uses the same
 * values to decode them. This common definition prevents both components from
 * silently interpreting the same bit pattern differently.
 * 
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#ifndef LEO8_ISA_H
#define LEO8_ISA_H

/******************************************************************************
 * Main opcode groups
 ******************************************************************************/

/*
 * Every instruction is 16 bits wide. Bits 15..12 select the main instruction
 * group. The remaining bits contain registers, function codes or operands.
 */
typedef enum
{
    LEO8_OP_SYSTEM = 0x0,
    LEO8_OP_LDI    = 0x1,
    LEO8_OP_MOV    = 0x2,
    LEO8_OP_ALU    = 0x3,
    LEO8_OP_LD     = 0x4,
    LEO8_OP_ST     = 0x5,
    LEO8_OP_JMP    = 0x6,
    LEO8_OP_CALL   = 0x7,
    LEO8_OP_BRANCH = 0x9
} Leo8Opcode;

/******************************************************************************
 * SYSTEM instruction functions
 ******************************************************************************/

/* SYSTEM function in bits 11..8; optional register in bits 7..6. */
typedef enum
{
    LEO8_SYS_NOP  = 0x0,
    LEO8_SYS_HALT = 0x1,
    LEO8_SYS_RET  = 0x2,
    LEO8_SYS_IN   = 0x3,
    LEO8_SYS_OUT  = 0x4,
    LEO8_SYS_PUSH = 0x5,
    LEO8_SYS_POP  = 0x6
} Leo8SystemFunction;

/******************************************************************************
 * ALU instruction functions
 ******************************************************************************/

/* ALU function in bits 11..8; Rd in bits 7..6; Rs in bits 5..4. */
typedef enum
{
    LEO8_ALU_ADD = 0x0,
    LEO8_ALU_ADC = 0x1,
    LEO8_ALU_SUB = 0x2,
    LEO8_ALU_SBC = 0x3,
    LEO8_ALU_AND = 0x4,
    LEO8_ALU_OR  = 0x5,
    LEO8_ALU_XOR = 0x6,
    LEO8_ALU_CMP = 0x7,
    LEO8_ALU_SHL = 0x8,
    LEO8_ALU_SHR = 0x9
} Leo8AluFunction;

/******************************************************************************
 * BRANCH instruction functions
 ******************************************************************************/

 /* BRANCH condition in bits 11..10 */
typedef enum
{
    LEO8_BRANCH_BEQ = 0x0,
    LEO8_BRANCH_BNE = 0x1,
    LEO8_BRANCH_BCS = 0x2,
    LEO8_BRANCH_BCC = 0x3
} Leo8BranchFunction;

#endif
