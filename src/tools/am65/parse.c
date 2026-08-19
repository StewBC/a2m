// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

static const char *skip_space(const char *p) {
    while(*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int token_text_is(ASSEMBLER *as, const char *text) {
    size_t len = strlen(text);
    return as->token.name_length == len && 0 == asm_strnicmp(as->token.name, text, len);
}

static char *copy_trimmed_slice(const char *start, int len) {
    while(len > 0 && (*start == ' ' || *start == '\t')) {
        start++;
        len--;
    }
    while(len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) {
        len--;
    }
    char *out = malloc((size_t)len + 1);
    if(!out) {
        return NULL;
    }
    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    return out;
}

static char *copy_slice(const char *start, int len) {
    if(len < 0) {
        len = 0;
    }
    char *out = malloc((size_t)len + 1);
    if(!out) {
        return NULL;
    }
    if(len > 0) {
        memcpy(out, start, (size_t)len);
    }
    out[len] = '\0';
    return out;
}

static char *copy_token_name(ASSEMBLER *as) {
    char *out = malloc((size_t)as->token.name_length + 1);
    if(!out) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory copying token name");
        return NULL;
    }
    memcpy(out, as->token.name, as->token.name_length);
    out[as->token.name_length] = '\0';
    return out;
}

static int identifier_start(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int identifier_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

// True when name is a legal identifier (identifier_start followed by
// identifier_char). Used to validate a quoted .scope name: scope names take part
// in :: qualified symbol resolution, so they must be referenceable identifiers
// even when written in quotes.
static int name_is_identifier(const char *name, int len) {
    if(len <= 0 || !identifier_start((unsigned char)name[0])) {
        return 0;
    }
    for(int i = 1; i < len; i++) {
        if(!identifier_char((unsigned char)name[i])) {
            return 0;
        }
    }
    return 1;
}

static char *copy_token_string(ASSEMBLER *as) {
    if(as->token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, "Expected quoted file name");
        return NULL;
    }
    char *out = malloc((size_t)as->token.name_length + 1);
    if(!out) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory copying string token");
        return NULL;
    }
    memcpy(out, as->token.name, as->token.name_length);
    out[as->token.name_length] = '\0';
    return out;
}

static int append_text_checked(ASSEMBLER *as, char *out, int *out_len, const char *text, int text_len) {
    if(text_len <= 0) {
        return 1;
    }
    int room = ASM_MAX_LINE - 1 - *out_len;
    if(text_len > room) {
        if(room > 0) {
            memcpy(out + *out_len, text, (size_t)room);
            *out_len += room;
        }
        out[*out_len] = '\0';
        asm_err(as, ASM_ERR_FATAL, "Macro expansion line too long; truncated to %d characters", ASM_MAX_LINE - 1);
        return 0;
    }
    memcpy(out + *out_len, text, (size_t)text_len);
    *out_len += text_len;
    out[*out_len] = '\0';
    return 1;
}

static int append_buffer_text(ASSEMBLER *as, char **buf, size_t *len, size_t *cap, const char *src, size_t n) {
    if(n == 0) {
        return 1;
    }
    if(*len + n + 1 > *cap) {
        size_t new_cap = *cap ? *cap : 64;
        while(*len + n + 1 > new_cap) {
            new_cap *= 2;
        }
        char *new_buf = realloc(*buf, new_cap);
        if(!new_buf) {
            asm_err(as, ASM_ERR_FATAL, "Out of memory building macro argument");
            return 0;
        }
        *buf = new_buf;
        *cap = new_cap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

static char *parse_macro_arg(ASSEMBLER *as, const char **pp, int *out_len) {
    const char *p = *pp;
    while(*p == ' ' || *p == '\t') {
        p++;
    }

    char *out = NULL;
    size_t out_len_size = 0;
    size_t out_cap = 0;
    int parens = 0;
    int in_string = 0;
    int quote_wrapped = *p == '"';

    if(quote_wrapped) {
        p++;
    }

    while(*p) {
        char c = *p;
        if(!in_string) {
            if(c == ';' || c == '\n' || c == '\r') {
                break;
            }
            if(!quote_wrapped && c == ',' && parens == 0) {
                break;
            }
            if(c == '(') {
                parens++;
            } else if(c == ')' && parens > 0) {
                parens--;
            } else if(c == '"') {
                if(quote_wrapped) {
                    p++;
                    break;
                }
                in_string = 1;
            }
        } else if(c == '"' && p[-1] != '\\') {
            in_string = 0;
        }

        if(!append_buffer_text(as, &out, &out_len_size, &out_cap, p, 1)) {
            free(out);
            *out_len = 0;
            return NULL;
        }
        p++;
    }

    while(out_len_size > 0 && (out[out_len_size - 1] == ' ' || out[out_len_size - 1] == '\t')) {
        out[--out_len_size] = '\0';
    }

    if(!out) {
        out = copy_slice("", 0);
        if(!out) {
            asm_err(as, ASM_ERR_FATAL, "Out of memory building macro argument");
            *out_len = 0;
            return NULL;
        }
    }

    *out_len = (int)out_len_size;
    *pp = p;
    return out;
}

static void loop_free(LOOP *loop) {
    if(!loop) {
        return;
    }
    if(loop->type == LOOP_FOR) {
        free(loop->condition);
        free(loop->adjust);
    } else if(loop->type == LOOP_REPEAT) {
        free(loop->var_name);
    }
    memset(loop, 0, sizeof(*loop));
}

void loop_stack_clear(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->loop_stack.items; i++) {
        LOOP *loop = AM65_ARRAY_GET(&as->loop_stack, LOOP, i);
        loop_free(loop);
    }
    as->loop_stack.items = 0;
}

static void loop_stack_pop(ASSEMBLER *as) {
    if(as->loop_stack.items == 0) {
        return;
    }
    LOOP *loop = AM65_ARRAY_GET(&as->loop_stack, LOOP, as->loop_stack.items - 1);
    loop_free(loop);
    as->loop_stack.items--;
}

static void macro_free(MACRO *macro) {
    if(!macro) {
        return;
    }
    free(macro->name);
    for(size_t i = 0; i < macro->parameters.items; i++) {
        MACRO_PARAM *param = AM65_ARRAY_GET(&macro->parameters, MACRO_PARAM, i);
        free(param->name);
    }
    am65_array_free(&macro->parameters);
    memset(macro, 0, sizeof(*macro));
}

void macro_definitions_clear(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->macros.items; i++) {
        MACRO *macro = AM65_ARRAY_GET(&as->macros, MACRO, i);
        macro_free(macro);
    }
    as->macros.items = 0;
}

static void macro_expand_free(ASSEMBLER *as, MACRO_EXPAND *expand) {
    if(!expand) {
        return;
    }
    for(int ri = (int)expand->renames.items - 1; ri >= 0; ri--) {
        RENAME_MAP *ren = AM65_ARRAY_GET(&expand->renames, RENAME_MAP, (size_t)ri);
        symbol_delete_local(as->active_scope, ren->generated_name, GEN_NAME_LEN, as->pass == 2);
        free(ren->generated_name);
        free(ren->user_name);
    }
    for(size_t ai = 0; ai < expand->args.items; ai++) {
        MACRO_ARG_VALUE *arg = AM65_ARRAY_GET(&expand->args, MACRO_ARG_VALUE, ai);
        free(arg->value);
    }
    am65_array_free(&expand->renames);
    am65_array_free(&expand->args);
    memset(expand, 0, sizeof(*expand));
}

static void macro_stack_pop(ASSEMBLER *as) {
    if(!as || as->macro_stack.items == 0) {
        return;
    }
    MACRO_EXPAND *expand = AM65_ARRAY_GET(&as->macro_stack, MACRO_EXPAND, as->macro_stack.items - 1);
    macro_expand_free(as, expand);
    as->macro_stack.items--;
}

void macro_stack_clear(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    while(as->macro_stack.items > 0) {
        macro_stack_pop(as);
    }
}

static int64_t evaluate_loop_expression(ASSEMBLER *as, const char *expr) {
    const char *saved_cur = as->cur;
    TOKEN saved_token = as->token;
    as->cur = expr;
    memset(&as->token, 0, sizeof(as->token));
    int64_t value = expr_full_evaluate(as);
    as->cur = saved_cur;
    as->token = saved_token;
    return value;
}

static int next_token_op_is(ASSEMBLER *as, char op) {
    const char *saved_cur = as->cur;
    TOKEN saved_token = as->token;
    get_token(as);
    int matched = as->token.type == TOKEN_OP && as->token.op == op;
    as->cur = saved_cur;
    as->token = saved_token;
    return matched;
}

