#include "paste_parser.h"

#include <ctype.h>
#include <string.h>

/* --- literal character map --------------------------------------------------
   Indexed by ASCII value 0x20-0x7E.  key values are host_key ordinals (see
   machine/keyboard.h — reproduced here as plain integers to keep util/ free
   of machine/ dependencies).  Uppercase letters map to SHIFT+key per the spec
   (lowercase = unshifted, uppercase = shifted), which differs intentionally
   from the runtime paste_ascii_map that treats both cases as unshifted for
   clipboard-paste convenience. */

/* host_key ordinals (must stay in sync with machine/keyboard.h) */
enum {
    PK_A=0,  PK_B,  PK_C,  PK_D,  PK_E,  PK_F,  PK_G,  PK_H,
    PK_I,    PK_J,  PK_K,  PK_L,  PK_M,  PK_N,  PK_O,  PK_P,
    PK_Q,    PK_R,  PK_S,  PK_T,  PK_U,  PK_V,  PK_W,  PK_X,
    PK_Y,    PK_Z,
    PK_0,    PK_1,  PK_2,  PK_3,  PK_4,  PK_5,  PK_6,  PK_7,
    PK_8,    PK_9,
    PK_SPACE, PK_RETURN, PK_DELETE,
    PK_SHIFT, PK_CTRL,
    PK_ESCAPE, PK_TAB, PK_LEFT, PK_RIGHT, PK_UP, PK_DOWN,
    PK_COMMA, PK_PERIOD, PK_SLASH, PK_SEMICOLON, PK_EQUALS, PK_MINUS,
    PK_QUOTE, PK_LBRACKET, PK_RBRACKET, PK_BACKSLASH, PK_BACKQUOTE,
    PK_PLUS, PK_ASTERISK, PK_AT, PK_COLON
};

typedef struct { uint8_t key; uint8_t shift; uint8_t valid; } char_entry_t;

