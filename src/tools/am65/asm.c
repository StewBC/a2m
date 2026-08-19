// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#define OVERLAP_MAX_SEGS 64
#define AUTO_ADJUST_MAX_RETRIES 3

typedef struct {
    size_t target_index;
    char *segment_name;
    uint32_t segment_name_length;
    uint16_t address;
} SEGMENT_ADJUSTMENT;

typedef struct {
    int overlaps;
    int wraps;
    int locked_conflict;
    int allocation_failed;
} SEGMENT_CHECK_RESULT;

static void strip_comment(char *line, int *line_len) {
    int in_string = 0;
    for(int i = 0; i < *line_len; i++) {
        if(in_string) {
            if(line[i] == '\\' && i + 1 < *line_len) {
                i++;
            } else if(line[i] == '"') {
                in_string = 0;
            }
        } else if(line[i] == '"') {
            in_string = 1;
        } else if(line[i] == ';') {
            line[i] = '\0';
            *line_len = i;
            return;
        }
    }
}

static size_t active_target_index(const ASSEMBLER *as) {
    for(size_t i = 0; i < as->targets.items; i++) {
        if(*AM65_ARRAY_GET((AM65_DYNARRAY *)&as->targets, TARGET*, i) == as->active_target) {
            return i;
        }
    }
    return (size_t)-1;
}

static void segment_adjustments_clear(AM65_DYNARRAY *adjustments) {
    for(size_t i = 0; i < adjustments->items; i++) {
        SEGMENT_ADJUSTMENT *adjustment =
            AM65_ARRAY_GET(adjustments, SEGMENT_ADJUSTMENT, i);
        free(adjustment->segment_name);
    }
    am65_array_free(adjustments);
    AM65_ARRAY_INIT(adjustments, SEGMENT_ADJUSTMENT);
}

static int segment_adjustment_add(
    AM65_DYNARRAY *adjustments,
    size_t target_index,
    const char *segment_name,
    uint32_t segment_name_length,
    uint16_t address) {
    SEGMENT_ADJUSTMENT adjustment;
    memset(&adjustment, 0, sizeof(adjustment));
    adjustment.target_index = target_index;
    adjustment.segment_name_length = segment_name_length;
    adjustment.address = address;
    if(segment_name_length > 0) {
        adjustment.segment_name = malloc((size_t)segment_name_length + 1);
        if(!adjustment.segment_name) {
            return ASM_ERR;
        }
        memcpy(adjustment.segment_name, segment_name, segment_name_length);
        adjustment.segment_name[segment_name_length] = '\0';
    }
    if(ASM_OK != AM65_ARRAY_ADD(adjustments, adjustment)) {
        free(adjustment.segment_name);
        return ASM_ERR;
    }
    return ASM_OK;
}

static int segment_adjustments_copy(AM65_DYNARRAY *dest, const AM65_DYNARRAY *source) {
    AM65_ARRAY_INIT(dest, SEGMENT_ADJUSTMENT);
    for(size_t i = 0; i < source->items; i++) {
        const SEGMENT_ADJUSTMENT *adjustment =
            AM65_ARRAY_GET((AM65_DYNARRAY *)source, SEGMENT_ADJUSTMENT, i);
        if(ASM_OK != segment_adjustment_add(
                dest,
                adjustment->target_index,
                adjustment->segment_name,
                adjustment->segment_name_length,
                adjustment->address)) {
            segment_adjustments_clear(dest);
            return ASM_ERR;
        }
    }
    return ASM_OK;
}

static void segment_adjustments_remove_target(
    AM65_DYNARRAY *adjustments,
    size_t target_index) {
    size_t write = 0;
    for(size_t read = 0; read < adjustments->items; read++) {
        SEGMENT_ADJUSTMENT *adjustment =
            AM65_ARRAY_GET(adjustments, SEGMENT_ADJUSTMENT, read);
        if(adjustment->target_index == target_index) {
            free(adjustment->segment_name);
            continue;
        }
        if(write != read) {
            *AM65_ARRAY_GET(adjustments, SEGMENT_ADJUSTMENT, write) = *adjustment;
        }
        write++;
    }
    adjustments->items = write;
}

