// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// input stack & macros
static void flush_macros(ASSEMBLER *as) {
    size_t i;
    for(i = 0; i < as->macros.items; i++) {
        MACRO *m = ARRAY_GET(&as->macros, MACRO, i);
        array_free(&m->macro_parameters);
    }
    as->macros.items = 0;
}

//----------------------------------------------------------------------------
// Token identification
int is_address(ASSEMBLER *as) {
    if(*as->token_start != '*') {
        return 0;
    }
    return 1;
}

int is_label(ASSEMBLER *as) {
    if(*(as->input - 1) != ':') {
        return 0;
    }
    return 1;
}

int is_opcode(ASSEMBLER *as) {
    OPCODEINFO *oi;
    if(as->input - as->token_start != 3 || *as->token_start == '.' || !(oi = in_word_set(as->token_start, 3))) {
        return 0;
    }
    as->opcode_info = *oi;
    return 1;
}

int is_parse_dot_command(ASSEMBLER *as) {
    OPCODEINFO *oi;
    if(*as->token_start != '.' || !(oi = in_word_set(as->token_start, as->input - as->token_start))) {
        return 0;
    }
    as->opcode_info = *oi;
    return 1;
}

int is_variable(ASSEMBLER *as) {
    const char *c = as->token_start;
    const char *e = as->input;

    unsigned char ch0 = (unsigned char)*c;

    // Starts with '_' or alpha or ':' (but if ':' it must be '::')
    if(ch0 == '_') {
        c++;
    } else if(isalpha(ch0)) {
        c++;
    } else if(ch0 == ':') {
        if(c + 1 >= e || c[1] != ':') {
            return 0;
        }
        c += 2;
    } else {
        return 0;
    }

    while(c < e) {
        unsigned char ch = (unsigned char)*c;

        if(ch == '_' || isalnum(ch)) {
            c++;
            continue;
        }

        if(ch == ':') {
            if(c + 1 >= e || c[1] != ':') {
                return 0;
            }
            c += 2;
            continue;
        }

        return 0;
    }

    return 1;
}


//----------------------------------------------------------------------------
// Assembler start and end routines
int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, CB_ASSEMBLER_CTX *cb_asm_ctx, void *initial_target_context) {

    memset(as, 0, sizeof(ASSEMBLER));
    as->errorlog = errorlog;
    as->cb_assembler_ctx = *cb_asm_ctx;

    ARRAY_INIT(&as->anon_symbols, uint16_t);
    ARRAY_INIT(&as->loop_stack, LOOP);
    ARRAY_INIT(&as->macros, MACRO);
    ARRAY_INIT(&as->macro_buffers, char *);
    ARRAY_INIT(&as->macro_expand_stack, MACRO_EXPAND);
    ARRAY_INIT(&as->targets, TARGET*);
    ARRAY_INIT(&as->scope_stack, SCOPE*);
    ARRAY_INIT(&as->input_stack, INPUT_STACK);

    as->active_target = add_target(as, initial_target_context);

    if(!as->active_target) {
        return A2_ERR;
    }

    as->root_scope = (SCOPE*)malloc(sizeof(SCOPE));
    if(!as->root_scope || A2_OK != scope_init(as->root_scope, GPERF_DOT_SCOPE)) {
        return A2_ERR;
    }
    if(!set_name(&as->root_scope->scope_name, "root", 4)) {
        return A2_ERR;
    }
    as->root_scope->scope_name_length = 4;
    scope_push(as, as->root_scope);
    include_files_init(as);

    as->pass = 0;
    return A2_OK;
}

static void assembler_reset_targets_and_scopes(ASSEMBLER *as) {
    // Leave the original unnamed segment alive
    for(size_t i = 1; i < as->targets.items; i++) {
        TARGET *t = *ARRAY_GET(&as->targets, TARGET*, i);
        for(size_t j = 0; j < t->segments.items; j++) {
            SEGMENT *s = *ARRAY_GET(&t->segments, SEGMENT*, j);
            free(s);
        }
        array_free(&t->segments);
        as->cb_assembler_ctx.output_redirect_release_context(t->target_ctx);
        free(t);
    }
    // And leave the original target alive as well
    as->active_target = *ARRAY_GET(&as->targets, TARGET*, 0);
    as->active_target->segments.items = 1;
    as->active_target->active_segment = *ARRAY_GET(&as->active_target->segments, SEGMENT*, 0);
    as->active_target->active_segment->segment_output_address = as->active_target->active_segment->segment_start_address;
    as->targets.items = 1;
    as->active_scope = as->root_scope;
    scope_reset_ids(as->active_scope);       
    as->macro_rename_id = 0;    // Reset the global macro unique making ID
}

static int assembler_segments_sort(const void *lhs, const void *rhs) {
    return (*(SEGMENT**)lhs)->segment_start_address >= (*(SEGMENT**)rhs)->segment_start_address;
}

