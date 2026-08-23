#ifndef A2M_DISASM_PC_LOCK_H
#define A2M_DISASM_PC_LOCK_H

/*
 * PC-centered disassembly: DP walk backward from PC so the PC row stays
 * fixed and no pre-PC instruction overlaps PC. Address arithmetic is 16-bit
 * wrapping — a path that crosses $FFFF/$0000 is valid.
 */

#include "disasm_6502.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DISASM_PC_LOCK_MAX_ROWS = 128,
    DISASM_PC_LOCK_DP_MAX_WINDOW = 255,
    DISASM_PC_LOCK_CENTER_SLOP = 32
};

typedef struct disasm_pc_lock_cache {
    uint8_t bytes[65536];
    bool valid[65536];
} disasm_pc_lock_cache;

typedef struct disasm_pc_lock_line {
    disasm_6502_line base;
    bool is_provisional;
} disasm_pc_lock_line;

/* Copy up to 3 bytes at address, wrapping the 16-bit bus. */
size_t disasm_pc_lock_fetch(
    const disasm_pc_lock_cache *cache,
    uint16_t address,
    uint8_t out[3]);

/*
 * True if [address, address+length) walking forward on the 16-bit bus ends
 * at or before pc (does not contain pc). length 0 is empty and does not
 * contain pc.
 */
bool disasm_pc_lock_ends_at_or_before(uint16_t address, uint8_t length, uint16_t pc);

void disasm_pc_lock_build(
    const disasm_pc_lock_cache *cache,
    const symbol_resolver *symbols,
    uint16_t pc,
    uint8_t rows,
    disasm_pc_lock_line *lines,
    uint16_t *out_top_address);

#ifdef __cplusplus
}
#endif

#endif /* A2M_DISASM_PC_LOCK_H */
