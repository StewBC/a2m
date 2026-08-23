#include "disasm_pc_lock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    DISASM_PC_LOCK_DP_INF = 0xFFFF
};

typedef struct disasm_pc_lock_dp_node {
    uint16_t score;
    uint8_t nsteps;
    uint8_t step;
    bool is_byte_edge;
} disasm_pc_lock_dp_node;

size_t disasm_pc_lock_fetch(
    const disasm_pc_lock_cache *cache,
    uint16_t address,
    uint8_t out[3])
{
    size_t available = 0;
    uint8_t i;

    if (cache == NULL || out == NULL) {
        return 0;
    }

    for (i = 0; i < 3u; ++i) {
        uint16_t a = (uint16_t)(address + i);
        if (!cache->valid[a]) {
            break;
        }
        out[i] = cache->bytes[a];
        available = (size_t)(i + 1u);
    }
    return available;
}

bool disasm_pc_lock_ends_at_or_before(uint16_t address, uint8_t length, uint16_t pc)
{
    if (length == 0u) {
        return true;
    }
    /* Forward distance on the 16-bit bus. (addr+len) <= pc is wrong when the
     * window wraps through $FFFF: a 1-byte line at $FFFE with pc=$0001 has
     * uint16 sum $FFFF, which is not <= $0001, even though the byte is before
     * PC. */
    return (uint16_t)(pc - address) >= length;
}

static void disasm_pc_lock_emit_provisional(disasm_pc_lock_line *line, uint16_t address)
{
    memset(line, 0, sizeof(*line));
    line->base.address = address;
    line->base.length = 1;
    snprintf(line->base.text, sizeof(line->base.text), "???");
    line->is_provisional = true;
}

static void disasm_pc_lock_emit_decoded(
    disasm_pc_lock_line *line,
    const disasm_pc_lock_cache *cache,
    const symbol_resolver *symbols,
    uint16_t address,
    uint8_t step,
    bool force_byte)
{
    uint8_t fetched[3] = {0, 0, 0};
    size_t available = disasm_pc_lock_fetch(cache, address, fetched);

    if (force_byte && available > 0u) {
        memset(line, 0, sizeof(*line));
        line->base.address = address;
        line->base.length = 1;
        line->base.bytes[0] = fetched[0];
        line->base.forced_byte = true;
        snprintf(line->base.text, sizeof(line->base.text), ".BYTE $%02X", fetched[0]);
        line->is_provisional = false;
    } else if (available == 0u) {
        disasm_pc_lock_emit_provisional(line, address);
    } else {
        line->base = disasm_6502_decode_line(address, fetched, available, symbols);
        line->is_provisional = false;
    }

    /* DP chose this row's occupancy. Keep the walk on that path even if
     * decode truncated for missing operand bytes. */
    if (step > 0u && line->base.length != step) {
        size_t copy = available;
        if (copy > step) {
            copy = step;
        }
        if (copy > 3u) {
            copy = 3u;
        }
        memcpy(line->base.bytes, fetched, copy);
        line->base.length = step;
    }
}

static bool disasm_pc_lock_bytes_valid(
    const disasm_pc_lock_cache *cache,
    uint16_t address,
    uint8_t length)
{
    uint8_t i;

    if (length == 0u) {
        return false;
    }
    for (i = 0; i < length; ++i) {
        if (!cache->valid[(uint16_t)(address + i)]) {
            return false;
        }
    }
    return true;
}