uint16_t assembler_adjust_segment_start(
    ASSEMBLER *as,
    const char *segment_name,
    uint32_t segment_name_length,
    uint16_t source_address) {
    if(!as || !as->auto_adjust_segments || as->segment_adjustments.items == 0) {
        return source_address;
    }
    size_t target_index = active_target_index(as);
    if(target_index == (size_t)-1) {
        return source_address;
    }
    for(size_t i = 0; i < as->segment_adjustments.items; i++) {
        SEGMENT_ADJUSTMENT *adjustment =
            AM65_ARRAY_GET(&as->segment_adjustments, SEGMENT_ADJUSTMENT, i);
        if(adjustment->target_index == target_index &&
           adjustment->segment_name_length == segment_name_length &&
           (segment_name_length == 0 ||
            0 == asm_strnicmp(
                adjustment->segment_name,
                segment_name,
                segment_name_length))) {
            return adjustment->address;
        }
    }
    return source_address;
}

static void reset_targets_for_pass(ASSEMBLER *as) {
    for(size_t i = 0; i < as->targets.items; i++) {
        TARGET *target = *AM65_ARRAY_GET(&as->targets, TARGET*, i);
        if(!target) {
            continue;
        }
        for(size_t j = 0; j < target->segments.items; j++) {
            SEGMENT *segment = *AM65_ARRAY_GET(&target->segments, SEGMENT*, j);
            segment->segment_output_address = segment->segment_start_address;
        }
        if(target->segments.items > 0) {
            target->active_segment = *AM65_ARRAY_GET(&target->segments, SEGMENT*, 0);
        }
    }
    if(as->targets.items > 0) {
        as->active_target = *AM65_ARRAY_GET(&as->targets, TARGET*, 0);
    }
}

static void reset_source_for_assemble(ASSEMBLER *as) {
    files_free(as);
    AM65_ARRAY_INIT(&as->files, ASM_FILE*);
    AM65_ARRAY_INIT(&as->file_stack, FILE_FRAME);
    as->root_file = NULL;
    as->current_file = NULL;
}

static void seed_predefines(ASSEMBLER *as) {
    for(size_t i = 0; i < as->predefines.items; i++) {
        DEFINE *d = AM65_ARRAY_GET(&as->predefines, DEFINE, i);
        define_add(as, d->from, d->from_len, d->to, d->to_len);
    }
}

static void reset_pass_state(ASSEMBLER *as) {
    as->cpu_profile = as->default_cpu_profile;
    reset_targets_for_pass(as);
    scope_reset_ids(as->root_scope);
    as->active_scope = as->root_scope;
    as->symbol_table = as->root_scope ? as->root_scope->symbol_table : NULL;
    as->scope_stack.items = 0;
    loop_stack_clear(as);
    macro_stack_clear(as);
    macro_definitions_clear(as);
    as->macro_id = 0;
    defines_free(as);
    seed_predefines(as);
    free((char *)as->strcode);
    as->strcode = NULL;
    as->if_stack.items = 0;
    as->if_skip_depth = 0;
    as->expression_depth = 0;
    as->expression_unknown = 0;
    as->cur = NULL;
    memset(&as->token, 0, sizeof(as->token));
}

static void parse_line(ASSEMBLER *as) {
    get_token(as);
    if(as->token.type == TOKEN_END) {
        return;
    }

    if(is_label(as)) {
        parse_label(as);
        get_token(as);
        if(as->token.type == TOKEN_END) {
            return;
        }
    }

    if(is_address(as)) {
        parse_address(as);
        return;
    }

    if(is_opcode(as)) {
        parse_opcode(as);
        return;
    }

    if(is_parse_dot_command(as)) {
        parse_dot_command(as);
        return;
    }

    if(as->token.type == TOKEN_VAR && parse_macro_if_is_macro(as)) {
        return;
    }

    if(is_variable(as)) {
        parse_variable(as);
        return;
    }

    if(as->token.type == TOKEN_VAR || as->token.type == TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token: %.*s",
                (int)as->token.name_length, as->token.name);
    } else if(as->token.name) {
        asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token: %.*s",
                (int)as->token.name_length, as->token.name);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Unrecognised token");
    }
}

