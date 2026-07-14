/******************************************************************************
 * @file        leo8_assembler.c
 * @brief       Two-pass assembler for the LEO-8 processor.
 *
 * The assembler converts readable assembly instructions into 16-bit machine
 * words. Labels require two passes over the source file:
 *
 *   First pass:   collect every label and its program address.
 *   Second pass:  encode instructions and replace labels by addresses.
 *
 * The generated files have different purposes:
 *
 *   HEX file: one machine instruction per line for the emulator or VHDL ROM.
 *   LST file: address, opcode and resolved source for human inspection.
 * 
 * @author      Michael Weilguni
 * @date        2026-07-14
 *
 * @copyright   MIT License
 ******************************************************************************/

#include "leo8_platform.h"
#include "leo8_assembler.h"
#include "leo8_cpu.h"
#include "leo8_isa.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEO8_MAX_LABELS  128u
#define LEO8_MAX_LINE    160u
#define LEO8_MAX_TOKEN   32u

/******************************************************************************
 * Symbol table
 ******************************************************************************/

typedef struct
{
    char name[LEO8_MAX_TOKEN];
    uint16_t address;
} Label;

typedef struct
{
    Label labels[LEO8_MAX_LABELS];
    size_t count;
} LabelTable;

/******************************************************************************
 * Text processing helpers
 ******************************************************************************/

/** Remove leading and trailing whitespace in place. */
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

/** Convert a token to uppercase so mnemonics are case-insensitive. */
static void uppercase(char *text)
{
    while (*text != '\0')
    {
        *text = (char)toupper((unsigned char)*text);
        text++;
    }
}

/** Remove the part of a source line beginning with a semicolon. */
static void removeComment(char *line)
{
    char *comment = strchr(line, ';');
    if (comment != NULL)
    {
        *comment = '\0';
    }
}

/**
 * @brief Add a label and its address to the symbol table.
 * @return 0 on success, -1 for a duplicate label or a full table.
 */
static int addLabel(LabelTable *table, const char *name, uint16_t address)
{
    size_t i;

    if (table->count >= LEO8_MAX_LABELS)
    {
        return -1;
    }

    for (i = 0u; i < table->count; i++)
    {
        if (strcmp(table->labels[i].name, name) == 0)
        {
            return -1;
        }
    }

    snprintf(table->labels[table->count].name,
             sizeof(table->labels[table->count].name), "%s", name);
    table->labels[table->count].address = address;
    table->count++;
    return 0;
}

/** Look up a label collected during the first assembler pass. */
static int findLabel(const LabelTable *table, const char *name, uint16_t *address)
{
    size_t i;

    for (i = 0u; i < table->count; i++)
    {
        if (strcmp(table->labels[i].name, name) == 0)
        {
            *address = table->labels[i].address;
            return 0;
        }
    }
    return -1;
}

/******************************************************************************
 * Operand parsing
 ******************************************************************************/

/** Convert R0..R3 into register numbers 0..3. */
static int parseRegister(const char *token, uint8_t *reg)
{
    if ((token[0] != 'R') || (token[2] != '\0') ||
        (token[1] < '0') || (token[1] > '3'))
    {
        return -1;
    }

    *reg = (uint8_t) (token[1] - '0');
    return 0;
}

/** Parse an indirect operand such as [R2]. */
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

/** Parse decimal or C-style hexadecimal numbers such as 42 or 0x2A. */
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

/**
 * @brief Resolve either a numeric address or a label name.
 *
 * Numeric operands can be used directly. Symbolic operands are replaced by
 * the address stored in the label table.
 */
static int resolveAddress(const LabelTable *labels, const char *token,
                          uint16_t *address)
{
    long value;

    if (parseNumber(token, &value) == 0)
    {
        if ((value < 0) || (value >= (long) LEO8_PROGRAM_SIZE))
        {
            return -1;
        }
        *address = (uint16_t) value;
        return 0;
    }

    return findLabel(labels, token, address);
}

/**
 * @brief Split comma-separated operands into individual uppercase tokens.
 *
 * Example: "R0, [R1]" becomes "R0" and "[R1]".
 */
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

/** Encode a SYSTEM instruction and its optional register operand. */
static uint16_t encodeSystem(uint8_t function, uint8_t reg)
{
    return (uint16_t) (((uint16_t) LEO8_OP_SYSTEM << 12) |
                       ((uint16_t) function << 8) |
                       ((uint16_t) reg << 6));
}

/** Encode instructions containing two register fields. */
static uint16_t encodeTwoRegisters(uint8_t opcode, uint8_t regA, uint8_t regB)
{
    return (uint16_t) (((uint16_t) opcode << 12) |
                       ((uint16_t) regA << 10) |
                       ((uint16_t) regB << 8));
}

/**
 * @brief Encode an ALU operation.
 *
 * ALU format:
 *
 *  15          12 11          8 7     6 5     4 3        0
 * +--------------+--------------+-------+-------+----------+
 * |   ALU = 3    |   function   |  Rd   |  Rs   | unused   |
 * +--------------+--------------+-------+-------+----------+
 */
