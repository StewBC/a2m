// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

typedef struct {                                  // This is for the macro call arguments
    const char *text;
    int text_length;
} MACRO_ARG;

typedef struct {                             // This is for the formal macro parameters
    const char *variable_name;
    int variable_name_length;
} MACRO_VARIABLE;

//----------------------------------------------------------------------------
// Parser static helpers
static inline void set_current_output_address(ASSEMBLER *as, uint16_t address) {
    if(as->active_segment) {
        as->active_segment->segment_output_address = address;
    } else {
        as->current_address = address;
    }
}

static int select_token_string(ASSEMBLER *as, int count, ...) {
    va_list args;
    va_start(args, count);

    // For all token arguments
    for(int i = 0; i < count; i++) {
        int j = 0;
        // Get the argument
        const char *str = va_arg(args, const char *);
        // Compare token start to the argument string
        while(*str && as->token_start[j] && *str == as->token_start[j]) {
            str++;
            j++;
        }
        if(!(*str)) {
            // If !*str it was a match
            as->token_start += j;
            va_end(args);
            return i;
        }
    }
    va_end(args);
    return -1;
}

static int is_implied_instruction(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
        case GPERF_OPCODE_ASL:
        case GPERF_OPCODE_LSR:
        case GPERF_OPCODE_ROL:
        case GPERF_OPCODE_ROR:
            if(as->current_token.type == TOKEN_VAR) {
                if(as->current_token.name_length == 1 && tolower(*as->token_start) == 'a') {
                    next_token(as);
                }
            }
            return as->current_token.type == TOKEN_END;

        default:
            if(!as->opcode_info.width) {
                return 1;
            }
    }
    return 0;
}

