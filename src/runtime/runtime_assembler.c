#include "runtime_assembler.h"

#include "apple2.h"
#include "asm.h"
#include "errorlog.h"
#include "hostfs.h"
#include "smrtprt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RUNTIME_ASSEMBLER_PATH_MAX
#define RUNTIME_ASSEMBLER_PATH_MAX 1024
#endif

typedef struct assembler_output_stats {
    uint16_t first_address;
    uint16_t last_address;
    uint32_t byte_count;
    bool has_output;
} assembler_output_stats;

/* Per-target host context. dest= selects a memory bank; file= buffers bytes and
   writes them beside the source after a successful assemble. Both may be set. */
typedef struct assembler_output_target {
    apple2_t *machine;
    view_flags_t view;
    bool write_memory;
    bool write_file;
    char *file_path;
    uint8_t *ram;
    int wrote_file_any;
    uint32_t file_lo;
    uint32_t file_hi;
    assembler_output_stats *stats;
} assembler_output_target;

typedef struct assembler_host_ctx {
    apple2_t *machine;
    assembler_output_stats *stats;
    const char *source_path;
} assembler_host_ctx;

typedef struct assembler_symbol_import {
    symbol_table *symbols;
    const char *source_name;
    bool ok;
} assembler_symbol_import;

typedef struct assembler_adjustment_format {
    char *buffer;
    size_t size;
    size_t written;
    bool has_adjustments;
} assembler_adjustment_format;

static bool runtime_assembler_path_is_absolute(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
#if defined(_WIN32)
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return isalpha((unsigned char)path[0]) && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\' || path[2] == '\0');
#else
    return path[0] == '/';
#endif
}

static const char *runtime_assembler_find_last_separator(const char *path) {
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(path, '\\');
    if (slash == NULL || (backslash != NULL && backslash > slash)) {
        return backslash;
    }
#endif
    return slash;
}

static bool runtime_assembler_resolve_output_path(
    const char *source_path,
    const char *file,
    int file_len,
    char *out,
    size_t out_size) {
    char file_name[RUNTIME_ASSEMBLER_PATH_MAX];
    const char *sep;

    if (out == NULL || out_size == 0 || file == NULL || file_len <= 0) {
        return false;
    }
    if ((size_t)file_len >= sizeof(file_name)) {
        return false;
    }
    memcpy(file_name, file, (size_t)file_len);
    file_name[file_len] = '\0';

    if (runtime_assembler_path_is_absolute(file_name) ||
        source_path == NULL || source_path[0] == '\0') {
        return snprintf(out, out_size, "%s", file_name) < (int)out_size;
    }

    sep = runtime_assembler_find_last_separator(source_path);
    if (sep == NULL) {
        return snprintf(out, out_size, "%s", file_name) < (int)out_size;
    }
    return snprintf(
               out,
               out_size,
               "%.*s%s",
               (int)(sep - source_path + 1),
               source_path,
               file_name) < (int)out_size;
}

static void runtime_assembler_note_memory_byte(
    assembler_output_stats *stats,
    uint16_t addr) {
    if (stats == NULL) {
        return;
    }
    if (!stats->has_output) {
        stats->first_address = addr;
        stats->last_address = addr;
        stats->has_output = true;
    } else {
        if (addr < stats->first_address) {
            stats->first_address = addr;
        }
        if (addr > stats->last_address) {
            stats->last_address = addr;
        }
    }
    stats->byte_count++;
}

static void runtime_assembler_output_byte(void *user, uint16_t addr, uint8_t val) {
    assembler_output_target *target = (assembler_output_target *)user;

    if (target->write_memory) {
        apple2_write_in_view(target->machine, target->view, addr, val);
        runtime_assembler_note_memory_byte(target->stats, addr);
    }
    if (target->write_file && target->ram != NULL) {
        target->ram[addr] = val;
        if (!target->wrote_file_any) {
            target->wrote_file_any = 1;
            target->file_lo = addr;
            target->file_hi = (uint32_t)addr + 1u;
        } else {
            if (addr < target->file_lo) {
                target->file_lo = addr;
            }
            if ((uint32_t)addr + 1u > target->file_hi) {
                target->file_hi = (uint32_t)addr + 1u;
            }
        }
    }
}

