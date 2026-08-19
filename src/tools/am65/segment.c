// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

SEGMENT *segment_find(AM65_DYNARRAY *segments, const SEGMENT *seg) {
    for(size_t si = 0; si < segments->items; si++) {
        SEGMENT *s = *AM65_ARRAY_GET(segments, SEGMENT*, si);
        if(s->segment_name_length == seg->segment_name_length &&
           0 == asm_strnicmp(s->segment_name, seg->segment_name, seg->segment_name_length)) {
            return s;
        }
    }
    return NULL;
}

TARGET *add_target(ASSEMBLER *as, void *ctx) {
    SEGMENT *segment = malloc(sizeof(SEGMENT));
    if(!segment) {
        return NULL;
    }

    TARGET *target = malloc(sizeof(TARGET));
    if(!target) {
        free(segment);
        return NULL;
    }

    memset(segment, 0, sizeof(SEGMENT));
    memset(target, 0, sizeof(TARGET));
    target->ctx = ctx;

    AM65_ARRAY_INIT(&target->segments, SEGMENT*);
    if(ASM_OK != AM65_ARRAY_ADD(&target->segments, segment)) {
        am65_array_free(&target->segments);
        free(segment);
        free(target);
        return NULL;
    }
    target->active_segment = segment;
    if(ASM_OK != AM65_ARRAY_ADD(&as->targets, target)) {
        am65_array_free(&target->segments);
        free(segment);
        free(target);
        return NULL;
    }
    return target;
}

void targets_free(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->targets.items; i++) {
        TARGET *t = *AM65_ARRAY_GET(&as->targets, TARGET*, i);
        if(!t) {
            continue;
        }
        // The default target (index 0) uses a host-owned ctx; only release the
        // contexts that were handed to us by target_open for named .scope redirects.
        if(i > 0 && t->ctx && as->cb.target_release) {
            as->cb.target_release(as->cb.user, t->ctx);
        }
        for(size_t j = 0; j < t->segments.items; j++) {
            SEGMENT *s = *AM65_ARRAY_GET(&t->segments, SEGMENT*, j);
            free((char *)s->segment_name);
            free((char *)s->reclaim_host_name);
            free(s);
        }
        am65_array_free(&t->segments);
        free(t);
    }
    am65_array_free(&as->targets);
    as->active_target = NULL;
}
