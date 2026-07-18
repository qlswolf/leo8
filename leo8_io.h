/******************************************************************************
 * @file        leo8_io.h
 * @brief       Memory-mapped input and output devices of the LEO-8 emulator.
 *
 * @author      Michael Weilguni
 * @date        2026-07-18
 *
 * @copyright   MIT License
 ******************************************************************************/
#ifndef LEO8_IO_H
#define LEO8_IO_H

#include <stdint.h>

/* Memory-mapped I/O addresses. */
#define LEO8_IO_OUTPUT   0xF0u
#define LEO8_IO_INPUT    0xF1u
#define LEO8_IO_RANDOM   0xF2u

/** Read one byte from the console input device. */
uint8_t leo8ReadInput(void);

/** Write one byte to the console output device. */
void leo8WriteOutput(uint8_t reg, uint8_t value);

/** Return the next byte of a deterministic pseudo-random sequence. */
uint8_t leo8ReadRandom(void);

#endif
