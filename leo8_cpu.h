/******************************************************************************
 * @file        leo8_cpu.h
 * @brief       Public data types and functions of the LEO-8 CPU emulator.
 * 
 * @author      Michael Weilguni
 * @date        2026-07-14
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
#define LEO8_STACK_TOP       0xFFu

/******************************************************************************
 * CPU state
 ******************************************************************************/

/**
 * @brief Complete visible state of one LEO-8 processor.
 *
 * The structure directly mirrors the main hardware blocks of the processor:
 * registers, program counter, stack pointer, flags, data memory and program
 * memory. Keeping the state in one structure also allows several independent
 * CPU instances to exist in the same C program.
 */
typedef struct
{
    uint8_t r[LEO8_REGISTER_COUNT];         /**< General-purpose registers R0..R3. */
    uint16_t pc;                            /**< Address of the next instruction. */
    uint8_t sp;                             /**< Stack pointer in data memory. */
    uint8_t flagZ;                          /**< Zero flag. */
    uint8_t flagC;                          /**< Carry / no-borrow flag. */
    uint8_t ram[LEO8_RAM_SIZE];             /**< 256 bytes of data memory. */
    uint16_t program[LEO8_PROGRAM_SIZE];    /**< 16-bit program memory. */
    uint16_t programSize;                   /**< Number of loaded instructions. */
    uint8_t halted;                         /**< Non-zero after HALT. */
} Leo8Cpu;

/******************************************************************************
 * Public functions
 ******************************************************************************/

/** Reset registers, RAM, flags, PC and SP. Program memory stays loaded. */
void leo8CpuReset(Leo8Cpu *cpu);

/** Load one 16-bit hexadecimal instruction per line from a HEX file. */
int leo8CpuLoadHex(Leo8Cpu *cpu, const char *filename);

/** Execute exactly one fetch-decode-execute cycle. */
int leo8CpuStep(Leo8Cpu *cpu, int trace);

/** Execute instructions until HALT or an error occurs. */
int leo8CpuRun(Leo8Cpu *cpu, int trace);

#endif
