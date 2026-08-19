#include "disasm_6502.h"

#include <stdio.h>
#include <string.h>

static int expect_string(const char *actual, const char *expected, const char *label)
{
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "%s: expected '%s', got '%s'\n", label, expected, actual);
        return 1;
    }

    return 0;
}

static symbol_lookup_result test_address_to_label(
    void *userdata,
    uint16_t address,
    char *out_label,
    size_t out_label_size)
{
    (void)userdata;

    if (address != 0xc000 || out_label == NULL || out_label_size < 6) {
        return SYMBOL_LOOKUP_NOT_FOUND;
    }

    snprintf(out_label, out_label_size, "Start");
    return SYMBOL_LOOKUP_FOUND;
}

static symbol_lookup_result test_label_to_address(
    void *userdata,
    const char *label,
    uint16_t *out_address)
{
    (void)userdata;

    if (label == NULL || strcmp(label, "Start") != 0 || out_address == NULL) {
        return SYMBOL_LOOKUP_NOT_FOUND;
    }

    *out_address = 0xc000;
    return SYMBOL_LOOKUP_FOUND;
}

static size_t test_enumerate(void *userdata, symbol_entry *out_entries, size_t max_entries)
{
    (void)userdata;

    if (out_entries != NULL && max_entries > 0) {
        out_entries[0].label = "Start";
        out_entries[0].address = 0xc000;
    }

    return 1;
}

int main(void)
{
    int failures = 0;
    symbol_resolver symbols;
    disasm_6502_line line;
    uint8_t lda[] = {0xa9, 0x7f};
    uint8_t jsr[] = {0x20, 0x00, 0xc0};
    uint8_t bne[] = {0xd0, 0xfc};
    uint8_t bad[] = {0x02};

    symbol_resolver_null(&symbols);

    line = disasm_6502_decode_line(0x0801, lda, sizeof(lda), &symbols);
    failures += expect_string(line.text, "LDA #$7F", "immediate");
    if (line.address != 0x0801 || line.length != 2 || line.forced_byte) {
        fprintf(stderr, "immediate metadata mismatch\n");
        failures++;
    }

    line = disasm_6502_decode_line(0x1000, bne, sizeof(bne), &symbols);
    failures += expect_string(line.text, "BNE $0FFE", "relative");

    symbols.userdata = NULL;
    symbols.address_to_label = test_address_to_label;
    symbols.label_to_address = test_label_to_address;
    symbols.enumerate = test_enumerate;
    line = disasm_6502_decode_line(0x080d, jsr, sizeof(jsr), &symbols);
    failures += expect_string(line.text, "JSR Start", "symbol absolute");

    line = disasm_6502_decode_line(0x2000, bad, sizeof(bad), &symbols);
    failures += expect_string(line.text, ".BYTE $02", "forced byte");
    if (!line.forced_byte || line.length != 1) {
        fprintf(stderr, "forced byte metadata mismatch\n");
        failures++;
    }
    if (!disasm_6502_opcode_is_valid(0xa9) || disasm_6502_opcode_is_valid(0x02)) {
        fprintf(stderr, "opcode validity mismatch\n");
        failures++;
    }

    return failures == 0 ? 0 : 1;
}
