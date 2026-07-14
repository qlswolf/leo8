/******************************************************************************
 * @file        leo8_cpu.c
 * @brief       Emulator for the LEO-8 processor core.
 *
 * The CPU repeatedly performs the classic fetch-decode-execute cycle:
 *
 *   1. Fetch a 16-bit instruction from program memory.
 *   2. Increase the program counter.
 *   3. Decode opcode, function and register fields.
 *   4. Execute the selected operation.
 *
 * The implementation is intentionally direct. Each switch case corresponds
 * closely to logic that can later be implemented in VHDL.
 * 
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#include "leo8_platform.h"
#include "leo8_cpu.h"
#include "leo8_isa.h"
#include "leo8_io.h"

#include <stdio.h>
#include <string.h>

/******************************************************************************
 * Stack helpers
 ******************************************************************************/

/**
 * @brief Push one byte onto the descending stack.
 *
 * The current stack address is written first; afterwards SP is decremented.
 */
static int pushByte(Leo8Cpu *cpu, uint8_t value)
{
    if (cpu->sp == 0u)
    {
        fprintf(stderr, "Stack overflow\n");
        return -1;
    }

    cpu->ram[cpu->sp] = value;
    cpu->sp--;
    return 0;
}

/**
 * @brief Pop one byte from the descending stack.
 *
 * This operation reverses pushByte(): SP is incremented before reading.
 */
static int popByte(Leo8Cpu *cpu, uint8_t *value)
{
    if (cpu->sp == LEO8_STACK_TOP)
    {
        fprintf(stderr, "Stack underflow\n");
        return -1;
    }

    cpu->sp++;
    *value = cpu->ram[cpu->sp];
    return 0;
}

/******************************************************************************
 * Flag and debug helpers
 ******************************************************************************/

/** Set Z when an 8-bit result is zero. */
static void updateZeroFlag(Leo8Cpu *cpu, uint8_t value)
{
    cpu->flagZ = (value == 0u);
}

/** Print the CPU state after one executed instruction. */
static void printTrace(const Leo8Cpu *cpu, uint16_t pc, uint16_t instruction)
{
    printf("PC=%04X IR=%04X  R0=%02X R1=%02X R2=%02X R3=%02X  Z=%u C=%u SP=%02X\n",
           pc, instruction,
           cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3],
           cpu->flagZ, cpu->flagC, cpu->sp);
}

/******************************************************************************
 * Public CPU functions
 ******************************************************************************/

void leo8CpuReset(Leo8Cpu *cpu)
{
    /* Program memory is deliberately not cleared: it has already been loaded. */
    memset(cpu->r, 0, sizeof(cpu->r));
    memset(cpu->ram, 0, sizeof(cpu->ram));
    cpu->pc = 0u;
    cpu->sp = LEO8_STACK_TOP;
    cpu->flagZ = 0u;
    cpu->flagC = 0u;
    cpu->halted = 0u;
}

int leo8CpuLoadHex(Leo8Cpu *cpu, const char *filename)
{
    FILE *file = fopen(filename, "r");
    unsigned value;

    if (file == NULL)
    {
        perror(filename);
        return -1;
    }

    /* One line in the HEX file represents one 16-bit program word. */
    cpu->programSize = 0u;
    while (fscanf(file, "%x", &value) == 1)
    {
        if ((cpu->programSize >= LEO8_PROGRAM_SIZE) || (value > 0xFFFFu))
        {
            fprintf(stderr, "Invalid or oversized HEX file\n");
            fclose(file);
            return -1;
        }

        cpu->program[cpu->programSize++] = (uint16_t)value;
    }

    fclose(file);
    return 0;
}

