#include "runtime_breakpoint_condition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    /* Immediates cover the widest left-hand side (raster / cycle_in_line) but
       never exceed 16 bits; anything larger is a typo, not a condition. */
    RUNTIME_BP_IMM_MAX = 0xffffu
};

typedef struct runtime_bp_lhs_name {
    const char *name;
    uint8_t lhs;
} runtime_bp_lhs_name;

/* Longest first is not required: the identifier scan consumes the whole token
   before it looks anything up, so "value" can never be mistaken for "v". */
static const runtime_bp_lhs_name runtime_bp_lhs_names[] = {
    { "a", RUNTIME_BP_LHS_A },
    { "x", RUNTIME_BP_LHS_X },
    { "y", RUNTIME_BP_LHS_Y },
    { "sp", RUNTIME_BP_LHS_SP },
    { "p", RUNTIME_BP_LHS_P },
    { "n", RUNTIME_BP_LHS_FLAG_N },
    { "v", RUNTIME_BP_LHS_FLAG_V },
    { "b", RUNTIME_BP_LHS_FLAG_B },
    { "d", RUNTIME_BP_LHS_FLAG_D },
    { "i", RUNTIME_BP_LHS_FLAG_I },
    { "z", RUNTIME_BP_LHS_FLAG_Z },
    { "c", RUNTIME_BP_LHS_FLAG_C },
    { "value", RUNTIME_BP_LHS_VALUE },
    { "raster", RUNTIME_BP_LHS_RASTER },
    { "cycle_in_line", RUNTIME_BP_LHS_CYCLE_IN_LINE },
    /* Legacy C64 token; still parsed, always formatted as cycle_in_line. */
    { "vic_cycle", RUNTIME_BP_LHS_CYCLE_IN_LINE }
};

typedef struct runtime_bp_op_name {
    const char *name;
    uint8_t op;
} runtime_bp_op_name;

/* Two-character forms must precede their one-character prefixes so that "<="
   is not read as "<" followed by a malformed immediate. */
static const runtime_bp_op_name runtime_bp_op_names[] = {
    { "==", RUNTIME_BP_OP_EQ },
    { "!=", RUNTIME_BP_OP_NE },
    { "!&", RUNTIME_BP_OP_MASK_CLEAR },
    { "<=", RUNTIME_BP_OP_LE },
    { ">=", RUNTIME_BP_OP_GE },
    { "<", RUNTIME_BP_OP_LT },
    { ">", RUNTIME_BP_OP_GT },
    { "&", RUNTIME_BP_OP_MASK_SET }
};

static void runtime_bp_set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u) {
        snprintf(error, error_size, "%s", message);
    }
}