static MACRO_ARG parse_macro_args(const char **pp) {
    const char *p = *pp;

    // trim leading space
    while(*p == ' ' || *p == '\t') {
        p++;
    }

    const char *start = p;
    int parens = 0;
    int in_string = 0;

    // lift the token, respecting '"'s - ','s have to be quoted, ie
    // my_macro "lda ($50),y", sta $25
    // will work, and the inner ',' isn't mistaken as a arg seperator
    while(*p) {
        char c = *p;

        if(!in_string) {
            if(c == ';' || c == '\n' || c == '\r') {
                break;
            }
            if(c == ',' && parens == 0) {
                break;
            }
            if(c == '(') {
                parens++;
            } else if(c == ')' && parens > 0) {
                parens--;
            }
            if(c == '"') {
                in_string = 1;
                if(p == start) {
                    start++;
                }
            }
        } else {
            if(c == '"' && p[-1] != '\\') {
                in_string = 0;
                break;
            }
            if(c == '\n' || c == '\r') {
                break;
            }
        }

        p++;
    }

    const char *end = p;

    // Skip the closing quote after setting the length right
    if(*p == '"') {
        p++;
    }

    // trim trailing space
    while(end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    MACRO_ARG a;
    a.text = start;
    a.text_length = (int)(end - start);

    *pp = p;
    return a;
}

static int resolve_def_target(ASSEMBLER *as, const char *name, int len, SCOPE **out_parent, const char **out_leaf, int *out_leaf_len) {
    if(len >= 2 && name[0] == ':' && name[1] == ':') {
        *out_parent = as->root_scope;
        *out_leaf = name + 2;
        *out_leaf_len = len - 2;
        return 1;
    }
    *out_parent = as->active_scope;
    *out_leaf = name;
    *out_leaf_len = len;
    return 1;
}

void append_bytes_to_buffer(char **buf, size_t *len, size_t *cap, const char *src, size_t n) {
    if(n == 0) {
        return;
    }
    if(*len + n + 1 > *cap) {
        size_t newcap = (*cap == 0) ? 256 : *cap;
        while(*len + n + 1 > newcap) {
            newcap *= 2;
        }
        char *nb = (char *)realloc(*buf, newcap);
        if(!nb) {
            return;
        }
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static void find_ab_passing_over_c(ASSEMBLER *as, const char *a, const char *b, const char *c) {
    int nest_level = 1;
    size_t a_len = strlen(a);
    size_t b_len = b ? strlen(b) : 0;
    size_t c_len = strlen(c);
    do {
        get_token(as);
        if(*as->input && !strnicmp(c, as->token_start, c_len)) {
            find_ab_passing_over_c(as, a, NULL, c);
            continue;
        }
        if(nest_level && !strnicmp(a, as->token_start, a_len)) {
            if(!(--nest_level)) {
                break;
            }
            continue;
        }
        if(nest_level && b_len && !strnicmp(b, as->token_start, b_len)) {
            if(!(--nest_level)) {
                break;
            }
            continue;
        }
    } while(*as->input);
    if(as->input == as->token_start) {
        get_token(as);
    }
}

//----------------------------------------------------------------------------
// Assembly generator helpers
static void decode_abs_rel_zp_opcode(ASSEMBLER *as) {
    // Relative (branch) instructions have their opcodes in zero page as well
    int relative = as->opcode_info.width == 1;
    as->opcode_info.addressing_mode = ADDRESS_MODE_ZEROPAGE;
    if(!relative) {
        // Not relative, so may be absolute based on operand size
        if(as->opcode_info.value >= 256 || as->opcode_info.width > 8) {
            as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE;
            as->opcode_info.width = 16;
        }
        if(as->current_token.op == ',') {
            // Make sure a comma is followed by x or y
            get_token(as);
            switch(tolower(*as->token_start)) {
                case 'x':
                    as->opcode_info.addressing_mode++;
                    break;
                case 'y':
                    if(as->opcode_info.width < 16 && as->opcode_info.addressing_mode == ADDRESS_MODE_ZEROPAGE) {
                        // lda 23, y is valid, but it's 16 bit, not ZP, it's absolute
                        as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE_Y;
                        as->opcode_info.width = 16;
                    } else {
                        as->opcode_info.addressing_mode += 2;
                    }
                    break;
                default:
                    asm_err(as, ASM_ERR_RESOLVE, "Unexpected ,%c", *as->token_start);
                    break;
            }
        }
    }
    emit_opcode(as);
}

static int is_indirect(ASSEMBLER *as, char *reg) {
    const char *c = as->token_start;
    int brackets = 1;
    // Only called when it's known there are (as)'s on the line
    // Scan forward counting brackets and looking for a ,
    while(*c && *c != ',' && *c != ';' && !util_is_newline(*c)) {
        if(*c == '(') {
            brackets++;
        } else if(*c == ')') {
            brackets--;
        }
        c++;
    }
    // No comma, not indexed
    if(*c == ',') {
        // Comma in bracket is x-indexed
        if(brackets != 0) {
            *reg = 'x';
            return ADDRESS_MODE_INDIRECT_X;
        } else {
            // Comma outside brackets is y-indexed
            *reg = 'y';
            return ADDRESS_MODE_INDIRECT_Y;
        }
    } else if(!brackets) {
        return ADDRESS_MODE_INDIRECT;
    }
    return 0;
}

//----------------------------------------------------------------------------
// Parser helpers that are used outside parse
int input_stack_push(ASSEMBLER *as) {
    INPUT_STACK is;
    is.input = as->input;
    is.token_start = as->token_start;
    is.current_file = as->current_file;
    is.current_line = as->current_line;
    is.next_line_count = as->next_line_count;
    is.next_line_start = as->next_line_start;
    is.line_start = as->line_start;
    return ARRAY_ADD(&as->input_stack, is);
}

void input_stack_pop(ASSEMBLER *as) {
    if(as->input_stack.items) {
        as->input_stack.items--;
        INPUT_STACK *is = ARRAY_GET(&as->input_stack, INPUT_STACK, as->input_stack.items);
        as->input = is->input;
        as->token_start = is->token_start;
        as->current_file = is->current_file;
        as->current_line = is->current_line;
        as->next_line_count = is->next_line_count;
        as->next_line_start = is->next_line_start;
        as->line_start = is->line_start;
    }
}

//----------------------------------------------------------------------------
// Parse DOT commands
void dot_byte(ASSEMBLER *as) {
    do {
        // Get to a token to process
        next_token(as);
        // String data, no .strcode processing
        if(as->current_token.type == TOKEN_STR) {
            emit_string(as, NULL);
        } else {
            // Non-string data must be byte expression (and 1st token already
            // loaded so use parse_ not evaluate_)
            // This includes variables, adresses, etc.
            uint64_t value = expr_evaluate(as);
            emit_byte(as, value);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Values %ld not between 0 and 255", value);
            }
        }
    } while(as->current_token.op == ',');
}

void dot_else(ASSEMBLER *as) {
    if(as->if_active) {
        as->if_active--;
    } else {
        asm_err(as, ASM_ERR_RESOLVE, ".else without .if");
    }
    find_ab_passing_over_c(as, ".endif", NULL, ".if");
    if(!*as->input) {
        asm_err(as, ASM_ERR_RESOLVE, ".else without .endif");
    }
}

void dot_endfor(ASSEMBLER *as) {
    if(as->loop_stack.items) {
        const char *post_loop = as->input;
        FOR_LOOP *for_loop = ARRAY_GET(&as->loop_stack, FOR_LOOP, as->loop_stack.items - 1);
        int same_file = stricmp(for_loop->loop_start_file, as->current_file) == 0;
        int loop_iterations = ++for_loop->iterations;
        as->token_start = as->input = for_loop->loop_adjust_start;
        expr_full_evaluate(as);
        as->token_start = as->input = for_loop->loop_condition_start;
        if(loop_iterations < 65536 && same_file && expr_full_evaluate(as)) {
            as->token_start = as->input = for_loop->loop_body_start;
            as->current_line = for_loop->body_line;
        } else {
            // Pop the for loop from the stack
            as->loop_stack.items--;
            as->token_start = as->input = post_loop;
            if(!same_file) {
                asm_err(as, ASM_ERR_RESOLVE, ".endfor matches .for in file %s, body at line %zd", for_loop->loop_start_file, for_loop->body_line);
            } else if(loop_iterations >= 65536) {
                asm_err(as, ASM_ERR_RESOLVE, "Exiting .for loop with body at line %zd, which has iterated 64K times", for_loop->body_line);
            }
            as->current_line--;
        }
    } else {
        asm_err(as, ASM_ERR_RESOLVE, ".endfor without a matching .for");
    }
}

void dot_endif(ASSEMBLER *as) {
    if(as->if_active) {
        as->if_active--;
    } else {
        asm_err(as, ASM_ERR_RESOLVE, ".endif with no .if");
    }
}

void dot_endmacro(ASSEMBLER *as) {
    // Make sure the input stack is not empty
    if(as->input_stack.items != 0) {
        // Do not free the macro buffer here, there might be a reference to a symbol name
        // Return to the parse point where the macro was called
        input_stack_pop(as);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, ".endmacro but not running a macro");
    }
}

void dot_endproc(ASSEMBLER *as) {
    if(as->active_scope->scope_type != GPERF_DOT_PROC || !scope_pop(as)) {
        asm_err(as, ASM_ERR_RESOLVE, ".endproc without a matching .proc");
    }    
}

void dot_endscope(ASSEMBLER *as) {
    if(as->active_scope->scope_type != GPERF_DOT_SCOPE || !scope_pop(as)) {
        asm_err(as, ASM_ERR_RESOLVE, ".endscope without a matching .scope");
    }    
}

void dot_for(ASSEMBLER *as) {
    FOR_LOOP for_loop;
    expr_full_evaluate(as);                                  // assignment
    for_loop.iterations = 0;
    for_loop.loop_condition_start = as->input;
    for_loop.loop_start_file = as->current_file;
    // evaluate the condition
    if(!expr_full_evaluate(as)) {
        find_ab_passing_over_c(as, ".endfor", NULL, ".for");
        if(!*as->input) {
            // failed so find .endfor (must be in same file)
            asm_err(as, ASM_ERR_RESOLVE, ".for without .endfor");
        }
    } else {
        // condition success
        for_loop.loop_adjust_start = as->input;
        // skip past the adjust condition
        do {
            get_token(as);
        } while(*as->input && as->token_start != as->input);
        // to find the loop body
        for_loop.loop_body_start = as->input;
        for_loop.body_line = as->current_line + as->next_line_count;
        ARRAY_ADD(&as->loop_stack, for_loop);
    }
}

void dot_if(ASSEMBLER *as) {
    if(!expr_full_evaluate(as)) {
        find_ab_passing_over_c(as, ".endif", ".else", ".if");
        if(!*as->input) {
            // failed so find .else or .endif (must be in same file)
            asm_err(as, ASM_ERR_RESOLVE, ".if without .endif");
        } else {
            // If not endif, it's else and that needs an endif
            if(tolower(as->token_start[2]) != 'n') {
                as->if_active++;
                // I could add a .elseif and call dot_if?
            }
        }
    } else {
        // condition success
        as->if_active++;
    }
}

void dot_incbin(ASSEMBLER *as) {
    get_token(as);
    if(*as->token_start != '"') {
        asm_err(as, ASM_ERR_RESOLVE, "include expects a \" enclosed string as a parameter");
    } else {
        UTIL_FILE new_file;
        char file_name[PATH_MAX];
        size_t string_length = as->input - as->token_start - 2;
        strncpy(file_name, as->token_start + 1, string_length);
        file_name[string_length] = '\0';

        // See if the file had previously been loaded
        UTIL_FILE *f = include_files_find_file(as, file_name);
        if(!f) {
            // If not, it's a new file, load it
            memset(&new_file, 0, sizeof(UTIL_FILE));

            if(A2_OK == util_file_load(&new_file, file_name, "rb")) {
                // Success, add to list of loaded files and assign it to f
                if(A2_OK != ARRAY_ADD(&as->include_files.included_files, new_file)) {
                    asm_err(as, ASM_ERR_FATAL, "Out of memory");
                }
                // Get a handle to the file in the array (safer than going with f = &new_file)
                f = ARRAY_GET(&as->include_files.included_files, UTIL_FILE, as->include_files.included_files.items - 1);
            } else {
                asm_err(as, ASM_ERR_RESOLVE, ".incbin could not load the file %s", file_name);
            }
        }
        if(f) {
            // if is now in memory, either previously or newly loaded
            size_t data_size = f->file_size;
            uint8_t *data = (uint8_t *)f->file_data;
            while(data_size--) {
                emit_byte(as, *data++);
            }
        }
    }
}

void dot_include(ASSEMBLER *as) {
    get_token(as);
    if(*as->token_start != '"') {
        asm_err(as, ASM_ERR_RESOLVE, "include expects a \" enclosed string as a parameter");
    } else {
        char file_name[PATH_MAX];
        size_t string_length = as->input - as->token_start - 2;
        strncpy(file_name, as->token_start + 1, string_length);
        file_name[string_length] = '\0';
        include_files_push(as, file_name);
    }
}

void dot_macro(ASSEMBLER *as) {
    int macro_okay = 1;
    MACRO macro;
    ARRAY_INIT(&macro.macro_parameters, MACRO_VARIABLE);
    next_token(as);   // Get to the name
    if(as->current_token.type != TOKEN_VAR) {
        macro.macro_name = NULL;
        macro.macro_name_length = 0;
        macro_okay = 0;
        asm_err(as, ASM_ERR_RESOLVE, "Macro has no name");
    } else {
        size_t i;
        macro.macro_name = as->token_start;
        macro.macro_name_length = (int)(as->input - as->token_start);
        // Make sure no other macro by this name exists
        for(i = 0; i < as->macros.items; i++) {
            MACRO *existing_macro = ARRAY_GET(&as->macros, MACRO, i);
            if(existing_macro->macro_name_length == macro.macro_name_length && 0 == strnicmp(existing_macro->macro_name, macro.macro_name, macro.macro_name_length)) {
                macro_okay = 0;
                asm_err(as, ASM_ERR_DEFINE, "Macro with name %.*s has already been defined", macro.macro_name_length, macro.macro_name);
            }
        }
    }
    // Add macro parameters, if any
    while(as->current_token.type == TOKEN_VAR) {
        next_token(as);
        if(as->current_token.op == ',') {
            next_token(as);
        }
        if(as->current_token.type != TOKEN_END) {
            MACRO_VARIABLE mv;
            mv.variable_name = as->token_start;
            mv.variable_name_length = (int)(as->input - as->token_start);
            ARRAY_ADD(&macro.macro_parameters, mv);
        }
    }
    // After parameters it must be the end of the line
    if(!(util_is_newline(as->current_token.op) || as->current_token.op == ';')) {
        macro_okay = 0;
        asm_err(as, ASM_ERR_DEFINE, "Macro defenition error");
    }
    // Save the parse point as the macro body start point
    input_stack_push(as);
    macro.macro_body_input = *ARRAY_GET(&as->input_stack, INPUT_STACK, as->input_stack.items - 1);
    input_stack_pop(as);
    // Look for .endmacro, ignoring the macro body
    find_ab_passing_over_c(as, ".endmacro", NULL, ".macro");
    macro.macro_body_end = as->input;
    if(!(*as->input)) {
        macro_okay = 0;
        asm_err(as, ASM_ERR_RESOLVE, ".macro %.*s L%05zu, with no .endmacro\n", macro.macro_name_length, macro.macro_name, macro.macro_body_input.current_line);
    }
    // If there were no errors, add the macro to the list of macros
    if(macro_okay) {
        ARRAY_ADD(&as->macros, macro);
    }
}

void dot_org(ASSEMBLER *as) {
    uint64_t value = expr_full_evaluate(as);
    if(as->current_address > value) {
        asm_err(as, ASM_ERR_RESOLVE, "Assigning address %"PRIx64" when address is already %04X error", value, as->current_address);
    } else {
        as->current_address = value;
        if(value < as->start_address) {
            as->start_address = value;
        }
    }
}

void dot_proc(ASSEMBLER *as) {
    next_token(as);
    if(as->current_token.type != TOKEN_VAR) {
        asm_err(as, ASM_ERR_RESOLVE, ".proc must be followed by a name");
        return;
    }

    // See if the name is root anchored or not
    SCOPE *parent = NULL;
    const char *leaf = NULL;
    int leaf_len = 0;
    resolve_def_target(as, as->current_token.name, as->current_token.name_length, &parent, &leaf, &leaf_len);
    
    // The name may also not contain further scopes
    if(token_has_scope_path(leaf, leaf_len)) {
        asm_err(as, ASM_ERR_DEFINE, "The name %.*s is scoped and not allowed", leaf_len, leaf);
        return;
    }

    SCOPE *s = scope_find_child(parent, leaf, leaf_len);
    if(s) {
        if(as->pass == 1) {
            asm_err(as, ASM_ERR_DEFINE, "In this scope a .proc has already been defined with the name %.*s", leaf_len, leaf);
        } else {
            scope_push(as, s);
        }
        return;
    }

    symbol_store_in_scope(as, parent, leaf, leaf_len, SYMBOL_ADDRESS, current_output_address(as));
    s = scope_add(as, leaf, leaf_len, parent, GPERF_DOT_PROC);
    if(s) {
        scope_push(as, s);
    }
}

void dot_res(ASSEMBLER *as) {
    uint64_t length = expr_full_evaluate(as);
    if(length > 0x10000 - current_output_address(as)) {
        asm_err(as, ASM_ERR_RESOLVE, "Reserving %"PRIx64" bytes when only %04X remain in 64K", length, 0x10000 - current_output_address(as));
    } else {
        uint64_t value = 0;
        if(as->current_token.op == ',') {
            value = expr_full_evaluate(as);
            if(value > 0xFF) {
                asm_err(as, ASM_ERR_RESOLVE, ".res cannot fill with %"PRIx64".  Only 0x00 - 0xFF allowed", value);
                return;
            }
        }
        uint8_t b = (uint8_t)value;
        while(length-- > 0) {
            emit_byte(as, b);
        }
    }
}

void dot_segdef(ASSEMBLER *as) {
    SEGMENT seg;
    int err = 0;
    memset(&seg, 0, sizeof(SEGMENT));

    next_token(as);
    if(as->current_token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, ".segdef expects a quoted segment name");
        return;
    }
    seg.segment_name = as->current_token.name;
    seg.segment_name_length = as->current_token.name_length;
    seg.segment_name_hash = as->current_token.name_hash;
    if(segment_find(&as->segments, &seg)) {
        asm_err(as, ASM_ERR_DEFINE, "Segment %.*s has already been defined", seg.segment_name_length, seg.segment_name);
        return;
    }
    next_token(as);
    if(as->current_token.type != TOKEN_END || as->current_token.op != ',') {
        asm_err(as, ASM_ERR_RESOLVE, ".segdef expects a comma then a start address after the name");
        return;
    }
    seg.segment_start_address = expr_full_evaluate(as);
    seg.segment_output_address = seg.segment_start_address;
    if(as->current_token.type != TOKEN_END || as->current_token.op == ',') {
        next_token(as);
        if(as->current_token.type != TOKEN_VAR) {
            err = 1;
        }
        if(!err) {
            if(as->current_token.name_length == 4 && 0 == strnicmp(as->current_token.name, "emit_byte", 4)) {
                seg.do_not_emit = 0;
            } else if(as->current_token.name_length == 6 && 0 == strnicmp(as->current_token.name, "noemit", 6)) {
                seg.do_not_emit = 1;
            } else {
                err = 1;
            }
        }
        if(err) {
            asm_err(as, ASM_ERR_RESOLVE, "The optional parameter to .segdef after the name and start is either emit_byte or noemit");
            return;
        }
    }
    ARRAY_ADD(&as->segments, seg);
}