static void asm_log_direct(ASSEMBLER *as, const char *fmt, ...) {
    if(!as || !as->errorlog) {
        return;
    }
    ERROR_ENTRY e;
    memset(&e, 0, sizeof(e));
    e.err_str = malloc(ASM_ERR_MAX_STR_LEN);
    if(!e.err_str) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    e.message_length = (size_t)vsnprintf(e.err_str, ASM_ERR_MAX_STR_LEN, fmt, args);
    va_end(args);
    errlog(as->errorlog, &e);
}

static SEGMENT_CHECK_RESULT check_segment_overlaps(
    ASSEMBLER *as,
    AM65_DYNARRAY *suggestions,
    int log_issues) {
    SEGMENT_CHECK_RESULT result;
    memset(&result, 0, sizeof(result));

    for(size_t ti = 0; ti < as->targets.items; ti++) {
        TARGET *target = *AM65_ARRAY_GET(&as->targets, TARGET*, ti);
        if(!target) {
            continue;
        }

        SEGMENT *segs[OVERLAP_MAX_SEGS];
        int count = 0;

        for(size_t si = 0; si < target->segments.items && count < OVERLAP_MAX_SEGS; si++) {
            SEGMENT *s = *AM65_ARRAY_GET(&target->segments, SEGMENT*, si);
            if(s->do_not_emit) {
                continue;
            }
            if(s->segment_output_address == s->segment_start_address) {
                continue;
            }
            segs[count++] = s;
        }

        if(count < 2) {
            continue;
        }

        // Stable insertion sort by start address (preserves definition order for ties)
        for(int i = 1; i < count; i++) {
            SEGMENT *key = segs[i];
            int j = i - 1;
            while(j >= 0 && segs[j]->segment_start_address > key->segment_start_address) {
                segs[j + 1] = segs[j];
                j--;
            }
            segs[j + 1] = key;
        }

        int target_overlaps = 0;
        int target_wraps = 0;

        // Wrap-around: output_address < start_address in uint16 arithmetic
        for(int i = 0; i < count; i++) {
            if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                if(log_issues) {
                    const char *name = segs[i]->segment_name ?
                        segs[i]->segment_name : "<default>";
                    asm_log_direct(
                        as,
                        "Segment \"%.*s\" wraps past $FFFF (start $%04X end $%04X)",
                        (int)segs[i]->segment_name_length,
                        name,
                        segs[i]->segment_start_address,
                        segs[i]->segment_output_address);
                }
                target_wraps++;
            }
        }

        // Overlap: for each pair (i,j) with i<j in sorted order, b_start < a_end means overlap
        for(int i = 0; i < count - 1; i++) {
            if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                continue;
            }
            for(int j = i + 1; j < count; j++) {
                if(segs[j]->segment_output_address < segs[j]->segment_start_address) {
                    continue;
                }
                if(segs[j]->segment_start_address < segs[i]->segment_output_address) {
                    if(log_issues) {
                        const char *na = segs[i]->segment_name ?
                            segs[i]->segment_name : "<default>";
                        const char *nb = segs[j]->segment_name ?
                            segs[j]->segment_name : "<default>";
                        asm_log_direct(
                            as,
                            "Segment \"%.*s\" [$%04X..$%04X) overlaps \"%.*s\" [$%04X..$%04X)",
                            (int)segs[i]->segment_name_length,
                            na,
                            segs[i]->segment_start_address,
                            segs[i]->segment_output_address,
                            (int)segs[j]->segment_name_length,
                            nb,
                            segs[j]->segment_start_address,
                            segs[j]->segment_output_address);
                    }
                    target_overlaps++;
                }
            }
        }

        result.overlaps += target_overlaps;
        result.wraps += target_wraps;

        if(target_overlaps > 0) {
            // A locked segment is an anchor: it must stay at its declared
            // start. If compacting the lower segments would run into a locked
            // segment, we do not attempt to reorder around it -- that is left
            // to the author. We report it and let assembly fail.
            int target_locked_conflict = 0;
            {
                uint16_t next_addr = segs[0]->segment_start_address;
                for(int i = 0; i < count; i++) {
                    if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                        continue;
                    }
                    uint16_t size = segs[i]->segment_output_address - segs[i]->segment_start_address;
                    if(segs[i]->is_locked) {
                        if(next_addr > segs[i]->segment_start_address) {
                            target_locked_conflict = 1;
                            if(log_issues) {
                                const char *lname = segs[i]->segment_name ?
                                    segs[i]->segment_name : "<default>";
                                asm_log_direct(
                                    as,
                                    "Locked segment \"%.*s\" at $%04X would be overrun by lower segments (they need up to $%04X) -- reorder aborted, adjust the layout by hand",
                                    (int)segs[i]->segment_name_length,
                                    lname,
                                    segs[i]->segment_start_address,
                                    next_addr);
                            }
                            break;
                        }
                        next_addr = segs[i]->segment_start_address + size;
                    } else {
                        next_addr += size;
                    }
                }
            }

            if(target_locked_conflict) {
                result.locked_conflict++;
                if(suggestions) {
                    segment_adjustments_remove_target(suggestions, ti);
                }
                continue;
            }

            if(suggestions) {
                segment_adjustments_remove_target(suggestions, ti);
            }
            if(log_issues) {
                asm_log_direct(as, "Segments overlap -- suggested addresses:");
            }
            uint16_t next_addr = segs[0]->segment_start_address;
            for(int i = 0; i < count; i++) {
                if(segs[i]->segment_output_address < segs[i]->segment_start_address) {
                    continue;
                }
                const char *name = segs[i]->segment_name ? segs[i]->segment_name : "<default>";
                uint16_t size = segs[i]->segment_output_address - segs[i]->segment_start_address;
                if(segs[i]->is_locked) {
                    // Anchor stays put: no suggestion, just advance past it.
                    next_addr = segs[i]->segment_start_address + size;
                    continue;
                }
                if(log_issues) {
                    asm_log_direct(
                        as,
                        "  Suggest \"%.*s\" at $%04X",
                        (int)segs[i]->segment_name_length,
                        name,
                        next_addr);
                }
                if(suggestions &&
                   ASM_OK != segment_adjustment_add(
                       suggestions,
                       ti,
                       segs[i]->segment_name,
                       segs[i]->segment_name_length,
                       next_addr)) {
                    result.allocation_failed = 1;
                    asm_err(as, ASM_ERR_FATAL,
                            "Out of memory tracking adjusted segment addresses");
                    return result;
                }
                next_addr += size;
            }
        }
    }
    return result;
}

