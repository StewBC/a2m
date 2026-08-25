#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_true(const char *name, bool condition)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void expect_streq(const char *name, const char *expected, const char *actual)
{
    if (expected == NULL || actual == NULL || strcmp(expected, actual) != 0) {
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n",
                name,
                expected != NULL ? expected : "(null)",
                actual != NULL ? actual : "(null)");
        exit(1);
    }
}

static char *read_file(const char *path)
{
    FILE *file;
    long size;
    char *buffer;
    size_t nread;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    nread = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[nread] = '\0';
    return buffer;
}

static int count_substring(const char *haystack, const char *needle)
{
    int count = 0;
    const char *p = haystack;
    size_t needle_len;

    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    needle_len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }
    return count;
}

/* Interleaved config_set calls must still write each section once. */
static void test_save_consolidates_sections(const char *path)
{
    config *cfg;
    char *body;

    cfg = config_load(NULL);
    expect_true("load empty", cfg != NULL);

    config_set(cfg, "config", "turbo_speeds", "1,max");
    config_set(cfg, "debug", "history_memory_mb", "256");
    config_set(cfg, "config", "history_off_on_max", "true");
    config_set(cfg, "assembler", "address", "8000");
    config_set(cfg, "debug", "inspector", "true");
    config_set(cfg, "assembler", "file", "samples/mminer/mminer-a2m.asm");
    config_set(cfg, "config", "symbol_files", "");

    expect_true("save", config_save(cfg, path));
    config_destroy(cfg);

    body = read_file(path);
    expect_true("read saved", body != NULL);
    expect_true("[config] once", count_substring(body, "[config]") == 1);
    expect_true("[debug] once", count_substring(body, "[debug]") == 1);
    expect_true("[assembler] once", count_substring(body, "[assembler]") == 1);

    /* Section order follows first appearance; keys keep relative order. */
    expect_true(
        "order config before debug",
        strstr(body, "[config]") < strstr(body, "[debug]"));
    expect_true(
        "order debug before assembler",
        strstr(body, "[debug]") < strstr(body, "[assembler]"));
    expect_true(
        "turbo before history_off",
        strstr(body, "turbo_speeds=") < strstr(body, "history_off_on_max="));
    expect_true(
        "history_off before symbol_files",
        strstr(body, "history_off_on_max=") < strstr(body, "symbol_files="));

    free(body);

    cfg = config_load(path);
    expect_true("reload", cfg != NULL);
    expect_streq("turbo", "1,max", config_get(cfg, "config", "turbo_speeds"));
    expect_streq("history_off", "true", config_get(cfg, "config", "history_off_on_max"));
    expect_streq("symbols", "", config_get(cfg, "config", "symbol_files"));
    expect_streq("history_mb", "256", config_get(cfg, "debug", "history_memory_mb"));
    expect_streq("inspector", "true", config_get(cfg, "debug", "inspector"));
    expect_streq("asm addr", "8000", config_get(cfg, "assembler", "address"));
    expect_streq(
        "asm file",
        "samples/mminer/mminer-a2m.asm",
        config_get(cfg, "assembler", "file"));
    config_destroy(cfg);
}

/* Loading an already-fragmented INI, then saving, must collapse sections. */
static void test_load_fragmented_then_save(const char *src, const char *dst)
{
    config *cfg;
    FILE *file;
    char *body;

    file = fopen(src, "w");
    expect_true("write fragmented", file != NULL);
    fputs(
        "[config]\n"
        "turbo_speeds=1,max\n"
        "\n"
        "[debug]\n"
        "history_memory_mb=256\n"
        "\n"
        "[config]\n"
        "history_off_on_max=true\n"
        "\n"
        "[assembler]\n"
        "address=8000\n"
        "\n"
        "[config]\n"
        "symbol_files=\n"
        "\n"
        "[assembler]\n"
        "file=foo.asm\n",
        file);
    expect_true("close fragmented", fclose(file) == 0);

    cfg = config_load(src);
    expect_true("load fragmented", cfg != NULL);
    expect_true("save consolidated", config_save(cfg, dst));
    config_destroy(cfg);

    body = read_file(dst);
    expect_true("read consolidated", body != NULL);
    expect_true("[config] once after load/save", count_substring(body, "[config]") == 1);
    expect_true("[debug] once after load/save", count_substring(body, "[debug]") == 1);
    expect_true("[assembler] once after load/save", count_substring(body, "[assembler]") == 1);
    free(body);
}

int main(void)
{
    char path_a[256];
    char path_b[256];
    char path_c[256];
    long stamp = (long)time(NULL);

    snprintf(path_a, sizeof(path_a), "a2m_config_test_a_%ld.tmp", stamp);
    snprintf(path_b, sizeof(path_b), "a2m_config_test_b_%ld.tmp", stamp);
    snprintf(path_c, sizeof(path_c), "a2m_config_test_c_%ld.tmp", stamp);

    test_save_consolidates_sections(path_a);
    test_load_fragmented_then_save(path_b, path_c);

    remove(path_a);
    remove(path_b);
    remove(path_c);

    printf("config: all tests passed\n");
    return 0;
}
