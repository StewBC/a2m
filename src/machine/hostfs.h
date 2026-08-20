/* HostFS — ProDOS volume backed by a host directory (SmartPort media).
   Stefan Wessels, 2026. Public domain. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOSTFS_BLOCK_SIZE 512u
#define HOSTFS_TOTAL_BLOCKS 65535u
#define HOSTFS_MAX_FILES 256 /* total nodes: files + directories */
#define HOSTFS_MAX_DEPTH 8
#define HOSTFS_PATH_MAX 1024
#define HOSTFS_NAME_MAX 16 /* ProDOS name + NUL */
/* Optional catalog-order manifest in a HostFS directory (not mounted as a file). */
#define HOSTFS_ORDER_FILENAME "hostfs.order"

typedef struct hostfs_volume hostfs_volume;

bool hostfs_path_is_dir(const char *path);

/* Build a ProDOS map for root_path. volume_name is typically HOSTFS.SNdM. */
hostfs_volume *hostfs_mount(const char *root_path, const char *volume_name);

void hostfs_eject(hostfs_volume *vol);

/* Flush dirty host file state (no-op if clean). */
int hostfs_flush(hostfs_volume *vol);

uint16_t hostfs_total_blocks(const hostfs_volume *vol);
const char *hostfs_root_path(const hostfs_volume *vol);
const char *hostfs_volume_name(const hostfs_volume *vol);
int hostfs_file_count(const hostfs_volume *vol);

/* Fill out[512]. Returns 0 on success, non-zero on I/O error. */
int hostfs_read_block(hostfs_volume *vol, uint32_t block, uint8_t *out);

/* Write 512 bytes. Data blocks pwrite the host file; meta stays in RAM.
   Directory writes trigger host CREATE/DESTROY/RENAME reconcile. */
int hostfs_write_block(hostfs_volume *vol, uint32_t block, const uint8_t *data);

/*
 * Phase 2: rate-limited host refresh on SmartPort touch (STATUS / READ / WRITE).
 * Wall-clock delta (~1s host time) so max turbo does not multiply rescans.
 * Skipped while guest write-through is active. Idle mounted volumes do nothing.
 */
void hostfs_maybe_refresh(hostfs_volume *vol);

/* Force a host rescan now (ignores rate limit; still skips during guest write). */
int hostfs_rescan(hostfs_volume *vol);

/* Compose host basename NAME#ttxxxx. If name is already NAPS-tagged (assembler),
   the stem is observed and type/aux from the args form the tag (no double #). */
bool hostfs_compose_naps_filename(
    const char *name,
    uint8_t file_type,
    uint16_t aux_type,
    char *out,
    size_t out_size);

/* Test helpers. */
bool hostfs_naps_parse_name(
    const char *filename,
    char *prodos_name,
    size_t prodos_name_size,
    uint8_t *file_type,
    uint16_t *aux_type);
bool hostfs_mangle_prodos_name(const char *stem, char *out, size_t out_size);
