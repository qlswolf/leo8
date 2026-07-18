/******************************************************************************
 * @file        leo8_io.c
 * @brief       Memory-mapped input and output devices of the LEO-8 emulator.
 *
 * @author      Michael Weilguni
 * @date        2026-07-18
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

    /* strtoul accepts decimal, hexadecimal and octal numbers. */
    if (fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
        return 0u;
    }

    value = strtoul(buffer, NULL, 0);
    return (uint8_t)value;
}

void leo8WriteOutput(uint8_t reg, uint8_t value)
{
    printf("OUT: R%u = %3u (0x%02X)  [", reg, value, value);

    for (int8_t bit = 7; bit >= 0; bit--)
    {
        if (value & (1u << bit))
        {
            printf("%c", 219);  /* 1 = white square */
        }   
        else
        {
            printf("%c", 176);  /* 0 = black square */
        }
    }

    printf("]\n");
}

uint8_t leo8ReadRandom(void)
{
    /*
     * An 8-bit linear-feedback shift register produces a repeatable
     * pseudo-random sequence. The fixed seed keeps classroom examples and
     * automated tests reproducible and maps well to a later VHDL design.
     */
    static uint8_t state = 0xA5u;
    uint8_t feedback = (uint8_t)(((state >> 7) ^ (state >> 5) ^
                                  (state >> 4) ^ (state >> 3)) & 0x01u);

    state = (uint8_t)((state << 1) | feedback);

    if (state == 0u)
    {
        state = 0xA5u;
    }

    return state;
}