static const char_entry_t s_char_map[128] = {
    /* 0x20 - 0x2F */
    [' ']  = { PK_SPACE,     0, 1 },
    ['!']  = { PK_1,         1, 1 },
    ['"']  = { PK_2,         1, 1 },
    ['#']  = { PK_3,         1, 1 },
    ['$']  = { PK_4,         1, 1 },
    ['%']  = { PK_5,         1, 1 },
    ['&']  = { PK_6,         1, 1 },
    ['\''] = { PK_7,         1, 1 },
    ['(']  = { PK_8,         1, 1 },
    [')']  = { PK_9,         1, 1 },
    ['*']  = { PK_ASTERISK,  0, 1 },
    ['+']  = { PK_PLUS,      0, 1 },
    [',']  = { PK_COMMA,     0, 1 },
    ['-']  = { PK_MINUS,     0, 1 },
    ['.']  = { PK_PERIOD,    0, 1 },
    ['/']  = { PK_SLASH,     0, 1 },
    /* 0x30 - 0x39 digits */
    ['0']  = { PK_0,  0, 1 }, ['1'] = { PK_1,  0, 1 }, ['2'] = { PK_2,  0, 1 },
    ['3']  = { PK_3,  0, 1 }, ['4'] = { PK_4,  0, 1 }, ['5'] = { PK_5,  0, 1 },
    ['6']  = { PK_6,  0, 1 }, ['7'] = { PK_7,  0, 1 }, ['8'] = { PK_8,  0, 1 },
    ['9']  = { PK_9,  0, 1 },
    /* 0x3A - 0x40 */
    [':']  = { PK_COLON,      0, 1 },
    [';']  = { PK_SEMICOLON,  0, 1 },
    ['<']  = { PK_COMMA,      1, 1 },
    ['=']  = { PK_EQUALS,     0, 1 },
    ['>']  = { PK_PERIOD,     1, 1 },
    ['?']  = { PK_SLASH,      1, 1 },
    ['@']  = { PK_AT,         0, 1 },
    /* 0x41 - 0x5A uppercase — shifted keys per the spec */
    ['A']  = { PK_A,  1, 1 }, ['B'] = { PK_B,  1, 1 }, ['C'] = { PK_C,  1, 1 },
    ['D']  = { PK_D,  1, 1 }, ['E'] = { PK_E,  1, 1 }, ['F'] = { PK_F,  1, 1 },
    ['G']  = { PK_G,  1, 1 }, ['H'] = { PK_H,  1, 1 }, ['I'] = { PK_I,  1, 1 },
    ['J']  = { PK_J,  1, 1 }, ['K'] = { PK_K,  1, 1 }, ['L'] = { PK_L,  1, 1 },
    ['M']  = { PK_M,  1, 1 }, ['N'] = { PK_N,  1, 1 }, ['O'] = { PK_O,  1, 1 },
    ['P']  = { PK_P,  1, 1 }, ['Q'] = { PK_Q,  1, 1 }, ['R'] = { PK_R,  1, 1 },
    ['S']  = { PK_S,  1, 1 }, ['T'] = { PK_T,  1, 1 }, ['U'] = { PK_U,  1, 1 },
    ['V']  = { PK_V,  1, 1 }, ['W'] = { PK_W,  1, 1 }, ['X'] = { PK_X,  1, 1 },
    ['Y']  = { PK_Y,  1, 1 }, ['Z'] = { PK_Z,  1, 1 },
    /* 0x5B - 0x5E */
    ['[']  = { PK_COLON,     1, 1 },
    /* 0x5C '\\' is the escape introducer — not a valid literal */
    [']']  = { PK_SEMICOLON, 1, 1 },
    ['^']  = { PK_UP,  0, 1 },
    /* 0x5F '_' — no mapping */
    /* 0x60 '`' — no mapping */
    /* 0x61 - 0x7A lowercase — unshifted keys */
    ['a']  = { PK_A,  0, 1 }, ['b'] = { PK_B,  0, 1 }, ['c'] = { PK_C,  0, 1 },
    ['d']  = { PK_D,  0, 1 }, ['e'] = { PK_E,  0, 1 }, ['f'] = { PK_F,  0, 1 },
    ['g']  = { PK_G,  0, 1 }, ['h'] = { PK_H,  0, 1 }, ['i'] = { PK_I,  0, 1 },
    ['j']  = { PK_J,  0, 1 }, ['k'] = { PK_K,  0, 1 }, ['l'] = { PK_L,  0, 1 },
    ['m']  = { PK_M,  0, 1 }, ['n'] = { PK_N,  0, 1 }, ['o'] = { PK_O,  0, 1 },
    ['p']  = { PK_P,  0, 1 }, ['q'] = { PK_Q,  0, 1 }, ['r'] = { PK_R,  0, 1 },
    ['s']  = { PK_S,  0, 1 }, ['t'] = { PK_T,  0, 1 }, ['u'] = { PK_U,  0, 1 },
    ['v']  = { PK_V,  0, 1 }, ['w'] = { PK_W,  0, 1 }, ['x'] = { PK_X,  0, 1 },
    ['y']  = { PK_Y,  0, 1 }, ['z'] = { PK_Z,  0, 1 },
    /* 0x7B - 0x7E: no mapping */
};

/* --- named key table -------------------------------------------------------- */

typedef struct {
    const char *canonical;
    const char *alias;     /* NULL if none */
    uint8_t     key;       /* host_key ordinal; 0 if is_nmi (unused) */
    uint8_t     needs_shift;
    uint8_t     is_nmi;
    uint8_t     is_oneshot_modifier;
} key_entry_t;

static const key_entry_t s_key_table[] = {
    { "RETURN",    "RT",  PK_RETURN,       0, 0, 0 },
    { "ESC",       "ES",  PK_ESCAPE,       0, 0, 0 },
    { "DELETE",    "DEL", PK_DELETE,       0, 0, 0 },
    { "BACKSPACE", "BS",  PK_DELETE,       0, 0, 0 },
    { "TAB",       NULL,  PK_TAB,          0, 0, 0 },
    { "SHIFT",     "SH",  PK_SHIFT,        0, 0, 1 },
    { "CTRL",      "CT",  PK_CTRL,         0, 0, 1 },
    { "CUU",       NULL,  PK_UP,           0, 0, 0 },
    { "CUD",       NULL,  PK_DOWN,         0, 0, 0 },
    { "CUL",       NULL,  PK_LEFT,         0, 0, 0 },
    { "CUR",       NULL,  PK_RIGHT,        0, 0, 0 },
    { "PLUS",      NULL,  PK_PLUS,         0, 0, 0 },
    { "AT",        NULL,  PK_AT,           0, 0, 0 },
};

