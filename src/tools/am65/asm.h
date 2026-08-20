// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "asm_common.h"
#include "dynarray.h"
#include "errorlog.h"
#include "define.h"
#include "file.h"
#include "gperf.h"
#include "opcode.h"
#include "scope.h"
#include "segment.h"
#include "symbol.h"
#include "token.h"
#include "expr.h"
#include "emit.h"
#include "parse.h"

#define ASM_MAX_LINE 1024

// Emit one assembled byte. `target` is the per-target context: for the default
// (unnamed) output it is CB_ASM_CTX.default_target; for a named `.scope file="..."`
// redirect it is whatever target_open returned.
typedef void (*asm_output_byte_fn)(void *target, uint16_t addr, uint8_t val);

// Open an output redirect for a named `.scope name file="..." dest="..."`.
// Returns an opaque per-target context (passed back to output_byte / target_release),
// or NULL to signal the redirect could not be honoured. May be NULL in the callback
// struct, in which case `.scope` with file=/dest= is rejected with an error.
// Hosts decide how file= and dest= compose (CLI: file= only; a2m emulator: each
// independently selects a file write and/or a memory bank).
typedef void *(*asm_target_open_fn)(
    void *user,
    const char *name, int name_len,
    const char *file, int file_len,
    const char *dest, int dest_len);

// Release a context previously returned by target_open. May be NULL.
typedef void (*asm_target_release_fn)(void *user, void *target);

typedef struct {
    void *user;                            // host context, passed to target_open/target_release
    void *default_target;                  // per-target context for the initial unnamed target
    asm_output_byte_fn output_byte;        // required
    asm_target_open_fn target_open;        // optional (NULL => .scope file=/dest= unsupported)
    asm_target_release_fn target_release;  // optional
    // Optional, case-insensitive destination vocabulary. When non-empty, every
    // comma-separated name in dest="..." must occur here before target_open is
    // called. A host that does not interpret destinations (the am65 CLI) leaves
    // this empty and may accept/ignore dest= for shared emulator source.
    const char *const *destination_names;
    size_t destination_name_count;
} CB_ASM_CTX;

typedef void (*assembler_symbol_cb)(
    const char *name,
    uint16_t address,
    void *user);

typedef void (*assembler_segment_adjustment_cb)(
    size_t target_index,
    const char *segment_name,
    uint16_t address,
    void *user);

struct ASSEMBLER {
    CB_ASM_CTX cb;

    AM65_DYNARRAY files;
    AM65_DYNARRAY file_stack;
    ASM_FILE *root_file;
    ASM_FILE *current_file;

    char line[ASM_MAX_LINE];
    int line_len;
    const char *cur;
    TOKEN token;

    AM65_DYNARRAY defines;
    AM65_DYNARRAY predefines;

    SCOPE *root_scope;
    SCOPE *active_scope;
    AM65_DYNARRAY scope_stack;
    AM65_DYNARRAY *symbol_table;
    AM65_DYNARRAY anon_symbols;
    AM65_DYNARRAY loop_stack;
    AM65_DYNARRAY macros;
    AM65_DYNARRAY macro_stack;
    int macro_id;

    AM65_DYNARRAY if_stack;
    int if_skip_depth;

    OPCODEINFO opcode_info;
    assembler_cpu_profile cpu_profile;
    assembler_cpu_profile default_cpu_profile;

    int expression_size;
    int expression_unknown;
    int expression_depth;
    TARGET *active_target;
    AM65_DYNARRAY targets;
    const char *strcode;

    ERRORLOG *errorlog;
    int pass;
    int error_log_level;
    const char *current_file_name;
    size_t current_line;
    char *root_dir;

    int auto_adjust_segments;
    AM65_DYNARRAY segment_adjustments;
};

static inline uint16_t current_output_address(ASSEMBLER *as) {
    if(!as->active_target || !as->active_target->active_segment) {
        return 0;
    }
    return as->active_target->active_segment->segment_output_address;
}

int assembler_init(ASSEMBLER *as, ERRORLOG *errorlog, CB_ASM_CTX *cb);
// Seed a text define that survives both passes (e.g. a build flag). Call after
// assembler_init and before assembler_assemble. `value` may be "" but not NULL.
int assembler_predefine(ASSEMBLER *as, const char *name, const char *value);
/* Select the accepted instruction set. The default is the documented NMOS
   6502 set; source directives may change the profile while assembling. */
void assembler_set_cpu_profile(ASSEMBLER *as, assembler_cpu_profile profile);
assembler_cpu_profile assembler_get_cpu_profile(const ASSEMBLER *as);
// When enabled, retry pass-1 segment layout up to three times using the overlap
// checker's suggested starts. The source is not modified and pass 2 only runs
// after a non-overlapping layout has been found.
void assembler_set_auto_adjust_segments(ASSEMBLER *as, int enabled);
int assembler_assemble(ASSEMBLER *as, const char *input_file, uint16_t address);
void assembler_walk_symbols(ASSEMBLER *as, assembler_symbol_cb cb, void *user);
void assembler_walk_segment_adjustments(
    ASSEMBLER *as,
    assembler_segment_adjustment_cb cb,
    void *user);
void assembler_shutdown(ASSEMBLER *as);

// Parser-internal hook: substitute an active target/segment start when the
// auto-adjust retry map contains one.
uint16_t assembler_adjust_segment_start(
    ASSEMBLER *as,
    const char *segment_name,
    uint32_t segment_name_length,
    uint16_t source_address);
