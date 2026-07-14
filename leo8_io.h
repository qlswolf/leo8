/******************************************************************************
 * @file        leo8_io.h
 * @brief       Simple input and output functions for the LEO-8 CPU emulator.
 *
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#ifndef LEO8_IO_H
#define LEO8_IO_H

#include <stdint.h>

/**
 * Read one byte from the input device.
 */
uint8_t leo8ReadInput(void);

/**
 * Write one byte to the output device.
 */
void leo8WriteOutput(uint8_t reg, uint8_t value);

#endif