#include "apple2_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    APPLESOFT_BASE = 0x0801,
    APPLESOFT_MAX_SOURCE_LINE = 239,
    APPLESINGLE_HEADER_SIZE = 58
};

typedef struct applesoft_keyword {
    const char *text;
    uint8_t token;
} applesoft_keyword;

static const char *const applesoft_tokens[] = {
    "END", "FOR", "NEXT", "DATA", "INPUT", "DEL", "DIM", "READ",
    "GR", "TEXT", "PR#", "IN#", "CALL", "PLOT", "HLIN", "VLIN",
    "HGR2", "HGR", "HCOLOR=", "HPLOT", "DRAW", "XDRAW", "HTAB", "HOME",
    "ROT=", "SCALE=", "SHLOAD", "TRACE", "NOTRACE", "NORMAL", "INVERSE", "FLASH",
    "COLOR=", "POP", "VTAB", "HIMEM:", "LOMEM:", "ONERR", "RESUME", "RECALL",
    "STORE", "SPEED=", "LET", "GOTO", "RUN", "IF", "RESTORE", "&",
    "GOSUB", "RETURN", "REM", "STOP", "ON", "WAIT", "LOAD", "SAVE",
    "DEF", "POKE", "PRINT", "CONT", "LIST", "CLEAR", "GET", "NEW",
    "TAB(", "TO", "FN", "SPC(", "THEN", "AT", "NOT", "STEP",
    "+", "-", "*", "/", "^", "AND", "OR", ">",
    "=", "<", "SGN", "INT", "ABS", "USR", "FRE", "SCRN(",
    "PDL", "POS", "SQR", "RND", "LOG", "EXP", "COS", "SIN",
    "TAN", "ATN", "PEEK", "LEN", "STR$", "VAL", "ASC", "CHR$",
    "LEFT$", "RIGHT$", "MID$"
};

typedef char applesoft_token_count_must_be_107[
    sizeof(applesoft_tokens) / sizeof(applesoft_tokens[0]) == 107 ? 1 : -1];

typedef struct source_line {
    unsigned number;
    const uint8_t *text;
    size_t length;
} source_line;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u) {
        snprintf(error, error_size, "%s", message != NULL ? message : "invalid file");
    }
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool apple2_naps_parse_path(const char *path, uint8_t *file_type, uint16_t *aux_type)
{
    const char *suffix;
    size_t length;
    unsigned value = 0;
    int i;

    if (path == NULL) return false;
    length = strlen(path);
    if (length < 7u) return false;
    suffix = path + length - 7u;
    if (suffix[0] != '#') return false;
    for (i = 1; i < 7; ++i) {
        int digit = hex_value(suffix[i]);
        if (digit < 0) return false;
        value = (value << 4) | (unsigned)digit;
    }
    if (file_type != NULL) *file_type = (uint8_t)(value >> 16);
    if (aux_type != NULL) *aux_type = (uint16_t)value;
    return true;
}

bool apple2_naps_make_path(
    const char *path,
    uint8_t file_type,
    uint16_t aux_type,
    char *out,
    size_t out_size)
{
    size_t base_length;
    char *base;
    uint8_t ignored_type;
    uint16_t ignored_aux;
    int written;

    if (path == NULL || path[0] == '\0' || out == NULL || out_size == 0u) return false;
    base_length = strlen(path);
    if (apple2_naps_parse_path(path, &ignored_type, &ignored_aux)) base_length -= 7u;
    base = (char *)malloc(base_length + 1u);
    if (base == NULL) return false;
    memcpy(base, path, base_length);
    base[base_length] = '\0';
    written = snprintf(out, out_size, "%s#%02x%04x",
        base, (unsigned)file_type, (unsigned)aux_type);
    free(base);
    return written >= 0 && (size_t)written < out_size;
}