static void assembler_segment_errors(ASSEMBLER *as) {
    for(int i=0; i < as->targets.items; i++) {
        TARGET *t = *ARRAY_GET(&as->targets, TARGET*, i);
        if(t->segments.items) {
            // Sort in ouput order
            qsort(t->segments.data, t->segments.items, t->segments.element_size, assembler_segments_sort);
            uint32_t emit = 0, issue = 0;
            uint16_t emit_end;
            // show errors for overlapping segments
            for(int si = 0; si < t->segments.items; si++) {
                SEGMENT *s = *ARRAY_GET(&t->segments, SEGMENT*, si);
                if(s->do_not_emit) {
                    continue;
                }
                if(!emit) {
                    emit_end = s->segment_output_address;
                    emit = 1;
                } else {
                    // There is no warning mechanism and I don't want intentional "growth gaps"
                    // to stop the iteration process, so ignore gaps, just error overlaps
                    if(s->segment_start_address < emit_end) {
                        asm_err(as, ASM_ERR_RESOLVE, "Start Segment %.*s at $%04X*", s->segment_name_length, s->segment_name, emit_end);
                        issue = 1;
                    }
                    uint16_t seg_size = s->segment_output_address - s->segment_start_address;
                    emit_end += seg_size;
                }
            }
            if(issue) {
                asm_err(as, ASM_ERR_RESOLVE, "* Offsets may be slightly off, based on .align statement changing output size");
            }
        }
    }
}

int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address) {
    as->pass = 0;
    as->current_file = input_file;
    char start_path[PATH_MAX];
    const char *name = util_strrtok(input_file, "\\/");
    if(name) {
        util_dir_get_current(start_path, PATH_MAX);
        char file_path[PATH_MAX];
        int len = name - input_file;
        len = len > PATH_MAX ? PATH_MAX : len;
        strncpy(file_path, input_file, len);
        file_path[len] = '\0';
        util_dir_change(file_path);
        input_file = name + 1;
    }
    while(as->pass < 2) {
        if(A2_OK != include_files_push(as, input_file)) {
            return A2_ERR;
        }
        as->current_file = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, 0)->file_display_name;
        as->pass++;
        if(as->active_scope != as->root_scope) {
            asm_err(as, ASM_ERR_RESOLVE, "Scope error.  Open scope %.*s, or one of its children, was not closed", as->active_scope->scope_name_length, as->active_scope->scope_name);
        }
        while(as->scope_stack.items > 1) {
            // Just to be safe...
            scope_pop(as);
        }
        // Reset scopes (solves open scope isues and repeatability, of course)
        if(as->pass == 2) {
            assembler_reset_targets_and_scopes(as);
        }
        while(as->pass < 3) {
            do {
                // a newline returns an empty token so keep
                // getting tokens till there's something to process
                get_token(as);
            } while(as->token_start == as->input && *as->input);

            if(as->token_start == as->input) {
                // The end of the file has been reached so if it was an included file
                // pop to the parent, or end
                if(A2_OK == include_files_pop(as)) {
                    continue;
                }
                break;
            }

            // Prioritize specific checks to avoid ambiguity
            if(is_opcode(as)) {                               // Opcode first
                parse_opcode(as);
            } else if(is_parse_dot_command(as)) {             // Dot commands next
                parse_dot_command(as);
            } else if(is_label(as)) {                         // Labels (endings in ":")
                parse_label(as);
            } else if(is_address(as)) {                       // Address (starting with "*")
                parse_address(as);
            } else if(parse_macro_if_is_macro(as)) {          // Macros are like variables but can be found
                continue;
            } else if(is_variable(as)) {                      // Variables are last (followed by "=")
                parse_variable(as);
            } else {
                asm_err(as, ASM_ERR_RESOLVE, "Unknown token");
            }
        }
        // Flush the macros between passes so they can be re-parsed and
        // errors reported properly om the second pass
        flush_macros(as);
    }
    as->token_start = as->input = as->line_start = NULL;
    // Show all errors
    as->error_log_level = 1;
    assembler_segment_errors(as);
    if(name) {
        // Go back to the start folder if it was changed
        util_dir_change(start_path);
    }
    return A2_OK;
}

void assembler_shutdown(ASSEMBLER *as) {
    include_files_cleanup(as);

    // Remove all of the scopes and symbol tables
    scope_destroy(as->root_scope);

    // free active macro buffer (if any)
    while(as->macro_buffers.items) {
        char *buf = *ARRAY_GET(&as->macro_buffers, char*, as->macro_buffers.items - 1);
        free(buf);
        as->macro_buffers.items--;
    }

    // Clean up the targets and the contexts within the targets
    for(size_t i = 0; i < as->targets.items; i++) {
        TARGET *t = *ARRAY_GET(&as->targets, TARGET*, i);
        for(size_t j = 0; j < t->segments.items; j++) {
            SEGMENT *s = *ARRAY_GET(&t->segments, SEGMENT*, j);
            free(s);
        }
        array_free(&t->segments);
        as->cb_assembler_ctx.output_redirect_release_context(t->target_ctx);
        free(t);
    }    
    array_free(&as->input_stack);
    array_free(&as->targets);
    array_free(&as->macros);
    array_free(&as->macro_buffers);
    array_free(&as->loop_stack);
    array_free(&as->scope_stack);
    array_free(&as->anon_symbols);
}