// Validate the noemit/reclaim rules that the emit-only overlap machinery above
// deliberately ignores. Two rules, both hard errors (never auto-adjusted):
//   * a plain noemit segment may not overlap any other segment -- overlaying
//     memory is only sanctioned through an explicit reclaim binding;
//   * a reclaim segment may not be larger than the host it piggybacks on.
// Returns the number of issues found; issues are also logged so the caller's
// error count reflects them. Reclaim segments are excluded from the noemit
// overlap scan -- sitting on their host is exactly what they are for.
static int check_noemit_reclaim(ASSEMBLER *as) {
    int issues = 0;

    for(size_t ti = 0; ti < as->targets.items; ti++) {
        TARGET *target = *AM65_ARRAY_GET(&as->targets, TARGET*, ti);
        if(!target) {
            continue;
        }

        for(size_t ai = 0; ai < target->segments.items; ai++) {
            SEGMENT *a = *AM65_ARRAY_GET(&target->segments, SEGMENT*, ai);
            if(a->is_reclaim || a->segment_output_address <= a->segment_start_address) {
                continue;
            }
            for(size_t bi = ai + 1; bi < target->segments.items; bi++) {
                SEGMENT *b = *AM65_ARRAY_GET(&target->segments, SEGMENT*, bi);
                if(b->is_reclaim || b->segment_output_address <= b->segment_start_address) {
                    continue;
                }
                if(!a->do_not_emit && !b->do_not_emit) {
                    continue;  // emit-vs-emit is the auto-adjust machinery's job
                }
                if(a->segment_start_address < b->segment_output_address &&
                   b->segment_start_address < a->segment_output_address) {
                    const char *na = a->segment_name ? a->segment_name : "<default>";
                    const char *nb = b->segment_name ? b->segment_name : "<default>";
                    asm_log_direct(
                        as,
                        "noemit segment \"%.*s\" [$%04X..$%04X) overlaps \"%.*s\" [$%04X..$%04X) -- use reclaim=\"host\" to overlay intentionally",
                        (int)a->segment_name_length, na,
                        a->segment_start_address, a->segment_output_address,
                        (int)b->segment_name_length, nb,
                        b->segment_start_address, b->segment_output_address);
                    issues++;
                }
            }
        }

        for(size_t ri = 0; ri < target->segments.items; ri++) {
            SEGMENT *r = *AM65_ARRAY_GET(&target->segments, SEGMENT*, ri);
            if(!r->is_reclaim) {
                continue;
            }
            SEGMENT key;
            memset(&key, 0, sizeof(key));
            key.segment_name = r->reclaim_host_name;
            key.segment_name_length = r->reclaim_host_name_length;
            SEGMENT *host = segment_find(&target->segments, &key);
            uint16_t r_size = r->segment_output_address > r->segment_start_address ?
                (uint16_t)(r->segment_output_address - r->segment_start_address) : 0;
            uint16_t h_size = 0;
            if(host && host->segment_output_address > host->segment_start_address) {
                h_size = (uint16_t)(host->segment_output_address - host->segment_start_address);
            }
            if(r_size > h_size) {
                const char *nr = r->segment_name ? r->segment_name : "<default>";
                asm_log_direct(
                    as,
                    "reclaim segment \"%.*s\" ($%04X bytes) overflows host \"%.*s\" ($%04X bytes)",
                    (int)r->segment_name_length, nr, r_size,
                    (int)r->reclaim_host_name_length,
                    r->reclaim_host_name ? r->reclaim_host_name : "<host>",
                    h_size);
                issues++;
            }
        }
    }

    return issues;
}

