#include "runtime_history_query_parse.h"

#include <stdlib.h>
#include <string.h>

static const char *const k_find_option_keys[] = {
    "pc",
    "address",
    "access",
    "direction",
    "limit",
    "from",
    "epoch",
    "timeline",
    "cycle",
    "value",
    "opcodes",
    NULL
};

static const char *const k_find_access_names[] = {
    "execute",
    "fetch",
    "opcode",
    "operand",
    "data-read",
    "data-write",
    "read",
    "write",
    "data",
    "dummy-read",
    "rmw-dummy-write",
    "stack-read",
    "stack-write",
    "vector-read",
    NULL
};

const char *const *runtime_history_find_option_keys(void)
{
    return k_find_option_keys;
}

const char *const *runtime_history_find_access_names(void)
{
    return k_find_access_names;
}

static bool parse_u16_token(const char *text, uint16_t *out)
{
    const char *start;
    char *end = NULL;
    unsigned long v;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    if (text[0] == '$') {
        start = text + 1;
        if (*start == '\0') {
            return false;
        }
        v = strtoul(start, &end, 16);
    } else {
        start = text;
        v = strtoul(start, &end, 0);
    }
    if (end == start || *end != '\0' || v > 0xfffful) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

static bool parse_u16_range_token(
    const char *value,
    uint16_t *first,
    uint16_t *last)
{
    char buf[64];
    char *dash;
    uint16_t a;
    uint16_t b;

    if (value == NULL || first == NULL || last == NULL) {
        return false;
    }
    if (strlen(value) >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, value, strlen(value) + 1u);
    dash = strchr(buf, '-');
    if (dash != NULL) {
        *dash = '\0';
        if (!parse_u16_token(buf, &a) || !parse_u16_token(dash + 1, &b) ||
            b < a) {
            return false;
        }
        *first = a;
        *last = b;
        return true;
    }
    if (!parse_u16_token(buf, &a)) {
        return false;
    }
    *first = a;
    *last = a;
    return true;
}

static bool parse_u64_token(const char *text, uint64_t *out)
{
    const char *start;
    char *end = NULL;
    unsigned long long v;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    /* Cycles are decimal counters; allow 0x but not '$'. */
    if (text[0] == '$') {
        return false;
    }
    start = text;
    v = strtoull(start, &end, 0);
    if (end == start || *end != '\0') {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static bool parse_u64_range_token(
    const char *value,
    uint64_t *first,
    uint64_t *last)
{
    char buf[80];
    char *dash;
    uint64_t a;
    uint64_t b;
    size_t len;

    if (value == NULL || first == NULL || last == NULL) {
        return false;
    }
    len = strlen(value);
    if (len == 0u || len >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, value, len + 1u);
    /*
     * Range delimiter is '-'. Prefer a dash that separates two non-empty
     * tokens (avoid treating a leading '-' as a range).
     */
    dash = strchr(buf + 1, '-');
    if (dash != NULL && dash > buf && dash[1] != '\0') {
        *dash = '\0';
        if (!parse_u64_token(buf, &a) || !parse_u64_token(dash + 1, &b) ||
            b < a) {
            return false;
        }
        *first = a;
        *last = b;
        return true;
    }
    if (!parse_u64_token(buf, &a)) {
        return false;
    }
    *first = a;
    *last = a;
    return true;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

/* Hex byte with optional '?' nibble wildcards. Sets value bits only where mask. */
static bool parse_hex_byte_pattern(
    const char *text,
    uint8_t *value,
    uint8_t *mask)
{
    char hi;
    char lo;
    int hi_v;
    int lo_v;
    uint8_t v = 0u;
    uint8_t m = 0u;

    if (text == NULL || value == NULL || mask == NULL ||
        text[0] == '\0' || text[1] == '\0' || text[2] != '\0') {
        return false;
    }
    hi = text[0];
    lo = text[1];
    if (hi == '?') {
        hi_v = 0;
    } else {
        hi_v = hex_nibble(hi);
        if (hi_v < 0) {
            return false;
        }
        m |= 0xf0u;
        v |= (uint8_t)(hi_v << 4);
    }
    if (lo == '?') {
        lo_v = 0;
    } else {
        lo_v = hex_nibble(lo);
        if (lo_v < 0) {
            return false;
        }
        m |= 0x0fu;
        v |= (uint8_t)lo_v;
    }
    *value = v;
    *mask = m;
    return true;
}

static bool parse_value_token(
    const char *text,
    uint8_t *value,
    uint8_t *value_mask)
{
    const char *hex;

    if (text == NULL || value == NULL || value_mask == NULL || text[0] == '\0') {
        return false;
    }

    if (text[0] == '$') {
        hex = text + 1;
        if (strchr(hex, '?') != NULL) {
            if (strlen(hex) != 2u) {
                return false;
            }
            return parse_hex_byte_pattern(hex, value, value_mask);
        }
        {
            char *end = NULL;
            unsigned long v = strtoul(hex, &end, 16);
            if (hex[0] == '\0' || end == hex || *end != '\0' || v > 0xfful) {
                return false;
            }
            *value = (uint8_t)v;
            *value_mask = 0xffu;
            return true;
        }
    }

    if ((text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))) {
        hex = text + 2;
        if (strchr(hex, '?') != NULL) {
            if (strlen(hex) != 2u) {
                return false;
            }
            return parse_hex_byte_pattern(hex, value, value_mask);
        }
        {
            char *end = NULL;
            unsigned long v = strtoul(text, &end, 0);
            if (end == text || *end != '\0' || v > 0xfful) {
                return false;
            }
            *value = (uint8_t)v;
            *value_mask = 0xffu;
            return true;
        }
    }

    /* Bare decimal: full-byte mask. No '?' wildcards. */
    if (strchr(text, '?') != NULL) {
        return false;
    }
    {
        char *end = NULL;
        unsigned long v = strtoul(text, &end, 10);
        if (end == text || *end != '\0' || v > 0xfful) {
            return false;
        }
        *value = (uint8_t)v;
        *value_mask = 0xffu;
        return true;
    }
}

static bool parse_opcodes_token(
    const char *text,
    runtime_history_query *query)
{
    const char *p;
    size_t count = 0u;

    if (text == NULL || query == NULL || text[0] == '\0') {
        return false;
    }
    p = text;
    while (*p != '\0') {
        char byte_tok[8];
        size_t n = 0u;
        uint8_t value = 0u;
        uint8_t mask = 0u;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (count >= RUNTIME_HISTORY_MAX_OPCODE_PATTERN) {
            return false;
        }
        while (*p != '\0' && *p != ',' && n + 1u < sizeof(byte_tok)) {
            if (*p != ' ' && *p != '\t') {
                byte_tok[n++] = *p;
            }
            p++;
        }
        byte_tok[n] = '\0';
        if (*p == ',') {
            p++;
        }
        if (n == 0u || !parse_hex_byte_pattern(byte_tok, &value, &mask)) {
            return false;
        }
        query->opcode_pattern[count].value = value;
        query->opcode_pattern[count].mask = mask;
        count++;
    }
    if (count == 0u) {
        return false;
    }
    query->opcode_pattern_length = (uint8_t)count;
    return true;
}

/*
 * Access name → mask. Returns true on success.
 * execute/fetch: special — has_access cleared by caller (mask unused).
 */
static bool access_mask_from_name(
    const char *name,
    uint16_t *mask,
    bool *is_execute)
{
    if (name == NULL || mask == NULL || is_execute == NULL) {
        return false;
    }
    *is_execute = false;
    *mask = 0u;

    if (strcmp(name, "execute") == 0 || strcmp(name, "fetch") == 0) {
        *is_execute = true;
        return true;
    }
    if (strcmp(name, "opcode") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_OPCODE;
        return true;
    }
    if (strcmp(name, "operand") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_OPERAND;
        return true;
    }
    if (strcmp(name, "write") == 0 || strcmp(name, "data-write") == 0) {
        *mask = (uint16_t)(RUNTIME_HISTORY_ACCESS_DATA_WRITE |
                           RUNTIME_HISTORY_ACCESS_RMW_DUMMY_WRITE |
                           RUNTIME_HISTORY_ACCESS_STACK_WRITE);
        return true;
    }
    if (strcmp(name, "read") == 0 || strcmp(name, "data-read") == 0) {
        *mask = (uint16_t)(RUNTIME_HISTORY_ACCESS_DATA_READ |
                           RUNTIME_HISTORY_ACCESS_OPCODE |
                           RUNTIME_HISTORY_ACCESS_OPERAND |
                           RUNTIME_HISTORY_ACCESS_DUMMY_READ |
                           RUNTIME_HISTORY_ACCESS_STACK_READ |
                           RUNTIME_HISTORY_ACCESS_VECTOR_READ);
        return true;
    }
    if (strcmp(name, "data") == 0) {
        *mask = (uint16_t)(RUNTIME_HISTORY_ACCESS_DATA_READ |
                           RUNTIME_HISTORY_ACCESS_DATA_WRITE);
        return true;
    }
    if (strcmp(name, "dummy-read") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_DUMMY_READ;
        return true;
    }
    if (strcmp(name, "rmw-dummy-write") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_RMW_DUMMY_WRITE;
        return true;
    }
    if (strcmp(name, "stack-read") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_STACK_READ;
        return true;
    }
    if (strcmp(name, "stack-write") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_STACK_WRITE;
        return true;
    }
    if (strcmp(name, "vector-read") == 0) {
        *mask = RUNTIME_HISTORY_ACCESS_VECTOR_READ;
        return true;
    }
    return false;
}

bool runtime_history_parse_find_options(
    const char *text,
    runtime_history_query *query,
    runtime_history_from_kind *from_kind,
    uint64_t *from_id,
    uint16_t *limit)
{
    char buf[RUNTIME_HISTORY_FIND_OPTIONS_MAX];
    char *p;
    size_t len;

    if (query == NULL || from_kind == NULL || from_id == NULL || limit == NULL) {
        return false;
    }
    memset(query, 0, sizeof(*query));
    query->direction = RUNTIME_HISTORY_QUERY_BACKWARD;
    *from_kind = RUNTIME_HISTORY_FROM_DEFAULT;
    *from_id = 0u;
    *limit = 64u;

    if (text == NULL || text[0] == '\0') {
        return true;
    }
    len = strlen(text);
    if (len >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, text, len + 1u);
    p = buf;
    while (*p != '\0') {
        char *token;
        char *eq;
        char *key;
        char *value;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        token = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }

        eq = strchr(token, '=');
        if (eq == NULL || eq == token || eq[1] == '\0') {
            return false;
        }
        *eq = '\0';
        key = token;
        value = eq + 1;

        if (strcmp(key, "pc") == 0) {
            if (!parse_u16_range_token(value, &query->pc_first, &query->pc_last)) {
                return false;
            }
            query->has_pc = true;
        } else if (strcmp(key, "address") == 0) {
            if (!parse_u16_range_token(
                    value, &query->address_first, &query->address_last)) {
                return false;
            }
            query->has_address = true;
        } else if (strcmp(key, "access") == 0) {
            uint16_t mask = 0u;
            bool is_execute = false;
            if (!access_mask_from_name(value, &mask, &is_execute)) {
                return false;
            }
            if (is_execute) {
                query->has_access = false;
                query->access_mask = 0u;
            } else {
                query->has_access = true;
                query->access_mask = mask;
            }
        } else if (strcmp(key, "direction") == 0) {
            if (strcmp(value, "forward") == 0) {
                query->direction = RUNTIME_HISTORY_QUERY_FORWARD;
            } else if (strcmp(value, "backward") == 0) {
                query->direction = RUNTIME_HISTORY_QUERY_BACKWARD;
            } else {
                return false;
            }
        } else if (strcmp(key, "limit") == 0) {
            char *end = NULL;
            unsigned long v = strtoul(value, &end, 10);
            if (end == value || *end != '\0' || v < 1ul || v > 256ul) {
                return false;
            }
            *limit = (uint16_t)v;
        } else if (strcmp(key, "from") == 0) {
            if (strcmp(value, "oldest") == 0) {
                *from_kind = RUNTIME_HISTORY_FROM_OLDEST;
                *from_id = 0u;
            } else if (strcmp(value, "newest") == 0) {
                *from_kind = RUNTIME_HISTORY_FROM_NEWEST;
                *from_id = 0u;
            } else {
                char *end = NULL;
                unsigned long long v = strtoull(value, &end, 0);
                if (end == value || *end != '\0' || v == 0ull) {
                    return false;
                }
                *from_kind = RUNTIME_HISTORY_FROM_ID;
                *from_id = (uint64_t)v;
            }
        } else if (strcmp(key, "epoch") == 0) {
            uint64_t epoch = 0u;
            if (!parse_u64_token(value, &epoch)) {
                return false;
            }
            query->has_epoch = true;
            query->epoch = epoch;
        } else if (strcmp(key, "timeline") == 0) {
            char *end = NULL;
            unsigned long v = strtoul(value, &end, 0);
            if (end == value || *end != '\0' || v > 0xfffffffful) {
                return false;
            }
            query->has_timeline = true;
            query->timeline = (uint32_t)v;
        } else if (strcmp(key, "cycle") == 0) {
            if (!parse_u64_range_token(
                    value, &query->cycle_first, &query->cycle_last)) {
                return false;
            }
            query->has_cycle = true;
        } else if (strcmp(key, "value") == 0) {
            uint8_t v = 0u;
            uint8_t m = 0u;
            if (!parse_value_token(value, &v, &m)) {
                return false;
            }
            query->has_value = true;
            query->value = v;
            query->value_mask = m;
        } else if (strcmp(key, "opcodes") == 0) {
            if (!parse_opcodes_token(value, query)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}
