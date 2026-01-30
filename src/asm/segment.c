// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// Segment helpers
SEGMENT *segment_find(DYNARRAY *segments, const SEGMENT *seg) {
    for(int si = 0; si < segments->items; si++) {
        SEGMENT *s = ARRAY_GET(segments, SEGMENT, si);
        if(s->segment_name_length == seg->segment_name_length && 0 == strnicmp(s->segment_name, seg->segment_name, seg->segment_name_length)) {
            return s;
        }
    }
    return NULL;
}

