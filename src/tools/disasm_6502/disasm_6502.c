#include "disasm_6502.h"

#include <stdio.h>
#include <string.h>

typedef struct opcode_info {
    const char *mnemonic;
    disasm_6502_mode mode;
    uint8_t length;
} opcode_info;

#define OP(mn, mode, len) {mn, mode, len}
#define XX OP(NULL, DISASM_MODE_IMP, 1)

static const opcode_info opcode_table[256] = {
    OP("BRK",DISASM_MODE_IMP,1), OP("ORA",DISASM_MODE_INDX,2), XX, XX, XX, OP("ORA",DISASM_MODE_ZP,2), OP("ASL",DISASM_MODE_ZP,2), XX,
    OP("PHP",DISASM_MODE_IMP,1), OP("ORA",DISASM_MODE_IMM,2), OP("ASL",DISASM_MODE_ACC,1), XX, XX, OP("ORA",DISASM_MODE_ABS,3), OP("ASL",DISASM_MODE_ABS,3), XX,
    OP("BPL",DISASM_MODE_REL,2), OP("ORA",DISASM_MODE_INDY,2), XX, XX, XX, OP("ORA",DISASM_MODE_ZPX,2), OP("ASL",DISASM_MODE_ZPX,2), XX,
    OP("CLC",DISASM_MODE_IMP,1), OP("ORA",DISASM_MODE_ABSY,3), XX, XX, XX, OP("ORA",DISASM_MODE_ABSX,3), OP("ASL",DISASM_MODE_ABSX,3), XX,
    OP("JSR",DISASM_MODE_ABS,3), OP("AND",DISASM_MODE_INDX,2), XX, XX, OP("BIT",DISASM_MODE_ZP,2), OP("AND",DISASM_MODE_ZP,2), OP("ROL",DISASM_MODE_ZP,2), XX,
    OP("PLP",DISASM_MODE_IMP,1), OP("AND",DISASM_MODE_IMM,2), OP("ROL",DISASM_MODE_ACC,1), XX, OP("BIT",DISASM_MODE_ABS,3), OP("AND",DISASM_MODE_ABS,3), OP("ROL",DISASM_MODE_ABS,3), XX,
    OP("BMI",DISASM_MODE_REL,2), OP("AND",DISASM_MODE_INDY,2), XX, XX, XX, OP("AND",DISASM_MODE_ZPX,2), OP("ROL",DISASM_MODE_ZPX,2), XX,
    OP("SEC",DISASM_MODE_IMP,1), OP("AND",DISASM_MODE_ABSY,3), XX, XX, XX, OP("AND",DISASM_MODE_ABSX,3), OP("ROL",DISASM_MODE_ABSX,3), XX,
    OP("RTI",DISASM_MODE_IMP,1), OP("EOR",DISASM_MODE_INDX,2), XX, XX, XX, OP("EOR",DISASM_MODE_ZP,2), OP("LSR",DISASM_MODE_ZP,2), XX,
    OP("PHA",DISASM_MODE_IMP,1), OP("EOR",DISASM_MODE_IMM,2), OP("LSR",DISASM_MODE_ACC,1), XX, OP("JMP",DISASM_MODE_ABS,3), OP("EOR",DISASM_MODE_ABS,3), OP("LSR",DISASM_MODE_ABS,3), XX,
    OP("BVC",DISASM_MODE_REL,2), OP("EOR",DISASM_MODE_INDY,2), XX, XX, XX, OP("EOR",DISASM_MODE_ZPX,2), OP("LSR",DISASM_MODE_ZPX,2), XX,
    OP("CLI",DISASM_MODE_IMP,1), OP("EOR",DISASM_MODE_ABSY,3), XX, XX, XX, OP("EOR",DISASM_MODE_ABSX,3), OP("LSR",DISASM_MODE_ABSX,3), XX,
    OP("RTS",DISASM_MODE_IMP,1), OP("ADC",DISASM_MODE_INDX,2), XX, XX, XX, OP("ADC",DISASM_MODE_ZP,2), OP("ROR",DISASM_MODE_ZP,2), XX,
    OP("PLA",DISASM_MODE_IMP,1), OP("ADC",DISASM_MODE_IMM,2), OP("ROR",DISASM_MODE_ACC,1), XX, OP("JMP",DISASM_MODE_IND,3), OP("ADC",DISASM_MODE_ABS,3), OP("ROR",DISASM_MODE_ABS,3), XX,
    OP("BVS",DISASM_MODE_REL,2), OP("ADC",DISASM_MODE_INDY,2), XX, XX, XX, OP("ADC",DISASM_MODE_ZPX,2), OP("ROR",DISASM_MODE_ZPX,2), XX,
    OP("SEI",DISASM_MODE_IMP,1), OP("ADC",DISASM_MODE_ABSY,3), XX, XX, XX, OP("ADC",DISASM_MODE_ABSX,3), OP("ROR",DISASM_MODE_ABSX,3), XX,
    XX, OP("STA",DISASM_MODE_INDX,2), XX, XX, OP("STY",DISASM_MODE_ZP,2), OP("STA",DISASM_MODE_ZP,2), OP("STX",DISASM_MODE_ZP,2), XX,
    OP("DEY",DISASM_MODE_IMP,1), XX, OP("TXA",DISASM_MODE_IMP,1), XX, OP("STY",DISASM_MODE_ABS,3), OP("STA",DISASM_MODE_ABS,3), OP("STX",DISASM_MODE_ABS,3), XX,
    OP("BCC",DISASM_MODE_REL,2), OP("STA",DISASM_MODE_INDY,2), XX, XX, OP("STY",DISASM_MODE_ZPX,2), OP("STA",DISASM_MODE_ZPX,2), OP("STX",DISASM_MODE_ZPY,2), XX,
    OP("TYA",DISASM_MODE_IMP,1), OP("STA",DISASM_MODE_ABSY,3), OP("TXS",DISASM_MODE_IMP,1), XX, XX, OP("STA",DISASM_MODE_ABSX,3), XX, XX,
    OP("LDY",DISASM_MODE_IMM,2), OP("LDA",DISASM_MODE_INDX,2), OP("LDX",DISASM_MODE_IMM,2), XX, OP("LDY",DISASM_MODE_ZP,2), OP("LDA",DISASM_MODE_ZP,2), OP("LDX",DISASM_MODE_ZP,2), XX,
    OP("TAY",DISASM_MODE_IMP,1), OP("LDA",DISASM_MODE_IMM,2), OP("TAX",DISASM_MODE_IMP,1), XX, OP("LDY",DISASM_MODE_ABS,3), OP("LDA",DISASM_MODE_ABS,3), OP("LDX",DISASM_MODE_ABS,3), XX,
    OP("BCS",DISASM_MODE_REL,2), OP("LDA",DISASM_MODE_INDY,2), XX, XX, OP("LDY",DISASM_MODE_ZPX,2), OP("LDA",DISASM_MODE_ZPX,2), OP("LDX",DISASM_MODE_ZPY,2), XX,
    OP("CLV",DISASM_MODE_IMP,1), OP("LDA",DISASM_MODE_ABSY,3), OP("TSX",DISASM_MODE_IMP,1), XX, OP("LDY",DISASM_MODE_ABSX,3), OP("LDA",DISASM_MODE_ABSX,3), OP("LDX",DISASM_MODE_ABSY,3), XX,
    OP("CPY",DISASM_MODE_IMM,2), OP("CMP",DISASM_MODE_INDX,2), XX, XX, OP("CPY",DISASM_MODE_ZP,2), OP("CMP",DISASM_MODE_ZP,2), OP("DEC",DISASM_MODE_ZP,2), XX,
    OP("INY",DISASM_MODE_IMP,1), OP("CMP",DISASM_MODE_IMM,2), OP("DEX",DISASM_MODE_IMP,1), XX, OP("CPY",DISASM_MODE_ABS,3), OP("CMP",DISASM_MODE_ABS,3), OP("DEC",DISASM_MODE_ABS,3), XX,
    OP("BNE",DISASM_MODE_REL,2), OP("CMP",DISASM_MODE_INDY,2), XX, XX, XX, OP("CMP",DISASM_MODE_ZPX,2), OP("DEC",DISASM_MODE_ZPX,2), XX,
    OP("CLD",DISASM_MODE_IMP,1), OP("CMP",DISASM_MODE_ABSY,3), XX, XX, XX, OP("CMP",DISASM_MODE_ABSX,3), OP("DEC",DISASM_MODE_ABSX,3), XX,
    OP("CPX",DISASM_MODE_IMM,2), OP("SBC",DISASM_MODE_INDX,2), XX, XX, OP("CPX",DISASM_MODE_ZP,2), OP("SBC",DISASM_MODE_ZP,2), OP("INC",DISASM_MODE_ZP,2), XX,
    OP("INX",DISASM_MODE_IMP,1), OP("SBC",DISASM_MODE_IMM,2), OP("NOP",DISASM_MODE_IMP,1), XX, OP("CPX",DISASM_MODE_ABS,3), OP("SBC",DISASM_MODE_ABS,3), OP("INC",DISASM_MODE_ABS,3), XX,
    OP("BEQ",DISASM_MODE_REL,2), OP("SBC",DISASM_MODE_INDY,2), XX, XX, XX, OP("SBC",DISASM_MODE_ZPX,2), OP("INC",DISASM_MODE_ZPX,2), XX,
    OP("SED",DISASM_MODE_IMP,1), OP("SBC",DISASM_MODE_ABSY,3), XX, XX, XX, OP("SBC",DISASM_MODE_ABSX,3), OP("INC",DISASM_MODE_ABSX,3), XX
};

