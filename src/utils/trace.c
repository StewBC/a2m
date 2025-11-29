// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

const char *u82binstr(uint8_t byte) {
    static char buffer[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for(int i = 0; i < 8 ; i++) {
        buffer[7 - i] = byte & (1 << i) ? '1' : '0';
    }
    return buffer;
}

// SQW - This was useful but maybe needs a bit of thought...

// const char *state2str(uint32_t state_flags) {
//     A2FLAGSPACK state;
//     static char buffer[128];
//     state.u32 = state_flags;
//     sprintf(buffer,
//             "s80 %d "\
//             "rd %d "\
//             "wr %d "\
//             "cx %d "\
//             "zp %d "\
//             "c3 %d "\
//             "c80 %d "\
//             "ac %d "\
//             "tx %d "\
//             "mx %d "\
//             "p2 %d "\
//             "hr %d "\
//             "dh %d "\
//             "st %d "\
//             "f80 %d ",
//             state.b.store80set,
//             state.b.ramrdset,
//             state.b.ramwrtset,
//             state.b.cxromset,
//             state.b.altzpset,
//             state.b.c3slotrom,
//             state.b.col80set,
//             state.b.altcharset,
//             state.b.text,
//             state.b.mixed,
//             state.b.page2set,
//             state.b.hires,
//             state.b.dhires,
//             state.b.strobed,
//             state.b.franklin80active
//            );
//     return buffer;
// }

// int trace_log(UTIL_FILE *f, APPLE2 *m) {
//     if(f->is_file_open) {
//         char trace_buffer[128];
//         CODE_LINE line;
//         line.text = trace_buffer;
//         unk_dasm_disassemble_line(v, m, m->cpu.pc, &line);
//         int length = strlen(trace_buffer);
//         if(length < CODE_LINE_LENGTH) {
//             strncat(trace_buffer, "                                      ", CODE_LINE_LENGTH - length);
//         }
//         return fprintf(f->fp, "%s A:%02X X:%02X Y:%02X P:%s %s\n", line.text, m->cpu.A, m->cpu.X, m->cpu.Y, u82binstr(m->cpu.flags), state2str(m->state_flags));
//     }
//     return -1;
// }

void trace_off(UTIL_FILE *f) {
    if(f->is_file_open) {
        fflush(f->fp);
    }
}

int trace_on(UTIL_FILE *f, const char *filename, const char *file_mode) {
    if(f->is_file_open) {
        return A2_OK;
    }
    return util_file_open(f, filename, file_mode);
}
