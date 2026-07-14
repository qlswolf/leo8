/******************************************************************************
 * @file        leo8_io.c
 * @brief       Simple input and output functions for the LEO-8 CPU emulator.
 *
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#include "leo8_io.h"

#include <stdio.h>
#include <stdlib.h>

uint8_t leo8ReadInput(void)
{
    char buffer[32];
    unsigned long value;

    printf("IN: ");

    /* fgets works with decimal, hex and octal numbers */
    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
        return 0;
    }

    value = strtoul(buffer, NULL, 0);

    return (uint8_t)value;
}

void leo8WriteOutput(uint8_t reg, uint8_t value)
{
    printf("OUT: R%u = %3u (0x%02x)\n", reg, value, value);
}