/******************************************************************************
 * @file        leo8_assembler.c
 * @brief       Two-pass assembler for the LEO-8 processor.
 *
 * The assembler converts readable assembly instructions into 16-bit machine
 * words. Labels and EQU constants are collected during the first pass and
 * resolved during the second pass.
 *
 * Supported EQU syntax:
 *
 *     IO_OUT      EQU 0xF0
 *     IO_IN       EQU 0xF1
 *     IO_RANDOM   EQU 0xF2
 *
 * EQU constants do not occupy program memory. Symbol names are
 * case-insensitive and share one namespace with labels.
 *
 * @author      Michael Weilguni
 * @date        2026-07-18
 *
 * @copyright   MIT License
 ******************************************************************************/

#include "leo8_platform.h"
#include "leo8_assembler.h"
#include "leo8_cpu.h"
#include "leo8_isa.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEO8_MAX_SYMBOLS 128u
#define LEO8_MAX_LINE    160u
#define LEO8_MAX_TOKEN   32u

/******************************************************************************
 * Symbol table
 ******************************************************************************/

typedef struct
{
    char name[LEO8_MAX_TOKEN];
    uint16_t value;
} Symbol;

typedef struct
{
    Symbol symbols[LEO8_MAX_SYMBOLS];
    size_t count;
} SymbolTable;

/******************************************************************************
 * Text processing helpers
 ******************************************************************************/

static void trim(char *text)
{
    char *start = text;
    char *end;

    while (isspace((unsigned char)*start))
    {
        start++;
    }

    if (start != text)
    {
        memmove(text, start, strlen(start) + 1u);
    }

    end = text + strlen(text);
    while ((end > text) && isspace((unsigned char)end[-1]))
    {
        end--;
    }
    *end = '\0';
}

static void uppercase(char *text)
{
    while (*text != '\0')
    {
        *text = (char)toupper((unsigned char)*text);
        text++;
    }
}

static void removeComment(char *line)
{
    char *comment = strchr(line, ';');

    if (comment != NULL)
    {
        *comment = '\0';
    }
}

static int isValidSymbolName(const char *name)
{
    size_t i;

    if ((name[0] == '\0') ||
        (!isalpha((unsigned char)name[0]) && (name[0] != '_')))
    {
        return 0;
    }

    for (i = 1u; name[i] != '\0'; i++)
    {
        if (!isalnum((unsigned char)name[i]) && (name[i] != '_'))
        {
            return 0;
        }
    }

    return 1;
}

static int addSymbol(SymbolTable *table, const char *name, uint16_t value)
{
    size_t i;

    if ((table->count >= LEO8_MAX_SYMBOLS) || !isValidSymbolName(name))
    {
        return -1;
    }

    for (i = 0u; i < table->count; i++)
    {
        if (strcmp(table->symbols[i].name, name) == 0)
        {
            return -1;
        }
    }

    snprintf(table->symbols[table->count].name, sizeof(table->symbols[table->count].name), "%s", name);
    table->symbols[table->count].value = value;
    table->count++;

    return 0;
}

static int findSymbol(const SymbolTable *table, const char *name, uint16_t *value)
{
    size_t i;

    for (i = 0u; i < table->count; i++)
    {
        if (strcmp(table->symbols[i].name, name) == 0)
        {
            *value = table->symbols[i].value;
            return 0;
        }
    }

    return -1;
}

/******************************************************************************
 * Operand and directive parsing
 ******************************************************************************/

static int parseRegister(const char *token, uint8_t *reg)
{
    if ((token[0] != 'R') || (token[2] != '\0') ||
        (token[1] < '0') || (token[1] > '3'))
    {
        return -1;
    }

    *reg = (uint8_t)(token[1] - '0');
    return 0;
}

static int parseIndirectRegister(const char *token, uint8_t *reg)
{
    char temp[LEO8_MAX_TOKEN];
    size_t len = strlen(token);

    if ((len < 4u) || (token[0] != '[') || (token[len - 1u] != ']'))
    {
        return -1;
    }

    if ((len - 2u) >= sizeof(temp))
    {
        return -1;
    }

    memcpy(temp, token + 1, len - 2u);
    temp[len - 2u] = '\0';
    trim(temp);
    uppercase(temp);

    return parseRegister(temp, reg);
}