static void assembler_program_state_destroy(ASSEMBLER *as) {
    free(as->root_dir);
    as->root_dir = NULL;
    defines_free(as);
    macro_stack_clear(as);
    macro_definitions_clear(as);
    files_free(as);
    as->root_file = NULL;
    as->current_file = NULL;
    as->current_file_name = NULL;
    as->current_line = 0;
    as->cur = NULL;
    scope_destroy(as->root_scope);
    as->root_scope = NULL;
    as->active_scope = NULL;
    as->symbol_table = NULL;
    am65_array_free(&as->scope_stack);
    am65_array_free(&as->anon_symbols);
    loop_stack_clear(as);
    am65_array_free(&as->loop_stack);
    am65_array_free(&as->macros);
    am65_array_free(&as->macro_stack);
    am65_array_free(&as->if_stack);
    targets_free(as);
    free((char *)as->strcode);
    as->strcode = NULL;
}

static int assembler_program_state_init(ASSEMBLER *as) {
    AM65_ARRAY_INIT(&as->files, ASM_FILE*);
    AM65_ARRAY_INIT(&as->file_stack, FILE_FRAME);
    AM65_ARRAY_INIT(&as->defines, DEFINE);
    AM65_ARRAY_INIT(&as->scope_stack, SCOPE*);
    AM65_ARRAY_INIT(&as->anon_symbols, uint16_t);
    AM65_ARRAY_INIT(&as->loop_stack, LOOP);
    AM65_ARRAY_INIT(&as->macros, MACRO);
    AM65_ARRAY_INIT(&as->macro_stack, MACRO_EXPAND);
    AM65_ARRAY_INIT(&as->if_stack, IF_FRAME);
    AM65_ARRAY_INIT(&as->targets, TARGET*);
    as->cpu_profile = as->default_cpu_profile;

    as->root_scope = malloc(sizeof(SCOPE));
    if(!as->root_scope || ASM_OK != scope_init(as->root_scope, 0)) {
        free(as->root_scope);
        as->root_scope = NULL;
        return ASM_ERR;
    }
    as->active_scope = as->root_scope;
    as->symbol_table = as->root_scope->symbol_table;
    // The default (unnamed) target routes emitted bytes to the host's default_target
    // context; fall back to `user` so a host that only sets user+output_byte still works.
    as->active_target = add_target(
        as,
        as->cb.default_target ? as->cb.default_target : as->cb.user);
    return as->active_target ? ASM_OK : ASM_ERR;
}