static uint16_t encodeAlu(uint8_t function, uint8_t rd, uint8_t rs)
{
    return (uint16_t) (((uint16_t )LEO8_OP_ALU << 12) |
                       ((uint16_t) function << 8) |
                       ((uint16_t) rd << 6) |
                       ((uint16_t) rs << 4));
}

/******************************************************************************
 * Instruction parser
 ******************************************************************************/

/**
 * @brief Translate one source instruction into one 16-bit machine word.
 *
 * This function checks the syntax, parses operands and selects the matching
 * instruction format. Labels have already been collected by the first pass.
 */
static int assembleInstruction(const char *source, uint16_t address,
                               const LabelTable *labels, uint16_t *word,
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
    long immediate;
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
                   (strcmp(mnemonic, "HALT") == 0) ? LEO8_SYS_HALT : LEO8_SYS_RET;
        *word = encodeSystem(function, 0u);
        return 0;
    }

    if ((strcmp(mnemonic, "IN") == 0) ||
        (strcmp(mnemonic, "OUT") == 0) ||
        (strcmp(mnemonic, "PUSH") == 0) ||
        (strcmp(mnemonic, "POP") == 0))
    {
        uint8_t function;

        if ((operandCount != 1) || (parseRegister(operands[0], &r0) != 0))
        {
            snprintf(error, errorSize, "%s requires one register", mnemonic);
            return -1;
        }

        if      (strcmp(mnemonic, "IN") == 0)   function = LEO8_SYS_IN;
        else if (strcmp(mnemonic, "OUT") == 0)  function = LEO8_SYS_OUT;
        else if (strcmp(mnemonic, "PUSH") == 0) function = LEO8_SYS_PUSH;
        else                                    function = LEO8_SYS_POP;

        *word = encodeSystem(function, r0);
        return 0;
    }

    if (strcmp(mnemonic, "LDI") == 0)
    {
        if ((operandCount != 2) || (parseRegister(operands[0], &r0) != 0) ||
            (parseNumber(operands[1], &immediate) != 0) ||
            (immediate < 0) || (immediate > 255))
        {
            snprintf(error, errorSize, "LDI syntax: LDI Rn, 0..255");
            return -1;
        }

        *word = (uint16_t) (((uint16_t) LEO8_OP_LDI << 12) |
                            ((uint16_t) r0 << 10) | (uint8_t) immediate);
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

    if ((strcmp(mnemonic, "ADD") == 0) ||
        (strcmp(mnemonic, "ADC") == 0) ||
        (strcmp(mnemonic, "SUB") == 0) ||
        (strcmp(mnemonic, "SBC") == 0) ||
        (strcmp(mnemonic, "AND") == 0) ||
        (strcmp(mnemonic, "OR")  == 0) ||
        (strcmp(mnemonic, "XOR") == 0) ||
        (strcmp(mnemonic, "CMP") == 0))
    {
        uint8_t function;

        if ((operandCount != 2) ||
            (parseRegister(operands[0], &r0) != 0) ||
            (parseRegister(operands[1], &r1) != 0))
        {
            snprintf(error, errorSize, "%s syntax: %s Rn, Rm", mnemonic, mnemonic);
            return -1;
        }

        if      (strcmp(mnemonic, "ADD") == 0) function = LEO8_ALU_ADD;
        else if (strcmp(mnemonic, "ADC") == 0) function = LEO8_ALU_ADC;
        else if (strcmp(mnemonic, "SUB") == 0) function = LEO8_ALU_SUB;
        else if (strcmp(mnemonic, "SBC") == 0) function = LEO8_ALU_SBC;
        else if (strcmp(mnemonic, "AND") == 0) function = LEO8_ALU_AND;
        else if (strcmp(mnemonic, "OR")  == 0) function = LEO8_ALU_OR;
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

    if ((strcmp(mnemonic, "JMP") == 0) || (strcmp(mnemonic, "CALL") == 0))
    {
        uint8_t opcode = (strcmp(mnemonic, "JMP") == 0) ? LEO8_OP_JMP : LEO8_OP_CALL;

        if ((operandCount != 1) ||
            (resolveAddress(labels, operands[0], &target) != 0))
        {
            snprintf(error, errorSize, "%s requires a valid label or address", mnemonic);
            return -1;
        }

        *word = (uint16_t)(((uint16_t)opcode << 12) | (target & 0x0FFFu));
        return 0;
    }

    if ((strcmp(mnemonic, "BEQ") == 0) || (strcmp(mnemonic, "BNE") == 0))
    {
        uint8_t opcode = (strcmp(mnemonic, "BEQ") == 0) ? LEO8_OP_BEQ : LEO8_OP_BNE;

        if ((operandCount != 1) ||
            (resolveAddress(labels, operands[0], &target) != 0))
        {
            snprintf(error, errorSize, "%s requires a valid label or address", mnemonic);
            return -1;
        }

        /*
         * Relative branches start from the instruction following the branch.
         * During execution PC has already been incremented by the fetch cycle.
         */
        offset = (int32_t)target - ((int32_t)address + 1);
        if ((offset < -128) || (offset > 127))
        {
            snprintf(error, errorSize, "%s target is outside the relative range", mnemonic);
            return -1;
        }

        *word = (uint16_t)(((uint16_t)opcode << 12) | (uint8_t)offset);
        return 0;
    }

    snprintf(error, errorSize, "unknown instruction '%s'", mnemonic);
    return -1;
}

/******************************************************************************
 * First assembler pass
 ******************************************************************************/

/**
 * @brief Collect labels and assign their program addresses.
 *
 * Labels themselves do not occupy program memory. The address is increased
 * only when a real instruction is found.
 */
static int firstPass(FILE *source, LabelTable *labels)
{
    char line[LEO8_MAX_LINE];
    uint16_t address = 0u;
    unsigned lineNumber = 0u;

    while (fgets(line, sizeof(line), source) != NULL)
    {
        char *colon;
        lineNumber++;
        removeComment(line);
        trim(line);

        if (line[0] == '\0')
        {
            continue;
        }

        /* A label may appear alone or before an instruction on the same line. */
        colon = strchr(line, ':');
        if (colon != NULL)
        {
            *colon = '\0';
            trim(line);
            uppercase(line);

            if ((line[0] == '\0') || (addLabel(labels, line, address) != 0))
            {
                fprintf(stderr, "Assembler error in line %u: invalid or duplicate label\n",
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

/**
 * @brief Create the source column for the LST file.
 *
 * Jump and CALL labels are replaced by absolute addresses because this is what
 * the CPU actually receives. Relative branches additionally show their signed
 * offset and retain the original label as a comment.
 */
static void formatListingSource(const char *source, uint16_t address,
                                const LabelTable *labels,
                                char *output, size_t outputSize)
{
    char line[LEO8_MAX_LINE];
    char mnemonic[LEO8_MAX_TOKEN];
    char operand[LEO8_MAX_TOKEN];
    char *space;
    uint16_t target;

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

    if ((strcmp(mnemonic, "JMP") == 0) ||
        (strcmp(mnemonic, "CALL") == 0) ||
        (strcmp(mnemonic, "BEQ") == 0) ||
        (strcmp(mnemonic, "BNE") == 0))
    {
        if (resolveAddress(labels, operand, &target) == 0)
        {
            long numericOperand;

            if (parseNumber(operand, &numericOperand) == 0)
            {
                snprintf(output, outputSize, "%s %04X", mnemonic, target);
            }
            else if ((strcmp(mnemonic, "BEQ") == 0) ||
                     (strcmp(mnemonic, "BNE") == 0))
            {
                int32_t offset = (int32_t)target - ((int32_t)address + 1);
                snprintf(output, outputSize, "%s %04X    ; %s, offset %+ld",
                         mnemonic, target, operand, (long)offset);
            }
            else
            {
                snprintf(output, outputSize, "%s %04X    ; %s",
                         mnemonic, target, operand);
            }
            return;
        }
    }

    snprintf(output, outputSize, "%s", source);
}

/******************************************************************************
 * Second assembler pass
 ******************************************************************************/

/**
 * @brief Encode all instructions and write HEX and LST output files.
 *
 * At this point the symbol table is complete, therefore forward references can
 * be resolved just like backward references.
 */
static int secondPass(FILE *source, FILE *hexFile, FILE *listing,
                      const LabelTable *labels, uint16_t *instructionCount)
{
    char original[LEO8_MAX_LINE];
    char line[LEO8_MAX_LINE];
    uint16_t address = 0u;
    unsigned lineNumber = 0u;

    fprintf(listing, "ADDR  CODE  SOURCE\n");
    fprintf(listing, "----  ----  ----------------------------------------\n");

    while (fgets(original, sizeof(original), source) != NULL)
    {
        char *colon;
        uint16_t word;
        char error[96];

        lineNumber++;
        snprintf(line, sizeof(line), "%s", original);
        removeComment(line);
        trim(line);

        if (line[0] == '\0')
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

        if (assembleInstruction(line, address, labels, &word,
                                error, sizeof(error)) != 0)
        {
            fprintf(stderr, "Assembler error in line %u: %s\n", lineNumber, error);
            return -1;
        }

        char listingSource[LEO8_MAX_LINE];

        fprintf(hexFile, "%04X\n", word);
        trim(original);
        formatListingSource(line, address, labels,
                            listingSource, sizeof(listingSource));
        fprintf(listing, "%04X  %04X  %s\n",
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
    LabelTable labels = {0};
    int result;

    source = fopen(sourceName, "r");
    if (source == NULL)
    {
        perror(sourceName);
        return -1;
    }

    /* Pass 1 builds the symbol table; no machine code is written yet. */
    result = firstPass(source, &labels);
    if (result != 0)
    {
        fclose(source);
        return -1;
    }

    /* Read the same source again now that every label address is known. */
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

    /* Pass 2 performs the actual instruction encoding. */
    result = secondPass(source, hexFile, listing, &labels, instructionCount);

    fclose(listing);
    fclose(hexFile);
    fclose(source);
    return result;
}
