/* HostFS — ProDOS volume backed by a host directory (SmartPort media).
   Stefan Wessels, 2026. Public domain. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOSTFS_BLOCK_SIZE 512u
#define HOSTFS_TOTAL_BLOCKS 65535u
#define HOSTFS_MAX_FILES 256
#define HOSTFS_PATH_MAX 1024
#define HOSTFS_NAME_MAX 16 /* ProDOS name + NUL */

typedef struct hostfs_volume hostfs_volume;

/* True if path exists and is a directory. */
bool hostfs_path_is_dir(const char *path);

/* Build a read-only ProDOS map for root_path. volume_name is typically
   HOSTFS.SNdM (slot/unit). Returns NULL on failure. */
hostfs_volume *hostfs_mount(const char *root_path, const char *volume_name);

void hostfs_eject(hostfs_volume *vol);

uint16_t hostfs_total_blocks(const hostfs_volume *vol);
const char *hostfs_root_path(const hostfs_volume *vol);
const char *hostfs_volume_name(const hostfs_volume *vol);
int hostfs_file_count(const hostfs_volume *vol);

/* Fill out[512]. Returns 0 on success, non-zero on I/O error. */
int hostfs_read_block(hostfs_volume *vol, uint32_t block, uint8_t *out);

/* Test helpers (also used by unit tests). */
bool hostfs_naps_parse_name(
    const char *filename,
    char *prodos_name,
    size_t prodos_name_size,
    uint8_t *file_type,
    uint16_t *aux_type);
bool hostfs_mangle_prodos_name(const char *stem, char *out, size_t out_size);
