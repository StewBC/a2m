// Tests for .search / assembler_add_search_dir include resolution.
// Stefan Wessels, 2026
// This is free and unencumbered software released into the public domain.

#include "asm.h"
#include "errorlog.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define mkdir_one(path) _mkdir(path)
#else
#include <unistd.h>
#define mkdir_one(path) mkdir((path), 0700)
#endif

typedef struct {
    uint8_t memory[65536];
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
}

static int write_file(const char *path, const char *source)
{
    FILE *fp = fopen(path, "w");
    if(!fp) {
        fprintf(stderr, "fopen %s: %s\n", path, strerror(errno));
        return 1;
    }
    fputs(source, fp);
    if(fclose(fp) != 0) {
        fprintf(stderr, "fclose %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

static void remove_tree(const char *root)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/root/main.asm", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/root/local.inc", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/root/before.inc", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/shared/from_search.inc", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/shared/local.inc", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/shared/seed_only.inc", root);
    remove(path);
    snprintf(path, sizeof(path), "%s/root", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/shared", root);
    rmdir(path);
    rmdir(root);
}

static int make_workspace(char *root, size_t root_size)
{
#if defined(_WIN32)
    char temp_dir[MAX_PATH];
    char temp_path[MAX_PATH];
    if(GetTempPathA((DWORD)sizeof(temp_dir), temp_dir) == 0 ||
       GetTempFileNameA(temp_dir, "a2msrch", 0, temp_path) == 0 ||
       strlen(temp_path) + 1 > root_size) {
        fprintf(stderr, "failed to create temporary directory path\n");
        return 1;
    }
    remove(temp_path);
    if(mkdir_one(temp_path) != 0) {
        fprintf(stderr, "mkdir %s failed\n", temp_path);
        return 1;
    }
    snprintf(root, root_size, "%s", temp_path);
#else
    snprintf(root, root_size, "/tmp/a2m_assembler_search_XXXXXX");
    if(!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
#endif

    char path[512];
    snprintf(path, sizeof(path), "%s/root", root);
    if(mkdir_one(path) != 0) {
        fprintf(stderr, "mkdir %s failed\n", path);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/shared", root);
    if(mkdir_one(path) != 0) {
        fprintf(stderr, "mkdir %s failed\n", path);
        return 1;
    }

    snprintf(path, sizeof(path), "%s/root/local.inc", root);
    if(write_file(path, "VAL = $11\n") != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/shared/local.inc", root);
    if(write_file(path, "VAL = $22\n") != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/shared/from_search.inc", root);
    if(write_file(path, "VAL = $33\n") != 0) {
        return 1;
    }
    snprintf(path, sizeof(path), "%s/shared/seed_only.inc", root);
    if(write_file(path, "VAL = $44\n") != 0) {
        return 1;
    }
    return 0;
}

static void print_errors(ERRORLOG *log)
{
    for(size_t i = 0; i < log->log_array.items; i++) {
        ERROR_ENTRY *e = AM65_ARRAY_GET(&log->log_array, ERROR_ENTRY, i);
        if(e && e->err_str) {
            fprintf(stderr, "  %s\n", e->err_str);
        }
    }
}

static int assemble_path(const char *path, test_memory *mem, ERRORLOG *log,
                         const char *seed_dir)
{
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    memset(mem, 0, sizeof(*mem));
    memset(&cb, 0, sizeof(cb));
    cb.user = mem;
    cb.output_byte = output_byte;

    errlog_init(log);
    if(assembler_init(&as, log, &cb) != ASM_OK) {
        fprintf(stderr, "assembler_init failed\n");
        return ASM_ERR;
    }
    if(seed_dir) {
        if(assembler_add_search_dir(&as, seed_dir) != ASM_OK) {
            fprintf(stderr, "assembler_add_search_dir failed for %s\n", seed_dir);
            print_errors(log);
            assembler_shutdown(&as);
            return ASM_ERR;
        }
    }
    result = assembler_assemble(&as, path, 0x0800);
    assembler_shutdown(&as);
    return result;
}

static int expect_lda_imm(const test_memory *mem, uint8_t imm, const char *label)
{
    if(mem->memory[0x0800] != 0xA9 || mem->memory[0x0801] != imm) {
        fprintf(stderr, "FAIL %s: expected lda #$%02X at $0800, got %02X %02X\n",
                label, imm, mem->memory[0x0800], mem->memory[0x0801]);
        return 1;
    }
    return 0;
}

/* .search finds an include that is not beside the including file. */
static int test_dot_search_finds_include(const char *root)
{
    char main_path[512];
    char source[512];
    test_memory mem;
    ERRORLOG log;
    int result;
    int failures = 0;

    snprintf(main_path, sizeof(main_path), "%s/root/main.asm", root);
    snprintf(source, sizeof(source),
             ".search \"../shared\"\n"
             ".include \"from_search.inc\"\n"
             "    lda #VAL\n");
    if(write_file(main_path, source) != 0) {
        return 1;
    }

    result = assemble_path(main_path, &mem, &log, NULL);
    if(result != ASM_OK) {
        fprintf(stderr, "FAIL .search find: assemble failed\n");
        print_errors(&log);
        failures++;
    } else {
        failures += expect_lda_imm(&mem, 0x33, ".search find");
    }
    errlog_shutdown(&log);
    remove(main_path);
    return failures;
}

/* Local file beside the includer wins over the same name on the search path. */
static int test_local_beats_search(const char *root)
{
    char main_path[512];
    char source[512];
    test_memory mem;
    ERRORLOG log;
    int result;
    int failures = 0;

    snprintf(main_path, sizeof(main_path), "%s/root/main.asm", root);
    snprintf(source, sizeof(source),
             ".search \"../shared\"\n"
             ".include \"local.inc\"\n"
             "    lda #VAL\n");
    if(write_file(main_path, source) != 0) {
        return 1;
    }

    result = assemble_path(main_path, &mem, &log, NULL);
    if(result != ASM_OK) {
        fprintf(stderr, "FAIL local beats search: assemble failed\n");
        print_errors(&log);
        failures++;
    } else {
        failures += expect_lda_imm(&mem, 0x11, "local beats search");
    }
    errlog_shutdown(&log);
    remove(main_path);
    return failures;
}

/* assembler_add_search_dir (CLI -I / host seed) finds an include. */
static int test_seed_search_dir(const char *root)
{
    char main_path[512];
    char shared[512];
    test_memory mem;
    ERRORLOG log;
    int result;
    int failures = 0;

    snprintf(main_path, sizeof(main_path), "%s/root/main.asm", root);
    snprintf(shared, sizeof(shared), "%s/shared", root);
    if(write_file(main_path,
                  ".include \"seed_only.inc\"\n"
                  "    lda #VAL\n") != 0) {
        return 1;
    }

    result = assemble_path(main_path, &mem, &log, shared);
    if(result != ASM_OK) {
        fprintf(stderr, "FAIL seed search: assemble failed\n");
        print_errors(&log);
        failures++;
    } else {
        failures += expect_lda_imm(&mem, 0x44, "seed search");
    }
    errlog_shutdown(&log);
    remove(main_path);
    return failures;
}

/* Without .search / -I, a missing local include fails. */
static int test_missing_without_search(const char *root)
{
    char main_path[512];
    test_memory mem;
    ERRORLOG log;
    int result;
    int failures = 0;

    snprintf(main_path, sizeof(main_path), "%s/root/main.asm", root);
    if(write_file(main_path,
                  ".include \"from_search.inc\"\n"
                  "    lda #VAL\n") != 0) {
        return 1;
    }

    result = assemble_path(main_path, &mem, &log, NULL);
    if(result == ASM_OK) {
        fprintf(stderr, "FAIL missing without search: expected assemble failure\n");
        failures++;
    }
    errlog_shutdown(&log);
    remove(main_path);
    return failures;
}

/* .search only affects includes that appear after it. */
static int test_search_order(const char *root)
{
    char main_path[512];
    char source[512];
    test_memory mem;
    ERRORLOG log;
    int result;
    int failures = 0;

    snprintf(main_path, sizeof(main_path), "%s/root/main.asm", root);
    snprintf(source, sizeof(source),
             ".include \"from_search.inc\"\n"
             ".search \"../shared\"\n"
             "    lda #0\n");
    if(write_file(main_path, source) != 0) {
        return 1;
    }

    result = assemble_path(main_path, &mem, &log, NULL);
    if(result == ASM_OK) {
        fprintf(stderr, "FAIL search order: include before .search should fail\n");
        failures++;
    }
    errlog_shutdown(&log);
    remove(main_path);
    return failures;
}

int main(void)
{
    char root[512];
    int failures = 0;

    if(make_workspace(root, sizeof(root)) != 0) {
        return 1;
    }

    failures += test_dot_search_finds_include(root);
    failures += test_local_beats_search(root);
    failures += test_seed_search_dir(root);
    failures += test_missing_without_search(root);
    failures += test_search_order(root);

    remove_tree(root);

    if(failures != 0) {
        fprintf(stderr, "%d search-path test failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
