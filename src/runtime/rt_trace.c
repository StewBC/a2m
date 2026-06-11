// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

#define TRACE_DEFAULT_FILE "./trace.txt"
#define TRACE_DEFAULT_SECONDS 1
#define TRACE_AVG_CYCLES_PER_OPCODE 2
#define TRACE_LINE_LENGTH 96

static const char *rt_trace_u82binstr(uint8_t byte) {
    static char buffer[9];
    for(int i = 0; i < 8 ; i++) {
        buffer[7 - i] = byte & (1u << i) ? '1' : '0';
    }
    buffer[8] = '\0';
    return buffer;
}

static int rt_trace_is_relative(uint8_t opcode) {
    switch(opcode) {
        case 0x10: // BPL
        case 0x30: // BMI
        case 0x50: // BVC
        case 0x70: // BVS
        case 0x80: // BRA (65C02)
        case 0x90: // BCC
        case 0xB0: // BCS
        case 0xD0: // BNE
        case 0xF0: // BEQ
            return 1;
        default:
            return 0;
    }
}

static void rt_trace_state2str(A2_STATE state_flags, char *buffer, size_t buffer_len) {
    snprintf(buffer, buffer_len,
             "80S:%d RD:%d WR:%d CX:%d ZP:%d C3:%d P2:%d HR:%d LC:%c%c%c 80C:%d ALT:%d TXT:%d MIX:%d DH:%d F80:%d",
             !!(state_flags & A2S_80STORE),
             !!(state_flags & A2S_RAMRD),
             !!(state_flags & A2S_RAMWRT),
             !!(state_flags & A2S_CXSLOTROM_MB_ENABLE),
             !!(state_flags & A2S_ALTZP),
             !!(state_flags & A2S_SLOT3ROM_MB_DISABLE),
             !!(state_flags & A2S_PAGE2),
             !!(state_flags & A2S_HIRES),
             state_flags & A2S_LC_READ ? 'R' : '-',
             state_flags & A2S_LC_WRITE ? 'W' : '-',
             state_flags & A2S_LC_BANK2 ? '2' : '1',
             !!(state_flags & A2S_COL80),
             !!(state_flags & A2S_ALTCHARSET),
             !!(state_flags & A2S_TEXT),
             !!(state_flags & A2S_MIXED),
             !!(state_flags & A2S_DHIRES),
             !!(state_flags & A2S_FRANKLIN80ACTIVE));
}

static int rt_trace_decode(RUNTIME *rt, TRACE_DATA *t, char *trace_str, size_t trace_str_len) {
    uint16_t pc = t->cpu.pc;
    char *text = trace_str;
    int remain = (int)trace_str_len;
    uint8_t instruction = t->b0;
    int length = opcode_lengths[instruction];
    int prt_len;
    uint16_t operands = 0;
    char *symbol = rt_sym_find_symbols(rt, pc);
    if(!symbol) {
        symbol = "";
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
            prt_len = snprintf(text, remain, "%02X %02X     %s", instruction, t->b1, opcode_text[instruction]);
            adjust(&text, &remain, prt_len);
            prt_len = snprintf(text, remain, opcode_hex_params[instruction], operands);
            adjust(&text, &remain, prt_len);
            if(rt_trace_is_relative(instruction)) {
                prt_len = snprintf(text, remain, " [%04X]", (uint16_t)(pc + 2 + (int8_t)t->b1));
                adjust(&text, &remain, prt_len);
            }
            break;
        case 3:
            operands = ((uint16_t)t->b2 << 8) | t->b1;
            prt_len = snprintf(text, remain, "%02X %02X %02X  %s", instruction, t->b1, t->b2, opcode_text[instruction]);
            adjust(&text, &remain, prt_len);
            prt_len = snprintf(text, remain, opcode_hex_params[instruction], operands);
            adjust(&text, &remain, prt_len);
            break;
        default:
            prt_len = snprintf(text, remain, "%02X        .DB", instruction);
            adjust(&text, &remain, prt_len);
            break;
    }

    return (int)trace_str_len - remain;
}

static int rt_trace_write(RUNTIME *rt, TRACE_DATA *trace_data) {
    TRACE_LOG *trace_log = &rt->trace_log;
    char trace_text[TRACE_LINE_LENGTH];
    char state_text[160];
    int length = rt_trace_decode(rt, trace_data, trace_text, sizeof(trace_text));
    if(length < (int)sizeof(trace_text)) {
        memset(&trace_text[length], ' ', sizeof(trace_text) - 1 - length);
        trace_text[sizeof(trace_text) - 1] = '\0';
    }
    rt_trace_state2str(trace_data->state_flags, state_text, sizeof(state_text));
    return fprintf(trace_log->file.fp,
                   "%s A:%02X X:%02X Y:%02X SP:%02X P:%s CYC:%llu %s\n",
                   trace_text,
                   trace_data->cpu.A,
                   trace_data->cpu.X,
                   trace_data->cpu.Y,
                   trace_data->cpu.sp & 0xff,
                   rt_trace_u82binstr(trace_data->cpu.flags),
                   (unsigned long long)trace_data->cpu.cycles,
                   state_text) < 0 ? A2_ERR : A2_OK;
}

