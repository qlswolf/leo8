/******************************************************************************
 * @file        main.c
 * @brief       Command-line frontend for the LEO-8 assembler and emulator.
 *
 * This file shows the complete tool chain at a glance:
 *   program.asm -> assembler -> program.hex -> emulator
 *                            -> program.lst
 * 
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#include "leo8_platform.h"
#include "leo8_assembler.h"
#include "leo8_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************************
 * File name helpers
 ******************************************************************************/

/**
 * @brief Replaces the extension of a file name.
 *
 * The source file and both generated files therefore share the same base name:
 *
 *   countdown.asm -> countdown.hex
 *                 -> countdown.lst
 *
 * @return 0 on success, -1 if the resulting file name is too long.
 */
static int changeExtension(const char *inputFile,
                           char *outputFile,
                           size_t outputSize,
                           const char *newExtension)
{
    const char *dot;
    const char *lastSlash;
    const char *lastBackslash;
    size_t baseLength;

    /*
     * A dot in a directory name is not a file extension. Therefore only a dot
     * after the last path separator may terminate the base name.
     */
    dot = strrchr(inputFile, '.');
    lastSlash = strrchr(inputFile, '/');
    lastBackslash = strrchr(inputFile, '\\');

    if (((lastSlash != NULL) && (dot < lastSlash)) || ((lastBackslash != NULL) && (dot < lastBackslash)))
    {
        dot = NULL;
    }

    baseLength = (dot != NULL) ? (size_t) (dot - inputFile) : strlen(inputFile);

    if ((baseLength + strlen(newExtension) + 1u) > outputSize)
    {
        return -1;
    }

    memcpy(outputFile, inputFile, baseLength);
    strcpy(outputFile + baseLength, newExtension);
    return 0;
}

/******************************************************************************
 * Program entry point
 ******************************************************************************/

int main(int argc, char *argv[])
{
    Leo8Cpu cpu = {0};
    uint16_t instructionCount = 0u;
    int trace = 0;
    char hexFile[260];
    char lstFile[260];

    /* The program expects one ASM file and optionally the trace switch. */
    if ((argc < 2) || (argc > 3))
    {
        printf("Usage: %s program.asm [--trace]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 3)
    {
        if (strcmp(argv[2], "--trace") != 0)
        {
            printf("Unknown option: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
        trace = 1;
    }

    /* Derive the output file names from the ASM source file. */
    if ((changeExtension(argv[1], hexFile, sizeof(hexFile), ".hex") != 0) ||
        (changeExtension(argv[1], lstFile, sizeof(lstFile), ".lst") != 0))
    {
        printf("Filename is too long.\n");
        return EXIT_FAILURE;
    }

    /* Step 1: Convert assembly language into 16-bit machine instructions. */
    printf("LEO-8 assembler\n");
    if (leo8AssembleFile(argv[1], hexFile, lstFile,
                         &instructionCount) != 0)
    {
        return EXIT_FAILURE;
    }

    printf("%u instructions assembled.\n", instructionCount);
    printf("Created %s and %s\n\n", hexFile, lstFile);

    /* Step 2: Load the generated machine code into the simulated CPU. */
    printf("LEO-8 emulator\n");
    if (leo8CpuLoadHex(&cpu, hexFile) != 0)
    {
        return EXIT_FAILURE;
    }

    /* Step 3: Reset the CPU and execute the loaded program. */
    leo8CpuReset(&cpu);
    if (leo8CpuRun(&cpu, trace) != 0)
    {
        return EXIT_FAILURE;
    }

    printf("Program halted.\n");
    return EXIT_SUCCESS;
}
