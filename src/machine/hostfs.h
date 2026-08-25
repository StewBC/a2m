/* HostFS — ProDOS volume backed by a host directory (SmartPort media).
   Stefan Wessels, 2026. Public domain. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOSTFS_BLOCK_SIZE 512u
#define HOSTFS_TOTAL_BLOCKS 65535u
/* Ceiling on nodes (files + directories) in a volume. Architectural, not a
   budget: ProDOS file_count is 16-bit and a 32 MB volume is 65535 blocks, so no
   legal volume can hold more. The table is grown on demand. */
#define HOSTFS_MAX_NODES 65535
#define HOSTFS_MAX_DEPTH 8
#define HOSTFS_PATH_MAX 1024
#define HOSTFS_NAME_MAX 16 /* ProDOS name + NUL */

/* ProDOS directory format. A 512-byte block is 4 bytes of prev/next plus
   13 entries of 39. The volume directory is always blocks 2-5 and cannot be
   extended, and its first entry is the volume header, so a HostFS root holds
   51 files. Subdirectories grow by linking blocks and have no such limit. */
#define HOSTFS_ENTRY_LENGTH 39
#define HOSTFS_ENTRIES_PER_BLOCK 13
#define HOSTFS_ROOT_DIR_BLOCKS 4
#define HOSTFS_ROOT_MAX_ENTRIES (HOSTFS_ROOT_DIR_BLOCKS * HOSTFS_ENTRIES_PER_BLOCK - 1)
/* Optional catalog-order manifest in a HostFS directory (not mounted as a file). */
#define HOSTFS_ORDER_FILENAME "hostfs.order"

typedef struct hostfs_volume hostfs_volume;
struct apple2;

typedef struct hostfs_memory_stats {
    size_t node_table_bytes;
    size_t name_arena_bytes;
    size_t block_index_bytes;
    size_t directory_block_bytes;
} hostfs_memory_stats;

/* Bind the owning Apple + slot/device so media events can reach Inspector. */
void hostfs_bind_apple(hostfs_volume *vol, struct apple2 *m, int slot, int device);

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
 * Drain native filesystem invalidations on a SmartPort touch (STATUS / READ /
 * WRITE) and reconcile only the affected file or immediate directory. When
 * notifications are unavailable, falls back to the legacy rate-limited full
 * rescan. Skipped while guest write-through or sealed replay is active.
 */
void hostfs_maybe_refresh(hostfs_volume *vol);

/* Force an unconditional recursive host rescan now (still skips during guest write). */
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
/* Diagnostics fired on this volume since mount (see hostfs_warn). */
int hostfs_warning_count(const hostfs_volume *vol);
const char *hostfs_last_warning(const hostfs_volume *vol);
/* Detach native notifications so injected events are deterministic. */
void hostfs_test_use_synthetic_events(hostfs_volume *vol);
bool hostfs_test_inject_event(
    hostfs_volume *vol, uint32_t flags, const char *relative_path);
void hostfs_test_require_full_rescan(hostfs_volume *vol);
void hostfs_test_reset_refresh_counters(hostfs_volume *vol);
int hostfs_test_targeted_file_stats(const hostfs_volume *vol);
int hostfs_test_targeted_directory_scans(const hostfs_volume *vol);
int hostfs_test_full_rescans(const hostfs_volume *vol);
bool hostfs_test_using_periodic_refresh(const hostfs_volume *vol);
/* Exercise the production node-ceiling path without constructing 65536 files. */
hostfs_volume *hostfs_test_mount_with_node_limit(
    const char *root_path, const char *volume_name, int node_limit);
void hostfs_test_memory_stats(
    const hostfs_volume *vol, hostfs_memory_stats *out_stats);
