/******************************************************************************
 * @file        leo8_assembler.h
 * @brief       Public interface of the LEO-8 assembler.
 *
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#ifndef LEO8_ASSEMBLER_H
#define LEO8_ASSEMBLER_H

#include <stdint.h>

/**
 * @brief Assemble one ASM source file.
 *
 * The assembler creates a machine-readable HEX file and a human-readable
 * listing file. Labels are collected during the first pass and resolved during
 * the second pass.
 *
 * @return 0 on success, -1 on an assembler or file error.
 */
int leo8AssembleFile(const char *sourceName,
                     const char *hexName,
                     const char *listingName,
                     uint16_t *instructionCount);

#endif
