// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

static char *parse_ltrim(char *s) {
    while(isspace(*s)) {
        s++;
    }
    return s;
}

static void parse_rtrim(char *s) {
    char *e = s + strlen(s);
    while(e > s && isspace(e[-1])) {
        --e;
    }
    *e = '\0';
}

char *parse_decode_c_string(const char *in, size_t *out_len) {
    size_t len = strlen(in);
    char *out = malloc(len + 1);  // worst-case would be same size
    if(!out) {
        if(out_len) {
            *out_len = 0;
        }
        return NULL;
    }

    size_t index = 0;

    for(size_t i = 0; i < len;) {
        unsigned char c = in[i++];

        if(c == '\\' && i < len) {
            unsigned char e = in[i++];

            switch(e) {
                case 'n':
                    out[index++] = '\n';
                    break;
                case 'r':
                    out[index++] = '\r';
                    break;
                case 't':
                    out[index++] = '\t';
                    break;
                case '0':
                    out[index++] = '\0';
                    break;
                case '\\':
                    out[index++] = '\\';
                    break;
                case '"':
                    out[index++] = '"';
                    break;
                case '\'':
                    out[index++] = '\'';
                    break;

                case 'x': {
                        // \xHH (1 or 2 hex digits)
                        char hex[3] = {0, 0, 0};
                        if(i < len && isxdigit((unsigned char)in[i])) {
                            hex[0] = in[i++];
                            if(i < len && isxdigit((unsigned char)in[i])) {
                                hex[1] = in[i++];
                            }
                            out[index++] = (char)strtol(hex, NULL, 16);
                        } else {
                            // Invalid \x — treat as a literal x
                            out[index++] = 'x';
                        }
                        break;
                    }

                default:
                    // Unknown escape — treat as literal character
                    out[index++] = e;
                    break;
            }
        } else {
            out[index++] = (char)c;
        }
    }

    out[index] = '\0';
    if(out_len) {
        *out_len = index;
    }
    return out;
}

int parse_encode_c_string(const char *in, char *out, int out_len) {
    size_t i = 0;
    size_t index = 0;
    uint8_t c;

    if(!in) {
        return 0;
    }

    while((c = in[i++])) {
        if(isprint(c)) {
            out[index++] = c;
        } else {
            switch(c) {
                case '\n':
                    if(out_len - index >= 3) {
                        out[index++] = '\\';
                        out[index++] = 'n';
                    }
                    break;
                case '\r':
                    if(out_len - index >= 3) {
                        out[index++] = '\\';
                        out[index++] = 'r';
                    }
                    break;
                case '\t':
                    if(out_len - index >= 3) {
                        out[index++] = '\\';
                        out[index++] = 't';
                    }
                    break;
                default:
                    if(out_len - index >= 5) {
                        char h = (c & 0xf0) >> 4;
                        out[index++] = '\\';
                        out[index++] = 'x';
                        out[index++] = h < 9 ? '0' + h : 'A' + h - 10;
                        h = (c & 0x0f);
                        out[index++] = h < 9 ? '0' + h : 'A' + h - 10;
                    }
                    break;
            }
        }
    }
    out[index] = c;
    return index;
}

static int parse_u32_auto(const char *s, char **end, uint32_t *out) {
    errno = 0;
    // base 0 => accepts 0x/0X hex, leading 0 octal, else decimal.
    unsigned long v = strtoul(s, end, 0);
    if(errno == ERANGE || *end == s) {
        return A2_ERR;
    }
    // SQW was going to use 64K but maybe setting breakpoints in aux could be done this way?
    if(v > 0xFFFFFFFFul) {
        return A2_ERR;
    }
    *out = v;
    return A2_OK;
}

