#include "apple2_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_naps(void)
{
    char path[128];
    uint8_t type;
    uint16_t aux;
    assert(apple2_naps_make_path("demo.bin", 0x06u, 0x2000u, path, sizeof(path)));
    assert(strcmp(path, "demo.bin#062000") == 0);
    assert(apple2_naps_parse_path(path, &type, &aux));
    assert(type == 0x06u && aux == 0x2000u);
    assert(apple2_naps_make_path(path, 0x06u, 0x0803u, path, sizeof(path)));
    assert(strcmp(path, "demo.bin#060803") == 0);
    assert(!apple2_naps_parse_path("demo#xyz123", NULL, NULL));
}

static void test_binary_formats(void)
{
    static const uint8_t payload[] = {0xa9u, 0x01u, 0x60u};
    uint8_t *single = NULL;
    size_t single_size = 0u;
    apple2_binary_view view;
    char error[128] = {0};
    uint8_t legacy[] = {0x00u, 0x20u, 0x03u, 0x00u, 0xa9u, 0x01u, 0x60u};
    bool ok;

    assert(apple2_binary_prodos_type_is_loadable(0x06u));
    assert(apple2_binary_prodos_type_is_loadable(0xffu));
    assert(!apple2_binary_prodos_type_is_loadable(0x04u));
    assert(!apple2_binary_prodos_type_is_loadable(0xfcu));

    /* Keep encode/decode outside assert(): Release builds define NDEBUG. */
    ok = apple2_applesingle_encode_bin(payload, sizeof(payload), 0x0803u, &single, &single_size);
    assert(ok);
    assert(single != NULL && single_size == 58u + sizeof(payload));
    ok = apple2_binary_decode(
        "demo.as", single, single_size, APPLE2_BINARY_FORMAT_AUTO, 0u,
        &view, error, sizeof(error));
    assert(ok);
    assert(view.format == APPLE2_BINARY_FORMAT_APPLESINGLE);
    assert(view.load_address == 0x0803u);
    assert(view.size == sizeof(payload));
    assert(view.has_prodos_type && view.prodos_type == 0x06u);
    assert(memcmp(view.data, payload, sizeof(payload)) == 0);

    /* AppleSingle SYS ($FF) with load address in aux. */
    single[52] = 0x00u;
    single[53] = 0xffu;
    single[54] = 0x00u;
    single[55] = 0x00u;
    single[56] = 0x20u;
    single[57] = 0x00u;
    ok = apple2_binary_decode(
        "demo.system.as", single, single_size, APPLE2_BINARY_FORMAT_AUTO, 0u,
        &view, error, sizeof(error));
    assert(ok);
    assert(view.format == APPLE2_BINARY_FORMAT_APPLESINGLE);
    assert(view.load_address == 0x2000u);
    assert(view.has_prodos_type && view.prodos_type == 0xffu);
    free(single);

    ok = apple2_binary_decode(
        "demo#062000", payload, sizeof(payload), APPLE2_BINARY_FORMAT_AUTO, 0u,
        &view, error, sizeof(error));
    assert(ok);
    assert(view.format == APPLE2_BINARY_FORMAT_NAPS && view.load_address == 0x2000u);
    assert(view.has_prodos_type && view.prodos_type == 0x06u);

    ok = apple2_binary_decode(
        "chess.system#ff2000", payload, sizeof(payload), APPLE2_BINARY_FORMAT_AUTO, 0u,
        &view, error, sizeof(error));
    assert(ok);
    assert(view.format == APPLE2_BINARY_FORMAT_NAPS && view.load_address == 0x2000u);
    assert(view.has_prodos_type && view.prodos_type == 0xffu);

    ok = apple2_binary_decode(
        "notes#040000", payload, sizeof(payload), APPLE2_BINARY_FORMAT_AUTO, 0u,
        &view, error, sizeof(error));
    assert(!ok);

    ok = apple2_binary_decode(
        "old.bin", legacy, sizeof(legacy), APPLE2_BINARY_FORMAT_AUTO, 0u,
        &view, error, sizeof(error));
    assert(ok);
    assert(view.format == APPLE2_BINARY_FORMAT_LEGACY_DOS && view.load_address == 0x2000u);
    assert(view.size == 3u && view.data[0] == 0xa9u);

    ok = apple2_binary_decode(
        "raw.bin", payload, sizeof(payload), APPLE2_BINARY_FORMAT_RAW, 0x3000u,
        &view, error, sizeof(error));
    assert(ok);
    assert(view.format == APPLE2_BINARY_FORMAT_RAW && view.load_address == 0x3000u);
}

static void test_applesoft(void)
{
    static const uint8_t listing[] =
        "20 PRINT \"GOTO\"\r\n"
        "10 REM GOTO IS LITERAL\n"
        "30 DATA 1,GOTO:PRINT 2\n";
    static const char expected[] =
        "10 REM GOTO IS LITERAL\n"
        "20 PRINT \"GOTO\"\n"
        "30 DATA 1,GOTO:PRINT 2\n";
    uint8_t *program = NULL;
    size_t program_size = 0u;
    uint8_t *text = NULL;
    size_t text_size = 0u;
    char error[128] = {0};

    assert(apple2_applesoft_tokenize(
        listing, sizeof(listing) - 1u, &program, &program_size, error, sizeof(error)));
    assert(program_size > 2u);
    assert(program[4] == 0xb2u); /* REM */
    assert(apple2_applesoft_detokenize(
        program, program_size, &text, &text_size, error, sizeof(error)));
    assert(text_size == sizeof(expected) - 1u);
    assert(memcmp(text, expected, text_size) == 0);
    free(text);
    free(program);

    {
        static const uint8_t live_program[] = {
            0x0b, 0x08, 0x0a, 0x00, 0xba, 0x22, 0x48, 0x49, 0x22, 0x00,
            0x00, 0x00, 0xaa, 0xbb
        };
        static const char live_expected[] = "10 PRINT\"HI\"\n";
        assert(apple2_applesoft_detokenize(
            live_program, sizeof(live_program), &text, &text_size,
            error, sizeof(error)));
        assert(text_size == sizeof(live_expected) - 1u);
        assert(memcmp(text, live_expected, text_size) == 0);
        free(text);
    }

    assert(!apple2_applesoft_tokenize(
        (const uint8_t *)"10 PRINT 1\n10 PRINT 2\n", 24u,
        &program, &program_size, error, sizeof(error)));
}

int main(void)
{
    test_naps();
    test_binary_formats();
    test_applesoft();
    puts("apple2 file format tests passed");
    return 0;
}