void dot_segment(ASSEMBLER *as) {
    SEGMENT seg, *s = NULL;

    next_token(as);
    if(as->current_token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, ".segment expects a quoted segment name");
        return;
    }
    seg.segment_name = as->current_token.name;
    seg.segment_name_length = as->current_token.name_length;
    seg.segment_name_hash = as->current_token.name_hash;
    // Empty segment name "turns off" segments
    if(seg.segment_name_length) {
        s = segment_find(&as->segments, &seg);
        if(!s) {
            asm_err(as, ASM_ERR_DEFINE, "Segment %.*s not defined", seg.segment_name_length, seg.segment_name);
            return;
        }
    }
    // Activate a segment
    as->active_segment = s;
}

void dot_scope(ASSEMBLER *as) {
    const char *name;
    int name_length;
    char anon_name[11];
    next_token(as);
    if(as->current_token.type == TOKEN_END) {
        name_length = snprintf(anon_name, 11, "anon_%04X", ++as->active_scope->anon_scope_id);
        name = anon_name;
    } else {
        name = as->current_token.name;
        name_length = as->current_token.name_length;

        if(token_has_scope_path(name, name_length)) {
            asm_err(as, ASM_ERR_DEFINE, "The name %.*s is scoped and not allowed as a scope name", name_length, name);
            return;
        }
    }

    SCOPE *s = scope_find_child(as->active_scope, name, name_length);
    if(s) {
        scope_push(as, s);
    } else {
        SCOPE *s = scope_add(as, name, name_length, as->active_scope, GPERF_DOT_SCOPE);
        if(s) {
            scope_push(as, s);
        }
    }
}

