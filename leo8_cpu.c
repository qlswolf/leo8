/******************************************************************************
 * @file        leo8_cpu.c
 * @brief       Emulator for the LEO-8 processor core.
 *
 * @author      Michael Weilguni
 * @date        2026-07-18
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
 * Data bus helpers
 ******************************************************************************/
static uint8_t readData(Leo8Cpu *cpu, uint8_t address)
{
    switch (address)
    {
        case LEO8_IO_INPUT:
            return leo8ReadInput();

        case LEO8_IO_RANDOM:
            return leo8ReadRandom();

        case LEO8_IO_OUTPUT:
            /* The output register is write-only. */
            return 0u;

        default:
            if (address >= 0xF0u)
            {
                /* Unused I/O addresses read as zero. */
                return 0u;
            }

            return cpu->ram[address];
    }
}

static void writeData(Leo8Cpu *cpu, uint8_t address, uint8_t value,
                      uint8_t sourceReg)
{
    switch (address)
    {
        case LEO8_IO_OUTPUT:
            leo8WriteOutput(sourceReg, value);
            break;

        case LEO8_IO_INPUT:
        case LEO8_IO_RANDOM:
            /* Read-only registers ignore writes. */
            break;

        default:
            if (address < 0xF0u)
            {
                cpu->ram[address] = value;
            }
            /* Writes to unused I/O addresses are ignored. */
            break;
    }
}

/******************************************************************************
 * Stack helpers
 ******************************************************************************/
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

static void updateZeroFlag(Leo8Cpu *cpu, uint8_t value)
{
    cpu->flagZ = (value == 0u);
}

static void printTrace(const Leo8Cpu *cpu, uint16_t pc, uint16_t instruction)
{
    printf("PC=0x%04X IR=0x%04X R0=0x%02X R1=0x%02X R2=0x%02X R3=0x%02X "
           "Z=%u C=%u SP=0x%02X\n",
           pc, instruction, cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3],
           cpu->flagZ, cpu->flagC, cpu->sp);
}

void leo8CpuReset(Leo8Cpu *cpu)
{
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

    currentPc = cpu->pc;
    instruction = cpu->program[cpu->pc++];
    opcode = (uint8_t)(instruction >> 12);

    switch (opcode)
    {
        case LEO8_OP_SYSTEM:
        {
            uint8_t function = (uint8_t)((instruction >> 8) & 0x0Fu);
            uint8_t reg = (uint8_t)((instruction >> 6) & 0x03u);
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
                    if ((popByte(cpu, &low) != 0) ||
                        (popByte(cpu, &high) != 0))
                    {
                        return -1;
                    }
                    cpu->pc = (uint16_t)(((uint16_t)high << 8) | low);
                    break;

                case LEO8_SYS_IN:
                    /* Legacy alias for reading address LEO8_IO_INPUT. */
                    cpu->r[reg] = readData(cpu, LEO8_IO_INPUT);
                    updateZeroFlag(cpu, cpu->r[reg]);
                    break;

                case LEO8_SYS_OUT:
                    /* Legacy alias for writing address LEO8_IO_OUTPUT. */
                    writeData(cpu, LEO8_IO_OUTPUT, cpu->r[reg], reg);
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
                    fprintf(stderr, "Unknown system instruction %04X\n",
                            instruction);
                    return -1;
            }
            break;
        }

        case LEO8_OP_LDI:
        {
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
                    carryIn = cpu->flagC;
                    result = (uint16_t)cpu->r[rd] + cpu->r[rs] + carryIn;
                    cpu->r[rd] = (uint8_t)result;
                    cpu->flagC = (result > 0xFFu);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_SUB:
                    cpu->flagC = (cpu->r[rd] >= cpu->r[rs]);
                    cpu->r[rd] = (uint8_t)(cpu->r[rd] - cpu->r[rs]);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_SBC:
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
                    result = (uint16_t)cpu->r[rd] - cpu->r[rs];
                    cpu->flagZ = ((uint8_t)result == 0u);
                    cpu->flagC = (cpu->r[rd] >= cpu->r[rs]);
                    break;

                case LEO8_ALU_SHL:
                    /* Shift bit 7 into Carry and insert zero at bit 0. */
                    cpu->flagC = (uint8_t)((cpu->r[rd] >> 7) & 0x01u);
                    cpu->r[rd] = (uint8_t)(cpu->r[rd] << 1);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                case LEO8_ALU_SHR:
                    /* Shift bit 0 into Carry and insert zero at bit 7. */
                    cpu->flagC = (uint8_t)(cpu->r[rd] & 0x01u);
                    cpu->r[rd] = (uint8_t)(cpu->r[rd] >> 1);
                    updateZeroFlag(cpu, cpu->r[rd]);
                    break;

                default:
                    fprintf(stderr, "Unknown ALU instruction %04X\n",
                            instruction);
                    return -1;
            }
            break;
        }

        case LEO8_OP_LD:
        {
            uint8_t rd = (uint8_t)((instruction >> 10) & 0x03u);
            uint8_t addressReg = (uint8_t)((instruction >> 8) & 0x03u);

            cpu->r[rd] = readData(cpu, cpu->r[addressReg]);
            updateZeroFlag(cpu, cpu->r[rd]);
            break;
        }

        case LEO8_OP_ST:
        {
            uint8_t addressReg = (uint8_t)((instruction >> 10) & 0x03u);
            uint8_t rs = (uint8_t)((instruction >> 8) & 0x03u);

            writeData(cpu, cpu->r[addressReg], cpu->r[rs], rs);
            break;
        }

        case LEO8_OP_JMP:
        {
            /* absolute jump using lower 12 bits */
            cpu->pc = (uint16_t)(instruction & 0x0FFFu);
            break;
        }

        case LEO8_OP_CALL:
        {
            uint16_t returnAddress = cpu->pc;

            if ((pushByte(cpu, (uint8_t)(returnAddress >> 8)) != 0) ||
                (pushByte(cpu, (uint8_t)returnAddress) != 0))
            {
                return -1;
            }

            cpu->pc = (uint16_t)(instruction & 0x0FFFu);
            break;
        }

        case LEO8_OP_BRANCH:

        {
            uint8_t condition = (uint8_t)((instruction >> 10) & 0x03u);
            int16_t offset = (int16_t)(instruction & 0x03FFu);

            /* Sign-extend the 10-bit relative offset to 16 bits. */
            if ((offset & 0x0200) != 0)
            {
                offset |= (int16_t)0xFC00;
            }

            /* Conditional branch with a signed 10-bit offset. */

            switch (condition)
            {
            case LEO8_BRANCH_BEQ:
                if (cpu->flagZ == 1u)
                {
                    cpu->pc = (uint16_t) (cpu->pc + offset);
                }
                break;
            case LEO8_BRANCH_BNE:
                if (cpu->flagZ != 1u)
                {
                    cpu->pc = (uint16_t) (cpu->pc + offset);
                }
                break;
            case LEO8_BRANCH_BCS:
                if (cpu->flagC == 1u)
                {
                    cpu->pc = (uint16_t) (cpu->pc + offset);
                }
                break;
            case LEO8_BRANCH_BCC:
                if (cpu->flagC != 1u)
                {
                    cpu->pc = (uint16_t) (cpu->pc + offset);
                }
                break;
            default:
                fprintf(stderr, "Unknown BRANCH instruction %04X\n", condition);
                return -1;
            }

            break;
        }
        default:
            fprintf(stderr, "Unknown opcode %X at address %04X\n",
                    opcode, currentPc);
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