static bool decode_applesingle(
    const uint8_t *bytes,
    size_t size,
    apple2_binary_view *out,
    char *error,
    size_t error_size)
{
    uint16_t count;
    size_t descriptor_end;
    uint32_t data_offset = 0, data_length = 0;
    uint32_t prodos_offset = 0, prodos_length = 0;
    uint16_t i;

    if (size < 26u || read_be32(bytes) != 0x00051600u ||
        read_be32(bytes + 4) != 0x00020000u) {
        set_error(error, error_size, "not an AppleSingle v2 file");
        return false;
    }
    count = read_be16(bytes + 24);
    descriptor_end = 26u + (size_t)count * 12u;
    if (descriptor_end < 26u || descriptor_end > size) {
        set_error(error, error_size, "invalid AppleSingle entry table");
        return false;
    }
    for (i = 0; i < count; ++i) {
        const uint8_t *entry = bytes + 26u + (size_t)i * 12u;
        uint32_t id = read_be32(entry);
        uint32_t offset = read_be32(entry + 4);
        uint32_t length = read_be32(entry + 8);
        if ((uint64_t)offset + length > size) {
            set_error(error, error_size, "AppleSingle entry is outside the file");
            return false;
        }
        if (id == 1u) {
            data_offset = offset;
            data_length = length;
        } else if (id == 11u) {
            prodos_offset = offset;
            prodos_length = length;
        }
    }
    if (data_length == 0u) {
        set_error(error, error_size, "AppleSingle file has no data fork");
        return false;
    }
    out->data = bytes + data_offset;
    out->size = data_length;
    out->format = APPLE2_BINARY_FORMAT_APPLESINGLE;
    if (prodos_length >= 8u) {
        const uint8_t *info = bytes + prodos_offset;
        uint16_t type = read_be16(info + 2);
        uint32_t aux = read_be32(info + 4);
        if (type == 0x0006u && aux <= 0xffffu) {
            out->load_address = (uint16_t)aux;
            out->has_load_address = true;
        }
    }
    if (!out->has_load_address) {
        set_error(error, error_size, "AppleSingle data is not a ProDOS BIN with a load address");
        return false;
    }
    return true;
}