static int assembler_restart_program_state(ASSEMBLER *as) {
    assembler_program_state_destroy(as);
    return assembler_program_state_init(as);
}

int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, CB_ASM_CTX *cb) {
    if(!as || !errorlog || !cb || !cb->output_byte) {
        return ASM_ERR;
    }
    memset(as, 0, sizeof(*as));
    as->cb = *cb;
    as->errorlog = errorlog;
    as->error_log_level = 0;
    as->default_cpu_profile = ASM_CPU_6502;
    AM65_ARRAY_INIT(&as->predefines, DEFINE);
    AM65_ARRAY_INIT(&as->segment_adjustments, SEGMENT_ADJUSTMENT);
    if(ASM_OK != assembler_program_state_init(as)) {
        assembler_shutdown(as);
        return ASM_ERR;
    }
    return ASM_OK;
}

int assembler_predefine(ASSEMBLER *as, const char *name, const char *value) {
    if(!as || !name || name[0] == '\0' || !value) {
        return ASM_ERR;
    }
    DEFINE d;
    memset(&d, 0, sizeof(d));
    d.from_len = (int)strlen(name);
    d.to_len = (int)strlen(value);
    d.from = malloc((size_t)d.from_len + 1);
    d.to = malloc((size_t)d.to_len + 1);
    if(!d.from || !d.to) {
        free(d.from);
        free(d.to);
        return ASM_ERR;
    }
    memcpy(d.from, name, (size_t)d.from_len + 1);
    memcpy(d.to, value, (size_t)d.to_len + 1);
    if(ASM_OK != AM65_ARRAY_ADD(&as->predefines, d)) {
        free(d.from);
        free(d.to);
        return ASM_ERR;
    }
    return ASM_OK;
}

void assembler_set_cpu_profile(ASSEMBLER *as, assembler_cpu_profile profile) {
    if(!as) {
        return;
    }
    if(profile < ASM_CPU_6502 || profile > ASM_CPU_WDC) {
        profile = ASM_CPU_6502;
    }
    as->cpu_profile = profile;
    as->default_cpu_profile = profile;
}

assembler_cpu_profile assembler_get_cpu_profile(const ASSEMBLER *as) {
    return as ? as->cpu_profile : ASM_CPU_6502;
}

void assembler_set_auto_adjust_segments(ASSEMBLER *as, int enabled) {
    if(as) {
        as->auto_adjust_segments = enabled ? 1 : 0;
    }
}