/* --- helpers ---------------------------------------------------------------- */

static bool str_icase_eq(const char *a, size_t alen, const char *b) {
    size_t bi = 0;
    while (bi < alen && b[bi] != '\0') {
        if (tolower((unsigned char)a[bi]) != tolower((unsigned char)b[bi])) return false;
        bi++;
    }
    return bi == alen && b[bi] == '\0';
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool is_oct_digit(char c) { return c >= '0' && c <= '7'; }

#define ERR(off, msg) do { err->offset = (int)(off); err->message = (msg); \
                           return false; } while (0)

static bool emit(paste_event_t *out, size_t out_max, size_t *n,
                 paste_event_t ev, size_t src_off,
                 paste_parse_error_t *err) {
    if (*n >= out_max) {
        ERR(src_off, "too many events: input exceeds PASTE_EVENTS_MAX limit");
    }
    out[(*n)++] = ev;
    return true;
}

static bool str_icase_prefix_eq(const char *a, size_t alen, const char *prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (i >= alen) return false;
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)prefix[i])) return false;
        i++;
    }
    return true;
}

static bool parse_wait_named(const char *buf, size_t blen,
                             paste_event_t *out, size_t out_max, size_t *n,
                             size_t esc_start, paste_parse_error_t *err) {
    size_t pos;
    uint32_t count = 0;
    paste_event_t ev = { 0 };

    if (blen >= 2 && str_icase_prefix_eq(buf, blen, "W:")) {
        pos = 2;
    } else if (blen >= 5 && str_icase_prefix_eq(buf, blen, "WAIT:")) {
        pos = 5;
    } else {
        return false;
    }

    if (pos >= blen) {
        ERR(esc_start, "wait token requires an integer value");
    }
    for (; pos < blen; pos++) {
        if (!isdigit((unsigned char)buf[pos])) {
            ERR(esc_start, "wait token value must be decimal digits");
        }
        if (count > (UINT32_MAX - (uint32_t)(buf[pos] - '0')) / 10u) {
            ERR(esc_start, "wait token value is too large");
        }
        count = count * 10u + (uint32_t)(buf[pos] - '0');
    }

    ev.type = PASTE_EV_WAIT;
    ev.wait.count = count;
    return emit(out, out_max, n, ev, esc_start, err);
}

/* --- named key token -------------------------------------------------------- */

static bool parse_named_key(const char *input, size_t *pos, size_t len,
                             paste_event_t *out, size_t out_max, size_t *n,
                             size_t esc_start, paste_parse_error_t *err) {
    char   buf[32];
    size_t blen = 0;
    char   modifier = '\0';
    const key_entry_t *entry;

    /* collect until ']' */
    while (*pos < len && input[*pos] != ']') {
        if (blen < sizeof(buf) - 1) buf[blen++] = input[*pos];
        (*pos)++;
    }
    if (*pos >= len) {
        ERR(esc_start, "unterminated named key sequence: missing ']'");
    }
    (*pos)++; /* consume ']' */

    if (blen == 0) { ERR(esc_start, "empty key name in '\\[...]'"); }

    if (parse_wait_named(buf, blen, out, out_max, n, esc_start, err)) {
        return true;
    }
    if (err->offset >= 0) {
        return false;
    }

    /* optional trailing modifier */
    if (buf[blen - 1] == '+' || buf[blen - 1] == '-') {
        modifier = buf[blen - 1];
        blen--;
    }
    if (blen == 0) { ERR(esc_start, "empty key name in '\\[...]' (only modifier, no name)"); }

    /* look up key name */
    for (entry = s_key_table; entry->canonical != NULL; entry++) {
        if (str_icase_eq(buf, blen, entry->canonical)) break;
        if (entry->alias && str_icase_eq(buf, blen, entry->alias)) break;
    }
    if (entry->canonical == NULL) {
        ERR(esc_start, "unknown key name in '\\[...]'");
    }

    /* build event */
    {
        paste_event_t ev = { 0 };
        if (entry->is_nmi) {
            ev.type = PASTE_EV_NMI;
        } else if (modifier == '+') {
            ev.type = PASTE_EV_KEY_ASSERT;
            ev.key.key = entry->key;
            ev.key.needs_shift = entry->needs_shift;
        } else if (modifier == '-') {
            ev.type = PASTE_EV_KEY_DEASSERT;
            ev.key.key = entry->key;
            ev.key.needs_shift = entry->needs_shift;
        } else if (entry->is_oneshot_modifier) {
            ev.type = PASTE_EV_KEY_ONESHOT;
            ev.key.key = entry->key;
            ev.key.needs_shift = entry->needs_shift;
        } else {
            ev.type = PASTE_EV_KEY_PRESS;
            ev.key.key = entry->key;
            ev.key.needs_shift = entry->needs_shift;
        }
        return emit(out, out_max, n, ev, esc_start, err);
    }
}