static int parseNumber(const char *token, long *value)
{
    char *end;
    long parsed = strtol(token, &end, 0);

    if ((*token == '\0') || (*end != '\0'))
    {
        return -1;
    }

    *value = parsed;
    return 0;
}

static int resolveValue(const SymbolTable *symbols, const char *token, uint16_t *value)
{
    char name[LEO8_MAX_TOKEN];
    long number;

    if (parseNumber(token, &number) == 0)
    {
        if ((number < 0) || (number > 0xFFFFL))
        {
            return -1;
        }

        *value = (uint16_t)number;
        return 0;
    }

    if (strlen(token) >= sizeof(name))
    {
        return -1;
    }

    strcpy(name, token);
    uppercase(name);
    return findSymbol(symbols, name, value);
}

static int resolveAddress(const SymbolTable *symbols, const char *token, uint16_t *address)
{
    uint16_t value;

    if ((resolveValue(symbols, token, &value) != 0) ||
        (value >= LEO8_PROGRAM_SIZE))
    {
        return -1;
    }

    *address = value;
    return 0;
}

static int parseEquDirective(const char *source, char *name, size_t nameSize, long *value)
{
    char line[LEO8_MAX_LINE];
    char first[LEO8_MAX_TOKEN];
    char directive[LEO8_MAX_TOKEN];
    char valueText[LEO8_MAX_TOKEN];
    char extra[LEO8_MAX_TOKEN];
    int count;

    snprintf(line, sizeof(line), "%s", source);
    trim(line);

    count = sscanf(line, "%31s %31s %31s %31s",
                   first, directive, valueText, extra);
    if (count < 2)
    {
        return 0;
    }

    uppercase(directive);
    if (strcmp(directive, "EQU") != 0)
    {
        return 0;
    }

    if ((count != 3) || (strlen(first) >= nameSize) ||
        (parseNumber(valueText, value) != 0) ||
        (*value < 0) || (*value > 0xFFFFL))
    {
        return -1;
    }

    strcpy(name, first);
    uppercase(name);
    return 1;
}

static int splitOperands(char *text, char operands[][LEO8_MAX_TOKEN], int maxOperands)
{
    int count = 0;
    char *token = strtok(text, ",");

    while ((token != NULL) && (count < maxOperands))
    {
        trim(token);
        uppercase(token);
        snprintf(operands[count], LEO8_MAX_TOKEN, "%s", token);
        count++;
        token = strtok(NULL, ",");
    }

    return count;
}

/******************************************************************************
 * Instruction encoding helpers
 ******************************************************************************/

static uint16_t encodeSystem(uint8_t function, uint8_t reg)
{
    return (uint16_t)(((uint16_t)LEO8_OP_SYSTEM << 12) |
                      ((uint16_t)function << 8) |
                      ((uint16_t)reg << 6));
}

static uint16_t encodeTwoRegisters(uint8_t opcode,
                                   uint8_t regA, uint8_t regB)
{
    return (uint16_t)(((uint16_t)opcode << 12) |
                      ((uint16_t)regA << 10) |
                      ((uint16_t)regB << 8));
}

static uint16_t encodeAlu(uint8_t function, uint8_t rd, uint8_t rs)
{
    return (uint16_t)(((uint16_t)LEO8_OP_ALU << 12) |
                      ((uint16_t)function << 8) |
                      ((uint16_t)rd << 6) |
                      ((uint16_t)rs << 4));
}

static uint16_t encodeBranch(uint8_t condition, int16_t offset)
{
    return (uint16_t) ((LEO8_OP_BRANCH << 12) | ((condition & 0x03) << 10) | ((uint16_t) offset & 0x03FF));
}

/******************************************************************************
 * Instruction parser
 ******************************************************************************/

