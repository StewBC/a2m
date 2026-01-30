// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// Output
void emit_byte(ASSEMBLER *as, uint8_t byte_value) {
    if(as->pass == 2) {
        if(as->active_segment) {
            if(!as->active_segment->do_not_emit) {
                as->cb_assembler_ctx.output_byte(as->cb_assembler_ctx.user, as->active_segment->segment_output_address, byte_value);
            }
            as->last_address = ++as->active_segment->segment_output_address;
        } else {
            as->cb_assembler_ctx.output_byte(as->cb_assembler_ctx.user, as->current_address, byte_value);
            as->last_address = ++as->current_address;
        }
    } else {
        if(as->active_segment) {
            as->active_segment->segment_output_address++;
        } else {
            as->current_address++;
        }
    }
}

void emit_opcode(ASSEMBLER *as) {
    int8_t opcode = asm_opcode[as->opcode_info.opcode_id][as->opcode_info.addressing_mode];
    if(opcode == -1) {
        asm_err(as, ASM_ERR_RESOLVE, "Invalid opcode %.3s with mode %s", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
    }
    if(as->valid_opcodes && !asm_opcode_type[as->opcode_info.opcode_id][as->opcode_info.addressing_mode]) {
        asm_err(as, ASM_ERR_RESOLVE, "Opcode %.3s with mode %s only valid in 65c02", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
    }

    // First the opcode
    emit_byte(as, opcode);
    // Then the operand (0, ie implied, will do nothing more)
    switch(as->opcode_info.width) {
        case 1: {                                               // Relative - 1 byte
                int32_t delta = as->opcode_info.value - 1 - current_output_address(as);
                if(delta > 128 || delta < -128) {
                    asm_err(as, ASM_ERR_RESOLVE, "Relative branch out of range $%X", delta);
                }
                emit_byte(as, delta);
            }
            break;
        case 8:                                                 // 1 byte
            if(as->opcode_info.value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "8-bit value expected but value = $%X", as->opcode_info.value);
            }
            emit_byte(as, as->opcode_info.value);
            break;
        case 16:                                                // 2 bytes
            if(as->opcode_info.value >= 65536) {
                asm_err(as, ASM_ERR_RESOLVE, "16-bit value expected but value = $%X", as->opcode_info.value);
            }
            emit_byte(as, as->opcode_info.value);
            emit_byte(as, as->opcode_info.value >> 8);
            break;
    }
}

void emit_values(ASSEMBLER *as, uint64_t value, int width, int order) {
    if(order == BYTE_ORDER_HI) {
        if(width == 8) {
            emit_byte(as, value);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value (%zd) > 255 output as byte value", value);
            }
        } else if(width == 16) {
            emit_byte(as, value >> 8);
            emit_byte(as, value);
            if(value >= 65536) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value (%zd) > 65535 output as drow value", value);
            }
        } else if(width == 32) {
            emit_byte(as, value >> 24);
            emit_byte(as, value >> 16);
            emit_byte(as, value >> 8);
            emit_byte(as, value);
            if(value >= 0X100000000) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value ($%zx) > $FFFFFFFF output as drowd value", value);
            }
        } else if(width == 64) {
            emit_byte(as, value >> 56);
            emit_byte(as, value >> 48);
            emit_byte(as, value >> 40);
            emit_byte(as, value >> 32);
            emit_byte(as, value >> 24);
            emit_byte(as, value >> 16);
            emit_byte(as, value >> 8);
            emit_byte(as, value);
            if(value >= 0X8000000000000000) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value ($%zx) > $7FFFFFFFFFFFFFFF output as drowq value", value);
            }
        }
    } else {
        if(width == 8) {
            emit_byte(as, value);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value (%zd) > 255 output as byte value", value);
            }
        } else if(width == 16) {
            emit_byte(as, value);
            emit_byte(as, value >> 8);
            if(value >= 65536) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value (%zd) > 65535 output as word value", value);
            }
        } else if(width == 32) {
            emit_byte(as, value);
            emit_byte(as, value >> 8);
            emit_byte(as, value >> 16);
            emit_byte(as, value >> 24);
            if(value >= 0X100000000) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value ($%zx) > $FFFFFFFF output as dword value", value);
            }
        } else if(width == 64) {
            emit_byte(as, value);
            emit_byte(as, value >> 8);
            emit_byte(as, value >> 16);
            emit_byte(as, value >> 24);
            emit_byte(as, value >> 32);
            emit_byte(as, value >> 40);
            emit_byte(as, value >> 48);
            emit_byte(as, value >> 56);
            if(value >= 0X8000000000000000) {
                asm_err(as, ASM_ERR_RESOLVE, "Warning: value ($%zx) > $7FFFFFFFFFFFFFFF output as qword value", value);
            }
        }
    }
}

void emit_string(ASSEMBLER *as, SYMBOL_LABEL *sl) {
    uint64_t value;
    // Loop over the string but stop before the closing quote
    while(++as->token_start < (as->input - 1)) {
        // Handle quoted numbers (no \n or \t handling here)
        if(*as->token_start == '\\') {
            as->token_start++;
            switch(*as->token_start) {
                case 'x':
                    as->token_start++;
                    value = strtoll(as->token_start, (char **) &as->token_start, 16);
                    as->token_start--;
                    break;
                case '%':
                    as->token_start++;
                    value = strtoll(as->token_start, (char **) &as->token_start, 2);
                    as->token_start--;
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
                    if(*as->token_start >= '0' && *as->token_start <= '9') {
                        value = strtoll(as->token_start, (char **) &as->token_start, 10);                    
                        as->token_start--;
                    } else {
                        value = *as->token_start;
                    }
                    break;
            }
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Escape value %ld not between 0 and 255", value);
            }
            emit_byte(as, value);
        } else {
            // Regular characters can go through the strcode process
            int character = *as->token_start;

            if(sl && as->strcode) {
                // Set the variable with the value of the string character
                sl->symbol_value = character;
                // Save the parse state to parse the expression
                input_stack_push(as);
                as->input = as->token_start = as->strcode;
                // Evaluate the expression
                character = expr_full_evaluate(as);
                // Restore the parse state into the string
                input_stack_pop(as);
            }
            // Write the resultant character out
            emit_byte(as, character);
        }
    }
    next_token(as);
}

void emit_cs_values(ASSEMBLER *as, int width, int order) {
    do {
        emit_values(as, expr_full_evaluate(as), width, order);
    } while(as->current_token.op == ',');
}