static int assembler_run_pass(
    ASSEMBLER *as,
    const char *input_file) {
    reset_pass_state(as);

    if(as->pass == 1) {
        if(ASM_OK != file_load(as, input_file)) {
            return ASM_ERR;
        }
        free(as->root_dir);
        as->root_dir = NULL;
        if(as->root_file && as->root_file->display_name) {
            const char *slash = strrchr(as->root_file->display_name, '/');
            if(slash) {
                size_t dir_len =
                    (size_t)(slash - as->root_file->display_name + 1);
                as->root_dir = malloc(dir_len + 1);
                if(as->root_dir) {
                    memcpy(as->root_dir, as->root_file->display_name, dir_len);
                    as->root_dir[dir_len] = '\0';
                }
            }
        }
    } else if(ASM_OK != file_stack_reset_for_pass2(as)) {
        return ASM_ERR;
    }

    while(as->file_stack.items > 0) {
        if(!file_read_line(as)) {
            file_stack_pop(as);
            continue;
        }

        strip_comment(as->line, &as->line_len);
        if(as->if_skip_depth > 0) {
            parse_if_skip(as);
            continue;
        }

        macro_substitute_line(as);
        define_substitute(as);
        as->cur = as->line;
        parse_line(as);
    }

    if(as->scope_stack.items > 0) {
        asm_err(as, ASM_ERR_RESOLVE, "Unclosed scope at end of assembly");
    }
    if(as->loop_stack.items > 0) {
        asm_err(as, ASM_ERR_RESOLVE, "Unclosed loop at end of assembly");
    }
    if(as->if_stack.items > 0 || as->if_skip_depth > 0) {
        asm_err(as, ASM_ERR_RESOLVE, "Unclosed conditional assembly block");
    }
    return ASM_OK;
}

int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address) {
    if(!as || !input_file || !as->active_target || !as->active_target->active_segment) {
        return ASM_ERR;
    }

    size_t initial_errors = as->errorlog ? as->errorlog->log_array.items : 0;
    int adjustment_retries = 0;

    reset_source_for_assemble(as);
    as->anon_symbols.items = 0;
    segment_adjustments_clear(&as->segment_adjustments);

    for(;;) {
        AM65_DYNARRAY suggestions;
        SEGMENT_CHECK_RESULT layout;
        SEGMENT *default_segment = as->active_target->active_segment;
        uint16_t adjusted_address =
            assembler_adjust_segment_start(as, NULL, 0, address);
        default_segment->segment_start_address = adjusted_address;
        default_segment->segment_output_address = adjusted_address;
        default_segment->segment_init = 1;

        as->pass = 1;
        if(ASM_OK != assembler_run_pass(as, input_file)) {
            return ASM_ERR;
        }

        if(!as->auto_adjust_segments ||
           (as->errorlog && as->errorlog->log_array.items > initial_errors)) {
            break;
        }

        if(ASM_OK != segment_adjustments_copy(
                &suggestions,
                &as->segment_adjustments)) {
            asm_err(as, ASM_ERR_FATAL,
                    "Out of memory tracking adjusted segment addresses");
            return ASM_ERR;
        }
        layout = check_segment_overlaps(as, &suggestions, 0);
        if(layout.allocation_failed) {
            segment_adjustments_clear(&suggestions);
            return ASM_ERR;
        }
        if(layout.wraps > 0 || layout.locked_conflict > 0) {
            segment_adjustments_clear(&suggestions);
            (void)check_segment_overlaps(as, NULL, 1);
            return ASM_ERR;
        }
        if(layout.overlaps == 0) {
            segment_adjustments_clear(&suggestions);
            break;
        }
        if(adjustment_retries >= AUTO_ADJUST_MAX_RETRIES) {
            segment_adjustments_clear(&suggestions);
            (void)check_segment_overlaps(as, NULL, 1);
            return ASM_ERR;
        }

        segment_adjustments_clear(&as->segment_adjustments);
        as->segment_adjustments = suggestions;
        adjustment_retries++;
        if(ASM_OK != assembler_restart_program_state(as)) {
            asm_err(as, ASM_ERR_FATAL,
                    "Out of memory restarting adjusted segment layout");
            return ASM_ERR;
        }
    }

    as->pass = 2;
    if(ASM_OK != assembler_run_pass(as, input_file)) {
        return ASM_ERR;
    }

    if(!as->errorlog || as->errorlog->log_array.items == initial_errors) {
        SEGMENT_CHECK_RESULT final_layout =
            check_segment_overlaps(as, NULL, 1);
        if(final_layout.allocation_failed) {
            return ASM_ERR;
        }
        (void)check_noemit_reclaim(as);
    }

    return as->errorlog && as->errorlog->log_array.items > initial_errors ?
        ASM_ERR : ASM_OK;
}