static int token_is_line_end(ASSEMBLER *as) {
    return as->token.type == TOKEN_END && as->token.op == '\0';
}

static void set_current_output_address(ASSEMBLER *as, uint16_t address) {
    SEGMENT *segment = as->active_target ? as->active_target->active_segment : NULL;
    if(!segment) {
        asm_err(as, ASM_ERR_FATAL, "No active segment for address assignment");
        return;
    }
    if(address >= segment->segment_output_address) {
        segment->segment_output_address = address;
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Address set to $%04X but is already at $%04X",
                address, segment->segment_output_address);
    }
}

static int is_register_token(ASSEMBLER *as, char reg) {
    return as->token.type == TOKEN_VAR && as->token.name_length == 1 &&
           tolower((unsigned char)as->token.name[0]) == reg;
}

static int is_implied_instruction(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
    case GPERF_OPCODE_ASL:
    case GPERF_OPCODE_LSR:
    case GPERF_OPCODE_ROL:
    case GPERF_OPCODE_ROR:
        if(is_register_token(as, 'a')) {
            next_token(as);
        }
        return as->token.type == TOKEN_END;
    default:
        return as->opcode_info.width == 0;
    }
}

static int indirect_mode_from_line(ASSEMBLER *as, char *reg) {
    const char *p = as->token.name;
    int depth = 0;
    while(*p) {
        if(*p == '(') {
            depth++;
        } else if(*p == ')') {
            depth--;
            if(depth == 0) {
                const char *q = skip_space(p + 1);
                if(*q == ',') {
                    q = skip_space(q + 1);
                    if((q[0] == 'y' || q[0] == 'Y') && !isalnum((unsigned char)q[1]) && q[1] != '_') {
                        *reg = 'y';
                        return ADDRESS_MODE_INDIRECT_Y;
                    }
                }
                return ADDRESS_MODE_INDIRECT;
            }
        } else if(*p == ',' && depth > 0) {
            const char *q = skip_space(p + 1);
            if((q[0] == 'x' || q[0] == 'X') && !isalnum((unsigned char)q[1]) && q[1] != '_') {
                *reg = 'x';
                return ADDRESS_MODE_INDIRECT_X;
            }
        }
        p++;
    }
    return 0;
}

static void decode_abs_rel_zp_opcode(ASSEMBLER *as) {
    int relative = as->opcode_info.width == 1;
    as->opcode_info.addressing_mode = ADDRESS_MODE_ZEROPAGE;
    if(!relative) {
        if(as->opcode_info.value >= 256 || as->opcode_info.width > 8) {
            as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE;
            as->opcode_info.width = 16;
        }
        if(as->token.op == ',') {
            next_token(as);
            if(is_register_token(as, 'x')) {
                as->opcode_info.addressing_mode++;
            } else if(is_register_token(as, 'y')) {
                if(as->opcode_info.width < 16 && as->opcode_info.addressing_mode == ADDRESS_MODE_ZEROPAGE) {
                    /* zeropage,Y exists only for LDX/STX; every other opcode's
                     * ,Y on a zeropage-sized base encodes as absolute,Y. Prefer
                     * the zeropage,Y form when the opcode actually has one so we
                     * emit two bytes (e.g. STX has no absolute,Y at all). */
                    if((int8_t)asm_opcode[as->opcode_info.opcode_id][ADDRESS_MODE_ZEROPAGE_Y] != -1) {
                        as->opcode_info.addressing_mode = ADDRESS_MODE_ZEROPAGE_Y;
                    } else {
                        as->opcode_info.addressing_mode = ADDRESS_MODE_ABSOLUTE_Y;
                        as->opcode_info.width = 16;
                    }
                } else {
                    as->opcode_info.addressing_mode += 2;
                }
            } else {
                asm_err(as, ASM_ERR_RESOLVE, "Unexpected indexed addressing register");
            }
            next_token(as);
        }
    }
    emit_opcode(as);
}

static void dot_org(ASSEMBLER *as) {
    uint64_t value = (uint64_t)expr_full_evaluate(as);
    SEGMENT *segment = as->active_target->active_segment;
    uint16_t address = (uint16_t)value;
    /* Re-anchor start while the segment is still empty (no bytes emitted). */
    if(!segment->segment_init ||
       segment->segment_output_address == segment->segment_start_address) {
        address = assembler_adjust_segment_start(
            as,
            segment->segment_name,
            segment->segment_name_length,
            address);
        segment->segment_start_address = address;
        segment->segment_init = 1;
    }
    segment->segment_output_address = address;
}

static void dot_align(ASSEMBLER *as) {
    uint64_t value = (uint64_t)expr_full_evaluate(as);
    if(!value) {
        asm_err(as, ASM_ERR_RESOLVE, ".align value must be greater than zero");
        return;
    }
    uint16_t aligned_address = (uint16_t)((current_output_address(as) + (value - 1)) & ~(value - 1));
    while(current_output_address(as) < aligned_address) {
        emit_byte(as, 0);
    }
}

static void dot_res(ASSEMBLER *as) {
    uint64_t length = (uint64_t)expr_full_evaluate(as);
    if(length > 0x10000ULL - current_output_address(as)) {
        asm_err(as, ASM_ERR_RESOLVE, "Reserving %llX bytes when only %04X remain in 64K",
                (unsigned long long)length, 0x10000 - current_output_address(as));
        return;
    }

    uint64_t value = 0;
    if(as->token.op == ',') {
        value = (uint64_t)expr_full_evaluate(as);
        if(value > 0xFF) {
            asm_err(as, ASM_ERR_RESOLVE, ".res cannot fill with %llX. Only 0x00 - 0xFF allowed",
                    (unsigned long long)value);
            return;
        }
    }

    while(length-- > 0) {
        emit_byte(as, (uint8_t)value);
    }
}

static void dot_byte(ASSEMBLER *as) {
    next_token(as);
    while(as->token.type != TOKEN_END) {
        if(as->token.type == TOKEN_STR) {
            emit_string(as, NULL);
        } else {
            emit_values(as, (uint64_t)expr_evaluate(as), 8, BYTE_ORDER_LO);
        }

        if(as->token.op == ',') {
            next_token(as);
        } else {
            break;
        }
    }
}

static void dot_string(ASSEMBLER *as, int terminate) {
    SYMBOL_LABEL *sl = NULL;
    if(as->strcode) {
        sl = symbol_store_in_scope(as, as->active_scope, "_", 1, SYMBOL_VARIABLE, 0);
    }

    next_token(as);
    while(as->token.type != TOKEN_END) {
        if(as->token.type == TOKEN_STR) {
            emit_string(as, sl);
        } else {
            uint64_t value = (uint64_t)expr_evaluate(as);
            emit_byte(as, (uint8_t)value);
            if(value >= 256) {
                asm_err(as, ASM_ERR_RESOLVE, "Value %llu not between 0 and 255",
                        (unsigned long long)value);
            }
        }

        if(as->token.op == ',') {
            next_token(as);
        } else {
            break;
        }
    }

    if(terminate) {
        emit_byte(as, 0);
    }
}

static void dot_strcode(ASSEMBLER *as) {
    const char *start = skip_space(as->cur);
    char *copy = copy_trimmed_slice(start, (int)strlen(start));
    if(!copy) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory storing .strcode expression");
        return;
    }
    free((char *)as->strcode);
    as->strcode = copy;
    while(as->token.type != TOKEN_END) {
        next_token(as);
    }
}

static void dot_include(ASSEMBLER *as) {
    next_token(as);
    char *path = copy_token_string(as);
    if(!path) {
        return;
    }
    file_load(as, path);
    free(path);
}

static void dot_incbin(ASSEMBLER *as) {
    next_token(as);
    char *path = copy_token_string(as);
    if(!path) {
        return;
    }

    char *full_path = file_resolve_path(as, path);
    free(path);
    if(!full_path) {
        asm_err(as, ASM_ERR_FATAL, "Unable to resolve .incbin path");
        return;
    }

    FILE *fp = fopen(full_path, "rb");
    if(!fp) {
        asm_err(as, ASM_ERR_FATAL, "Unable to open .incbin file: %s", full_path);
        free(full_path);
        return;
    }

    int c;
    while((c = fgetc(fp)) != EOF) {
        emit_byte(as, (uint8_t)c);
    }
    fclose(fp);
    free(full_path);
}

