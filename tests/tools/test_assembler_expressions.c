#include "asm.h"
#include "errorlog.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t memory[65536];
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
}

static int write_source(char *path, size_t path_size, const char *source)
{
    return a2m_test_write_temp_file(path, path_size, "a2m_assembler_expressions", source);
}

static int assemble_file(const char *path, test_memory *mem, ERRORLOG *log)
{
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    memset(&cb, 0, sizeof(cb));
    cb.user = mem;
    cb.output_byte = output_byte;

    if (assembler_init(&as, log, &cb) != ASM_OK) {
        fprintf(stderr, "assembler_init failed\n");
        return ASM_ERR;
    }

    result = assembler_assemble(&as, path, 0x0801);
    assembler_shutdown(&as);
    return result;
}

/*
 * The `*` (program counter) symbol must evaluate to the address of the current
 * instruction/line, matching the standard assembler convention. In particular
 * `jmp *` is the classic self-loop and must encode its own address.
 */
static int test_star_is_current_address(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /*
     * $c000: AD 00 C0   lda $c000   ; lda * -> address of this lda
     * $c003: 4C 03 C0   jmp $c003   ; jmp * -> self-loop
     * $c006: 08 C0      .word *     ; -> address of this .word
     */
    const uint8_t expected[] = {
        0xAD, 0x00, 0xC0,
        0x4C, 0x03, 0xC0,
        0x06, 0xC0,
    };
    const char *source =
        "* = $c000\n"
        "    lda *\n"
        "    jmp *\n"
        "    .word *\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);

    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assembler_assemble failed with %zu errors\n", log.log_array.items);
        failures++;
    }

    if (memcmp(&mem.memory[0xC000], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "'*' current-address output mismatch\n");
        for (size_t i = 0; i < sizeof(expected); i++) {
            if (mem.memory[0xC000 + i] != expected[i]) {
                fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                        (size_t)(0xC000 + i), expected[i], mem.memory[0xC000 + i]);
            }
        }
        failures++;
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

/* `*` participates in arithmetic as the current address (e.g. skip-over jump). */
static int test_star_arithmetic(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /*
     * $c000: 4C 05 C0   jmp $c005   ; jmp *+5 -> skip the two .byte $ea after it
     * $c003: EA EA      .byte $ea, $ea
     */
    const uint8_t expected[] = {
        0x4C, 0x05, 0xC0,
        0xEA, 0xEA,
    };
    const char *source =
        "* = $c000\n"
        "    jmp *+5\n"
        "    .byte $ea, $ea\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);

    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assembler_assemble failed with %zu errors\n", log.log_array.items);
        failures++;
    }

    if (memcmp(&mem.memory[0xC000], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "'*' arithmetic output mismatch\n");
        for (size_t i = 0; i < sizeof(expected); i++) {
            if (mem.memory[0xC000 + i] != expected[i]) {
                fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                        (size_t)(0xC000 + i), expected[i], mem.memory[0xC000 + i]);
            }
        }
        failures++;
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

/*
 * Read-modify-write shifts (asl/lsr/rol/ror) must emit their operand byte in
 * memory addressing modes. Regression: lsr carried width 0 in the opcode table,
 * so `lsr $nn` emitted only the opcode ($46) and silently dropped the address
 * byte, desyncing every following instruction. Accumulator mode (no operand)
 * must still emit a lone opcode byte for all four.
 */
static int test_shift_addressing_modes(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /*
     * $c000: 06 03      asl $03
     * $c002: 46 03      lsr $03
     * $c004: 26 03      rol $03
     * $c006: 66 03      ror $03
     * $c008: 4E 00 D0   lsr $d000   ; absolute still 3 bytes
     * $c00b: 0A         asl         ; accumulator, opcode only
     * $c00c: 4A         lsr
     * $c00d: 2A         rol
     * $c00e: 6A         ror
     */
    const uint8_t expected[] = {
        0x06, 0x03,
        0x46, 0x03,
        0x26, 0x03,
        0x66, 0x03,
        0x4E, 0x00, 0xD0,
        0x0A,
        0x4A,
        0x2A,
        0x6A,
    };
    const char *source =
        "* = $c000\n"
        "    asl $03\n"
        "    lsr $03\n"
        "    rol $03\n"
        "    ror $03\n"
        "    lsr $d000\n"
        "    asl\n"
        "    lsr\n"
        "    rol\n"
        "    ror\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);

    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assembler_assemble failed with %zu errors\n", log.log_array.items);
        failures++;
    }

    if (memcmp(&mem.memory[0xC000], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "shift addressing-mode output mismatch\n");
        for (size_t i = 0; i < sizeof(expected); i++) {
            if (mem.memory[0xC000 + i] != expected[i]) {
                fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                        (size_t)(0xC000 + i), expected[i], mem.memory[0xC000 + i]);
            }
        }
        failures++;
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

/*
 * zeropage,Y is a real addressing mode, but only LDX and STX have it. The
 * decoder used to promote every zeropage base + `,y` to absolute,Y, which
 * silently grew `ldx $nn,y` from 2 bytes to 3 (opcode $B6 -> $BE) and made
 * `stx $nn,y` a hard error (STX has no absolute,Y). A zeropage base + `,y` must
 * pick zeropage,Y when the opcode has that form, and only otherwise promote to
 * absolute,Y (e.g. `lda $nn,y`, which genuinely has no zeropage,Y).
 */
static int test_zeropage_y_addressing(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /*
     * $c000: B6 03      ldx $03,y   ; zeropage,Y (not $BE abs,Y)
     * $c002: 96 03      stx $03,y   ; zeropage,Y (STX has no abs,Y)
     * $c004: BE 03 C0   ldx $c003,y ; explicit absolute,Y still 3 bytes
     * $c007: B9 03 00   lda $03,y   ; LDA has no zp,Y -> absolute,Y
     */
    const uint8_t expected[] = {
        0xB6, 0x03,
        0x96, 0x03,
        0xBE, 0x03, 0xC0,
        0xB9, 0x03, 0x00,
    };
    const char *source =
        "* = $c000\n"
        "    ldx $03,y\n"
        "    stx $03,y\n"
        "    ldx $c003,y\n"
        "    lda $03,y\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);

    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assembler_assemble failed with %zu errors\n", log.log_array.items);
        failures++;
    }

    if (memcmp(&mem.memory[0xC000], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "zeropage,Y addressing output mismatch\n");
        for (size_t i = 0; i < sizeof(expected); i++) {
            if (mem.memory[0xC000 + i] != expected[i]) {
                fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                        (size_t)(0xC000 + i), expected[i], mem.memory[0xC000 + i]);
            }
        }
        failures++;
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_star_is_current_address();
    failures += test_star_arithmetic();
    failures += test_shift_addressing_modes();
    failures += test_zeropage_y_addressing();

    return failures == 0 ? 0 : 1;
}