static int assembler_symbol_is_macro_local(const SYMBOL_LABEL *symbol) {
    return symbol->symbol_length == GEN_NAME_LEN &&
        0 == strncmp(symbol->symbol_name, "__macro_local_", 14);
}

static int assembler_symbol_append(char *out, size_t out_size, size_t *len, const char *text, size_t text_len) {
    if(*len + text_len >= out_size) {
        return 0;
    }
    memcpy(out + *len, text, text_len);
    *len += text_len;
    out[*len] = '\0';
    return 1;
}

static void assembler_walk_scope_symbols(SCOPE *scope, char *prefix, size_t prefix_len, assembler_symbol_cb cb, void *user) {
    char name[512];

    if(!scope || !cb) {
        return;
    }

    for(int bucket = 0; bucket < HASH_BUCKETS; bucket++) {
        AM65_DYNARRAY *symbols = &scope->symbol_table[bucket];
        for(size_t i = 0; i < symbols->items; i++) {
            SYMBOL_LABEL *symbol = AM65_ARRAY_GET(symbols, SYMBOL_LABEL, i);
            size_t name_len = 0;
            if(symbol->symbol_type != SYMBOL_ADDRESS || assembler_symbol_is_macro_local(symbol)) {
                continue;
            }
            if(!assembler_symbol_append(name, sizeof(name), &name_len, prefix, prefix_len)) {
                continue;
            }
            if(!assembler_symbol_append(name, sizeof(name), &name_len, symbol->symbol_name, symbol->symbol_length)) {
                continue;
            }
            cb(name, (uint16_t)symbol->symbol_value, user);
        }
    }

    for(size_t i = 0; i < scope->child_scopes.items; i++) {
        SCOPE *child = *AM65_ARRAY_GET(&scope->child_scopes, SCOPE*, i);
        char child_prefix[512];
        size_t child_prefix_len = prefix_len;
        memcpy(child_prefix, prefix, prefix_len);
        child_prefix[child_prefix_len] = '\0';
        if(!assembler_symbol_append(child_prefix, sizeof(child_prefix), &child_prefix_len, child->scope_name, (size_t)child->scope_name_length)) {
            continue;
        }
        if(!assembler_symbol_append(child_prefix, sizeof(child_prefix), &child_prefix_len, "::", 2)) {
            continue;
        }
        assembler_walk_scope_symbols(child, child_prefix, child_prefix_len, cb, user);
    }
}

void assembler_walk_symbols(ASSEMBLER *as, assembler_symbol_cb cb, void *user) {
    char prefix[1] = {0};

    if(!as || !cb || !as->root_scope) {
        return;
    }
    assembler_walk_scope_symbols(as->root_scope, prefix, 0, cb, user);
}

void assembler_walk_segment_adjustments(
    ASSEMBLER *as,
    assembler_segment_adjustment_cb cb,
    void *user) {
    if(!as || !cb) {
        return;
    }
    for(size_t i = 0; i < as->segment_adjustments.items; i++) {
        SEGMENT_ADJUSTMENT *adjustment =
            AM65_ARRAY_GET(&as->segment_adjustments, SEGMENT_ADJUSTMENT, i);
        cb(
            adjustment->target_index,
            adjustment->segment_name ? adjustment->segment_name : "",
            adjustment->address,
            user);
    }
}

void assembler_shutdown(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    assembler_program_state_destroy(as);
    for(size_t i = 0; i < as->predefines.items; i++) {
        DEFINE *d = AM65_ARRAY_GET(&as->predefines, DEFINE, i);
        free(d->from);
        free(d->to);
    }
    am65_array_free(&as->predefines);
    segment_adjustments_clear(&as->segment_adjustments);
}
