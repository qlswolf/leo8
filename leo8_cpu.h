/******************************************************************************
 * @file        leo8_cpu.h
 * @brief       Public data types and functions of the LEO-8 CPU emulator.
 *
 * @author      Michael Weilguni
 * @date        2026-07-18
 *
 * @copyright   MIT License
 ******************************************************************************/
#ifndef LEO8_CPU_H
#define LEO8_CPU_H

#include <stdint.h>

/******************************************************************************
 * Processor configuration
 ******************************************************************************/
#define LEO8_REGISTER_COUNT  4u
#define LEO8_PROGRAM_SIZE    256u
#define LEO8_RAM_SIZE        256u

/* 0xF0..0xFF are reserved for memory-mapped I/O. */
#define LEO8_STACK_TOP       0xEFu

/******************************************************************************
 * CPU state
 ******************************************************************************/
typedef struct
{
    uint8_t r[LEO8_REGISTER_COUNT];
    uint16_t pc;
    uint8_t sp;
    uint8_t flagZ;
    uint8_t flagC;
    uint8_t ram[LEO8_RAM_SIZE];
    uint16_t program[LEO8_PROGRAM_SIZE];
    uint16_t programSize;
    uint8_t halted;
} Leo8Cpu;

void leo8CpuReset(Leo8Cpu *cpu);
int leo8CpuLoadHex(Leo8Cpu *cpu, const char *filename);
int leo8CpuStep(Leo8Cpu *cpu, int trace);
int leo8CpuRun(Leo8Cpu *cpu, int trace);

#endif