static void dot_define(ASSEMBLER *as) {
    const char *p = skip_space(as->cur);
    const char *from = p;
    while(*p && *p != ' ' && *p != '\t') {
        p++;
    }
    int from_len = (int)(p - from);
    p = skip_space(p);
    const char *to = p;
    int to_len = (int)strlen(to);
    while(to_len > 0 && (to[to_len - 1] == ' ' || to[to_len - 1] == '\t')) {
        to_len--;
    }

    if(from_len <= 0) {
        asm_err(as, ASM_ERR_DEFINE, ".define requires a source pattern");
        return;
    }
    define_add(as, from, from_len, to, to_len);
    while(as->token.type != TOKEN_END) {
        next_token(as);
    }
}

static int line_dot_command_id(ASSEMBLER *as, const char *line) {
    const char *saved_cur = as->cur;
    TOKEN saved_token = as->token;
    int opcode_id = -1;

    as->cur = line;
    get_token(as);
    if(as->token.type == TOKEN_VAR && as->token.name_length >= 2 && as->token.name[0] == '.') {
        OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
        if(info && info->mnemonic[0] == '.') {
            opcode_id = info->opcode_id;
        }
    }

    as->cur = saved_cur;
    as->token = saved_token;
    return opcode_id;
}

static int skip_to_matching_dot(ASSEMBLER *as, int start_id, int end_id, const char **matched_line_start) {
    int depth = 0;
    FILE_FRAME *frame = file_stack_top(as);
    while(frame && frame->read_ptr && *frame->read_ptr) {
        const char *line_start = frame->read_ptr;
        if(!file_read_line(as)) {
            break;
        }

        int scan_len = as->line_len;
        char scan_line[ASM_MAX_LINE];
        memcpy(scan_line, as->line, (size_t)scan_len + 1);

        int in_string = 0;
        for(int i = 0; i < scan_len; i++) {
            if(in_string) {
                if(scan_line[i] == '\\' && i + 1 < scan_len) {
                    i++;
                } else if(scan_line[i] == '"') {
                    in_string = 0;
                }
            } else if(scan_line[i] == '"') {
                in_string = 1;
            } else if(scan_line[i] == ';') {
                scan_line[i] = '\0';
                break;
            }
        }

        int opcode_id = line_dot_command_id(as, scan_line);
        if(opcode_id == start_id) {
            depth++;
        } else if(opcode_id == end_id) {
            if(depth == 0) {
                if(matched_line_start) {
                    *matched_line_start = line_start;
                }
                return 1;
            }
            depth--;
        }
        frame = file_stack_top(as);
    }
    return 0;
}

static int skip_to_matching_loop_end(ASSEMBLER *as, int start_id, int end_id) {
    return skip_to_matching_dot(as, start_id, end_id, NULL);
}

static int token_is_opcode_text(const char *name, int name_len) {
    if(name_len != 3) {
        return 0;
    }
    OPCODEINFO *info = in_word_set(name, (unsigned int)name_len);
    return info && info->mnemonic[0] != '.';
}

static MACRO_ARG_VALUE *macro_arg_lookup(MACRO_EXPAND *expand, const char *name, int name_len) {
    for(size_t i = 0; i < expand->args.items; i++) {
        MACRO_ARG_VALUE *arg = AM65_ARRAY_GET(&expand->args, MACRO_ARG_VALUE, i);
        if(arg->name_len == name_len && 0 == asm_strnicmp(arg->name, name, (size_t)name_len)) {
            return arg;
        }
    }
    return NULL;
}

static RENAME_MAP *macro_rename_lookup(MACRO_EXPAND *expand, const char *name, int name_len) {
    for(size_t i = 0; i < expand->renames.items; i++) {
        RENAME_MAP *ren = AM65_ARRAY_GET(&expand->renames, RENAME_MAP, i);
        if((int)ren->user_name_len == name_len && 0 == asm_strnicmp(ren->user_name, name, (size_t)name_len)) {
            return ren;
        }
    }
    return NULL;
}

void macro_substitute_line(ASSEMBLER *as) {
    FILE_FRAME *frame = file_stack_top(as);
    if(!as || !frame || !frame->is_macro || as->macro_stack.items == 0) {
        return;
    }
    if(line_dot_command_id(as, as->line) == GPERF_DOT_LOCAL) {
        return;
    }

    MACRO_EXPAND *expand = AM65_ARRAY_GET(&as->macro_stack, MACRO_EXPAND, as->macro_stack.items - 1);
    char out[ASM_MAX_LINE];
    int out_len = 0;
    int in_string = 0;

    for(int i = 0; i < as->line_len;) {
        char c = as->line[i];
        if(in_string) {
            append_text_checked(as, out, &out_len, &as->line[i], 1);
            if(c == '\\' && i + 1 < as->line_len) {
                i++;
                append_text_checked(as, out, &out_len, &as->line[i], 1);
            } else if(c == '"') {
                in_string = 0;
            }
            i++;
            continue;
        }

        if(c == '"') {
            in_string = 1;
            append_text_checked(as, out, &out_len, &as->line[i], 1);
            i++;
            continue;
        }

        if(identifier_start(c) || (c == ':' && i + 1 < as->line_len && as->line[i + 1] == ':')) {
            int start = i;
            if(c == ':' && i + 1 < as->line_len && as->line[i + 1] == ':') {
                i += 2;
            } else {
                i++;
            }
            while(i < as->line_len) {
                if(identifier_char(as->line[i])) {
                    i++;
                } else if(as->line[i] == ':' && i + 1 < as->line_len && as->line[i + 1] == ':') {
                    i += 2;
                } else {
                    break;
                }
            }

            const char *name = &as->line[start];
            int name_len = i - start;
            MACRO_ARG_VALUE *arg = NULL;
            RENAME_MAP *ren = NULL;
            if(!token_is_opcode_text(name, name_len)) {
                arg = macro_arg_lookup(expand, name, name_len);
                if(!arg) {
                    ren = macro_rename_lookup(expand, name, name_len);
                }
            }

            if(arg) {
                append_text_checked(as, out, &out_len, arg->value, arg->value_len);
            } else if(ren) {
                append_text_checked(as, out, &out_len, ren->generated_name, GEN_NAME_LEN);
            } else {
                append_text_checked(as, out, &out_len, name, name_len);
            }
            continue;
        }

        append_text_checked(as, out, &out_len, &as->line[i], 1);
        i++;
    }

    out[out_len] = '\0';
    memcpy(as->line, out, (size_t)out_len + 1);
    as->line_len = out_len;
}

static void dot_local(ASSEMBLER *as) {
    FILE_FRAME *frame = file_stack_top(as);
    if(!frame || !frame->is_macro || as->macro_stack.items == 0) {
        asm_err(as, ASM_ERR_RESOLVE, ".local is only valid inside a .macro");
        return;
    }

    do {
        next_token(as);
        if(as->token.type != TOKEN_VAR) {
            asm_err(as, ASM_ERR_RESOLVE, ".local needs to be followed by a variable");
            return;
        }
        if(!symbol_declare_local(as, as->token.name, as->token.name_length)) {
            asm_err(as, ASM_ERR_FATAL, "Unable to declare macro local %.*s",
                    (int)as->token.name_length, as->token.name);
            return;
        }
        next_token(as);
    } while(as->token.op == ',');
}

static int macro_add_parameter(ASSEMBLER *as, MACRO *macro, const char *name, int name_len) {
    MACRO_PARAM param;
    param.name = copy_slice(name, name_len);
    param.name_len = name_len;
    if(!param.name) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory storing macro parameter");
        return 0;
    }
    if(ASM_OK != AM65_ARRAY_ADD(&macro->parameters, param)) {
        free(param.name);
        asm_err(as, ASM_ERR_FATAL, "Out of memory storing macro parameter");
        return 0;
    }
    return 1;
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