static int assembleInstruction(const char *source, uint16_t address,
                               const SymbolTable *symbols,
                               uint16_t *word,
                               char *error, size_t errorSize)
{
    char line[LEO8_MAX_LINE];
    char mnemonic[LEO8_MAX_TOKEN];
    char operandText[LEO8_MAX_LINE] = "";
    char operands[3][LEO8_MAX_TOKEN];
    char *space;
    int operandCount;
    uint8_t r0;
    uint8_t r1;
    uint16_t target;
    uint16_t immediate;
    int32_t offset;

    snprintf(line, sizeof(line), "%s", source);
    trim(line);

    space = strpbrk(line, " \t");
    if (space != NULL)
    {
        *space = '\0';
        snprintf(operandText, sizeof(operandText), "%s", space + 1);
        trim(operandText);
    }

    if (strlen(line) >= sizeof(mnemonic))
    {
        snprintf(error, errorSize, "mnemonic is too long");
        return -1;
    }

    strcpy(mnemonic, line);
    uppercase(mnemonic);
    operandCount = splitOperands(operandText, operands, 3);

    if ((strcmp(mnemonic, "NOP") == 0) ||
        (strcmp(mnemonic, "HALT") == 0) ||
        (strcmp(mnemonic, "RET") == 0))
    {
        uint8_t function;

        if (operandCount != 0)
        {
            snprintf(error, errorSize, "%s does not take operands", mnemonic);
            return -1;
        }

        function = (strcmp(mnemonic, "NOP") == 0) ? LEO8_SYS_NOP :
                   (strcmp(mnemonic, "HALT") == 0) ? LEO8_SYS_HALT :
                                                    LEO8_SYS_RET;
        *word = encodeSystem(function, 0u);
        return 0;
    }

    if ((strcmp(mnemonic, "IN") == 0) ||
        (strcmp(mnemonic, "OUT") == 0) ||
        (strcmp(mnemonic, "PUSH") == 0) ||
        (strcmp(mnemonic, "POP") == 0))
    {
        uint8_t function;

        if ((operandCount != 1) ||
            (parseRegister(operands[0], &r0) != 0))
        {
            snprintf(error, errorSize, "%s requires one register", mnemonic);
            return -1;
        }

        if (strcmp(mnemonic, "IN") == 0) function = LEO8_SYS_IN;
        else if (strcmp(mnemonic, "OUT") == 0) function = LEO8_SYS_OUT;
        else if (strcmp(mnemonic, "PUSH") == 0) function = LEO8_SYS_PUSH;
        else function = LEO8_SYS_POP;

        *word = encodeSystem(function, r0);
        return 0;
    }

    if (strcmp(mnemonic, "LDI") == 0)
    {
        if ((operandCount != 2) ||
            (parseRegister(operands[0], &r0) != 0) ||
            (resolveValue(symbols, operands[1], &immediate) != 0) ||
            (immediate > 255u))
        {
            snprintf(error, errorSize,
                     "LDI syntax: LDI Rn, 0..255 or EQU symbol");
            return -1;
        }

        *word = (uint16_t)(((uint16_t)LEO8_OP_LDI << 12) |
                           ((uint16_t)r0 << 10) |
                           (uint8_t)immediate);
        return 0;
    }

    if (strcmp(mnemonic, "MOV") == 0)
    {
        if ((operandCount != 2) ||
            (parseRegister(operands[0], &r0) != 0) ||
            (parseRegister(operands[1], &r1) != 0))
        {
            snprintf(error, errorSize, "MOV syntax: MOV Rn, Rm");
            return -1;
        }

        *word = encodeTwoRegisters(LEO8_OP_MOV, r0, r1);
        return 0;
    }

    if ((strcmp(mnemonic, "SHL") == 0) ||
        (strcmp(mnemonic, "SHR") == 0))
    {
        uint8_t function;

        if ((operandCount != 1) ||
            (parseRegister(operands[0], &r0) != 0))
        {
            snprintf(error, errorSize, "%s syntax: %s Rn",
                     mnemonic, mnemonic);
            return -1;
        }

        function = (strcmp(mnemonic, "SHL") == 0) ?
                   LEO8_ALU_SHL : LEO8_ALU_SHR;

        /* Rs is unused for unary shift instructions and encoded as zero. */
        *word = encodeAlu(function, r0, 0u);
        return 0;
    }

    if ((strcmp(mnemonic, "ADD") == 0) ||
        (strcmp(mnemonic, "ADC") == 0) ||
        (strcmp(mnemonic, "SUB") == 0) ||
        (strcmp(mnemonic, "SBC") == 0) ||
        (strcmp(mnemonic, "AND") == 0) ||
        (strcmp(mnemonic, "OR") == 0) ||
        (strcmp(mnemonic, "XOR") == 0) ||
        (strcmp(mnemonic, "CMP") == 0))
    {
        uint8_t function;

        if ((operandCount != 2) ||
            (parseRegister(operands[0], &r0) != 0) ||
            (parseRegister(operands[1], &r1) != 0))
        {
            snprintf(error, errorSize, "%s syntax: %s Rn, Rm",
                     mnemonic, mnemonic);
            return -1;
        }

        if      (strcmp(mnemonic, "ADD") == 0) function = LEO8_ALU_ADD;
        else if (strcmp(mnemonic, "ADC") == 0) function = LEO8_ALU_ADC;
        else if (strcmp(mnemonic, "SUB") == 0) function = LEO8_ALU_SUB;
        else if (strcmp(mnemonic, "SBC") == 0) function = LEO8_ALU_SBC;
        else if (strcmp(mnemonic, "AND") == 0) function = LEO8_ALU_AND;
        else if (strcmp(mnemonic, "OR") == 0)  function = LEO8_ALU_OR;
        else if (strcmp(mnemonic, "XOR") == 0) function = LEO8_ALU_XOR;
        else                                   function = LEO8_ALU_CMP;

        *word = encodeAlu(function, r0, r1);
        return 0;
    }

    if (strcmp(mnemonic, "LD") == 0)
    {
        if ((operandCount != 2) ||
            (parseRegister(operands[0], &r0) != 0) ||
            (parseIndirectRegister(operands[1], &r1) != 0))
        {
            snprintf(error, errorSize, "LD syntax: LD Rn, [Rm]");
            return -1;
        }

        *word = encodeTwoRegisters(LEO8_OP_LD, r0, r1);
        return 0;
    }

    if (strcmp(mnemonic, "ST") == 0)
    {
        if ((operandCount != 2) ||
            (parseIndirectRegister(operands[0], &r0) != 0) ||
            (parseRegister(operands[1], &r1) != 0))
        {
            snprintf(error, errorSize, "ST syntax: ST [Rn], Rm");
            return -1;
        }

        *word = encodeTwoRegisters(LEO8_OP_ST, r0, r1);
        return 0;
    }

    if ((strcmp(mnemonic, "JMP") == 0) ||
        (strcmp(mnemonic, "CALL") == 0))
    {
        uint8_t opcode = (strcmp(mnemonic, "JMP") == 0) ?
                         LEO8_OP_JMP : LEO8_OP_CALL;

        if ((operandCount != 1) ||
            (resolveAddress(symbols, operands[0], &target) != 0))
        {
            snprintf(error, errorSize,
                     "%s requires a valid label or address", mnemonic);
            return -1;
        }

        *word = (uint16_t)(((uint16_t)opcode << 12) |
                           (target & 0x0FFFu));
        return 0;
    }

    if ((strcmp(mnemonic, "BEQ") == 0) ||
        (strcmp(mnemonic, "BNE") == 0) ||
        (strcmp(mnemonic, "BCS") == 0) ||
        (strcmp(mnemonic, "BCC") == 0))
    {
        uint8_t condition;
        if      (strcmp(mnemonic, "BEQ") == 0)   condition = LEO8_BRANCH_BEQ;
        else if (strcmp(mnemonic, "BNE") == 0)   condition = LEO8_BRANCH_BNE;
        else if (strcmp(mnemonic, "BCS") == 0)   condition = LEO8_BRANCH_BCS;
        else                                     condition = LEO8_BRANCH_BCC;
        
        if ((operandCount != 1) ||
            (resolveAddress(symbols, operands[0], &target) != 0))
        {
            snprintf(error, errorSize, "%s requires a valid label or address", mnemonic);
            return -1;
        }

        offset = (int32_t)target - ((int32_t)address + 1);
        if ((offset < -512) || (offset > 511))
        {
            snprintf(error, errorSize, "%s target is outside the relative range", mnemonic);
            return -1;
        }

        *word = encodeBranch(condition, offset);
        return 0;
    }

    snprintf(error, errorSize, "unknown instruction '%s'", mnemonic);
    return -1;
}