// Parse the first field: "<addr>" or "<addr>-<addr>" with optional spaces around '-'
static int parse_addr_field(char *field, uint32_t *start, uint32_t *end) {
    char *p = parse_ltrim(field);
    parse_rtrim(p);

    // scan first number
    char *end1 = NULL;
    if(A2_OK != parse_u32_auto(p, &end1, start)) {
        return A2_ERR;
    }

    // skip spaces
    while(isspace(*end1)) {
        ++end1;
    }

    if(*end1 != '-') {
        *end = *start;
        // ensure only whitespace/trailing nothing after first number
        while(isspace(*end1)) {
            ++end1;
        }
        return *end1 == '\0' ? A2_OK : A2_ERR;
    }

    // has a '-'
    ++end1;
    while(isspace(*end1)) {
        ++end1;
    }

    char *end2 = NULL;
    if(A2_OK != parse_u32_auto(end1, &end2, end)) {
        return A2_ERR;
    }

    while(isspace(*end2)) {
        ++end2;
    }
    return *end2 == '\0' ? A2_OK : A2_ERR;
}

// Main parse function
int parse_breakpoint_line(const char *val, PARSEDBP *out) {
    memset(out, 0, sizeof(PARSEDBP));
    int set_count = 0;
    int set_reset = 0;

    // make a modifiable copy
    char *buf = strdup(val);
    if(!buf) {
        return A2_ERR;
    }

    int retval = A2_ERR;

    // split on first comma: addr/range | rest
    char *first = buf;
    char *rest  = strchr(buf, ',');
    if(rest) {
        *rest++ = '\0';
    }

    // parse the address or address-range
    if(A2_OK != parse_addr_field(first, &out->start, &out->end)) {
        goto done;
    }

    // parse remaining comma-separated (optional) parameters
    while(rest) {
        // next parameter
        char *next_comma = strchr(rest, ',');
        if(next_comma) {
            *next_comma = '\0';
        }

        // trim value
        char *param = parse_ltrim(rest);
        parse_rtrim(param);

        if(*param != '\0') {
            // Try keywords first
            if(stricmp(param, "pc") == 0) {
                out->mode |= BREAK_MODE_PC;
            } else if(0 == stricmp(param, "access")) {
                out->mode = BREAK_MODE_PC | BREAK_MODE_READ | BREAK_MODE_WRITE;
            } else if(0 == stricmp(param, "fast")) {
                out->action = ACTION_FAST;
            } else if(0 == stricmp(param, "read")) {
                out->mode |= BREAK_MODE_READ;
            } else if(0 == stricmp(param, "restore")) {
                out->action = ACTION_RESTORE;
            } else if(0 == stricmp(param, "slow")) {
                out->action = ACTION_SLOW;
            } else if(0 == strnicmp(param, "swap", 4)) {
                int slot, device;
                if(sscanf(param + 4, "%*1[ \t=]%*1[Ss]%d%*1[Dd]%d", &slot, &device) == 2 &&
                        slot >= 1 && slot <= 7 && device >= 0 && device <= 1) {
                    out->action = ACTION_SWAP;
                    out->slot = slot;
                    out->device = device;
                }
            } else if(0 == stricmp(param, "tron")) {
                out->action = ACTION_TRON;
            } else if(0 == stricmp(param, "troff")) {
                out->action = ACTION_TROFF;
            } else if(0 == strnicmp(param, "type", 4)) {
                if(strchr(" \t=", param[4]) != NULL) {
                    out->action = ACTION_TYPE;
                    out->type_text = parse_decode_c_string(&param[5], NULL);
                }
            } else if(0 == stricmp(param, "write")) {
                out->mode |= BREAK_MODE_WRITE;
            } else {
                // Otherwise, treat as integer (count then reset)
                char *endp = NULL;
                errno = 0;
                long v = strtol(param, &endp, 0); // accepts 0x.. too
                if(errno == 0 && endp != param) {
                    while(isspace(*endp)) {
                        ++endp;
                    }
                    if(*endp == '\0') {
                        if(!set_count) {
                            out->count = (int)v;
                            set_count = 1;
                        } else if(!set_reset) {
                            out->reset = (int)v;
                            set_reset = 1;
                        } else {
                            goto done;    // unexpected extra number
                        }
                    } else {
                        goto done; // garbage in value
                    }
                } else {
                    goto done; // unknown value
                }
            }
        }

        // advance
        if(!next_comma) {
            break;
        }
        rest = next_comma + 1;
    }

    retval = A2_OK;
done:
    free(buf);
    return retval;
}