// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

void emit_byte(ASSEMBLER *as, uint8_t byte_value) {
    TARGET *target = as->active_target;
    SEGMENT *segment = target ? target->active_segment : NULL;
    if(!segment) {
        asm_err(as, ASM_ERR_FATAL, "No active segment for output");
        return;
    }

    uint16_t address = segment->segment_output_address;
    if(as->pass == 2 && !segment->do_not_emit) {
        as->cb.output_byte(target->ctx, address, byte_value);
    }
    segment->segment_output_address = address + 1;
}

void emit_opcode(ASSEMBLER *as) {
    int8_t opcode = (int8_t)asm_opcode[as->opcode_info.opcode_id][as->opcode_info.addressing_mode];
    if(opcode == -1) {
        asm_err(as, ASM_ERR_RESOLVE, "Invalid opcode %.3s with mode %s", as->opcode_info.mnemonic,
                address_mode_txt[as->opcode_info.addressing_mode]);
    }
    if(opcode != -1 && !asm_opcode_allowed(
            as->cpu_profile,
            as->opcode_info.opcode_id,
            as->opcode_info.addressing_mode)) {
        asm_err(as, ASM_ERR_RESOLVE, "Opcode %s with mode %s is not valid for %s",
                as->opcode_info.mnemonic,
                address_mode_txt[as->opcode_info.addressing_mode],
                assembler_cpu_profile_name(as->cpu_profile));
    }

    emit_byte(as, (uint8_t)opcode);
    switch(as->opcode_info.width) {
    case 1: {
        int32_t delta = (int32_t)as->opcode_info.value - 1 - current_output_address(as);
        if(delta > 127 || delta < -128) {
            asm_err(as, ASM_ERR_RESOLVE, "Relative branch out of range $%X", delta);
        }
        emit_byte(as, (uint8_t)delta);
        break;
    }
    case 8:
        if(as->opcode_info.value >= 256) {
            asm_err(as, ASM_ERR_RESOLVE, "8-bit value expected but value = $%llX",
                    (unsigned long long)as->opcode_info.value);
        }
        emit_byte(as, (uint8_t)as->opcode_info.value);
        break;
    case 16:
        if(as->opcode_info.value >= 65536) {
            asm_err(as, ASM_ERR_RESOLVE, "16-bit value expected but value = $%llX",
                    (unsigned long long)as->opcode_info.value);
        }
        emit_byte(as, (uint8_t)as->opcode_info.value);
        emit_byte(as, (uint8_t)(as->opcode_info.value >> 8));
        break;
    default:
        break;
    }
}

void emit_values(ASSEMBLER *as, uint64_t value, int width, int order) {
    if(order == BYTE_ORDER_HI) {
        for(int shift = width - 8; shift >= 0; shift -= 8) {
            emit_byte(as, (uint8_t)(value >> shift));
        }
    } else {
        for(int shift = 0; shift < width; shift += 8) {
            emit_byte(as, (uint8_t)(value >> shift));
        }
    }

    if(width == 8 && value >= 256) {
        asm_err(as, ASM_ERR_RESOLVE, "Warning: value (%llu) > 255 output as byte value",
                (unsigned long long)value);
    } else if(width == 16 && value >= 65536) {
        asm_err(as, ASM_ERR_RESOLVE, "Warning: value (%llu) > 65535 output as word value",
                (unsigned long long)value);
    } else if(width == 32 && value >= 0x100000000ULL) {
        asm_err(as, ASM_ERR_RESOLVE, "Warning: value ($%llX) > $FFFFFFFF output as dword value",
                (unsigned long long)value);
    }
}

static uint64_t parse_escape_value(const char **pp) {
    const char *p = *pp;
    uint64_t value;

    switch(*p) {
    case 'x':
        p++;
        value = strtoull(p, (char **)&p, 16);
        p--;
        break;
    case '%':
        p++;
        value = strtoull(p, (char **)&p, 2);
        p--;
        break;
    case 'n':
        value = '\n';
        break;
    case 'r':
        value = '\r';
        break;
    case 't':
        value = '\t';
        break;
    default:
        if(*p >= '0' && *p <= '9') {
            value = strtoull(p, (char **)&p, 10);
            p--;
        } else {
            value = (uint8_t)*p;
        }
        break;
    }
    *pp = p;
    return value;
}

void emit_string(ASSEMBLER *as, SYMBOL_LABEL *sl) {
    if(as->token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, "String token expected");
        return;
    }

    const char *p = as->token.name;
    const char *end = p + as->token.name_length;
    while(p < end) {
        uint64_t value;
        if(*p == '\\' && p + 1 < end) {
            p++;
            value = parse_escape_value(&p);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Escape value %llu not between 0 and 255",
                        (unsigned long long)value);
            }
        } else {
            value = (uint8_t)*p;
            if(sl && as->strcode) {
                TOKEN saved_token = as->token;
                const char *saved_cur = as->cur;
                sl->symbol_value = value;
                as->cur = as->strcode;
                value = (uint64_t)expr_full_evaluate(as);
                as->cur = saved_cur;
                as->token = saved_token;
            }
        }
        emit_byte(as, (uint8_t)value);
        p++;
    }
    next_token(as);
}

void emit_cs_values(ASSEMBLER *as, int width, int order) {
    do {
        emit_values(as, (uint64_t)expr_full_evaluate(as), width, order);
    } while(as->token.op == ',');
}