/******************************************************************************
 * First assembler pass
 ******************************************************************************/

static int firstPass(FILE *source, SymbolTable *symbols)
{
    char line[LEO8_MAX_LINE];
    uint16_t address = 0u;
    unsigned lineNumber = 0u;

    while (fgets(line, sizeof(line), source) != NULL)
    {
        char *colon;
        char equName[LEO8_MAX_TOKEN];
        long equValue;
        int equResult;

        lineNumber++;
        removeComment(line);
        trim(line);

        if (line[0] == '\0')
        {
            continue;
        }

        equResult = parseEquDirective(line, equName, sizeof(equName),
                                      &equValue);
        if (equResult < 0)
        {
            fprintf(stderr,
                    "Assembler error in line %u: EQU syntax is NAME EQU value\n",
                    lineNumber);
            return -1;
        }
        if (equResult > 0)
        {
            if (addSymbol(symbols, equName, (uint16_t)equValue) != 0)
            {
                fprintf(stderr,
                        "Assembler error in line %u: invalid or duplicate symbol\n",
                        lineNumber);
                return -1;
            }
            continue;
        }

        colon = strchr(line, ':');
        if (colon != NULL)
        {
            *colon = '\0';
            trim(line);
            uppercase(line);

            if ((line[0] == '\0') ||
                (addSymbol(symbols, line, address) != 0))
            {
                fprintf(stderr,
                        "Assembler error in line %u: invalid or duplicate label\n",
                        lineNumber);
                return -1;
            }

            memmove(line, colon + 1, strlen(colon + 1) + 1u);
            trim(line);
            if (line[0] == '\0')
            {
                continue;
            }
        }

        if (address >= LEO8_PROGRAM_SIZE)
        {
            fprintf(stderr, "Assembler error: program is too large\n");
            return -1;
        }

        address++;
    }

    return 0;
}

