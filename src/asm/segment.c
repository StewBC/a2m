// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// Segment helpers
SEGMENT *segment_find(DYNARRAY *segments, const SEGMENT *seg) {
    for(int si = 0; si < segments->items; si++) {
        SEGMENT *s = *ARRAY_GET(segments, SEGMENT*, si);
        if(s->segment_name_length == seg->segment_name_length && 0 == strnicmp(s->segment_name, seg->segment_name, seg->segment_name_length)) {
            return s;
        }
    }
    return NULL;
}

TARGET *add_target(ASSEMBLER *as, void *target_ctx) {
    SEGMENT *segment = (SEGMENT *)malloc(sizeof(SEGMENT));
    if(!segment) {
        return NULL;
    }
    
    TARGET *target = (TARGET *)malloc(sizeof(TARGET));
    if(!target) {
        free(segment);
        return NULL;
    }
      
    memset(segment, 0, sizeof(SEGMENT));
    memset(target, 0, sizeof(TARGET));

    ARRAY_INIT(&target->segments, SEGMENT*);
    if(A2_ERR == ARRAY_ADD(&target->segments, segment)) {
        return NULL;
    }
    target->target_ctx = target_ctx;
    target->prev_target = as->active_target;
    target->active_segment = segment;
    if(A2_ERR == ARRAY_ADD(&as->targets, target)) {
        array_free(&target->segments);
        free(segment);
        free(target);
        return NULL;
    }
    return target;
}