int leo8CpuStep(Leo8Cpu *cpu, int trace)
{
    uint16_t currentPc;
    uint16_t instruction;
    uint8_t opcode;

    if (cpu->pc >= cpu->programSize)
    {
        fprintf(stderr, "Program counter outside program memory\n");
        return -1;
    }

    /**************************************************************************
     * Fetch
     **************************************************************************/

    currentPc = cpu->pc;
    instruction = cpu->program[cpu->pc++];

    /**************************************************************************
     * Decode
     *
     * Bits 15..12 contain the main opcode for every instruction format.
     **************************************************************************/

    opcode = (uint8_t)(instruction >> 12);

    /**************************************************************************
     * Execute
     **************************************************************************/

    switch (opcode)
    {
        case LEO8_OP_SYSTEM:
        {
            /*
             * SYSTEM format:
             *
             *  15          12 11          8 7        6 5              0
             * +--------------+--------------+----------+----------------+
             * | SYSTEM = 0   |   function   | register |    unused      |
             * +--------------+--------------+----------+----------------+
             */
            uint8_t function = (uint8_t) ((instruction >> 8) & 0x0Fu);
            uint8_t reg = (uint8_t) ((instruction >> 6) & 0x03u);
            uint8_t low;
            uint8_t high;

            switch (function)
            {
                case LEO8_SYS_NOP:
                    break;

                case LEO8_SYS_HALT:
                    cpu->halted = 1u;
                    break;

                case LEO8_SYS_RET:
                    /* CALL stores high byte first and low byte second. */
                    if ((popByte(cpu, &low) != 0) ||
                        (popByte(cpu, &high) != 0))
                    {
                        return -1;
                    }
                    cpu->pc = (uint16_t)(((uint16_t)high << 8) | low);
                    break;
                
                case LEO8_SYS_IN:
                    cpu->r[reg] = leo8ReadInput();
                    cpu->flagZ = (cpu->r[reg] == 0);

                    break;
                
                case LEO8_SYS_OUT:
                    leo8WriteOutput(reg, cpu->r[reg]);
                    break;

                case LEO8_SYS_PUSH:
                    if (pushByte(cpu, cpu->r[reg]) != 0)
                    {
                        return -1;
                    }
                    break;

                case LEO8_SYS_POP:
                    if (popByte(cpu, &cpu->r[reg]) != 0)
                    {
                        return -1;
                    }
                    break;

                default:
                    fprintf(stderr, "Unknown system instruction %04X\n", instruction);
                    return -1;
            }
            break;
        }

        case LEO8_OP_LDI:
        {
            /* LDI stores an 8-bit constant directly in the instruction word. */
            uint8_t rd = (uint8_t)((instruction >> 10) & 0x03u);
            cpu->r[rd] = (uint8_t)(instruction & 0x00FFu);
            updateZeroFlag(cpu, cpu->r[rd]);
            break;
        }

        case LEO8_OP_MOV:
        {
            uint8_t rd = (uint8_t)((instruction >> 10) & 0x03u);
            uint8_t rs = (uint8_t)((instruction >> 8) & 0x03u);
            cpu->r[rd] = cpu->r[rs];
            updateZeroFlag(cpu, cpu->r[rd]);
            break;
        }

        case LEO8_OP_ALU:
        {
            /*
             * ALU format:
             *
             *  15          12 11          8 7     6 5     4 3        0
             * +--------------+--------------+-------+-------+----------+
             * |   ALU = 3    |   function   |  Rd   |  Rs   | unused   |
             * +--------------+--------------+-------+-------+----------+
             *
             * Most operations use Rd both as input and destination:
             *     Rd = Rd operation Rs
             */
            uint8_t function = (uint8_t)((instruction >> 8) & 0x0Fu);
            uint8_t rd = (uint8_t)((instruction >> 6) & 0x03u);
            uint8_t rs = (uint8_t)((instruction >> 4) & 0x03u);
            uint16_t result;
            uint8_t carryIn;

            switch (function)
            {
                case LEO8_ALU_ADD:
                    result = (uint16_t)cpu->r[rd] + cpu->r[rs];
                    cpu->r[rd] = (uint8_t)result;
                    cpu->flagC = (result > 0xFFu);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_ADC:
                    /* The previous carry is saved before the flag is updated. */
                    carryIn = cpu->flagC;
                    result = (uint16_t)cpu->r[rd] + cpu->r[rs] + carryIn;
                    cpu->r[rd] = (uint8_t)result;
                    cpu->flagC = (result > 0xFFu);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_SUB:
                    /* C=1 means no borrow; this simplifies multi-byte SBC. */
                    cpu->flagC = (cpu->r[rd] >= cpu->r[rs]);
                    cpu->r[rd] = (uint8_t)(cpu->r[rd] - cpu->r[rs]);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_SBC:
                    /* Borrow is represented by C=0 and subtracted as one. */
                    carryIn = cpu->flagC;
                    result = (uint16_t)cpu->r[rs] + (carryIn ? 0u : 1u);
                    cpu->flagC = ((uint16_t)cpu->r[rd] >= result);
                    cpu->r[rd] = (uint8_t)((uint16_t)cpu->r[rd] - result);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_AND:
                    cpu->r[rd] &= cpu->r[rs];
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_OR:
                    cpu->r[rd] |= cpu->r[rs];
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_XOR:
                    cpu->r[rd] ^= cpu->r[rs];
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_CMP:
                    /* CMP updates flags like SUB but discards the result. */
                    result = (uint16_t)cpu->r[rd] - cpu->r[rs];
                    cpu->flagZ = ((uint8_t)result == 0u);
                    cpu->flagC = (cpu->r[rd] >= cpu->r[rs]);
                    break;

                default:
                    fprintf(stderr, "Unknown ALU instruction %04X\n", instruction);
                    return -1;
            }
            break;
        }

        case LEO8_OP_LD:
        {
            /* The second register contains the 8-bit RAM address. */
            uint8_t rd = (uint8_t)((instruction >> 10) & 0x03u);
            uint8_t addressReg = (uint8_t)((instruction >> 8) & 0x03u);
            cpu->r[rd] = cpu->ram[cpu->r[addressReg]];
            updateZeroFlag(cpu, cpu->r[rd]);
            break;
        }

        case LEO8_OP_ST:
        {
            /* The first register contains the RAM address. */
            uint8_t addressReg = (uint8_t)((instruction >> 10) & 0x03u);
            uint8_t rs = (uint8_t)((instruction >> 8) & 0x03u);
            cpu->ram[cpu->r[addressReg]] = cpu->r[rs];
            break;
        }

        case LEO8_OP_JMP:
            /* JMP stores an absolute 12-bit program address. */
            cpu->pc = (uint16_t)(instruction & 0x0FFFu);
            break;

        case LEO8_OP_CALL:
        {
            uint16_t returnAddress = cpu->pc;

            /*
             * PC already points to the next instruction after fetch. This is
             * exactly the address RET must restore later.
             */
            if ((pushByte(cpu, (uint8_t)(returnAddress >> 8)) != 0) ||
                (pushByte(cpu, (uint8_t)returnAddress) != 0))
            {
                return -1;
            }

            cpu->pc = (uint16_t)(instruction & 0x0FFFu);
            break;
        }

        case LEO8_OP_BEQ:
            if (cpu->flagZ != 0u)
            {
                /* Branch offsets are relative to the already incremented PC. */
                cpu->pc = (uint16_t)(cpu->pc + (int8_t)(instruction & 0x00FFu));
            }
            break;

        case LEO8_OP_BNE:
            if (cpu->flagZ == 0u)
            {
                cpu->pc = (uint16_t)(cpu->pc + (int8_t)(instruction & 0x00FFu));
            }
            break;

        default:
            fprintf(stderr, "Unknown opcode %X at address %04X\n", opcode, currentPc);
            return -1;
    }

    if (trace != 0)
    {
        printTrace(cpu, currentPc, instruction);
    }

    return 0;
}

int leo8CpuRun(Leo8Cpu *cpu, int trace)
{
    unsigned long steps = 0ul;
    const unsigned long maxSteps = 1000000ul;

    while (cpu->halted == 0u)
    {
        /* Prevent an accidental endless loop from locking the emulator. */
        if (steps++ >= maxSteps)
        {
            fprintf(stderr, "Execution stopped after %lu steps\n", maxSteps);
            return -1;
        }

        if (leo8CpuStep(cpu, trace) != 0)
        {
            return -1;
        }
    }

    return 0;
}