static symbol_lookup_result null_address_to_label(
    void *userdata,
    uint16_t address,
    char *out_label,
    size_t out_label_size)
{
    (void)userdata;
    (void)address;
    if (out_label != NULL && out_label_size > 0) {
        out_label[0] = '\0';
    }
    return SYMBOL_LOOKUP_NOT_FOUND;
}

static symbol_lookup_result null_label_to_address(
    void *userdata,
    const char *label,
    uint16_t *out_address)
{
    (void)userdata;
    (void)label;
    (void)out_address;
    return SYMBOL_LOOKUP_NOT_FOUND;
}

static size_t null_enumerate(void *userdata, symbol_entry *out_entries, size_t max_entries)
{
    (void)userdata;
    (void)out_entries;
    (void)max_entries;
    return 0;
}

void symbol_resolver_null(symbol_resolver *resolver)
{
    if (resolver == NULL) {
        return;
    }

    resolver->userdata = NULL;
    resolver->address_to_label = null_address_to_label;
    resolver->label_to_address = null_label_to_address;
    resolver->enumerate = null_enumerate;
}

uint8_t disasm_6502_instruction_length(uint8_t opcode)
{
    return opcode_table[opcode].length;
}

bool disasm_6502_opcode_is_valid(uint8_t opcode)
{
    return opcode_table[opcode].mnemonic != NULL;
}