void dot_strcode(ASSEMBLER *as) {
    // Encoding doesn't matter till 2nd pass
    if(as->pass == 2) {
        get_token(as);                                        // skip past .strcode
        // Mark the expression start
        as->strcode = as->token_start;
    }
    // Find the end of the .strcode expression
    do {
        next_token(as);
    } while(as->current_token.type != TOKEN_END);
}

void dot_string(ASSEMBLER *as) {
    SYMBOL_LABEL *sl = NULL;
    if(as->strcode) {
        // Add the _ variable if it doesn't yet exist and get a handle to the storage
        sl = symbol_store_in_scope(as, as->active_scope, "_", 1, SYMBOL_VARIABLE, 0);
    }
    do {
        // Get to a token to process
        next_token(as);
        // String data
        if(as->current_token.type == TOKEN_STR) {
            emit_string(as, sl);
        } else {
            // Non-string data must be byte expression (and 1st token already
            // loaded so use parse_ not evaluate_)
            uint64_t value = expr_evaluate(as);
            emit_byte(as, value);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Values %ld not between 0 and 255", value);
            }
        }
    } while(as->current_token.op == ',');
}

//----------------------------------------------------------------------------
// Non dot parse routines
void parse_address(ASSEMBLER *as) {
    // Address assignment like `* = EXPRESSION` or modification `* += EXPRESSION`
    uint16_t address;
    int op;
    get_token(as);
    if(-1 == (op = select_token_string(as, 3, "+=", "+", "="))) {
        asm_err(as, ASM_ERR_RESOLVE, "Address assign error");
        return;
    }
    if(0 == op) {
        // += returned just the + so get the ='s
        next_token(as);
    }
    int64_t value = expr_full_evaluate(as);
    switch(op) {
        case 0:
        case 1:
            address = current_output_address(as) + value;
            break;
        case 2:
            address = value;
            break;
    }
    if(current_output_address(as) > address) {
        asm_err(as, ASM_ERR_RESOLVE, "Assigning address %04X when address is already %04X error", address, current_output_address(as));
    } else {
        set_current_output_address(as, address);
        // as->current_address = address;
        if(address < as->start_address) {
            as->start_address = address;
        }
    }
}