static bool runtime_bp_is_identifier_char(char ch)
{
    return (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '_';
}

/* Parse `$hex`, `0x...`, or decimal. Rejects signs and empty digit runs so a
   stray separator cannot silently become zero. */
static bool runtime_bp_parse_number(
    const char **cursor,
    uint32_t max_value,
    uint32_t *out_value)
{
    const char *start = *cursor;
    char *end = NULL;
    unsigned long parsed;
    int base = 0;

    if (*start == '$') {
        start++;
        base = 16;
        if (!((*start >= '0' && *start <= '9') ||
              (*start >= 'a' && *start <= 'f') ||
              (*start >= 'A' && *start <= 'F'))) {
            return false;
        }
    } else if (*start < '0' || *start > '9') {
        /* No sign, no whitespace, no empty immediate. */
        return false;
    }

    parsed = strtoul(start, &end, base);
    if (end == start || parsed > (unsigned long)max_value) {
        return false;
    }
    *cursor = end;
    *out_value = (uint32_t)parsed;
    return true;
}

static bool runtime_bp_parse_lhs(
    const char **cursor,
    runtime_bp_term *term,
    char *error,
    size_t error_size)
{
    const char *scan = *cursor;
    size_t length;
    size_t i;

    if (strncmp(scan, "mem(", 4) == 0) {
        uint32_t address = 0u;
        const char *inner = scan + 4;

        if (!runtime_bp_parse_number(&inner, 0xffffu, &address)) {
            runtime_bp_set_error(error, error_size,
                "mem() needs a 16-bit address");
            return false;
        }
        if (*inner != ')') {
            runtime_bp_set_error(error, error_size, "mem( is not closed");
            return false;
        }
        term->lhs = RUNTIME_BP_LHS_MEM;
        term->mem_address = (uint16_t)address;
        *cursor = inner + 1;
        return true;
    }

    while (runtime_bp_is_identifier_char(*scan)) {
        scan++;
    }
    length = (size_t)(scan - *cursor);
    if (length == 0u) {
        runtime_bp_set_error(error, error_size, "expected a condition term");
        return false;
    }

    for (i = 0; i < sizeof(runtime_bp_lhs_names) / sizeof(runtime_bp_lhs_names[0]); ++i) {
        const char *name = runtime_bp_lhs_names[i].name;
        if (strlen(name) == length && strncmp(*cursor, name, length) == 0) {
            term->lhs = runtime_bp_lhs_names[i].lhs;
            *cursor = scan;
            return true;
        }
    }

    runtime_bp_set_error(error, error_size, "unknown condition term");
    return false;
}

static bool runtime_bp_parse_op(
    const char **cursor,
    runtime_bp_term *term,
    char *error,
    size_t error_size)
{
    size_t i;

    for (i = 0; i < sizeof(runtime_bp_op_names) / sizeof(runtime_bp_op_names[0]); ++i) {
        const char *name = runtime_bp_op_names[i].name;
        size_t length = strlen(name);
        if (strncmp(*cursor, name, length) == 0) {
            term->op = runtime_bp_op_names[i].op;
            *cursor += length;
            return true;
        }
    }

    runtime_bp_set_error(error, error_size, "unknown condition operator");
    return false;
}

bool runtime_bp_condition_parse(
    const char *text,
    runtime_bp_condition *out_condition,
    char *error,
    size_t error_size)
{
    const char *cursor = text;

    if (out_condition == NULL) {
        runtime_bp_set_error(error, error_size, "no condition output");
        return false;
    }
    memset(out_condition, 0, sizeof(*out_condition));

    if (text == NULL || text[0] == '\0') {
        runtime_bp_set_error(error, error_size, "empty condition");
        return false;
    }
    if (strlen(text) >= RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX) {
        runtime_bp_set_error(error, error_size, "condition too long");
        return false;
    }

    for (;;) {
        runtime_bp_term term;
        uint32_t imm = 0u;

        if (out_condition->term_count >= RUNTIME_BREAKPOINT_CONDITION_TERMS) {
            runtime_bp_set_error(error, error_size,
                "too many condition terms (max 4)");
            return false;
        }

        memset(&term, 0, sizeof(term));
        if (!runtime_bp_parse_lhs(&cursor, &term, error, error_size)) {
            return false;
        }
        if (!runtime_bp_parse_op(&cursor, &term, error, error_size)) {
            return false;
        }
        if (!runtime_bp_parse_number(&cursor, RUNTIME_BP_IMM_MAX, &imm)) {
            runtime_bp_set_error(error, error_size,
                "condition needs a 16-bit immediate");
            return false;
        }
        term.imm = imm;
        out_condition->terms[out_condition->term_count] = term;
        out_condition->term_count++;

        if (*cursor == '\0') {
            break;
        }
        /* ';' is accepted alongside ',' because the .ini breakpoint value is
           itself a comma-separated item list (see runtime_breakpoint_ini.c). */
        if (*cursor != ',' && *cursor != ';') {
            runtime_bp_set_error(error, error_size,
                "unexpected character in condition");
            return false;
        }
        cursor++;
        if (*cursor == '\0') {
            runtime_bp_set_error(error, error_size,
                "condition ends with a separator");
            return false;
        }
    }

    return true;
}

/* Resolve a term's left-hand side. Returns false when the value is not
   available in this context (a `value` term on an exec match, or a `mem` term
   with no reader), which fails the term rather than guessing. */
static bool runtime_bp_term_lhs_value(
    const runtime_bp_term *term,
    const runtime_bp_eval_context *context,
    uint32_t *out_value)
{
    switch ((runtime_bp_term_lhs)term->lhs) {
    case RUNTIME_BP_LHS_A:
        *out_value = context->a;
        return true;
    case RUNTIME_BP_LHS_X:
        *out_value = context->x;
        return true;
    case RUNTIME_BP_LHS_Y:
        *out_value = context->y;
        return true;
    case RUNTIME_BP_LHS_SP:
        *out_value = context->sp;
        return true;
    case RUNTIME_BP_LHS_P:
        *out_value = context->p;
        return true;
    case RUNTIME_BP_LHS_FLAG_N:
        *out_value = (context->p & 0x80u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_FLAG_V:
        *out_value = (context->p & 0x40u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_FLAG_B:
        *out_value = (context->p & 0x10u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_FLAG_D:
        *out_value = (context->p & 0x08u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_FLAG_I:
        *out_value = (context->p & 0x04u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_FLAG_Z:
        *out_value = (context->p & 0x02u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_FLAG_C:
        *out_value = (context->p & 0x01u) ? 1u : 0u;
        return true;
    case RUNTIME_BP_LHS_VALUE:
        if (!context->has_value) {
            return false;
        }
        *out_value = context->value;
        return true;
    case RUNTIME_BP_LHS_MEM:
        if (context->mem_read == NULL) {
            return false;
        }
        *out_value = context->mem_read(context->mem_read_user, term->mem_address);
        return true;
    case RUNTIME_BP_LHS_RASTER:
        *out_value = context->raster;
        return true;
    case RUNTIME_BP_LHS_CYCLE_IN_LINE:
        *out_value = context->cycle_in_line;
        return true;
    default:
        return false;
    }
}

static bool runtime_bp_term_holds(
    const runtime_bp_term *term,
    const runtime_bp_eval_context *context)
{
    uint32_t lhs = 0u;

    if (!runtime_bp_term_lhs_value(term, context, &lhs)) {
        return false;
    }

    switch ((runtime_bp_term_op)term->op) {
    case RUNTIME_BP_OP_EQ:
        return lhs == term->imm;
    case RUNTIME_BP_OP_NE:
        return lhs != term->imm;
    case RUNTIME_BP_OP_LT:
        return lhs < term->imm;
    case RUNTIME_BP_OP_GT:
        return lhs > term->imm;
    case RUNTIME_BP_OP_LE:
        return lhs <= term->imm;
    case RUNTIME_BP_OP_GE:
        return lhs >= term->imm;
    case RUNTIME_BP_OP_MASK_SET:
        return (lhs & term->imm) != 0u;
    case RUNTIME_BP_OP_MASK_CLEAR:
        return (lhs & term->imm) == 0u;
    default:
        return false;
    }
}

bool runtime_bp_condition_eval(
    const runtime_bp_condition *condition,
    const runtime_bp_eval_context *context)
{
    uint8_t i;

    if (condition == NULL || condition->term_count == 0u) {
        return true;
    }
    if (context == NULL) {
        return false;
    }

    for (i = 0; i < condition->term_count &&
                i < RUNTIME_BREAKPOINT_CONDITION_TERMS; ++i) {
        if (!runtime_bp_term_holds(&condition->terms[i], context)) {
            return false;
        }
    }
    return true;
}

static const char *runtime_bp_lhs_text(uint8_t lhs)
{
    size_t i;

    for (i = 0; i < sizeof(runtime_bp_lhs_names) / sizeof(runtime_bp_lhs_names[0]); ++i) {
        if (runtime_bp_lhs_names[i].lhs == lhs) {
            return runtime_bp_lhs_names[i].name;
        }
    }
    return NULL;
}

static const char *runtime_bp_op_text(uint8_t op)
{
    size_t i;

    for (i = 0; i < sizeof(runtime_bp_op_names) / sizeof(runtime_bp_op_names[0]); ++i) {
        if (runtime_bp_op_names[i].op == op) {
            return runtime_bp_op_names[i].name;
        }
    }
    return NULL;
}

bool runtime_bp_condition_format(
    const runtime_bp_condition *condition,
    char *out,
    size_t out_size)
{
    char buffer[RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX];
    size_t used = 0u;
    uint8_t i;

    if (out == NULL || out_size == 0u) {
        return false;
    }
    if (condition == NULL || condition->term_count == 0u) {
        out[0] = '\0';
        return true;
    }

    buffer[0] = '\0';
    for (i = 0; i < condition->term_count &&
                i < RUNTIME_BREAKPOINT_CONDITION_TERMS; ++i) {
        const runtime_bp_term *term = &condition->terms[i];
        const char *op_text = runtime_bp_op_text(term->op);
        char lhs_text[16];
        int written;

        if (op_text == NULL) {
            return false;
        }
        if (term->lhs == RUNTIME_BP_LHS_MEM) {
            snprintf(lhs_text, sizeof(lhs_text), "mem($%04X)", term->mem_address);
        } else {
            const char *name = runtime_bp_lhs_text(term->lhs);
            if (name == NULL) {
                return false;
            }
            snprintf(lhs_text, sizeof(lhs_text), "%s", name);
        }

        written = snprintf(
            buffer + used,
            sizeof(buffer) - used,
            "%s%s%s$%X",
            i == 0u ? "" : ",",
            lhs_text,
            op_text,
            term->imm);
        if (written < 0 || (size_t)written >= sizeof(buffer) - used) {
            return false;
        }
        used += (size_t)written;
    }

    if (used + 1u > out_size) {
        return false;
    }
    memcpy(out, buffer, used + 1u);
    return true;
}

bool runtime_bp_condition_is_valid(const runtime_bp_condition *condition)
{
    uint8_t i;

    if (condition == NULL) {
        return false;
    }
    if (condition->term_count > RUNTIME_BREAKPOINT_CONDITION_TERMS) {
        return false;
    }
    for (i = 0; i < condition->term_count; ++i) {
        if (runtime_bp_lhs_text(condition->terms[i].lhs) == NULL &&
            condition->terms[i].lhs != RUNTIME_BP_LHS_MEM) {
            return false;
        }
        if (runtime_bp_op_text(condition->terms[i].op) == NULL) {
            return false;
        }
    }
    return true;
}

bool runtime_bp_condition_uses_value(const runtime_bp_condition *condition)
{
    uint8_t i;

    if (condition == NULL) {
        return false;
    }
    for (i = 0; i < condition->term_count &&
                i < RUNTIME_BREAKPOINT_CONDITION_TERMS; ++i) {
        if (condition->terms[i].lhs == RUNTIME_BP_LHS_VALUE) {
            return true;
        }
    }
    return false;
}