static void dot_macro(ASSEMBLER *as) {
    MACRO macro;
    memset(&macro, 0, sizeof(macro));
    AM65_ARRAY_INIT(&macro.parameters, MACRO_PARAM);
    int macro_ok = 1;

    next_token(as);
    if(as->token.type != TOKEN_VAR) {
        macro_ok = 0;
        asm_err(as, ASM_ERR_RESOLVE, "Macro has no name");
    } else {
        macro.name = copy_token_name(as);
        macro.name_len = (int)as->token.name_length;
        if(!macro.name) {
            macro_free(&macro);
            return;
        }
        for(size_t i = 0; i < as->macros.items; i++) {
            MACRO *existing = AM65_ARRAY_GET(&as->macros, MACRO, i);
            if(existing->name_len == macro.name_len &&
               0 == asm_strnicmp(existing->name, macro.name, (size_t)macro.name_len)) {
                macro_ok = 0;
                asm_err(as, ASM_ERR_DEFINE, "Macro with name %.*s has already been defined",
                        macro.name_len, macro.name);
            }
        }
        next_token(as);
    }

    while(!token_is_line_end(as)) {
        if(as->token.op == ',') {
            next_token(as);
            continue;
        }
        if(as->token.type != TOKEN_VAR) {
            macro_ok = 0;
            asm_err(as, ASM_ERR_DEFINE, "Macro definition error");
            break;
        }
        if(!macro_add_parameter(as, &macro, as->token.name, (int)as->token.name_length)) {
            macro_free(&macro);
            return;
        }
        next_token(as);
    }

    FILE_FRAME *frame = file_stack_top(as);
    macro.body_file = frame ? frame->file : NULL;
    macro.body_start = frame ? frame->read_ptr : NULL;
    macro.body_line = frame ? frame->line_num : 0;

    const char *end_line = NULL;
    if(!skip_to_matching_dot(as, GPERF_DOT_MACRO, GPERF_DOT_ENDMACRO, &end_line)) {
        macro_ok = 0;
        asm_err(as, ASM_ERR_RESOLVE, ".macro %.*s with no .endmacro",
                macro.name_len, macro.name ? macro.name : "");
    }

    if(macro_ok) {
        if(ASM_OK != AM65_ARRAY_ADD(&as->macros, macro)) {
            macro_free(&macro);
            asm_err(as, ASM_ERR_FATAL, "Out of memory storing macro");
        }
    } else {
        macro_free(&macro);
    }
    (void)end_line;
}

static void dot_endmacro(ASSEMBLER *as) {
    FILE_FRAME *frame = file_stack_top(as);
    if(frame && frame->is_macro && as->macro_stack.items > 0) {
        macro_stack_pop(as);
        file_stack_pop(as);
        return;
    }
    asm_err(as, ASM_ERR_RESOLVE, ".endmacro but not running a macro");
}

static void dot_endscope(ASSEMBLER *as) {
    if(!as->active_scope || as->active_scope->scope_type != GPERF_DOT_SCOPE) {
        asm_err(as, ASM_ERR_RESOLVE, ".endscope without a matching .scope");
        return;
    }
    SCOPE *closing = as->active_scope;
    if(!scope_pop(as)) {
        asm_err(as, ASM_ERR_RESOLVE, ".endscope without a matching .scope");
        return;
    }
    // Leaving a named output-redirect scope restores the previously active target.
    if(closing->has_output_redirect && closing->output_target) {
        as->active_target = closing->output_target->parent;
    }
}

static void dot_endproc(ASSEMBLER *as) {
    if(!as->active_scope || as->active_scope->scope_type != GPERF_DOT_PROC) {
        asm_err(as, ASM_ERR_RESOLVE, ".endproc without a matching .proc");
        return;
    }
    if(!scope_pop(as)) {
        asm_err(as, ASM_ERR_RESOLVE, ".endproc without a matching .proc");
    }
}

// Parse the optional `file="..."` / `dest="..."` clauses that follow a named
// .scope. Copies of the strings are not made here; they point into the current
// line buffer and are only valid until the redirect target is opened below.
static void dot_scope_parse_redirect(ASSEMBLER *as,
                                     const char **file_name, int *file_len,
                                     const char **dest_name, int *dest_len) {
    while(!token_is_line_end(as)) {
        if(as->token.op == ',') {
            next_token(as);
            continue;
        }
        if(as->token.type == TOKEN_VAR &&
           as->token.name_length == 4 && 0 == asm_strnicmp(as->token.name, "file", 4)) {
            next_token(as);
            expect_op(as, '=');
            if(as->token.type != TOKEN_STR) {
                asm_err(as, ASM_ERR_RESOLVE, ".scope file= must be followed by a file name in quotes");
                return;
            }
            *file_name = as->token.name;
            *file_len = (int)as->token.name_length;
            next_token(as);
        } else if(as->token.type == TOKEN_VAR &&
                  as->token.name_length == 4 && 0 == asm_strnicmp(as->token.name, "dest", 4)) {
            next_token(as);
            expect_op(as, '=');
            if(as->token.type != TOKEN_STR) {
                asm_err(as, ASM_ERR_RESOLVE, ".scope dest= must be followed by a name in quotes");
                return;
            }
            *dest_name = as->token.name;
            *dest_len = (int)as->token.name_length;
            next_token(as);
        } else {
            asm_err(as, ASM_ERR_RESOLVE, ".scope name may only be followed by file=\"...\" and/or dest=\"...\"");
            return;
        }
    }
}

static int dot_scope_destination_supported(
    ASSEMBLER *as,
    const char *dest,
    int dest_len) {
    int offset = 0;

    if(!dest || as->cb.destination_name_count == 0 || !as->cb.destination_names) {
        return 1;
    }
    while(offset < dest_len) {
        int start;
        int end;
        size_t i;
        int found = 0;

        while(offset < dest_len && (dest[offset] == ' ' || dest[offset] == '\t')) {
            offset++;
        }
        start = offset;
        while(offset < dest_len && dest[offset] != ',') {
            offset++;
        }
        end = offset;
        while(end > start && (dest[end - 1] == ' ' || dest[end - 1] == '\t')) {
            end--;
        }
        for(i = 0; i < as->cb.destination_name_count; i++) {
            const char *candidate = as->cb.destination_names[i];
            size_t candidate_len = candidate ? strlen(candidate) : 0;
            if(candidate_len == (size_t)(end - start) &&
               candidate_len > 0 &&
               0 == asm_strnicmp(dest + start, candidate, candidate_len)) {
                found = 1;
                break;
            }
        }
        if(!found) {
            asm_err(as, ASM_ERR_RESOLVE,
                    ".scope destination '%.*s' is not supported by this assembler host",
                    end - start, dest + start);
            return 0;
        }
        if(offset < dest_len && dest[offset] == ',') {
            offset++;
            if(offset == dest_len) {
                asm_err(as, ASM_ERR_RESOLVE, ".scope dest= may not end with a comma");
                return 0;
            }
        }
    }
    if(dest_len == 0) {
        asm_err(as, ASM_ERR_RESOLVE, ".scope dest= may not be empty");
        return 0;
    }
    return 1;
}

static void dot_scope(ASSEMBLER *as) {
    const char *name;
    int name_len;
    char anon_name[16];
    const char *file_name = NULL;
    int file_len = 0;
    const char *dest_name = NULL;
    int dest_len = 0;

    next_token(as);
    if(token_is_line_end(as)) {
        name_len = snprintf(anon_name, sizeof(anon_name), "anon_%04X", ++as->active_scope->anon_scope_id);
        name = anon_name;
    } else if(as->token.type == TOKEN_VAR || as->token.type == TOKEN_STR) {
        // A quoted name is accepted for symmetry with .segdef, but it must still be
        // a legal identifier: the name feeds :: qualified symbol resolution, and the
        // tokenizer cannot express a reference through a name that contains spaces or
        // other non-identifier characters.
        name = as->token.name;
        name_len = (int)as->token.name_length;
        if(symbol_has_scope_path(name, name_len)) {
            asm_err(as, ASM_ERR_DEFINE, "The name %.*s is scoped and not allowed as a scope name", name_len, name);
            return;
        }
        if(!name_is_identifier(name, name_len)) {
            asm_err(as, ASM_ERR_RESOLVE, ".scope name must be an identifier");
            return;
        }
        next_token(as);
        dot_scope_parse_redirect(as, &file_name, &file_len, &dest_name, &dest_len);
        if(dest_name && !dot_scope_destination_supported(as, dest_name, dest_len)) {
            return;
        }
    } else {
        asm_err(as, ASM_ERR_RESOLVE, ".scope name must be an identifier");
        return;
    }

    SCOPE *scope = scope_find_child(as->active_scope, name, name_len);
    if(!scope) {
        scope = scope_add(as, name, name_len, as->active_scope, GPERF_DOT_SCOPE);
        if(!scope) {
            asm_err(as, ASM_ERR_FATAL, "Out of memory creating scope %.*s", name_len, name);
            return;
        }
    }

    if(file_name || dest_name) {
        // Open the redirect target once (pass 1); reuse it on pass 2 so target_open
        // is called exactly once and the host context persists across both passes.
        if(!scope->output_target) {
            if(!as->cb.target_open) {
                asm_err(as, ASM_ERR_RESOLVE,
                        ".scope file=/dest= output redirection is not supported by this assembler host");
                scope_push(as, scope);
                return;
            }
            void *ctx = as->cb.target_open(as->cb.user, name, name_len,
                                           file_name, file_len, dest_name, dest_len);
            if(!ctx) {
                asm_err(as, ASM_ERR_RESOLVE, ".scope could not open output redirect for %.*s", name_len, name);
                scope_push(as, scope);
                return;
            }
            TARGET *target = add_target(as, ctx);
            if(!target) {
                as->cb.target_release(as->cb.user, ctx);
                asm_err(as, ASM_ERR_FATAL, "Out of memory creating output target for %.*s", name_len, name);
                scope_push(as, scope);
                return;
            }
            target->parent = as->active_target;
            scope->output_target = target;
            scope->has_output_redirect = 1;
        }
        as->active_target = scope->output_target;
    }

    scope_push(as, scope);
}