/* --- numeric escape tokens -------------------------------------------------- */

static bool parse_hex_esc(const char *input, size_t *pos, size_t len,
                           paste_event_t *out, size_t out_max, size_t *n,
                           size_t esc_start, paste_parse_error_t *err) {
    int hi, lo;
    paste_event_t ev = { 0 };

    if (*pos + 1 >= len ||
        (hi = hex_digit(input[*pos])) < 0 ||
        (lo = hex_digit(input[*pos + 1])) < 0) {
        ERR(esc_start, "\\x requires exactly 2 hex digits");
    }
    ev.type = PASTE_EV_PETSCII;
    ev.petscii.petscii = (uint8_t)(hi * 16 + lo);
    *pos += 2;
    return emit(out, out_max, n, ev, esc_start, err);
}

static bool parse_dec_esc(const char *input, size_t *pos, size_t len,
                           paste_event_t *out, size_t out_max, size_t *n,
                           size_t esc_start, paste_parse_error_t *err) {
    unsigned val;
    paste_event_t ev = { 0 };

    if (*pos + 2 >= len ||
        !isdigit((unsigned char)input[*pos]) ||
        !isdigit((unsigned char)input[*pos + 1]) ||
        !isdigit((unsigned char)input[*pos + 2])) {
        ERR(esc_start, "\\d requires exactly 3 decimal digits");
    }
    val = (unsigned)(input[*pos] - '0') * 100u
        + (unsigned)(input[*pos + 1] - '0') * 10u
        + (unsigned)(input[*pos + 2] - '0');
    if (val > 255) { ERR(esc_start, "\\d value exceeds 255"); }
    ev.type = PASTE_EV_PETSCII;
    ev.petscii.petscii = (uint8_t)val;
    *pos += 3;
    return emit(out, out_max, n, ev, esc_start, err);
}

static bool parse_oct_esc(const char *input, size_t *pos, size_t len,
                           paste_event_t *out, size_t out_max, size_t *n,
                           size_t esc_start, paste_parse_error_t *err) {
    unsigned val;
    paste_event_t ev = { 0 };

    if (*pos + 2 >= len ||
        !is_oct_digit(input[*pos]) ||
        !is_oct_digit(input[*pos + 1]) ||
        !is_oct_digit(input[*pos + 2])) {
        ERR(esc_start, "\\o requires exactly 3 octal digits (0-7)");
    }
    val = (unsigned)(input[*pos] - '0') * 64u
        + (unsigned)(input[*pos + 1] - '0') * 8u
        + (unsigned)(input[*pos + 2] - '0');
    if (val > 255) { ERR(esc_start, "\\o value exceeds 255"); }
    ev.type = PASTE_EV_PETSCII;
    ev.petscii.petscii = (uint8_t)val;
    *pos += 3;
    return emit(out, out_max, n, ev, esc_start, err);
}

/* --- matrix token ----------------------------------------------------------- */

static bool parse_matrix(const char *input, size_t *pos, size_t len,
                          paste_event_t *out, size_t out_max, size_t *n,
                          size_t esc_start, paste_parse_error_t *err) {
    paste_event_t ev = { 0 };

    if (*pos + 2 >= len ||
        !isdigit((unsigned char)input[*pos]) ||
        (input[*pos] - '0') > 7 ||
        input[*pos + 1] != ',' ||
        !isdigit((unsigned char)input[*pos + 2]) ||
        (input[*pos + 2] - '0') > 7) {
        ERR(esc_start, "\\m requires row,col each a digit 0-7 (e.g. \\m3,5)");
    }
    ev.type = PASTE_EV_MATRIX;
    ev.matrix.row = (uint8_t)(input[*pos]     - '0');
    ev.matrix.col = (uint8_t)(input[*pos + 2] - '0');
    *pos += 3;
    return emit(out, out_max, n, ev, esc_start, err);
}