static bool runtime_assembler_dest_is(
    const char *text,
    int length,
    const char *name) {
    size_t name_length = strlen(name);
    int i;

    if ((size_t)length != name_length) {
        return false;
    }
    for (i = 0; i < length; i++) {
        if (tolower((unsigned char)text[i]) !=
            tolower((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}

static bool runtime_assembler_parse_destination(
    const char *dest,
    int dest_len,
    view_flags_t *out_view) {
    int offset = 0;
    view_flags_t view = 0;

    while (offset < dest_len) {
        int start;
        int end;

        while (offset < dest_len &&
               (dest[offset] == ' ' || dest[offset] == '\t')) {
            offset++;
        }
        start = offset;
        while (offset < dest_len && dest[offset] != ',') {
            offset++;
        }
        end = offset;
        while (end > start &&
               (dest[end - 1] == ' ' || dest[end - 1] == '\t')) {
            end--;
        }

        if (runtime_assembler_dest_is(dest + start, end - start, "map")) {
            view = view_flags_from_area(RUNTIME_VIEW_AREA_MAP);
        } else if (runtime_assembler_dest_is(dest + start, end - start, "main")) {
            vf_set_ram(&view, A2SEL48K_MAIN);
        } else if (runtime_assembler_dest_is(dest + start, end - start, "aux")) {
            vf_set_ram(&view, A2SEL48K_AUX);
        } else if (runtime_assembler_dest_is(dest + start, end - start, "lc1")) {
            vf_set_d000(&view, A2SELD000_LC_B1);
        } else if (runtime_assembler_dest_is(dest + start, end - start, "lc2")) {
            vf_set_d000(&view, A2SELD000_LC_B2);
        } else {
            return false;
        }
        if (offset < dest_len) {
            offset++;
        }
    }
    *out_view = view;
    return true;
}

static void *runtime_assembler_target_open(
    void *user,
    const char *name,
    int name_len,
    const char *file,
    int file_len,
    const char *dest,
    int dest_len) {
    assembler_host_ctx *host = (assembler_host_ctx *)user;
    assembler_output_target *target;
    bool write_memory = dest != NULL && dest_len > 0;
    bool write_file = file != NULL && file_len > 0;
    view_flags_t view = 0;
    char resolved[RUNTIME_ASSEMBLER_PATH_MAX];

    (void)name;
    (void)name_len;

    if (!write_memory && !write_file) {
        return NULL;
    }
    if (write_memory &&
        !runtime_assembler_parse_destination(dest, dest_len, &view)) {
        return NULL;
    }
    if (write_file &&
        !runtime_assembler_resolve_output_path(
            host->source_path, file, file_len, resolved, sizeof(resolved))) {
        return NULL;
    }

    target = (assembler_output_target *)calloc(1, sizeof(*target));
    if (!target) {
        return NULL;
    }
    target->machine = host->machine;
    target->view = view;
    target->write_memory = write_memory;
    target->write_file = write_file;
    target->stats = host->stats;

    if (write_file) {
        target->file_path = strdup(resolved);
        target->ram = (uint8_t *)calloc(1, 65536u);
        if (target->file_path == NULL || target->ram == NULL) {
            free(target->file_path);
            free(target->ram);
            free(target);
            return NULL;
        }
    }
    return target;
}

static void runtime_assembler_target_release(void *user, void *target) {
    assembler_output_target *output = (assembler_output_target *)target;

    (void)user;
    if (output == NULL) {
        return;
    }
    free(output->file_path);
    free(output->ram);
    free(output);
}

static void runtime_assembler_rescan_hostfs_for_path(
    apple2_t *machine,
    const char *path) {
    int slot;
    int device;

    if (machine == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    for (slot = 1; slot <= 7; slot++) {
        SP_DEVICE *spd = &machine->sp_device[slot];
        for (device = 0; device <= 1; device++) {
            const char *root;
            size_t root_len;

            if (spd->backend[device] != SP_BACKEND_HOSTFS ||
                spd->hostfs[device] == NULL) {
                continue;
            }
            root = hostfs_root_path(spd->hostfs[device]);
            if (root == NULL || root[0] == '\0') {
                continue;
            }
            root_len = strlen(root);
            while (root_len > 1 &&
                   (root[root_len - 1u] == '/' || root[root_len - 1u] == '\\')) {
                root_len--;
            }
            if (strncmp(path, root, root_len) == 0 &&
                (path[root_len] == '\0' ||
                 path[root_len] == '/' ||
                 path[root_len] == '\\')) {
                (void)hostfs_rescan(spd->hostfs[device]);
            }
        }
    }
}

static bool runtime_assembler_flush_file_target(
    assembler_output_target *target,
    char *error,
    size_t error_size) {
    FILE *fp;
    size_t length;
    size_t written;

    if (target == NULL || !target->write_file || target->file_path == NULL) {
        return true;
    }
    if (!target->wrote_file_any || target->ram == NULL) {
        return true;
    }

    fp = fopen(target->file_path, "wb");
    if (fp == NULL) {
        if (error != NULL && error_size > 0) {
            snprintf(
                error,
                error_size,
                "could not write assembler output file %s",
                target->file_path);
        }
        return false;
    }
    length = (size_t)(target->file_hi - target->file_lo);
    written = fwrite(&target->ram[target->file_lo], 1, length, fp);
    fclose(fp);
    if (written != length) {
        if (error != NULL && error_size > 0) {
            snprintf(
                error,
                error_size,
                "short write to assembler output file %s",
                target->file_path);
        }
        return false;
    }
    runtime_assembler_rescan_hostfs_for_path(target->machine, target->file_path);
    return true;
}

static bool runtime_assembler_flush_files(
    ASSEMBLER *assembler,
    char *error,
    size_t error_size) {
    size_t i;

    if (assembler == NULL) {
        return false;
    }
    /* Index 0 is the host-owned default target; named redirects start at 1. */
    for (i = 1; i < assembler->targets.items; i++) {
        TARGET *target = *AM65_ARRAY_GET(&assembler->targets, TARGET *, i);
        assembler_output_target *output;

        if (target == NULL || target->ctx == NULL) {
            continue;
        }
        output = (assembler_output_target *)target->ctx;
        if (!runtime_assembler_flush_file_target(output, error, error_size)) {
            return false;
        }
    }
    return true;
}

static void runtime_assembler_import_symbol(const char *name, uint16_t address, void *user) {
    assembler_symbol_import *import = (assembler_symbol_import *)user;

    if (symbol_table_add(
            import->symbols,
            address,
            name,
            SYMBOL_SOURCE_ASSEMBLER,
            import->source_name,
            true) == SYMBOL_OUT_OF_MEMORY) {
        import->ok = false;
    }
}

static void runtime_assembler_format_errors(const ERRORLOG *log, char *error, size_t error_size) {
    size_t written = 0;

    if (error == NULL || error_size == 0) {
        return;
    }
    error[0] = '\0';

    if (log == NULL || log->log_array.items == 0) {
        snprintf(error, error_size, "assembly failed");
        return;
    }

    for (size_t i = 0; i < log->log_array.items; i++) {
        const ERROR_ENTRY *entry = AM65_ARRAY_GET(
            (AM65_DYNARRAY *)&log->log_array, ERROR_ENTRY, i);
        int n;

        if (entry == NULL || entry->err_str == NULL) {
            continue;
        }
        n = snprintf(
            error + written,
            written < error_size ? error_size - written : 0,
            "%s%s",
            written > 0 ? "\n" : "",
            entry->err_str);
        if (n < 0) {
            return;
        }
        written += (size_t)n;
        if (written >= error_size) {
            error[error_size - 1] = '\0';
            return;
        }
    }

    if (written == 0) {
        snprintf(error, error_size, "assembly failed");
    }
}

static void runtime_assembler_format_adjustment(
    size_t target_index,
    const char *segment_name,
    uint16_t address,
    void *user) {
    assembler_adjustment_format *format =
        (assembler_adjustment_format *)user;
    const char *name = segment_name && segment_name[0] ?
        segment_name : "<default>";
    int n;

    if (format->buffer == NULL || format->size == 0 ||
        format->written >= format->size) {
        return;
    }
    if (!format->has_adjustments) {
        n = snprintf(
            format->buffer + format->written,
            format->size - format->written,
            "Assembly succeeded with adjusted segment addresses; update the source to:");
        if (n < 0) {
            return;
        }
        format->written += (size_t)n;
        format->has_adjustments = true;
    }
    if (format->written >= format->size) {
        format->buffer[format->size - 1] = '\0';
        return;
    }
    if (target_index == 0) {
        n = snprintf(
            format->buffer + format->written,
            format->size - format->written,
            "\n  Suggest \"%s\" at $%04X",
            name,
            address);
    } else {
        n = snprintf(
            format->buffer + format->written,
            format->size - format->written,
            "\n  Target %zu: Suggest \"%s\" at $%04X",
            target_index,
            name,
            address);
    }
    if (n < 0) {
        return;
    }
    format->written += (size_t)n;
    if (format->written >= format->size) {
        format->buffer[format->size - 1] = '\0';
    }
}

static bool runtime_assemble_file_legacy_ex(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    const runtime_assembler_options *options,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *notice,
    size_t notice_size,
    char *error,
    size_t error_size) {
    ERRORLOG log;
    ASSEMBLER assembler;
    assembler_output_stats output_stats;
    assembler_output_target default_target;
    assembler_host_ctx host_ctx;
    CB_ASM_CTX cb;
    bool ok = false;
    static const char *const destination_names[] = {
        "map", "main", "aux", "lc1", "lc2"
    };

    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (notice != NULL && notice_size > 0) {
        notice[0] = '\0';
    }
    if (machine == NULL || path == NULL || path[0] == '\0') {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "invalid assembler input");
        }
        return false;
    }

    errlog_init(&log);
    memset(&output_stats, 0, sizeof(output_stats));
    memset(&default_target, 0, sizeof(default_target));
    default_target.machine = (apple2_t *)machine;
    default_target.view = view_flags_from_area(RUNTIME_VIEW_AREA_MAP);
    default_target.write_memory = true;
    default_target.stats = &output_stats;
    memset(&host_ctx, 0, sizeof(host_ctx));
    host_ctx.machine = (apple2_t *)machine;
    host_ctx.stats = &output_stats;
    host_ctx.source_path = path;
    memset(&cb, 0, sizeof(cb));
    cb.user = &host_ctx;
    cb.default_target = &default_target;
    cb.output_byte = runtime_assembler_output_byte;
    cb.target_open = runtime_assembler_target_open;
    cb.target_release = runtime_assembler_target_release;
    cb.destination_names = destination_names;
    cb.destination_name_count =
        sizeof(destination_names) / sizeof(destination_names[0]);

    if (assembler_init(&assembler, &log, &cb) != ASM_OK) {
        if (error != NULL && error_size > 0) {
            snprintf(error, error_size, "assembler initialization failed");
        }
        errlog_shutdown(&log);
        return false;
    }

    // Let source detect it is being assembled live in the emulator.
    assembler_predefine(&assembler, "AM65", "0");
    assembler_predefine(&assembler, "APPLE2", "1");
    assembler_set_cpu_profile(
        &assembler,
        options != NULL && options->enable_65c02 ?
            ASM_CPU_65C02 : ASM_CPU_6502);
    assembler_set_auto_adjust_segments(
        &assembler,
        options != NULL && options->auto_adjust_segments);

    if (assembler_assemble(&assembler, path, address) == ASM_OK) {
        ok = true;
        if (!runtime_assembler_flush_files(&assembler, error, error_size)) {
            ok = false;
        }
        if (ok && symbols != NULL) {
            assembler_symbol_import import;
            import.symbols = symbols;
            import.source_name = source_name != NULL && source_name[0] != '\0' ? source_name : path;
            import.ok = true;
            symbol_table_remove_kind(symbols, SYMBOL_SOURCE_ASSEMBLER);
            assembler_walk_symbols(&assembler, runtime_assembler_import_symbol, &import);
            if (!import.ok) {
                ok = false;
                if (error != NULL && error_size > 0) {
                    snprintf(error, error_size, "failed to import assembler symbols");
                }
            }
        }
        if (ok && notice != NULL && notice_size > 0) {
            assembler_adjustment_format format;
            memset(&format, 0, sizeof(format));
            format.buffer = notice;
            format.size = notice_size;
            assembler_walk_segment_adjustments(
                &assembler,
                runtime_assembler_format_adjustment,
                &format);
        }
    } else {
        runtime_assembler_format_errors(&log, error, error_size);
    }

    assembler_shutdown(&assembler);
    errlog_shutdown(&log);
    if (ok) {
        if (out_start_address != NULL) {
            *out_start_address =
                output_stats.has_output ? output_stats.first_address : address;
        }
        if (out_end_address != NULL) {
            *out_end_address = output_stats.has_output
                ? (uint16_t)(output_stats.last_address + 1u)
                : address;
        }
        if (out_byte_count != NULL) {
            *out_byte_count = output_stats.byte_count;
        }
    }
    return ok;
}

bool runtime_assemble_file_legacy(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size) {
    return runtime_assemble_file_legacy_ex(
        machine,
        symbols,
        path,
        address,
        source_name,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        error,
        error_size);
}

bool runtime_assemble_file(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    char *error,
    size_t error_size) {
    return runtime_assemble_file_legacy(machine, symbols, path, address, source_name, error, error_size);
}

bool runtime_assemble_file_ex(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *error,
    size_t error_size) {
    return runtime_assemble_file_legacy_ex(
        machine,
        symbols,
        path,
        address,
        source_name,
        NULL,
        out_start_address,
        out_end_address,
        out_byte_count,
        NULL,
        0,
        error,
        error_size);
}

bool runtime_assemble_file_ex_options(
    void *machine,
    symbol_table *symbols,
    const char *path,
    uint16_t address,
    const char *source_name,
    const runtime_assembler_options *options,
    uint16_t *out_start_address,
    uint16_t *out_end_address,
    uint32_t *out_byte_count,
    char *notice,
    size_t notice_size,
    char *error,
    size_t error_size) {
    return runtime_assemble_file_legacy_ex(
        machine,
        symbols,
        path,
        address,
        source_name,
        options,
        out_start_address,
        out_end_address,
        out_byte_count,
        notice,
        notice_size,
        error,
        error_size);
}