static void dot_proc(ASSEMBLER *as) {
    next_token(as);
    if(as->token.type != TOKEN_VAR) {
        asm_err(as, ASM_ERR_RESOLVE, ".proc must be followed by a name");
        return;
    }

    SCOPE *parent = NULL;
    const char *leaf = NULL;
    int leaf_len = 0;
    resolve_def_target(as, as->token.name, (int)as->token.name_length, &parent, &leaf, &leaf_len);
    if(leaf_len <= 0 || symbol_has_scope_path(leaf, leaf_len)) {
        asm_err(as, ASM_ERR_DEFINE, "The name %.*s is scoped and not allowed", leaf_len, leaf ? leaf : "");
        return;
    }

    SCOPE *scope = scope_find_child(parent, leaf, leaf_len);
    if(scope) {
        if(as->pass == 1) {
            asm_err(as, ASM_ERR_DEFINE, "In this scope a .proc has already been defined with the name %.*s", leaf_len, leaf);
        } else {
            scope_push(as, scope);
        }
        return;
    }

    symbol_store_in_scope(as, parent, leaf, (uint32_t)leaf_len, SYMBOL_ADDRESS, current_output_address(as));
    scope = scope_add(as, leaf, leaf_len, parent, GPERF_DOT_PROC);
    if(!scope) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory creating proc %.*s", leaf_len, leaf);
        return;
    }
    scope_push(as, scope);
}

static void dot_segdef(ASSEMBLER *as) {
    SEGMENT segment;
    memset(&segment, 0, sizeof(segment));

    next_token(as);
    if(as->token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, ".segdef expects a quoted segment name");
        return;
    }
    segment.segment_name = as->token.name;
    segment.segment_name_length = as->token.name_length;
    segment.segment_name_hash = as->token.name_hash;

    next_token(as);
    if(as->token.op != ',') {
        asm_err(as, ASM_ERR_RESOLVE, ".segdef expects a comma then a start address after the name");
        return;
    }

    uint16_t start = 0;
    int do_not_emit = 0;
    int is_locked = 0;
    int is_reclaim = 0;
    const char *host_name = NULL;
    int host_name_len = 0;

    next_token(as);
    if(as->token.type == TOKEN_VAR &&
       as->token.name_length == 7 &&
       0 == asm_strnicmp(as->token.name, "reclaim", 7)) {
        // A reclaim segment piggybacks on an emitted "host" segment: it takes the
        // host's start address (following it across any auto-adjust move, because
        // the host's .segdef re-runs earlier in this pass) and is implicitly
        // noemit. It carries no address or flags of its own.
        is_reclaim = 1;
        do_not_emit = 1;
        next_token(as);
        expect_op(as, '=');
        if(as->token.type != TOKEN_STR) {
            asm_err(as, ASM_ERR_RESOLVE, ".segdef reclaim= must be followed by a host segment name in quotes");
            return;
        }
        host_name = as->token.name;
        host_name_len = (int)as->token.name_length;

        SEGMENT host_key;
        memset(&host_key, 0, sizeof(host_key));
        host_key.segment_name = as->token.name;
        host_key.segment_name_length = as->token.name_length;
        SEGMENT *host = segment_find(&as->active_target->segments, &host_key);
        if(!host) {
            asm_err(as, ASM_ERR_RESOLVE,
                    "reclaim host segment \"%.*s\" is not defined -- define it before reclaiming it",
                    host_name_len, host_name);
            return;
        }
        if(host->do_not_emit) {
            asm_err(as, ASM_ERR_RESOLVE,
                    "reclaim host segment \"%.*s\" must be an emitted segment",
                    host_name_len, host_name);
            return;
        }
        start = host->segment_start_address;

        next_token(as);
        if(!token_is_line_end(as)) {
            asm_err(as, ASM_ERR_RESOLVE, "a reclaim segment takes no address or flags after reclaim=\"host\"");
            return;
        }
    } else {
        start = (uint16_t)expr_evaluate(as);
        start = assembler_adjust_segment_start(
            as,
            segment.segment_name,
            segment.segment_name_length,
            start);
        while(as->token.op == ',') {
            next_token(as);
            if(as->token.type == TOKEN_VAR &&
               as->token.name_length == 4 &&
               0 == asm_strnicmp(as->token.name, "emit", 4)) {
                do_not_emit = 0;
                next_token(as);
            } else if(as->token.type == TOKEN_VAR &&
                      as->token.name_length == 6 &&
                      0 == asm_strnicmp(as->token.name, "noemit", 6)) {
                do_not_emit = 1;
                next_token(as);
            } else if(as->token.type == TOKEN_VAR &&
                      as->token.name_length == 6 &&
                      0 == asm_strnicmp(as->token.name, "locked", 6)) {
                is_locked = 1;
                next_token(as);
            } else {
                asm_err(as, ASM_ERR_RESOLVE, "The optional .segdef flags after the name and start are any of emit, noemit or locked, separated by commas");
                return;
            }
        }
        if(!token_is_line_end(as)) {
            asm_err(as, ASM_ERR_RESOLVE, "Unexpected token after .segdef");
            return;
        }
    }

    SEGMENT *existing = segment_find(&as->active_target->segments, &segment);
    if(existing) {
        if(as->pass == 1) {
            asm_err(as, ASM_ERR_DEFINE, "Segment %.*s has already been defined",
                    (int)segment.segment_name_length, segment.segment_name);
        }
        return;
    }

    SEGMENT *new_segment = malloc(sizeof(*new_segment));
    if(!new_segment) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory creating segment");
        return;
    }
    memset(new_segment, 0, sizeof(*new_segment));
    if(!set_name((char **)&new_segment->segment_name, segment.segment_name, (int)segment.segment_name_length)) {
        free(new_segment);
        asm_err(as, ASM_ERR_FATAL, "Out of memory storing segment name");
        return;
    }
    new_segment->segment_name_length = segment.segment_name_length;
    new_segment->segment_name_hash = segment.segment_name_hash;
    new_segment->segment_start_address = start;
    new_segment->segment_output_address = start;
    new_segment->segment_init = 1;
    new_segment->do_not_emit = do_not_emit;
    new_segment->is_locked = is_locked;
    new_segment->is_reclaim = is_reclaim;
    if(is_reclaim) {
        if(!set_name((char **)&new_segment->reclaim_host_name, host_name, host_name_len)) {
            free((char *)new_segment->segment_name);
            free(new_segment);
            asm_err(as, ASM_ERR_FATAL, "Out of memory storing reclaim host name");
            return;
        }
        new_segment->reclaim_host_name_length = (uint32_t)host_name_len;
    }
    if(ASM_OK != AM65_ARRAY_ADD(&as->active_target->segments, new_segment)) {
        free((char *)new_segment->segment_name);
        free((char *)new_segment->reclaim_host_name);
        free(new_segment);
        asm_err(as, ASM_ERR_FATAL, "Out of memory tracking segment");
    }
}

static void dot_segment(ASSEMBLER *as) {
    SEGMENT key;
    memset(&key, 0, sizeof(key));

    next_token(as);
    if(as->token.type != TOKEN_STR) {
        asm_err(as, ASM_ERR_RESOLVE, ".segment expects a quoted segment name");
        return;
    }
    key.segment_name = as->token.name;
    key.segment_name_length = as->token.name_length;
    key.segment_name_hash = as->token.name_hash;

    SEGMENT *segment = NULL;
    if(key.segment_name_length == 0) {
        segment = *AM65_ARRAY_GET(&as->active_target->segments, SEGMENT*, 0);
    } else {
        segment = segment_find(&as->active_target->segments, &key);
    }
    if(!segment) {
        asm_err(as, ASM_ERR_DEFINE, "Segment %.*s not defined",
                (int)key.segment_name_length, key.segment_name);
        return;
    }
    as->active_target->active_segment = segment;

    next_token(as);
    if(!token_is_line_end(as)) {
        asm_err(as, ASM_ERR_RESOLVE, "Unexpected token after .segment");
    }
}