void parse_dot_command(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
        case GPERF_DOT_6502:
            as->valid_opcodes = 1;
            break;
        case GPERF_DOT_65c02:
            as->valid_opcodes = 0;
            break;
        case GPERF_DOT_ALIGN: {
                uint64_t value = expr_full_evaluate(as);
                // as->current_address = (as->current_address + (value - 1)) & ~(value - 1);
                set_current_output_address(as, (current_output_address(as) + (value - 1)) & ~(value - 1));
            }
            break;
        case GPERF_DOT_BYTE:
            dot_byte(as);
            break;
        case GPERF_DOT_DROW:
            emit_cs_values(as, 16, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DROWD:
            emit_cs_values(as, 32, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DROWQ:
            emit_cs_values(as, 64, BYTE_ORDER_HI);
            break;
        case GPERF_DOT_DWORD:
            emit_cs_values(as, 32, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_ELSE:
            dot_else(as);
            break;
        case GPERF_DOT_ENDFOR:
            dot_endfor(as);
            break;
        case GPERF_DOT_ENDIF:
            dot_endif(as);
            break;
        case GPERF_DOT_ENDMACRO:
            dot_endmacro(as);
            break;
        case GPERF_DOT_ENDPROC:
            dot_endproc(as);
            break;
        case GPERF_DOT_ENDSCOPE:
            dot_endscope(as);
            break;
        case GPERF_DOT_FOR:
            dot_for(as);
            break;
        case GPERF_DOT_IF:
            dot_if(as);
            break;
        case GPERF_DOT_INCBIN:
            dot_incbin(as);
            break;
        case GPERF_DOT_INCLUDE:
            dot_include(as);
            break;
        case GPERF_DOT_MACRO:
            dot_macro(as);
            break;
        case GPERF_DOT_ORG:
            dot_org(as);
            break;
        case GPERF_DOT_PROC:
            dot_proc(as);
            break;
        case GPERF_DOT_QWORD:
            emit_cs_values(as, 64, BYTE_ORDER_LO);
            break;
        case GPERF_DOT_RES:
            dot_res(as);
            break;
        case GPERF_DOT_SEGDEF:
            dot_segdef(as);
            break;
        case GPERF_DOT_SEGMENT:
            dot_segment(as);
            break;
        case GPERF_DOT_SCOPE:
            dot_scope(as);
            break;
        case GPERF_DOT_STRCODE:
            dot_strcode(as);
            break;
        case GPERF_DOT_STRING:
            dot_string(as);
            break;
        case GPERF_DOT_WORD:
            emit_cs_values(as, 16, BYTE_ORDER_LO);
            break;
        default:
            asm_err(as, ASM_ERR_RESOLVE, "opcode with id:%d not understood", as->opcode_info.opcode_id);
    }
}

void parse_label(ASSEMBLER *as) {
    const char *symbol_name = as->token_start;
    uint32_t name_length = as->input - 1 - as->token_start;
    if(!name_length) {
        // No name = anonymous label
        if(as->pass == 1) {
            // Only add anonymous labels in pass 1 (so there's no doubling up)
            uint16_t address = current_output_address(as);
            ARRAY_ADD(&as->anon_symbols, address);
        }
    } else {
        symbol_store_qualified(as, symbol_name, name_length, SYMBOL_ADDRESS, current_output_address(as));
    }
}

// In this case, also do the work since finding the macro is
// already a significant effort
int parse_macro_if_is_macro(ASSEMBLER *as) {
    size_t i;
    int name_length = (int)(as->input - as->token_start);

    for(i = 0; i < as->macros.items; i++) {
        MACRO *macro = ARRAY_GET(&as->macros, MACRO, i);
        if(name_length == macro->macro_name_length && 0 == strnicmp(as->token_start, macro->macro_name, name_length)) {

            const size_t argc = macro->macro_parameters.items;

            // Get the raw parameters to the macro
            MACRO_ARG *args = NULL;
            if(argc) {
                args = (MACRO_ARG *)calloc(argc, sizeof(MACRO_ARG));
                if(!args) {
                    asm_err(as, ASM_ERR_FATAL, "Out of memory");
                    return 1;
                }
            }

            const char *p = as->input; // points after macro name token
            for(size_t ai = 0; ai < argc; ai++) {
                // If at end-of-line or at a comment, remaining args are empty
                while(*p == ' ' || *p == '\t') {
                    p++;
                }
                if(*p == '\0' || *p == ';' || *p == '\n' || *p == '\r') {
                    args[ai].text = NULL;
                    args[ai].text_length = 0;
                    continue;
                }
                args[ai] = parse_macro_args(&p);
                // skip comma
                if(*p == ',') {
                    p++;    
                }
            }

            // Advance input to end-of-line, past all macro parameters so parsing can continue
            while(*p && *p != '\n' && *p != '\r') {
                if(*p == ';') {
                    break;
                }
                p++;
            }
            as->input = p;
            as->token_start = p;
            input_stack_push(as);

            // Expand macro body using get_token()
            const char *body_start = macro->macro_body_input.input;
            const char *body_end   = macro->macro_body_end;

            if(!body_start || !body_end || body_end < body_start) {
                free(args);
                asm_err(as, ASM_ERR_RESOLVE, "Macro body end not set for %.*s", macro->macro_name_length, macro->macro_name);
                return 1;
            }

            // Set up to parse macro body
            as->input = as->token_start = body_start;
            as->next_line_count = 0;

            char *out = NULL;
            size_t out_len = 0, out_cap = 0;

            const char *cursor = body_start;

            while(as->input < body_end && *as->input) {
                get_token(as); // advances as->input, sets as->token_start

                const char *ts = as->token_start;
                const char *te = as->input;

                if(ts > body_end) {
                    ts = body_end;
                }
                if(te > body_end) {
                    te = body_end;
                }

                // Copy bytes before token
                if(cursor < ts) {
                    append_bytes_to_buffer(&out, &out_len, &out_cap, cursor, (size_t)(ts - cursor));
                }

                // Copy or substitute token bytes
                if(ts < te) {
                    if(is_variable(as)) {
                        int substituted = 0;
                        for(size_t mi = 0; mi < argc; mi++) {
                            MACRO_VARIABLE *mv = ARRAY_GET(&macro->macro_parameters, MACRO_VARIABLE, mi);
                            if(mv->variable_name_length == (int)(te - ts) &&
                                    0 == strnicmp(ts, mv->variable_name, (size_t)(te - ts))) {
                                // Substitute raw argument text
                                if(args[mi].text && args[mi].text_length > 0) {
                                    append_bytes_to_buffer(&out, &out_len, &out_cap, args[mi].text, (size_t)args[mi].text_length);
                                }
                                substituted = 1;
                                break;
                            }
                        }
                        if(!substituted) {
                            append_bytes_to_buffer(&out, &out_len, &out_cap, ts, (size_t)(te - ts));
                        }
                    } else {
                        append_bytes_to_buffer(&out, &out_len, &out_cap, ts, (size_t)(te - ts));
                    }
                }

                cursor = te;
            }

            // Copy any trailing bytes up to body_end
            if(cursor < body_end) {
                append_bytes_to_buffer(&out, &out_len, &out_cap, cursor, (size_t)(body_end - cursor));
            }
            // Add the body to the list
            // Note that these have to stay past pass 1 so that
            // symbols in pass 2, from here in pass 1, still exist
            // I could reuse the buffer in pass 2 if I hash the sig
            // or I can just live with all of these buffers, which
            // is what I currently choose to do
            ARRAY_ADD(&as->macro_buffers, out);


            free(args);

            if(!out) {
                asm_err(as, ASM_ERR_FATAL, "Out of memory");
                return 1;
            }

            // Ensure buffer ends with newline
            if(out_len == 0 || out[out_len - 1] != '\n') {
                append_bytes_to_buffer(&out, &out_len, &out_cap, "\n", 1);
            }

            // Activate expanded macro buffer directly
            as->current_file = macro->macro_body_input.current_file;
            as->current_line = macro->macro_body_input.current_line;
            as->line_start   = out;
            as->next_line_start = out;
            as->next_line_count = 0;
            as->input = as->token_start = out;

            return 1;
        }
    }
    return 0;
}

void parse_opcode(ASSEMBLER *as) {
    next_token(as);
    if(is_implied_instruction(as)) {
        // Implied
        if(as->valid_opcodes && !asm_opcode_type[as->opcode_info.opcode_id][as->opcode_info.addressing_mode]) {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %.3s with mode %s only valid in 65c02", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
        }
        emit_byte(as, asm_opcode[as->opcode_info.opcode_id][ADDRESS_MODE_ACCUMULATOR]);
    } else {
        int processed = 0;
        switch(as->current_token.op) {
            case '#':
                // Immediate
                as->opcode_info.value = expr_full_evaluate(as);
                as->opcode_info.addressing_mode = ADDRESS_MODE_IMMEDIATE;
                emit_opcode(as);
                processed = 1;
                break;
            case '(': {
                    char reg;
                    int indirect = is_indirect(as, &reg);
                    if(indirect) {
                        // Already inside bracket
                        if(reg == 'x') {
                            // , in ,x ends expression so evaluate to ignore active open (
                            as->opcode_info.value = expr_full_evaluate(as);
                        } else {
                            // start with the "active" ( and end in ) so parse
                            as->opcode_info.value = expr_evaluate(as);
                        }
                        as->opcode_info.addressing_mode = indirect;
                        emit_opcode(as);
                        if(indirect != ADDRESS_MODE_INDIRECT) {
                            // Indirect x or y need extra steps
                            next_token(as);
                            // Make sure it was ,x or ,y
                            if(tolower(*as->token_start) != reg) {
                                asm_err(as, ASM_ERR_RESOLVE, "Expected ,%c", reg);
                            }
                            next_token(as);
                            // If it was ,x go past the closing )
                            if(as->current_token.op == ')') {
                                next_token(as);
                            }
                        }
                        processed = 1;
                    }
                }
                break;
        }
        if(!processed) {
            // general case
            if(as->current_token.type == TOKEN_END) {
                asm_err(as, ASM_ERR_RESOLVE, "Opcode %.3s with mode %s expects an operand", as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
            }
            as->opcode_info.value = expr_evaluate(as);
            if(as->opcode_info.width >= 8 && as->expression_size > 8) {
                as->opcode_info.width = 16;                 //as->expression_size;
            }
            decode_abs_rel_zp_opcode(as);
        }
    }
}

void parse_variable(ASSEMBLER *as) {
    const char *symbol_name = as->token_start;
    uint32_t name_length = as->input - as->token_start;
    next_token(as);
    expect_op(as, '=');
    symbol_store_in_scope(as, as->active_scope, symbol_name, name_length, SYMBOL_VARIABLE, expr_evaluate(as));
}