void disasm_pc_lock_build(
    const disasm_pc_lock_cache *cache,
    const symbol_resolver *symbols,
    uint16_t pc,
    uint8_t rows,
    disasm_pc_lock_line *lines,
    uint16_t *out_top_address)
{
    uint8_t pc_row;
    uint8_t pre_pc_rows;
    uint8_t row;
    uint16_t window_size;
    uint16_t search_start;
    disasm_pc_lock_dp_node dp[DISASM_PC_LOCK_DP_MAX_WINDOW + 1];
    uint16_t i;

    if (cache == NULL || lines == NULL || rows == 0 || rows > DISASM_PC_LOCK_MAX_ROWS) {
        return;
    }

    pc_row = rows / 2u;
    pre_pc_rows = pc_row;

    {
        uint16_t addr = pc;
        for (row = pc_row; row < rows; ++row) {
            disasm_pc_lock_emit_decoded(&lines[row], cache, symbols, addr, 0, false);
            addr = (uint16_t)(addr + lines[row].base.length);
        }
    }

    if (pre_pc_rows == 0) {
        if (out_top_address != NULL) {
            *out_top_address = pc;
        }
        return;
    }

    window_size = (uint16_t)((uint16_t)pre_pc_rows * 3u + DISASM_PC_LOCK_CENTER_SLOP);
    if (window_size > DISASM_PC_LOCK_DP_MAX_WINDOW) {
        window_size = DISASM_PC_LOCK_DP_MAX_WINDOW;
    }
    search_start = (uint16_t)(pc - window_size);

    for (i = 0; i <= window_size; ++i) {
        dp[i].score = (uint16_t)DISASM_PC_LOCK_DP_INF;
        dp[i].nsteps = 0;
        dp[i].step = 0;
        dp[i].is_byte_edge = false;
    }
    dp[window_size].score = 0;

    for (i = window_size; i-- > 0; ) {
        uint16_t addr = (uint16_t)(search_start + i);

        if (!cache->valid[addr]) {
            continue;
        }

        {
            uint8_t opcode = cache->bytes[addr];
            uint8_t len = disasm_6502_instruction_length(opcode);
            uint16_t j = (uint16_t)(i + len);
            if (j <= window_size &&
                disasm_pc_lock_bytes_valid(cache, addr, len) &&
                dp[j].score != (uint16_t)DISASM_PC_LOCK_DP_INF) {
                uint16_t edge_cost = disasm_6502_opcode_is_valid(opcode) ? 1u : 10u;
                uint16_t cand_score = (uint16_t)(edge_cost + dp[j].score);
                uint8_t cand_steps = dp[j].nsteps < 254u ? (uint8_t)(dp[j].nsteps + 1u) : 255u;
                if (cand_score < dp[i].score) {
                    dp[i].score = cand_score;
                    dp[i].nsteps = cand_steps;
                    dp[i].step = len;
                    dp[i].is_byte_edge = false;
                }
            }
        }

        {
            uint16_t j1 = (uint16_t)(i + 1u);
            if (j1 <= window_size && dp[j1].score != (uint16_t)DISASM_PC_LOCK_DP_INF) {
                uint16_t cand_score = (uint16_t)(100u + dp[j1].score);
                uint8_t cand_steps = dp[j1].nsteps < 254u ? (uint8_t)(dp[j1].nsteps + 1u) : 255u;
                if (cand_score < dp[i].score) {
                    dp[i].score = cand_score;
                    dp[i].nsteps = cand_steps;
                    dp[i].step = 1u;
                    dp[i].is_byte_edge = true;
                }
            }
        }
    }

    {
        int best_start = -1;
        uint16_t best_score = (uint16_t)DISASM_PC_LOCK_DP_INF;
        uint8_t best_steps = 0;

        for (i = 0; i < window_size; ++i) {
            if (dp[i].score != (uint16_t)DISASM_PC_LOCK_DP_INF &&
                dp[i].nsteps == pre_pc_rows &&
                dp[i].score < best_score) {
                best_score = dp[i].score;
                best_start = (int)i;
                best_steps = pre_pc_rows;
            }
        }

        if (best_start < 0) {
            uint8_t max_steps = 0;
            for (i = 0; i < window_size; ++i) {
                if (dp[i].score != (uint16_t)DISASM_PC_LOCK_DP_INF &&
                    dp[i].nsteps > 0 && dp[i].nsteps < pre_pc_rows &&
                    dp[i].nsteps > max_steps) {
                    max_steps = dp[i].nsteps;
                }
            }
            if (max_steps > 0) {
                for (i = 0; i < window_size; ++i) {
                    if (dp[i].score != (uint16_t)DISASM_PC_LOCK_DP_INF &&
                        dp[i].nsteps == max_steps &&
                        dp[i].score < best_score) {
                        best_score = dp[i].score;
                        best_start = (int)i;
                        best_steps = max_steps;
                    }
                }
            }
        }

        if (best_start >= 0) {
            uint8_t provisional_rows = (uint8_t)(pre_pc_rows - best_steps);
            uint16_t path_start = (uint16_t)(search_start + (uint16_t)best_start);
            uint16_t addr;
            uint16_t node;

            for (row = 0; row < provisional_rows; ++row) {
                uint16_t prov_addr = (uint16_t)(path_start -
                    (uint16_t)(provisional_rows - row));
                disasm_pc_lock_emit_provisional(&lines[row], prov_addr);
            }

            addr = path_start;
            node = (uint16_t)best_start;
            for (row = provisional_rows; row < pre_pc_rows; ++row) {
                uint8_t step = dp[node].step;
                if (step == 0u) {
                    step = 1u;
                }
                disasm_pc_lock_emit_decoded(
                    &lines[row],
                    cache,
                    symbols,
                    addr,
                    step,
                    dp[node].is_byte_edge && cache->valid[addr]);
                node = (uint16_t)(node + step);
                addr = (uint16_t)(addr + step);
            }
        } else {
            for (row = 0; row < pre_pc_rows; ++row) {
                uint16_t prov_addr = (uint16_t)(pc -
                    (uint16_t)(pre_pc_rows - row));
                disasm_pc_lock_emit_provisional(&lines[row], prov_addr);
            }
        }
    }

    if (out_top_address != NULL) {
        *out_top_address = lines[0].base.address;
    }

#ifndef NDEBUG
    assert(lines[pc_row].base.address == pc);
    {
        uint8_t k;
        for (k = 0; k < pc_row; ++k) {
            uint16_t addr = lines[k].base.address;
            uint8_t len = lines[k].base.length;
            uint16_t dist = (uint16_t)(pc - addr);
            assert(len > 0);
            assert(disasm_pc_lock_ends_at_or_before(addr, len, pc));
            assert(dist <= (uint16_t)(window_size + pre_pc_rows));
        }
    }
#endif
}