static int rt_trace_flush(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    int rval = A2_OK;
    if(!trace_log->file.is_file_open || !trace_log->trace_buffer || !trace_log->trace_dirty) {
        return A2_OK;
    }

    if(trace_log->trace_wrapped) {
        for(size_t i = trace_log->trace_position; i < trace_log->trace_max_entries; i++) {
            if(A2_OK != rt_trace_write(rt, &trace_log->trace_buffer[i])) {
                rval = A2_ERR;
                break;
            }
        }
    }
    if(rval == A2_OK) {
        for(size_t i = 0; i < trace_log->trace_position; i++) {
            if(A2_OK != rt_trace_write(rt, &trace_log->trace_buffer[i])) {
                rval = A2_ERR;
                break;
            }
        }
    }

    fflush(trace_log->file.fp);
    trace_log->trace_position = 0;
    trace_log->trace_wrapped = 0;
    trace_log->trace_dirty = 0;
    return rval;
}

int rt_trace(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    APPLE2 *m = rt->m;
    if(!trace_log->trace_on || !trace_log->trace_buffer || !trace_log->trace_max_entries) {
        return A2_OK;
    }

    TRACE_DATA *td = &trace_log->trace_buffer[trace_log->trace_position];
    td->cpu = m->cpu;
    td->state_flags = m->state_flags;
    td->b0 = read_from_memory_debug(m, m->cpu.pc);
    td->b1 = read_from_memory_debug(m, m->cpu.pc + 1);
    td->b2 = read_from_memory_debug(m, m->cpu.pc + 2);

    trace_log->trace_dirty = 1;
    if(++trace_log->trace_position >= trace_log->trace_max_entries) {
        trace_log->trace_position = 0;
        trace_log->trace_wrapped = 1;
    }
    return A2_OK;
}

void rt_trace_off(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    trace_log->trace_on = 0;
    if(rt->m) {
        rt->m->a2out_cb.cb_trace_ctx.cb_trace = NULL;
    }
    rt_trace_flush(rt);
}

int rt_trace_init(RUNTIME *rt, const char *filename, size_t transactions) {
    TRACE_LOG *trace_log = &rt->trace_log;
    if(trace_log->trace_buffer && trace_log->file.is_file_open) {
        return A2_OK;
    }

    rt_trace_shutdown(rt);
    memset(trace_log, 0, sizeof(TRACE_LOG));
    trace_log->trace_buffer = (TRACE_DATA *)malloc(sizeof(TRACE_DATA) * transactions);
    if(!trace_log->trace_buffer) {
        rt_trace_shutdown(rt);
        return A2_ERR;
    }
    if(A2_OK != util_file_open(&trace_log->file, filename, "w")) {
        rt_trace_shutdown(rt);
        return A2_ERR;
    }
    trace_log->trace_max_entries = transactions;
    if(rt->m) {
        rt->m->a2out_cb.cb_trace_ctx.user = rt;
    }
    return A2_OK;
}

void rt_trace_on(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    if(!trace_log->trace_buffer || !trace_log->file.is_file_open) {
        size_t entries = TRACE_DEFAULT_SECONDS * (size_t)(CPU_FREQUENCY / TRACE_AVG_CYCLES_PER_OPCODE);
        if(A2_OK != rt_trace_init(rt, TRACE_DEFAULT_FILE, entries)) {
            return;
        }
    }
    trace_log->trace_on = 1;
    if(rt->m) {
        rt->m->a2out_cb.cb_trace_ctx.user = rt;
        rt->m->a2out_cb.cb_trace_ctx.cb_trace = (cb_trace)rt_trace;
    }
}

void rt_trace_shutdown(RUNTIME *rt) {
    TRACE_LOG *trace_log = &rt->trace_log;
    trace_log->trace_on = 0;
    if(rt->m) {
        rt->m->a2out_cb.cb_trace_ctx.cb_trace = NULL;
    }
    rt_trace_flush(rt);
    free(trace_log->trace_buffer);
    util_file_discard(&trace_log->file);
    memset(trace_log, 0, sizeof(TRACE_LOG));
}