static void restore_loop_body(ASSEMBLER *as, LOOP *loop) {
    FILE_FRAME *frame = file_stack_top(as);
    if(!frame) {
        return;
    }
    frame->file = loop->body_file;
    frame->read_ptr = loop->body_start;
    frame->line_num = loop->body_line;
    as->current_file = frame->file;
    as->current_file_name = frame->file ? frame->file->display_name : NULL;
    as->current_line = frame->line_num;
}

static void dot_for(ASSEMBLER *as) {
    LOOP loop;
    memset(&loop, 0, sizeof(loop));
    loop.type = LOOP_FOR;

    next_token(as);
    if(as->token.type != TOKEN_VAR) {
        asm_err(as, ASM_ERR_RESOLVE, ".for must be followed by a variable name");
        return;
    }
    const char *var_name = as->token.name;
    uint32_t var_name_len = as->token.name_length;

    next_token(as);
    expect_op(as, '=');
    uint64_t value = (uint64_t)expr_evaluate(as);
    symbol_write(as, var_name, var_name_len, SYMBOL_VARIABLE, value);

    if(as->token.op != ',') {
        asm_err(as, ASM_ERR_RESOLVE, ".for requires a condition after the initializer");
        return;
    }

    const char *condition_start = as->cur;
    int64_t condition = expr_full_evaluate(as);
    const char *condition_end = as->token.op == ',' ? as->token.name : as->cur;
    if(as->pass == 1 && as->expression_unknown) {
        asm_err(as, ASM_ERR_RESOLVE, ".for condition must resolve on pass 1");
        condition = 0;
    }

    if(as->token.op != ',') {
        asm_err(as, ASM_ERR_RESOLVE, ".for requires an iteration expression");
        return;
    }

    const char *adjust_start = as->cur;
    int adjust_len = (int)strlen(adjust_start);
    loop.condition = copy_trimmed_slice(condition_start, (int)(condition_end - condition_start));
    loop.adjust = copy_trimmed_slice(adjust_start, adjust_len);
    if(!loop.condition || !loop.adjust) {
        loop_free(&loop);
        asm_err(as, ASM_ERR_FATAL, "Out of memory storing .for loop");
        return;
    }
    if(loop.condition[0] == '\0' || loop.adjust[0] == '\0') {
        loop_free(&loop);
        asm_err(as, ASM_ERR_RESOLVE, ".for requires condition and iteration expressions");
        return;
    }

    if(!condition) {
        loop_free(&loop);
        if(!skip_to_matching_loop_end(as, GPERF_DOT_FOR, GPERF_DOT_ENDFOR)) {
            asm_err(as, ASM_ERR_RESOLVE, ".for without .endfor");
        }
        return;
    }

    FILE_FRAME *frame = file_stack_top(as);
    loop.body_file = frame ? frame->file : NULL;
    loop.body_start = frame ? frame->read_ptr : NULL;
    loop.body_line = frame ? frame->line_num : 0;
    if(ASM_OK != AM65_ARRAY_ADD(&as->loop_stack, loop)) {
        loop_free(&loop);
        asm_err(as, ASM_ERR_FATAL, "Out of memory pushing .for loop");
    }
}

static void dot_endfor(ASSEMBLER *as) {
    if(as->loop_stack.items == 0) {
        asm_err(as, ASM_ERR_RESOLVE, ".endfor without a matching .for");
        return;
    }

    LOOP *loop = AM65_ARRAY_GET(&as->loop_stack, LOOP, as->loop_stack.items - 1);
    if(loop->type != LOOP_FOR) {
        asm_err(as, ASM_ERR_RESOLVE, ".endfor without a matching .for");
        return;
    }

    FILE_FRAME *frame = file_stack_top(as);
    int same_file = frame && frame->file == loop->body_file;
    size_t iterations = ++loop->iterations;
    evaluate_loop_expression(as, loop->adjust);
    int64_t condition = evaluate_loop_expression(as, loop->condition);
    if(as->pass == 1 && as->expression_unknown) {
        asm_err(as, ASM_ERR_RESOLVE, ".for condition must resolve on pass 1");
        condition = 0;
    }

    if(iterations < 65536 && same_file && condition) {
        restore_loop_body(as, loop);
        return;
    }

    if(!same_file) {
        asm_err(as, ASM_ERR_RESOLVE, ".endfor matches .for in file %s, body at line %zu",
                loop->body_file && loop->body_file->display_name ? loop->body_file->display_name : "<unknown>",
                loop->body_line + 1);
    } else if(iterations >= 65536) {
        asm_err(as, ASM_ERR_RESOLVE, "Exiting .for loop with body at line %zu, which has iterated 64K times",
                loop->body_line + 1);
    }
    loop_stack_pop(as);
}

static void dot_repeat(ASSEMBLER *as) {
    LOOP loop;
    memset(&loop, 0, sizeof(loop));
    loop.type = LOOP_REPEAT;

    next_token(as);
    loop.max_iterations = expr_evaluate(as);
    if(as->pass == 1 && as->expression_unknown) {
        asm_err(as, ASM_ERR_RESOLVE, ".repeat count must resolve on pass 1");
        loop.max_iterations = 0;
    }

    if(as->token.op == ',') {
        next_token(as);
        if(as->token.type == TOKEN_VAR) {
            loop.var_name = copy_token_name(as);
            loop.var_name_len = (int)as->token.name_length;
            if(!loop.var_name) {
                loop_free(&loop);
                return;
            }
            symbol_write(as, loop.var_name, (uint32_t)loop.var_name_len, SYMBOL_VARIABLE, 0);
            next_token(as);
        } else {
            asm_err(as, ASM_ERR_RESOLVE, ".repeat expects a variable after the comma");
        }
    }

    if(as->token.type != TOKEN_END) {
        loop_free(&loop);
        asm_err(as, ASM_ERR_RESOLVE, "Unexpected token after .repeat");
        return;
    }

    if(loop.max_iterations <= 0) {
        loop_free(&loop);
        if(!skip_to_matching_loop_end(as, GPERF_DOT_REPEAT, GPERF_DOT_ENDREPEAT)) {
            asm_err(as, ASM_ERR_RESOLVE, ".repeat without .endrepeat");
        }
        return;
    }

    FILE_FRAME *frame = file_stack_top(as);
    loop.body_file = frame ? frame->file : NULL;
    loop.body_start = frame ? frame->read_ptr : NULL;
    loop.body_line = frame ? frame->line_num : 0;
    if(ASM_OK != AM65_ARRAY_ADD(&as->loop_stack, loop)) {
        loop_free(&loop);
        asm_err(as, ASM_ERR_FATAL, "Out of memory pushing .repeat loop");
    }
}

static void dot_endrepeat(ASSEMBLER *as) {
    if(as->loop_stack.items == 0) {
        asm_err(as, ASM_ERR_RESOLVE, ".endrepeat without a matching .repeat");
        return;
    }

    LOOP *loop = AM65_ARRAY_GET(&as->loop_stack, LOOP, as->loop_stack.items - 1);
    if(loop->type != LOOP_REPEAT) {
        asm_err(as, ASM_ERR_RESOLVE, ".endrepeat without a matching .repeat");
        return;
    }

    FILE_FRAME *frame = file_stack_top(as);
    int same_file = frame && frame->file == loop->body_file;
    size_t iterations = ++loop->iterations;
    if(iterations < 65536 && same_file && (int64_t)iterations < loop->max_iterations) {
        if(loop->var_name_len > 0) {
            symbol_write(as, loop->var_name, (uint32_t)loop->var_name_len, SYMBOL_VARIABLE, iterations);
        }
        restore_loop_body(as, loop);
        return;
    }

    if(!same_file) {
        asm_err(as, ASM_ERR_RESOLVE, ".endrepeat matches .repeat in file %s, body at line %zu",
                loop->body_file && loop->body_file->display_name ? loop->body_file->display_name : "<unknown>",
                loop->body_line + 1);
    } else if(iterations >= 65536) {
        asm_err(as, ASM_ERR_RESOLVE, "Exiting .repeat loop with body at line %zu, which has iterated 64K times",
                loop->body_line + 1);
    }
    loop_stack_pop(as);
}

static void if_stack_push(ASSEMBLER *as, int was_true) {
    IF_FRAME frame;
    frame.was_true = was_true;
    frame.else_seen = 0;
    if(ASM_OK != AM65_ARRAY_ADD(&as->if_stack, frame)) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory pushing conditional assembly frame");
    }
}