/******************************************************************************
 * Listing generation
 ******************************************************************************/

static void formatListingSource(const char *source, uint16_t address,
                                const SymbolTable *symbols,
                                char *output, size_t outputSize)
{
    char line[LEO8_MAX_LINE];
    char mnemonic[LEO8_MAX_TOKEN];
    char operand[LEO8_MAX_TOKEN];
    char *space;
    uint16_t value;

    snprintf(line, sizeof(line), "%s", source);
    trim(line);

    space = strpbrk(line, " \t");
    if (space == NULL)
    {
        snprintf(output, outputSize, "%s", line);
        return;
    }

    *space = '\0';
    if (strlen(line) >= sizeof(mnemonic))
    {
        snprintf(output, outputSize, "%s", source);
        return;
    }

    strcpy(mnemonic, line);
    uppercase(mnemonic);
    snprintf(operand, sizeof(operand), "%s", space + 1);
    trim(operand);
    uppercase(operand);

    if (strcmp(mnemonic, "LDI") == 0)
    {
        char operands[2][LEO8_MAX_TOKEN];
        char operandText[LEO8_MAX_LINE];

        snprintf(operandText, sizeof(operandText), "%s", operand);
        if ((splitOperands(operandText, operands, 2) == 2) &&
            (resolveValue(symbols, operands[1], &value) == 0) &&
            (value <= 255u))
        {
            long number;
            if (parseNumber(operands[1], &number) != 0)
            {
                snprintf(output, outputSize, "LDI %s, 0x%02X ; %s",
                         operands[0], value, operands[1]);
                return;
            }
        }
    }

    if ((strcmp(mnemonic, "JMP") == 0) ||
        (strcmp(mnemonic, "CALL") == 0) ||
        (strcmp(mnemonic, "BEQ") == 0) ||
        (strcmp(mnemonic, "BNE") == 0) ||
        (strcmp(mnemonic, "BCS") == 0) ||
        (strcmp(mnemonic, "BCC") == 0))
    {
        if (resolveAddress(symbols, operand, &value) == 0)
        {
            long numericOperand;

            if (parseNumber(operand, &numericOperand) == 0)
            {
                snprintf(output, outputSize, "%s %04X", mnemonic, value);
            }
            else if ((strcmp(mnemonic, "BEQ") == 0) ||
                     (strcmp(mnemonic, "BNE") == 0) ||
                     (strcmp(mnemonic, "BCS") == 0) ||
                     (strcmp(mnemonic, "BCC") == 0))
            {
                int32_t offset = (int32_t)value - ((int32_t)address + 1);
                snprintf(output, outputSize,
                         "%s %04X ; %s, offset %+ld",
                         mnemonic, value, operand, (long)offset);
            }
            else
            {
                snprintf(output, outputSize, "%s %04X ; %s",
                         mnemonic, value, operand);
            }
            return;
        }
    }

    snprintf(output, outputSize, "%s", source);
}

