#include "apple_type_script.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define ERR(off, msg)                                                                 \
    do {                                                                              \
        if (error != NULL) {                                                          \
            error->offset = (int)(off);                                               \
            error->message = (msg);                                                   \
        }                                                                             \
        return false;                                                                 \
    } while (0)

static bool icase_eq(const char *a, size_t alen, const char *b)
{
    size_t i;
    size_t blen = strlen(b);
    if (alen != blen) {
        return false;
    }
    for (i = 0; i < alen; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

static bool emit(
    apple_type_event *out,
    size_t out_max,
    size_t *n,
    apple_type_event ev,
    size_t err_off,
    apple_type_parse_error *error)
{
    if (*n >= out_max) {
        ERR(err_off, "type script too long");
    }
    out[(*n)++] = ev;
    return true;
}

static bool emit_pulse_mod(
    apple_type_event *out,
    size_t out_max,
    size_t *n,
    apple_type_event_kind set_kind,
    size_t err_off,
    apple_type_parse_error *error)
{
    apple_type_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = (uint8_t)set_kind;
    ev.value = 1u;
    if (!emit(out, out_max, n, ev, err_off, error)) {
        return false;
    }
    memset(&ev, 0, sizeof(ev));
    ev.kind = (uint8_t)APPLE_TYPE_EV_WAIT;
    ev.wait_count = 1u;
    if (!emit(out, out_max, n, ev, err_off, error)) {
        return false;
    }
    memset(&ev, 0, sizeof(ev));
    ev.kind = (uint8_t)set_kind;
    ev.value = 0u;
    return emit(out, out_max, n, ev, err_off, error);
}

static bool emit_pulse_btn(
    apple_type_event *out,
    size_t out_max,
    size_t *n,
    uint8_t btn,
    size_t err_off,
    apple_type_parse_error *error)
{
    apple_type_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = (uint8_t)APPLE_TYPE_EV_BUTTON_SET;
    ev.axis_or_btn = btn;
    ev.value = 1u;
    if (!emit(out, out_max, n, ev, err_off, error)) {
        return false;
    }
    memset(&ev, 0, sizeof(ev));
    ev.kind = (uint8_t)APPLE_TYPE_EV_WAIT;
    ev.wait_count = 1u;
    if (!emit(out, out_max, n, ev, err_off, error)) {
        return false;
    }
    memset(&ev, 0, sizeof(ev));
    ev.kind = (uint8_t)APPLE_TYPE_EV_BUTTON_SET;
    ev.axis_or_btn = btn;
    ev.value = 0u;
    return emit(out, out_max, n, ev, err_off, error);
}

static bool parse_u8_dec(const char *s, size_t len, uint8_t *out_v)
{
    size_t i;
    unsigned v = 0;
    if (len == 0) {
        return false;
    }
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10u + (unsigned)(s[i] - '0');
        if (v > 255u) {
            return false;
        }
    }
    *out_v = (uint8_t)v;
    return true;
}

static bool parse_u16_dec(const char *s, size_t len, uint16_t *out_v)
{
    size_t i;
    unsigned long v = 0;
    if (len == 0) {
        return false;
    }
    for (i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10ul + (unsigned long)(s[i] - '0');
        if (v > 65535ul) {
            return false;
        }
    }
    *out_v = (uint16_t)v;
    return true;
}

static bool parse_bracket_token(
    const char *tok,
    size_t tlen,
    apple_type_event *out,
    size_t out_max,
    size_t *n,
    size_t err_off,
    apple_type_parse_error *error)
{
    char mod = '\0';
    const char *body = tok;
    size_t blen = tlen;
    apple_type_event ev;

    if (blen == 0) {
        ERR(err_off, "empty \\[…] token");
    }

    /* Trailing + / - for hold/release */
    if (body[blen - 1u] == '+' || body[blen - 1u] == '-') {
        mod = body[blen - 1u];
        blen--;
        if (blen == 0) {
            ERR(err_off, "modifier without name in \\[…]");
        }
    }

    /* WAIT */
    if (blen >= 2u && (body[0] == 'W' || body[0] == 'w') && body[1] == ':') {
        uint16_t w;
        if (mod != '\0') {
            ERR(err_off, "wait token cannot use +/-");
        }
        if (!parse_u16_dec(body + 2, blen - 2u, &w) || w == 0u) {
            ERR(err_off, "\\[W:N] needs positive decimal N");
        }
        memset(&ev, 0, sizeof(ev));
        ev.kind = (uint8_t)APPLE_TYPE_EV_WAIT;
        ev.wait_count = w;
        return emit(out, out_max, n, ev, err_off, error);
    }
    if (blen >= 5u && icase_eq(body, 5u, "WAIT:") ) {
        /* handled below with memcmp style — WAIT: is 5 chars */
    }
    if (blen > 5u && icase_eq(body, 5u, "wait:")) {
        uint16_t w;
        if (mod != '\0') {
            ERR(err_off, "wait token cannot use +/-");
        }
        if (!parse_u16_dec(body + 5, blen - 5u, &w) || w == 0u) {
            ERR(err_off, "\\[WAIT:N] needs positive decimal N");
        }
        memset(&ev, 0, sizeof(ev));
        ev.kind = (uint8_t)APPLE_TYPE_EV_WAIT;
        ev.wait_count = w;
        return emit(out, out_max, n, ev, err_off, error);
    }

    /* RESET / COLDRESET */
    if (icase_eq(body, blen, "RESET") || icase_eq(body, blen, "WARMRESET")) {
        if (mod != '\0') {
            ERR(err_off, "RESET cannot use +/-");
        }
        memset(&ev, 0, sizeof(ev));
        ev.kind = (uint8_t)APPLE_TYPE_EV_RESET_WARM;
        return emit(out, out_max, n, ev, err_off, error);
    }
    if (icase_eq(body, blen, "COLDRESET")) {
        if (mod != '\0') {
            ERR(err_off, "COLDRESET cannot use +/-");
        }
        memset(&ev, 0, sizeof(ev));
        ev.kind = (uint8_t)APPLE_TYPE_EV_RESET_COLD;
        return emit(out, out_max, n, ev, err_off, error);
    }

    /* Open / Closed Apple */
    if (icase_eq(body, blen, "OA") || icase_eq(body, blen, "OPENAPPLE") ||
        icase_eq(body, blen, "OPEN-APPLE") || icase_eq(body, blen, "OPEN_APPLE")) {
        if (mod == '+') {
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_OA_SET;
            ev.value = 1u;
            return emit(out, out_max, n, ev, err_off, error);
        }
        if (mod == '-') {
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_OA_SET;
            ev.value = 0u;
            return emit(out, out_max, n, ev, err_off, error);
        }
        return emit_pulse_mod(out, out_max, n, APPLE_TYPE_EV_OA_SET, err_off, error);
    }
    if (icase_eq(body, blen, "CA") || icase_eq(body, blen, "CLOSEDAPPLE") ||
        icase_eq(body, blen, "CLOSED-APPLE") || icase_eq(body, blen, "CLOSED_APPLE")) {
        if (mod == '+') {
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CA_SET;
            ev.value = 1u;
            return emit(out, out_max, n, ev, err_off, error);
        }
        if (mod == '-') {
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CA_SET;
            ev.value = 0u;
            return emit(out, out_max, n, ev, err_off, error);
        }
        return emit_pulse_mod(out, out_max, n, APPLE_TYPE_EV_CA_SET, err_off, error);
    }

    /* Buttons B0 / B1 */
    if (icase_eq(body, blen, "B0") || icase_eq(body, blen, "B1")) {
        uint8_t btn = icase_eq(body, blen, "B1") ? 1u : 0u;
        if (mod == '+') {
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_BUTTON_SET;
            ev.axis_or_btn = btn;
            ev.value = 1u;
            return emit(out, out_max, n, ev, err_off, error);
        }
        if (mod == '-') {
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_BUTTON_SET;
            ev.axis_or_btn = btn;
            ev.value = 0u;
            return emit(out, out_max, n, ev, err_off, error);
        }
        return emit_pulse_btn(out, out_max, n, btn, err_off, error);
    }

    /* Joystick: J1C / J2C both axes center */
    if (icase_eq(body, blen, "J1C") || icase_eq(body, blen, "J2C")) {
        uint8_t stick = (body[1] == '2') ? 1u : 0u;
        if (mod != '\0') {
            ERR(err_off, "stick center cannot use +/-");
        }
        memset(&ev, 0, sizeof(ev));
        ev.kind = (uint8_t)APPLE_TYPE_EV_AXIS_SET;
        ev.stick = stick;
        ev.axis_or_btn = 0u;
        ev.value = 128u;
        if (!emit(out, out_max, n, ev, err_off, error)) {
            return false;
        }
        ev.axis_or_btn = 1u;
        return emit(out, out_max, n, ev, err_off, error);
    }

    /* J1X=n / J1Y=n / J2X=n / J2Y=n */
    if (blen >= 4u && (body[0] == 'J' || body[0] == 'j') &&
        (body[1] == '1' || body[1] == '2')) {
        uint8_t stick = (body[1] == '2') ? 1u : 0u;
        char axis_ch;
        uint8_t axis;
        size_t i;

        axis_ch = (char)toupper((unsigned char)body[2]);
        if (axis_ch != 'X' && axis_ch != 'Y') {
            /* fall through to extreme forms J1XL etc. handled below if blen>=4 */
        } else if (blen >= 4u && body[3] == '=') {
            uint8_t v;
            if (mod != '\0') {
                ERR(err_off, "axis assign cannot use +/-");
            }
            if (!parse_u8_dec(body + 4, blen - 4u, &v)) {
                ERR(err_off, "axis value must be 0..255 decimal");
            }
            axis = (axis_ch == 'Y') ? 1u : 0u;
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_AXIS_SET;
            ev.stick = stick;
            ev.axis_or_btn = axis;
            ev.value = v;
            return emit(out, out_max, n, ev, err_off, error);
        }

        /* J1XL J1XR J1YU J1YD J1XC J1YC */
        if (blen == 4u) {
            char a = (char)toupper((unsigned char)body[2]);
            char d = (char)toupper((unsigned char)body[3]);
            uint8_t v;
            if (mod != '\0') {
                ERR(err_off, "axis extreme cannot use +/-");
            }
            if (a != 'X' && a != 'Y') {
                ERR(err_off, "unknown joystick token");
            }
            axis = (a == 'Y') ? 1u : 0u;
            if (d == 'C') {
                v = 128u;
            } else if (a == 'X' && d == 'L') {
                v = 0u;
            } else if (a == 'X' && d == 'R') {
                v = 255u;
            } else if (a == 'Y' && d == 'U') {
                v = 0u;
            } else if (a == 'Y' && d == 'D') {
                v = 255u;
            } else {
                ERR(err_off, "use XL/XR/YU/YD/XC/YC for stick extremes");
            }
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_AXIS_SET;
            ev.stick = stick;
            ev.axis_or_btn = axis;
            ev.value = v;
            return emit(out, out_max, n, ev, err_off, error);
        }

        (void)i;
    }

    ERR(err_off, "unknown \\[…] token");
}

bool apple_type_script_parse(
    const char *text,
    apple_type_event *out,
    size_t out_max,
    size_t *out_count,
    apple_type_parse_error *error)
{
    size_t i = 0;
    size_t n = 0;
    size_t len;

    if (error != NULL) {
        error->offset = -1;
        error->message = NULL;
    }
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (text == NULL || out == NULL || out_count == NULL) {
        ERR(0, "invalid parse arguments");
    }
    len = strlen(text);

    while (i < len) {
        unsigned char c = (unsigned char)text[i];

        if (c == '\\') {
            size_t esc = i;
            size_t j;
            char tok[48];
            size_t tlen = 0;

            i++;
            if (i >= len) {
                ERR(esc, "trailing backslash");
            }
            if (text[i] == '\\') {
                apple_type_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
                ev.value = (uint8_t)'\\';
                if (!emit(out, out_max, &n, ev, esc, error)) {
                    return false;
                }
                i++;
                continue;
            }
            if (text[i] == 'r' || text[i] == 'R' ||
                text[i] == 'n' || text[i] == 'N') {
                apple_type_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
                ev.value = 0x0du;
                if (!emit(out, out_max, &n, ev, esc, error)) {
                    return false;
                }
                i++;
                continue;
            }
            if (text[i] == 't' || text[i] == 'T') {
                apple_type_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
                ev.value = (uint8_t)' ';
                if (!emit(out, out_max, &n, ev, esc, error)) {
                    return false;
                }
                i++;
                continue;
            }
            if (text[i] != '[') {
                /* Literal backslash + char */
                apple_type_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
                ev.value = (uint8_t)'\\';
                if (!emit(out, out_max, &n, ev, esc, error)) {
                    return false;
                }
                continue;
            }
            i++; /* skip [ */
            j = i;
            while (j < len && text[j] != ']') {
                if (tlen + 1u < sizeof(tok)) {
                    tok[tlen++] = text[j];
                }
                j++;
            }
            if (j >= len) {
                ERR(esc, "unterminated \\[…]");
            }
            tok[tlen] = '\0';
            if (!parse_bracket_token(tok, tlen, out, out_max, &n, esc, error)) {
                return false;
            }
            i = j + 1u;
            continue;
        }

        if (c == '\r') {
            apple_type_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
            ev.value = 0x0du;
            if (!emit(out, out_max, &n, ev, i, error)) {
                return false;
            }
            if (i + 1u < len && text[i + 1u] == '\n') {
                i += 2u;
            } else {
                i++;
            }
            continue;
        }
        if (c == '\n') {
            apple_type_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
            ev.value = 0x0du;
            if (!emit(out, out_max, &n, ev, i, error)) {
                return false;
            }
            i++;
            continue;
        }
        if (c == '\t') {
            apple_type_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
            ev.value = (uint8_t)' ';
            if (!emit(out, out_max, &n, ev, i, error)) {
                return false;
            }
            i++;
            continue;
        }
        if (c >= 0x20u && c <= 0x7eu) {
            apple_type_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
            ev.value = (uint8_t)c;
            if (!emit(out, out_max, &n, ev, i, error)) {
                return false;
            }
            i++;
            continue;
        }
        if (c < 0x20u) {
            apple_type_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = (uint8_t)APPLE_TYPE_EV_CHAR;
            ev.value = (uint8_t)c;
            if (!emit(out, out_max, &n, ev, i, error)) {
                return false;
            }
            i++;
            continue;
        }
        /* Drop high bytes / UTF-8 lead */
        i++;
    }

    *out_count = n;
    return true;
}