static IF_FRAME *if_stack_top(ASSEMBLER *as) {
    if(as->if_stack.items == 0) {
        return NULL;
    }
    return AM65_ARRAY_GET(&as->if_stack, IF_FRAME, as->if_stack.items - 1);
}

static void if_stack_pop(ASSEMBLER *as) {
    if(as->if_stack.items == 0) {
        asm_err(as, ASM_ERR_RESOLVE, ".endif with no .if");
        return;
    }
    as->if_stack.items--;
}

static void dot_if(ASSEMBLER *as) {
    int64_t value = expr_full_evaluate(as);
    int was_true = value != 0;
    if(as->pass == 1 && as->expression_unknown) {
        asm_err(as, ASM_ERR_RESOLVE, ".if expression must resolve on pass 1");
        was_true = 0;
    }
    if_stack_push(as, was_true);
    if(!was_true) {
        as->if_skip_depth = 1;
    }
}

static void dot_else(ASSEMBLER *as) {
    IF_FRAME *frame = if_stack_top(as);
    if(!frame) {
        asm_err(as, ASM_ERR_RESOLVE, ".else without .if");
        while(as->token.type != TOKEN_END) {
            next_token(as);
        }
        return;
    }
    if(frame->else_seen) {
        asm_err(as, ASM_ERR_RESOLVE, ".else after .else");
    }
    frame->else_seen = 1;
    if(frame->was_true) {
        as->if_skip_depth = 1;
    }
    while(as->token.type != TOKEN_END) {
        next_token(as);
    }
}

static void dot_endif(ASSEMBLER *as) {
    if_stack_pop(as);
    while(as->token.type != TOKEN_END) {
        next_token(as);
    }
}

int is_label(ASSEMBLER *as) {
    if(as->token.type == TOKEN_OP && as->token.op == ':') {
        return 1;
    }
    if(as->token.type != TOKEN_VAR) {
        return 0;
    }
    return *as->cur == ':' && as->cur[1] != ':';
}

int is_address(ASSEMBLER *as) {
    if(as->token.type != TOKEN_OP || as->token.op != '*') {
        return 0;
    }
    const char *p = skip_space(as->cur);
    return *p == '=' || *p == '+';
}

int is_opcode(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR ||
       (as->token.name_length != 3 && as->token.name_length != 4) ||
       as->token.name[0] == '.') {
        return 0;
    }

    OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
    if(!info || info->mnemonic[0] == '.') {
        return 0;
    }
    as->opcode_info = *info;
    return 1;
}

int is_parse_dot_command(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR || as->token.name_length < 2 || as->token.name[0] != '.') {
        return 0;
    }

    OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
    if(!info || info->mnemonic[0] != '.') {
        return 0;
    }
    as->opcode_info = *info;
    return 1;
}

int is_variable(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR || as->token.name[0] == '.') {
        return 0;
    }

    const char *p = skip_space(as->cur);
    return *p == '=' || (p[0] == ':' && p[1] == '=') || (p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-');
}

void parse_address(ASSEMBLER *as) {
    next_token(as);
    int relative = 0;
    if(as->token.type == TOKEN_OP && as->token.op == '+') {
        relative = 1;
        next_token(as);
        if(as->token.type == TOKEN_OP && as->token.op == '=') {
            next_token(as);
        }
    } else if(as->token.type == TOKEN_OP && as->token.op == '=') {
        next_token(as);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Address assign error");
        return;
    }

    uint16_t address;
    int64_t value = expr_evaluate(as);
    SEGMENT *segment = as->active_target ? as->active_target->active_segment : NULL;
    if(relative) {
        address = current_output_address(as) + value;
    } else {
        address = (uint16_t)value;
        /* Host-supplied start (or a fresh segment) is only a default until bytes are
           emitted. Allow * = $nnnn to re-anchor freely in that case, matching .org. */
        if(segment &&
           (!segment->segment_init ||
            segment->segment_output_address == segment->segment_start_address)) {
            address = assembler_adjust_segment_start(
                as,
                segment->segment_name,
                segment->segment_name_length,
                address);
            segment->segment_start_address = address;
            segment->segment_output_address = address;
            segment->segment_init = 1;
            return;
        }
        if(segment) {
            segment->segment_init = 1;
        }
    }

    if(current_output_address(as) > address) {
        asm_err(as, ASM_ERR_RESOLVE, "Assigning address %04X when address is already %04X error",
                address, current_output_address(as));
    } else {
        set_current_output_address(as, address);
    }
}

void parse_dot_command(ASSEMBLER *as) {
    switch(as->opcode_info.opcode_id) {
    case GPERF_DOT_6502:
        as->cpu_profile = ASM_CPU_6502;
        next_token(as);
        break;
    case GPERF_DOT_65c02:
        as->cpu_profile = ASM_CPU_65C02;
        next_token(as);
        break;
    case GPERF_DOT_ROCKWELL:
        as->cpu_profile = ASM_CPU_ROCKWELL;
        next_token(as);
        break;
    case GPERF_DOT_WDC:
        as->cpu_profile = ASM_CPU_WDC;
        next_token(as);
        break;
    case GPERF_DOT_ALIGN:
        dot_align(as);
        break;
    case GPERF_DOT_ASCIIZ:
        dot_string(as, 1);
        break;
    case GPERF_DOT_BYTE:
        dot_byte(as);
        break;
    case GPERF_DOT_DEFINE:
        dot_define(as);
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
    case GPERF_DOT_ENDFOR:
        dot_endfor(as);
        break;
    case GPERF_DOT_ENDMACRO:
        dot_endmacro(as);
        break;
    case GPERF_DOT_ENDPROC:
        dot_endproc(as);
        break;
    case GPERF_DOT_ENDREPEAT:
        dot_endrepeat(as);
        break;
    case GPERF_DOT_ENDSCOPE:
        dot_endscope(as);
        break;
    case GPERF_DOT_FOR:
        dot_for(as);
        break;
    case GPERF_DOT_INCBIN:
        dot_incbin(as);
        break;
    case GPERF_DOT_INCLUDE:
        dot_include(as);
        break;
    case GPERF_DOT_LOCAL:
        dot_local(as);
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
    case GPERF_DOT_REPEAT:
        dot_repeat(as);
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
        dot_string(as, 0);
        break;
    case GPERF_DOT_WORD:
        emit_cs_values(as, 16, BYTE_ORDER_LO);
        break;
    case GPERF_DOT_ELSE:
        dot_else(as);
        break;
    case GPERF_DOT_ENDIF:
        dot_endif(as);
        break;
    case GPERF_DOT_IF:
        dot_if(as);
        break;
    default:
        asm_err(as, ASM_ERR_RESOLVE, "Dot command %.*s not implemented yet",
                (int)as->token.name_length, as->token.name);
        while(as->token.type != TOKEN_END) {
            next_token(as);
        }
        break;
    }
}

void parse_if_skip(ASSEMBLER *as) {
    as->cur = as->line;
    get_token(as);
    if(as->token.type != TOKEN_VAR || as->token.name_length < 2 || as->token.name[0] != '.') {
        return;
    }

    OPCODEINFO *info = in_word_set(as->token.name, as->token.name_length);
    if(!info || info->mnemonic[0] != '.') {
        return;
    }

    switch(info->opcode_id) {
    case GPERF_DOT_IF:
        as->if_skip_depth++;
        break;
    case GPERF_DOT_ELSE:
        if(as->if_skip_depth == 1) {
            IF_FRAME *frame = if_stack_top(as);
            if(!frame) {
                asm_err(as, ASM_ERR_RESOLVE, ".else without .if");
                return;
            }
            if(frame->else_seen) {
                asm_err(as, ASM_ERR_RESOLVE, ".else after .else");
            }
            frame->else_seen = 1;
            as->if_skip_depth = 0;
        }
        break;
    case GPERF_DOT_ENDIF:
        if(as->if_skip_depth == 1) {
            if_stack_pop(as);
            as->if_skip_depth = 0;
        } else if(as->if_skip_depth > 1) {
            as->if_skip_depth--;
        }
        break;
    default:
        break;
    }
}

void parse_label(ASSEMBLER *as) {
    if(as->token.type == TOKEN_OP && as->token.op == ':') {
        if(as->pass == 1) {
            uint16_t address = current_output_address(as);
            AM65_ARRAY_ADD(&as->anon_symbols, address);
        }
    } else if(as->token.type == TOKEN_VAR && next_token_op_is(as, ':')) {
        symbol_write(as, as->token.name, as->token.name_length, SYMBOL_ADDRESS, current_output_address(as));
        get_token(as);
    }
}