bool apple2_binary_decode(
    const char *path,
    const uint8_t *bytes,
    size_t size,
    apple2_binary_format requested,
    uint16_t raw_address,
    apple2_binary_view *out,
    char *error,
    size_t error_size)
{
    uint8_t naps_type = 0;
    uint16_t naps_aux = 0;

    if (bytes == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (requested == APPLE2_BINARY_FORMAT_APPLESINGLE ||
        (requested == APPLE2_BINARY_FORMAT_AUTO && size >= 4u &&
         read_be32(bytes) == 0x00051600u)) {
        return decode_applesingle(bytes, size, out, error, error_size);
    }
    if (requested == APPLE2_BINARY_FORMAT_NAPS ||
        (requested == APPLE2_BINARY_FORMAT_AUTO &&
         apple2_naps_parse_path(path, &naps_type, &naps_aux))) {
        if (!apple2_naps_parse_path(path, &naps_type, &naps_aux) || naps_type != 0x06u) {
            set_error(error, error_size, "NAPS filename is not a ProDOS BIN (#06AAAA)");
            return false;
        }
        out->data = bytes;
        out->size = size;
        out->load_address = naps_aux;
        out->has_load_address = true;
        out->format = APPLE2_BINARY_FORMAT_NAPS;
    } else if (requested == APPLE2_BINARY_FORMAT_LEGACY_DOS ||
               (requested == APPLE2_BINARY_FORMAT_AUTO && size >= 4u &&
                read_le16(bytes + 2) == size - 4u)) {
        if (size < 4u || read_le16(bytes + 2) != size - 4u) {
            set_error(error, error_size, "invalid legacy DOS binary header");
            return false;
        }
        out->data = bytes + 4;
        out->size = size - 4u;
        out->load_address = read_le16(bytes);
        out->has_load_address = true;
        out->format = APPLE2_BINARY_FORMAT_LEGACY_DOS;
    } else {
        out->data = bytes;
        out->size = size;
        out->load_address = raw_address;
        out->has_load_address = true;
        out->format = APPLE2_BINARY_FORMAT_RAW;
    }
    if (out->size > 65536u - out->load_address) {
        set_error(error, error_size, "binary does not fit in the 16-bit address space");
        return false;
    }
    return true;
}

bool apple2_applesingle_encode_bin(
    const uint8_t *bytes,
    size_t size,
    uint16_t load_address,
    uint8_t **out_bytes,
    size_t *out_size)
{
    uint8_t *out;
    size_t total;

    if ((size > 0u && bytes == NULL) || out_bytes == NULL || out_size == NULL ||
        size > UINT32_MAX || size > SIZE_MAX - APPLESINGLE_HEADER_SIZE) return false;
    total = APPLESINGLE_HEADER_SIZE + size;
    out = (uint8_t *)calloc(total == 0u ? 1u : total, 1u);
    if (out == NULL) return false;
    write_be32(out, 0x00051600u);
    write_be32(out + 4, 0x00020000u);
    write_be16(out + 24, 2u);
    write_be32(out + 26, 1u);
    write_be32(out + 30, APPLESINGLE_HEADER_SIZE);
    write_be32(out + 34, (uint32_t)size);
    write_be32(out + 38, 11u);
    write_be32(out + 42, 50u);
    write_be32(out + 46, 8u);
    write_be16(out + 50, 0x00c3u);
    write_be16(out + 52, 0x0006u);
    write_be32(out + 54, load_address);
    if (size > 0u) memcpy(out + APPLESINGLE_HEADER_SIZE, bytes, size);
    *out_bytes = out;
    *out_size = total;
    return true;
}

static int compare_source_lines(const void *a, const void *b)
{
    const source_line *left = (const source_line *)a;
    const source_line *right = (const source_line *)b;
    return left->number < right->number ? -1 : left->number > right->number ? 1 : 0;
}

static bool keyword_at(const uint8_t *text, size_t remaining, const char *keyword)
{
    size_t i, length = strlen(keyword);
    if (length > remaining) return false;
    for (i = 0; i < length; ++i) {
        if (toupper((unsigned char)text[i]) != (unsigned char)keyword[i]) return false;
    }
    return true;
}

static bool tokenize_line(
    const source_line *line,
    uint8_t *out,
    size_t out_capacity,
    size_t *out_length,
    char *error,
    size_t error_size)
{
    size_t src = 0, dst = 0;
    bool quoted = false, data = false, rem = false;

    while (src < line->length) {
        uint8_t ch = line->text[src];
        size_t best = 0, best_index = 0, i;
        if (ch == '\t') ch = ' ';
        if (ch < 0x20u || ch > 0x7eu) {
            set_error(error, error_size, "Applesoft listing contains a non-ASCII character");
            return false;
        }
        if (dst >= out_capacity) return false;
        if (rem) {
            out[dst++] = ch;
            ++src;
            continue;
        }
        if (ch == '"') {
            quoted = !quoted;
            out[dst++] = ch;
            ++src;
            continue;
        }
        if (data) {
            out[dst++] = ch;
            ++src;
            if (ch == ':' && !quoted) data = false;
            continue;
        }
        if (quoted) {
            out[dst++] = ch;
            ++src;
            continue;
        }
        if (ch == '?') {
            out[dst++] = 0xbau;
            ++src;
            continue;
        }
        for (i = 0; i < sizeof(applesoft_tokens) / sizeof(applesoft_tokens[0]); ++i) {
            size_t length = strlen(applesoft_tokens[i]);
            if (length > best && keyword_at(line->text + src, line->length - src, applesoft_tokens[i])) {
                best = length;
                best_index = i;
            }
        }
        if (best > 0u) {
            uint8_t token = (uint8_t)(0x80u + best_index);
            out[dst++] = token;
            src += best;
            if (token == 0x83u) data = true;
            if (token == 0xb2u) rem = true;
            continue;
        }
        out[dst++] = (uint8_t)toupper((unsigned char)ch);
        ++src;
    }
    *out_length = dst;
    return true;
}

bool apple2_applesoft_tokenize(
    const uint8_t *text,
    size_t text_size,
    uint8_t **out_program,
    size_t *out_size,
    char *error,
    size_t error_size)
{
    source_line *lines = NULL;
    size_t line_count = 0, line_capacity = 0, pos = 0, total = 2u, i;
    uint8_t *program = NULL;

    if ((text_size > 0u && text == NULL) || out_program == NULL || out_size == NULL) return false;
    while (pos < text_size) {
        size_t begin = pos, end, content;
        unsigned number = 0;
        while (pos < text_size && text[pos] != '\r' && text[pos] != '\n') ++pos;
        end = pos;
        if (pos < text_size && text[pos] == '\r') ++pos;
        if (pos < text_size && text[pos] == '\n') ++pos;
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        if (begin == end) continue;
        if (!isdigit((unsigned char)text[begin])) {
            set_error(error, error_size, "every Applesoft source line must begin with a line number");
            goto fail;
        }
        while (begin < end && isdigit((unsigned char)text[begin])) {
            number = number * 10u + (unsigned)(text[begin++] - '0');
            if (number > 63999u) {
                set_error(error, error_size, "Applesoft line number exceeds 63999");
                goto fail;
            }
        }
        if (begin < end && text[begin] != ' ' && text[begin] != '\t') {
            set_error(error, error_size, "line number must be followed by whitespace");
            goto fail;
        }
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
        content = end - begin;
        if (content > APPLESOFT_MAX_SOURCE_LINE) {
            set_error(error, error_size, "Applesoft source line exceeds 239 characters");
            goto fail;
        }
        if (line_count == line_capacity) {
            size_t next_capacity = line_capacity == 0u ? 32u : line_capacity * 2u;
            source_line *next = (source_line *)realloc(lines, next_capacity * sizeof(*next));
            if (next == NULL) goto fail;
            lines = next;
            line_capacity = next_capacity;
        }
        lines[line_count].number = number;
        lines[line_count].text = text + begin;
        lines[line_count].length = content;
        ++line_count;
    }
    if (line_count == 0u) {
        set_error(error, error_size, "Applesoft listing contains no program lines");
        goto fail;
    }
    qsort(lines, line_count, sizeof(*lines), compare_source_lines);
    for (i = 0; i < line_count; ++i) {
        uint8_t tokenized[512];
        size_t tokenized_size = 0;
        if (i > 0u && lines[i - 1u].number == lines[i].number) {
            set_error(error, error_size, "Applesoft listing contains a duplicate line number");
            goto fail;
        }
        if (!tokenize_line(&lines[i], tokenized, sizeof(tokenized), &tokenized_size, error, error_size)) goto fail;
        if (tokenized_size > SIZE_MAX - total - 5u) goto fail;
        total += 5u + tokenized_size;
    }
    if (total > 0xffffu - APPLESOFT_BASE) {
        set_error(error, error_size, "Applesoft program does not fit in memory");
        goto fail;
    }
    program = (uint8_t *)malloc(total);
    if (program == NULL) goto fail;
    pos = 0u;
    for (i = 0; i < line_count; ++i) {
        uint8_t tokenized[512];
        size_t tokenized_size = 0;
        uint16_t next_address;
        (void)tokenize_line(&lines[i], tokenized, sizeof(tokenized), &tokenized_size, NULL, 0u);
        next_address = (uint16_t)(APPLESOFT_BASE + pos + 5u + tokenized_size);
        program[pos++] = (uint8_t)next_address;
        program[pos++] = (uint8_t)(next_address >> 8);
        program[pos++] = (uint8_t)lines[i].number;
        program[pos++] = (uint8_t)(lines[i].number >> 8);
        memcpy(program + pos, tokenized, tokenized_size);
        pos += tokenized_size;
        program[pos++] = 0u;
    }
    program[pos++] = 0u;
    program[pos++] = 0u;
    free(lines);
    *out_program = program;
    *out_size = pos;
    return true;

fail:
    free(program);
    free(lines);
    if (error != NULL && error_size > 0u && error[0] == '\0') set_error(error, error_size, "out of memory");
    return false;
}

static bool append_text(uint8_t **buffer, size_t *size, size_t *capacity, const void *bytes, size_t length)
{
    if (length > SIZE_MAX - *size) return false;
    if (*size + length > *capacity) {
        size_t next_capacity = *capacity == 0u ? 1024u : *capacity;
        uint8_t *next;
        while (next_capacity < *size + length) {
            if (next_capacity > SIZE_MAX / 2u) return false;
            next_capacity *= 2u;
        }
        next = (uint8_t *)realloc(*buffer, next_capacity);
        if (next == NULL) return false;
        *buffer = next;
        *capacity = next_capacity;
    }
    memcpy(*buffer + *size, bytes, length);
    *size += length;
    return true;
}

bool apple2_applesoft_detokenize(
    const uint8_t *program,
    size_t program_size,
    uint8_t **out_text,
    size_t *out_size,
    char *error,
    size_t error_size)
{
    size_t pos = 0u, text_size = 0u, capacity = 0u;
    uint8_t *text = NULL;

    if (program == NULL || out_text == NULL || out_size == NULL) return false;
    while (true) {
        uint16_t next_address, line_number;
        size_t next_pos;
        char prefix[16];
        int prefix_length;
        if (pos + 2u > program_size) goto malformed;
        next_address = read_le16(program + pos);
        if (next_address == 0u) {
            /* The forward-link list defines the program's end. VARTAB may be
               higher than that marker when Applesoft LOMEM reserves a gap. */
            break;
        }
        if (pos + 4u > program_size) goto malformed;
        if (next_address < APPLESOFT_BASE ||
            (size_t)(next_address - APPLESOFT_BASE) <= pos + 4u ||
            (size_t)(next_address - APPLESOFT_BASE) > program_size) {
            goto malformed;
        }
        next_pos = (size_t)(next_address - APPLESOFT_BASE);
        if (program[next_pos - 1u] != 0u) goto malformed;
        line_number = read_le16(program + pos + 2u);
        pos += 4u;
        prefix_length = snprintf(prefix, sizeof(prefix), "%u ", (unsigned)line_number);
        if (!append_text(&text, &text_size, &capacity, prefix, (size_t)prefix_length)) goto oom;
        while (pos + 1u < next_pos) {
            uint8_t value = program[pos++];
            if (value >= 0x80u) {
                size_t index = value - 0x80u;
                const char *keyword;
                if (index >= sizeof(applesoft_tokens) / sizeof(applesoft_tokens[0])) goto malformed;
                keyword = applesoft_tokens[index];
                if (!append_text(&text, &text_size, &capacity, keyword, strlen(keyword))) goto oom;
            } else {
                if (value < 0x20u && value != '\t') goto malformed;
                if (!append_text(&text, &text_size, &capacity, &value, 1u)) goto oom;
            }
        }
        pos = next_pos;
        if (!append_text(&text, &text_size, &capacity, "\n", 1u)) goto oom;
    }
    if (!append_text(&text, &text_size, &capacity, "", 1u)) goto oom;
    --text_size;
    *out_text = text;
    *out_size = text_size;
    return true;

malformed:
    set_error(error, error_size, "invalid Applesoft tokenized program");
    free(text);
    return false;
oom:
    set_error(error, error_size, "out of memory");
    free(text);
    return false;
}
