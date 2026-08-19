#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct symbol_resolver symbol_resolver;

typedef enum disasm_6502_mode {
    DISASM_MODE_IMP = 0,
    DISASM_MODE_ACC,
    DISASM_MODE_IMM,
    DISASM_MODE_ZP,
    DISASM_MODE_ZPX,
    DISASM_MODE_ZPY,
    DISASM_MODE_ABS,
    DISASM_MODE_ABSX,
    DISASM_MODE_ABSY,
    DISASM_MODE_IND,
    DISASM_MODE_INDX,
    DISASM_MODE_INDY,
    DISASM_MODE_REL
} disasm_6502_mode;

typedef enum symbol_lookup_result {
    SYMBOL_LOOKUP_NOT_FOUND = 0,
    SYMBOL_LOOKUP_FOUND
} symbol_lookup_result;

typedef struct symbol_entry {
    const char *label;
    const char *scope_path;
    const char *display_name;
    size_t scope_path_length;
    size_t display_name_length;
    uint16_t address;
} symbol_entry;

struct symbol_resolver {
    void *userdata;
    symbol_lookup_result (*address_to_label)(
        void *userdata,
        uint16_t address,
        char *out_label,
        size_t out_label_size);
    symbol_lookup_result (*label_to_address)(
        void *userdata,
        const char *label,
        uint16_t *out_address);
    size_t (*enumerate)(
        void *userdata,
        symbol_entry *out_entries,
        size_t max_entries);
};

typedef struct disasm_6502_line {
    uint16_t address;
    uint8_t bytes[3];
    uint8_t length;
    bool forced_byte;
    char text[48];
} disasm_6502_line;

void symbol_resolver_null(symbol_resolver *resolver);

disasm_6502_line disasm_6502_decode_line(
    uint16_t address,
    const uint8_t *bytes,
    size_t length,
    const symbol_resolver *symbols);

uint8_t disasm_6502_instruction_length(uint8_t opcode);
bool disasm_6502_opcode_is_valid(uint8_t opcode);
disasm_6502_mode disasm_6502_opcode_mode(uint8_t opcode);