int parse_macro_if_is_macro(ASSEMBLER *as) {
    if(as->token.type != TOKEN_VAR) {
        return 0;
    }

    for(size_t i = 0; i < as->macros.items; i++) {
        MACRO *macro = AM65_ARRAY_GET(&as->macros, MACRO, i);
        if(macro->name_len != (int)as->token.name_length ||
           0 != asm_strnicmp(macro->name, as->token.name, as->token.name_length)) {
            continue;
        }

        for(size_t si = 0; si < as->macro_stack.items; si++) {
            MACRO_EXPAND *active = AM65_ARRAY_GET(&as->macro_stack, MACRO_EXPAND, si);
            if(active->macro_name_length == (uint32_t)macro->name_len &&
               0 == asm_strnicmp(active->macro_name, macro->name, (size_t)macro->name_len)) {
                asm_err(as, ASM_ERR_RESOLVE, "Macro %.*s cannot call itself", macro->name_len, macro->name);
                return 1;
            }
        }

        MACRO_EXPAND expand;
        memset(&expand, 0, sizeof(expand));
        AM65_ARRAY_INIT(&expand.renames, RENAME_MAP);
        AM65_ARRAY_INIT(&expand.args, MACRO_ARG_VALUE);
        expand.macro_name = macro->name;
        expand.macro_name_length = (uint32_t)macro->name_len;

        const char *p = as->cur;
        for(size_t pi = 0; pi < macro->parameters.items; pi++) {
            MACRO_PARAM *param = AM65_ARRAY_GET(&macro->parameters, MACRO_PARAM, pi);
            while(*p == ' ' || *p == '\t') {
                p++;
            }

            int value_len = 0;
            char *value = NULL;
            if(*p == '\0' || *p == ';') {
                value = copy_slice("", 0);
            } else {
                value = parse_macro_arg(as, &p, &value_len);
                if(*p == ',') {
                    p++;
                }
            }
            if(!value) {
                macro_expand_free(as, &expand);
                return 1;
            }

            MACRO_ARG_VALUE arg;
            arg.name = param->name;
            arg.name_len = param->name_len;
            arg.value = value;
            arg.value_len = value_len;
            if(ASM_OK != AM65_ARRAY_ADD(&expand.args, arg)) {
                free(value);
                macro_expand_free(as, &expand);
                asm_err(as, ASM_ERR_FATAL, "Out of memory storing macro argument");
                return 1;
            }
        }

        if(ASM_OK != AM65_ARRAY_ADD(&as->macro_stack, expand)) {
            macro_expand_free(as, &expand);
            asm_err(as, ASM_ERR_FATAL, "Out of memory pushing macro invocation");
            return 1;
        }

        if(ASM_OK != file_stack_push(as, macro->body_file, macro->body_start, macro->body_line, 1)) {
            macro_stack_pop(as);
            return 1;
        }
        return 1;
    }

    return 0;
}

void parse_opcode(ASSEMBLER *as) {
    next_token(as);
    if(is_implied_instruction(as)) {
        int8_t opcode = (int8_t)asm_opcode[
            as->opcode_info.opcode_id][ADDRESS_MODE_ACCUMULATOR];
        if(opcode == -1) {
            asm_err(as, ASM_ERR_RESOLVE, "Invalid opcode %s with mode %s",
                    as->opcode_info.mnemonic,
                    address_mode_txt[ADDRESS_MODE_ACCUMULATOR]);
        } else if(!asm_opcode_allowed(
                as->cpu_profile,
                as->opcode_info.opcode_id,
                ADDRESS_MODE_ACCUMULATOR)) {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %s with mode %s is not valid for %s",
                    as->opcode_info.mnemonic,
                    address_mode_txt[ADDRESS_MODE_ACCUMULATOR],
                    assembler_cpu_profile_name(as->cpu_profile));
        }
        emit_byte(as, (uint8_t)opcode);
        return;
    }

    if((as->opcode_info.opcode_id >= GPERF_OPCODE_BBR0 &&
        as->opcode_info.opcode_id <= GPERF_OPCODE_BBR7) ||
       (as->opcode_info.opcode_id >= GPERF_OPCODE_BBS0 &&
        as->opcode_info.opcode_id <= GPERF_OPCODE_BBS7)) {
        uint64_t zero_page = (uint64_t)expr_evaluate(as);
        uint64_t target;
        int32_t delta;
        int8_t opcode = (int8_t)asm_opcode[
            as->opcode_info.opcode_id][ADDRESS_MODE_ZEROPAGE];

        if(as->token.op != ',') {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %s expects zero-page,target",
                    as->opcode_info.mnemonic);
            return;
        }
        next_token(as);
        target = (uint64_t)expr_evaluate(as);
        if(zero_page >= 256) {
            asm_err(as, ASM_ERR_RESOLVE, "8-bit zero-page value expected but value = $%llX",
                    (unsigned long long)zero_page);
        }
        if(!asm_opcode_allowed(
                as->cpu_profile,
                as->opcode_info.opcode_id,
                ADDRESS_MODE_ZEROPAGE)) {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %s is not valid for %s",
                    as->opcode_info.mnemonic,
                    assembler_cpu_profile_name(as->cpu_profile));
        }
        emit_byte(as, (uint8_t)opcode);
        emit_byte(as, (uint8_t)zero_page);
        delta = (int32_t)target - 1 - current_output_address(as);
        if(delta > 127 || delta < -128) {
            asm_err(as, ASM_ERR_RESOLVE, "Relative branch out of range $%X", delta);
        }
        emit_byte(as, (uint8_t)delta);
        return;
    }

    int processed = 0;
    switch(as->token.op) {
    case '#':
        as->opcode_info.value = (uint64_t)expr_full_evaluate(as);
        as->opcode_info.addressing_mode = ADDRESS_MODE_IMMEDIATE;
        emit_opcode(as);
        processed = 1;
        break;
    case '(': {
        char reg = 0;
        int indirect = indirect_mode_from_line(as, &reg);
        if(indirect) {
            if(reg == 'x') {
                as->opcode_info.value = (uint64_t)expr_full_evaluate(as);
            } else {
                as->opcode_info.value = (uint64_t)expr_evaluate(as);
            }
            as->opcode_info.addressing_mode = indirect;
            emit_opcode(as);
            if(indirect == ADDRESS_MODE_INDIRECT_X) {
                next_token(as);
                if(!is_register_token(as, 'x')) {
                    asm_err(as, ASM_ERR_RESOLVE, "Expected ,x");
                }
                next_token(as);
                if(as->token.type == TOKEN_OP && as->token.op == ')') {
                    next_token(as);
                }
            } else if(indirect == ADDRESS_MODE_INDIRECT_Y) {
                if(as->token.op == ',') {
                    next_token(as);
                }
                if(!is_register_token(as, 'y')) {
                    asm_err(as, ASM_ERR_RESOLVE, "Expected ,y");
                }
                next_token(as);
            }
            processed = 1;
        }
        break;
    }
    default:
        break;
    }

    if(!processed) {
        if(as->token.type == TOKEN_END) {
            asm_err(as, ASM_ERR_RESOLVE, "Opcode %.3s with mode %s expects an operand",
                    as->opcode_info.mnemonic, address_mode_txt[as->opcode_info.addressing_mode]);
            return;
        }
        as->opcode_info.value = (uint64_t)expr_evaluate(as);
        if(as->opcode_info.width >= 8 && as->expression_size > 8) {
            as->opcode_info.width = 16;
        }
        decode_abs_rel_zp_opcode(as);
    }
}

void parse_variable(ASSEMBLER *as) {
    const char *symbol_name = as->token.name;
    uint32_t name_length = as->token.name_length;
    next_token(as);

    if(as->token.type == TOKEN_OP && as->token.op == '=') {
        next_token(as);
        symbol_write(as, symbol_name, name_length, SYMBOL_VARIABLE, (uint64_t)expr_evaluate(as));
    } else if(as->token.type == TOKEN_OP && (as->token.op == '+' || as->token.op == '-')) {
        char op = as->token.op;
        next_token(as);
        if(as->token.type != TOKEN_OP || as->token.op != op) {
            asm_err(as, ASM_ERR_RESOLVE, "Expected %c%c", op, op);
            return;
        }
        SYMBOL_LABEL *sl = symbol_read(as, symbol_name, name_length);
        uint64_t value = sl ? sl->symbol_value : 0;
        if(op == '+') {
            value++;
        } else {
            value--;
        }
        symbol_write(as, symbol_name, name_length, SYMBOL_VARIABLE, value);
        next_token(as);
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Expected assignment operator");
    }
}