/* --- joystick token --------------------------------------------------------- */

static bool parse_joystick(const char *input, size_t *pos, size_t len,
                             paste_event_t *out, size_t out_max, size_t *n,
                             size_t esc_start, paste_parse_error_t *err) {
    paste_event_t ev = { 0 };
    char port_c, dir_c;

    if (*pos + 1 >= len) { ERR(esc_start, "\\j requires port and direction digits"); }

    port_c = input[(*pos)++];
    if (port_c != '1' && port_c != '2') { ERR(*pos - 1, "\\j port must be 1 or 2"); }

    dir_c = input[(*pos)++];
    if (dir_c < '0' || dir_c > '8') { ERR(*pos - 1, "\\j direction must be 0-8"); }

    ev.type = PASTE_EV_JOYSTICK;
    ev.joy.port = (uint8_t)(port_c - '0');
    ev.joy.dir  = (uint8_t)(dir_c  - '0');

    if (*pos < len && input[*pos] == ',') {
        (*pos)++;
        if (*pos >= len || (input[*pos] != '0' && input[*pos] != '1')) {
            ERR(*pos - 1, "\\j button after comma must be 0 or 1");
        }
        ev.joy.button     = (uint8_t)(input[(*pos)++] - '0');
        ev.joy.has_button = 1;
    }

    return emit(out, out_max, n, ev, esc_start, err);
}

/* --- public API ------------------------------------------------------------- */

bool paste_parse(const char          *input,
                 paste_event_t       *out,
                 size_t               out_max,
                 size_t              *count,
                 paste_parse_error_t *err) {
    size_t i, len, n;

    err->offset  = -1;
    err->message = NULL;
    *count = 0;

    if (!input || !out || out_max == 0) { return true; }

    len = strlen(input);
    n   = 0;

    for (i = 0; i < len; ) {
        unsigned char ch = (unsigned char)input[i];

        if (ch == '\\') {
            size_t esc_start = i;
            i++;

            if (i >= len) { ERR(esc_start, "lone '\\' at end of input"); }

            switch (input[i++]) {
                case '[':
                    if (!parse_named_key(input, &i, len, out, out_max, &n, esc_start, err)) return false;
                    break;
                case 'x':
                    if (!parse_hex_esc(input, &i, len, out, out_max, &n, esc_start, err)) return false;
                    break;
                case 'd':
                    if (!parse_dec_esc(input, &i, len, out, out_max, &n, esc_start, err)) return false;
                    break;
                case 'o':
                    if (!parse_oct_esc(input, &i, len, out, out_max, &n, esc_start, err)) return false;
                    break;
                case 'm':
                    if (!parse_matrix(input, &i, len, out, out_max, &n, esc_start, err)) return false;
                    break;
                case 'j':
                    if (!parse_joystick(input, &i, len, out, out_max, &n, esc_start, err)) return false;
                    break;
                default:
                    ERR(esc_start, "unknown escape: '\\' must be followed by [ x d o m or j");
            }
        } else if (ch == '\n' || ch == '\r') {
            paste_event_t ev = { 0 };
            ev.type    = PASTE_EV_KEY_PRESS;
            ev.key.key = PK_RETURN;
            i++;
            if (!emit(out, out_max, &n, ev, i - 1, err)) return false;
        } else if (ch < 0x20 || ch > 0x7E) {
            ERR(i, "non-printable character in input (must be 0x20-0x7E)");
        } else {
            /* printable ASCII 0x20-0x7E, not '\\' */
            paste_event_t ev = { 0 };

            if (!s_char_map[ch].valid) {
                ERR(i, "character has no key mapping");
            }
            ev.type            = PASTE_EV_KEY_PRESS;
            ev.key.key         = s_char_map[ch].key;
            ev.key.needs_shift = s_char_map[ch].shift;
            i++;
            if (!emit(out, out_max, &n, ev, i - 1, err)) return false;
        }
    }

    *count = n;
    return true;
}