disasm_6502_mode disasm_6502_opcode_mode(uint8_t opcode)
{
    return opcode_table[opcode].mode;
}

static bool resolve_label(
    const symbol_resolver *symbols,
    uint16_t address,
    char *out,
    size_t out_size)
{
    if (symbols == NULL || symbols->address_to_label == NULL) {
        return false;
    }

    return symbols->address_to_label(symbols->userdata, address, out, out_size) == SYMBOL_LOOKUP_FOUND;
}

static void format_absolute_operand(
    char *out,
    size_t out_size,
    const char *prefix,
    uint16_t address,
    const char *suffix,
    const symbol_resolver *symbols)
{
    char label[32];

    if (resolve_label(symbols, address, label, sizeof(label))) {
        snprintf(out, out_size, "%s%s%s", prefix, label, suffix);
        return;
    }

    snprintf(out, out_size, "%s$%04X%s", prefix, address, suffix);
}

disasm_6502_line disasm_6502_decode_line(
    uint16_t address,
    const uint8_t *bytes,
    size_t length,
    const symbol_resolver *symbols)
{
    disasm_6502_line line;
    opcode_info info;
    uint8_t opcode;
    uint16_t operand16;
    uint16_t target;
    char operand[32] = "";

    memset(&line, 0, sizeof(line));
    line.address = address;
    if (bytes == NULL || length == 0) {
        line.length = 1;
        line.forced_byte = true;
        snprintf(line.text, sizeof(line.text), ".BYTE $00");
        return line;
    }

    opcode = bytes[0];
    info = opcode_table[opcode];
    line.length = info.length;
    if (line.length > length) {
        line.length = (uint8_t)length;
    }
    if (line.length == 0) {
        line.length = 1;
    }
    memcpy(line.bytes, bytes, line.length);

    if (info.mnemonic == NULL) {
        line.length = 1;
        line.forced_byte = true;
        snprintf(line.text, sizeof(line.text), ".BYTE $%02X", opcode);
        return line;
    }

    operand16 = length >= 3 ? (uint16_t)(bytes[1] | ((uint16_t)bytes[2] << 8)) : 0;
    switch (info.mode) {
        case DISASM_MODE_IMP:
            operand[0] = '\0';
            break;
        case DISASM_MODE_ACC:
            snprintf(operand, sizeof(operand), "A");
            break;
        case DISASM_MODE_IMM:
            snprintf(operand, sizeof(operand), "#$%02X", length >= 2 ? bytes[1] : 0);
            break;
        case DISASM_MODE_ZP:
            snprintf(operand, sizeof(operand), "$%02X", length >= 2 ? bytes[1] : 0);
            break;
        case DISASM_MODE_ZPX:
            snprintf(operand, sizeof(operand), "$%02X,X", length >= 2 ? bytes[1] : 0);
            break;
        case DISASM_MODE_ZPY:
            snprintf(operand, sizeof(operand), "$%02X,Y", length >= 2 ? bytes[1] : 0);
            break;
        case DISASM_MODE_ABS:
            format_absolute_operand(operand, sizeof(operand), "", operand16, "", symbols);
            break;
        case DISASM_MODE_ABSX:
            format_absolute_operand(operand, sizeof(operand), "", operand16, ",X", symbols);
            break;
        case DISASM_MODE_ABSY:
            format_absolute_operand(operand, sizeof(operand), "", operand16, ",Y", symbols);
            break;
        case DISASM_MODE_IND:
            format_absolute_operand(operand, sizeof(operand), "(", operand16, ")", symbols);
            break;
        case DISASM_MODE_INDX:
            snprintf(operand, sizeof(operand), "($%02X,X)", length >= 2 ? bytes[1] : 0);
            break;
        case DISASM_MODE_INDY:
            snprintf(operand, sizeof(operand), "($%02X),Y", length >= 2 ? bytes[1] : 0);
            break;
        case DISASM_MODE_REL:
            target = (uint16_t)(address + 2u + (int8_t)(length >= 2 ? bytes[1] : 0));
            format_absolute_operand(operand, sizeof(operand), "", target, "", symbols);
            break;
    }

    if (operand[0] == '\0') {
        snprintf(line.text, sizeof(line.text), "%s", info.mnemonic);
    } else {
        snprintf(line.text, sizeof(line.text), "%s %s", info.mnemonic, operand);
    }
    return line;
}
