// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

// addres + tag + hex + op + tag + \0
#define CODE_LINE_LENGTH (5+SYMBOL_COL_LEN+3*3+4+SYMBOL_COL_LEN+1)

const char *rt_trace_u82binstr(uint8_t byte) {
    static char buffer[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for(int i = 0; i < 8 ; i++) {
        buffer[7 - i] = byte & (1u << i) ? '1' : '0';
    }
    return buffer;
}

// SQW - This was useful but maybe needs a bit of thought...

const char *rt_trace_state2str(uint32_t state_flags) {
    // SQW - Trace needs to be fixed
    // A2FLAGSPACK state;
    static char buffer[128];
    // state.u32 = state_flags;
    buffer[0] = '\0';
    // sprintf(buffer,
    //         "s80 %d "\
    //         "rd %d "\
    //         "wr %d "\
    //         "cx %d "\
    //         "zp %d "\
    //         "c3 %d "\
    //         "c80 %d "\
    //         "ac %d "\
    //         "tx %d "\
    //         "mx %d "\
    //         "p2 %d "\
    //         "hr %d "\
    //         "dh %d "\
    //         "st %d "\
    //         "f80 %d ",
    //         state.b.store80set,
    //         state.b.ramrdset,
    //         state.b.ramwrtset,
    //         state.b.cxromset,
    //         state.b.altzpset,
    //         state.b.c3slotrom,
    //         state.b.col80set,
    //         state.b.altcharset,
    //         state.b.text,
    //         state.b.mixed,
    //         state.b.page2set,
    //         state.b.hires,
    //         state.b.dhires,
    //         state.b.strobed,
    //         state.b.franklin80active
    //        );
    return buffer;
}

int rt_trace_decode(RUNTIME *rt, TRACE_DATA *t, char *trace_str, size_t trace_str_len) {
    char address_symbol[11];
    uint16_t pc = t->cpu.pc;
    char *text = trace_str;
    int remain = trace_str_len;
    uint8_t instruction = t->b0;
    int length = opcode_lengths[instruction];
    int prt_len;
    uint16_t operands;
    char *symbol = rt_sym_find_symbols(rt, pc);
    if(!symbol) {
        symbol = "                                ";
    }
    prt_len = snprintf(text, remain, "%04X: ", pc);
    adjust(&text, &remain, prt_len);
    prt_len = snprintf(text, remain, "%-*.*s ", SYMBOL_COL_LEN - 1, SYMBOL_COL_LEN - 1, symbol);
    adjust(&text, &remain, prt_len);
    switch(length) {
        case 1:
            prt_len = snprintf(text, remain, "%02X        %s", instruction, opcode_text[instruction]);
            adjust(&text, &remain, prt_len);
            break;
        case 2:
            operands = t->b1;
            prt_len = snprintf(text, remain, "%02X %02X     %s", instruction, operands, opcode_text[instruction]);
            adjust(&text, &remain, prt_len);
            // Decode the class to decide if a symbol lookup is needed
            // SQW 65c02 probably needs something here...
            switch(instruction & 0x0f) {
                case 0x00:                                          // Branches, adjusted (destination) lookup
                    if(instruction == 0xa0 || instruction == 0xc0 || instruction == 0xe0) {
                        symbol = 0;
                    } else {
                        symbol = rt_sym_find_symbols(rt, pc + 2 + (int8_t) operands);
                        // If no symbol but symbol view is on - resolve to an address and pretend that's the symbol
                        if(!symbol) {
                            snprintf(address_symbol, 11, "$%02X [%04X]", (uint8_t) operands, (uint16_t)(pc + 2 + (int8_t) operands));
                            symbol = address_symbol;
                        }
                    }
                    break;
                case 0x09:                                          // Immediate - no lookup
                    symbol = 0;
                    break;
                default:                                            // just look for the byte address
                    symbol = rt_sym_find_symbols(rt, operands);
                    break;
            }
            if(!symbol) {
                prt_len = snprintf(text, remain, opcode_hex_params[instruction], operands);
            } else {
                prt_len = snprintf(text, remain, opcode_symbol_params[instruction], symbol);
            }
            adjust(&text, &remain, prt_len);
            break;
        case 3: {
                uint8_t al = t->b1;
                uint8_t ah = t->b2;
                operands = ((ah << 8) | al);
                prt_len = snprintf(text, remain, "%02X %02X %02X  %s", instruction, al, ah, opcode_text[instruction]);
                adjust(&text, &remain, prt_len);
                symbol = rt_sym_find_symbols(rt, operands);
                if(!symbol) {
                    prt_len = snprintf(text, remain, opcode_hex_params[instruction], operands);
                } else {
                    prt_len = snprintf(text, remain, opcode_symbol_params[instruction], symbol);
                }
                adjust(&text, &remain, prt_len);
                break;
            }
    }
    return trace_str_len - remain;
}

int trace_write(RUNTIME *rt, TRACE_DATA *trace_data) {
    TRACE_LOG *trace_log = &rt->trace_log;
    char trace_text[CODE_LINE_LENGTH];
    int length = rt_trace_decode(rt, trace_data, trace_text, CODE_LINE_LENGTH);
    if(length < CODE_LINE_LENGTH) {
        memset(&trace_text[length], ' ', CODE_LINE_LENGTH - 1 - length);
        trace_text[CODE_LINE_LENGTH - 1] = '\0';
    }
    // return fprintf(trace_log->file.fp, "%s A:%02X X:%02X Y:%02X P:%s %s\n", trace_text, trace_data->cpu.A, trace_data->cpu.X, trace_data->cpu.Y, rt_trace_u82binstr(trace_data->cpu.flags), rt_trace_state2str(trace_data->state_flags));
    return 0;
}

void rt_trace(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    APPLE2 *m = rt->m;
    if(!trace_log->trace_on) {
        return;
    }
    TRACE_DATA *td = &trace_log->trace_buffer[trace_log->trace_position];
    td->cpu = m->cpu;
    // td->state_flags = m->state_flags;
    td->b0 = read_from_memory_debug(m, m->cpu.pc);
    td->b1 = read_from_memory_debug(m, m->cpu.pc + 1);
    td->b2 = read_from_memory_debug(m, m->cpu.pc + 2);
    if(++trace_log->trace_position >= trace_log->trace_max_entries) {
        trace_log->trace_position = 0;
        trace_log->trace_wrapped = 1;
    }
    // fill in the trace data
}

void rt_trace_off(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    if(trace_log->file.is_file_open) {
        fflush(trace_log->file.fp);
    }
    trace_log->trace_on = 0;
}

void rt_trace_init(RUNTIME *rt, const char *filename, size_t transactions) {
    TRACE_LOG *trace_log = &rt->trace_log;
    memset(trace_log, 0, sizeof(TRACE_LOG));
    trace_log->trace_buffer = (TRACE_DATA *)malloc(sizeof(TRACE_DATA) * transactions);
    if(!trace_log->trace_buffer) {
        rt_trace_shutdown(rt);
    }
    if(A2_OK != util_file_open(&trace_log->file, filename, "w")) {
        rt_trace_shutdown(rt);
    }
    trace_log->trace_max_entries = transactions;
}

void rt_trace_on(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    if(trace_log->file.is_file_open) {
        trace_log->trace_on = 1;
    }
}

void rt_trace_shutdown(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    if(trace_log->trace_position || trace_log->trace_wrapped) {
        int i;
        if(trace_log->trace_wrapped) {
            for(i = trace_log->trace_position; i < trace_log->trace_max_entries; i++) {
                trace_write(rt, &trace_log->trace_buffer[i]);
            }
        }
        for(i = 0; i < trace_log->trace_position; i++) {
            trace_write(rt, &trace_log->trace_buffer[i]);
        }
    }
    free(trace_log->trace_buffer);
    util_file_discard(&trace_log->file);
    memset(trace_log, 0, sizeof(TRACE_LOG));
}