/******************************************************************************
 * Second assembler pass
 ******************************************************************************/

static int secondPass(FILE *source, FILE *hexFile, FILE *listing,
                      const SymbolTable *symbols,
                      uint16_t *instructionCount)
{
    char original[LEO8_MAX_LINE];
    char line[LEO8_MAX_LINE];
    uint16_t address = 0u;
    unsigned lineNumber = 0u;

    fprintf(listing, "ADDR CODE SOURCE\n");
    fprintf(listing, "---- ---- ----------------------------------------\n");

    while (fgets(original, sizeof(original), source) != NULL)
    {
        char *colon;
        char equName[LEO8_MAX_TOKEN];
        long equValue;
        int equResult;
        uint16_t word;
        char error[96];
        char listingSource[LEO8_MAX_LINE];

        lineNumber++;
        snprintf(line, sizeof(line), "%s", original);
        removeComment(line);
        trim(line);

        if (line[0] == '\0')
        {
            continue;
        }

        equResult = parseEquDirective(line, equName, sizeof(equName),
                                      &equValue);
        if (equResult != 0)
        {
            continue;
        }

        colon = strchr(line, ':');
        if (colon != NULL)
        {
            memmove(line, colon + 1, strlen(colon + 1) + 1u);
            trim(line);
            if (line[0] == '\0')
            {
                continue;
            }
        }

        if (assembleInstruction(line, address, symbols, &word,
                                error, sizeof(error)) != 0)
        {
            fprintf(stderr, "Assembler error in line %u: %s\n",
                    lineNumber, error);
            return -1;
        }

        fprintf(hexFile, "%04X\n", word);
        formatListingSource(line, address, symbols,
                            listingSource, sizeof(listingSource));
        fprintf(listing, "%04X %04X %s\n",
                address, word, listingSource);
        address++;
    }

    *instructionCount = address;
    return 0;
}

/******************************************************************************
 * Public assembler function
 ******************************************************************************/

int leo8AssembleFile(const char *sourceName,
                     const char *hexName,
                     const char *listingName,
                     uint16_t *instructionCount)
{
    FILE *source;
    FILE *hexFile;
    FILE *listing;
    SymbolTable symbols = {0};
    int result;

    source = fopen(sourceName, "r");
    if (source == NULL)
    {
        perror(sourceName);
        return -1;
    }

    result = firstPass(source, &symbols);
    if (result != 0)
    {
        fclose(source);
        return -1;
    }

    rewind(source);
    hexFile = fopen(hexName, "w");
    listing = fopen(listingName, "w");

    if ((hexFile == NULL) || (listing == NULL))
    {
        perror("Cannot create assembler output");
        fclose(source);
        if (hexFile != NULL) fclose(hexFile);
        if (listing != NULL) fclose(listing);
        return -1;
    }

    result = secondPass(source, hexFile, listing,
                        &symbols, instructionCount);

    fclose(listing);
    fclose(hexFile);
    fclose(source);

    return result;
}
