#pragma once

/* Guarded-breakpoint conditions.
 *
 * A condition is a bounded AND-list of comparison terms evaluated only *after*
 * a breakpoint's address/access/mapping test has already matched. That keeps
 * the CPU hot path untouched: a guard on $D021 costs work on the handful of
 * accesses that hit $D021, never on the general bus stream.
 *
 * This is deliberately not an expression language (no OR, no grouping, no
 * precedence) - the same discipline the flight recorder applied to its query
 * keys. If a case needs OR, arm two breakpoints.
 *
 * Parsing and evaluation are pure: evaluation reads a caller-supplied context
 * rather than the machine, so both are unit-testable without a running C64.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    /* Maximum ANDed terms in one condition. */
    RUNTIME_BREAKPOINT_CONDITION_TERMS = 4,
    /* Upper bound on a formatted condition, including the NUL. */
    RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX = 128
};

typedef enum runtime_bp_term_lhs {
    RUNTIME_BP_LHS_A = 0,
    RUNTIME_BP_LHS_X,
    RUNTIME_BP_LHS_Y,
    RUNTIME_BP_LHS_SP,
    RUNTIME_BP_LHS_P,
    RUNTIME_BP_LHS_FLAG_N,
    RUNTIME_BP_LHS_FLAG_V,
    RUNTIME_BP_LHS_FLAG_B,
    RUNTIME_BP_LHS_FLAG_D,
    RUNTIME_BP_LHS_FLAG_I,
    RUNTIME_BP_LHS_FLAG_Z,
    RUNTIME_BP_LHS_FLAG_C,
    /* The byte carried by the matching access. Meaningless for exec matches,
       which is why a definition combining `value` with exec access is
       rejected when the breakpoint is created. */
    RUNTIME_BP_LHS_VALUE,
    /* One CPU-map byte read at match time (see term.mem_address). */
    RUNTIME_BP_LHS_MEM,
    RUNTIME_BP_LHS_RASTER,
    RUNTIME_BP_LHS_VIC_CYCLE
} runtime_bp_term_lhs;

typedef enum runtime_bp_term_op {
    RUNTIME_BP_OP_EQ = 0,
    RUNTIME_BP_OP_NE,
    RUNTIME_BP_OP_LT,
    RUNTIME_BP_OP_GT,
    RUNTIME_BP_OP_LE,
    RUNTIME_BP_OP_GE,
    RUNTIME_BP_OP_MASK_SET,   /* (lhs & imm) != 0 */
    RUNTIME_BP_OP_MASK_CLEAR  /* (lhs & imm) == 0 */
} runtime_bp_term_op;

typedef struct runtime_bp_term {
    uint8_t lhs;           /* runtime_bp_term_lhs */
    uint8_t op;            /* runtime_bp_term_op */
    uint16_t mem_address;  /* meaningful only when lhs == RUNTIME_BP_LHS_MEM */
    uint32_t imm;
} runtime_bp_term;

typedef struct runtime_bp_condition {
    uint8_t term_count; /* 0 = unguarded; evaluates true */
    runtime_bp_term terms[RUNTIME_BREAKPOINT_CONDITION_TERMS];
} runtime_bp_condition;

typedef uint8_t (*runtime_bp_mem_read_fn)(void *user, uint16_t address);

typedef struct runtime_bp_eval_context {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint8_t value;    /* valid only when has_value */
    bool has_value;
    uint16_t raster;
    uint16_t vic_cycle;
    runtime_bp_mem_read_fn mem_read; /* may be NULL; mem terms then fail */
    void *mem_read_user;
} runtime_bp_eval_context;

/* Parse a `when=` value, e.g. "i==1" or "value!&1,mem($D000)>$F0".
   Immediates accept $hex, 0x, and decimal. Terms are comma separated and no
   whitespace is permitted inside the text. Returns false and writes a
   diagnostic to `error` on any malformed or over-long input. */
bool runtime_bp_condition_parse(
    const char *text,
    runtime_bp_condition *out_condition,
    char *error,
    size_t error_size);

/* True when every term holds. Vacuously true for an empty condition, so an
   unguarded breakpoint keeps exactly its pre-guard behavior. */
bool runtime_bp_condition_eval(
    const runtime_bp_condition *condition,
    const runtime_bp_eval_context *context);

/* Render back to parse syntax (break-list echo, .ini persistence). Writes an
   empty string for an empty condition. Returns false if `out` is too small,
   rather than emitting a truncated condition that would parse back as
   something else. */
bool runtime_bp_condition_format(
    const runtime_bp_condition *condition,
    char *out,
    size_t out_size);

/* True when any term reads the accessed byte. Used to reject exec breakpoints
   that reference `value`, which has no meaning on an instruction fetch. */
bool runtime_bp_condition_uses_value(const runtime_bp_condition *condition);

/* True when the term count and every term's lhs/op are in range. Callers build
   `runtime_breakpoint_definition` field by field, so this is checked when a
   breakpoint is armed: an unzeroed definition then yields an unguarded
   breakpoint instead of one whose garbage guard can never hold. */
bool runtime_bp_condition_is_valid(const runtime_bp_condition *condition);
