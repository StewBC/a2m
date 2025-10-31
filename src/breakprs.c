// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

static char *ltrim(char *s) {
    while(isspace(*s)) {
        s++;
    }
    return s;
}
static void rtrim(char *s) {
    char *e = s + strlen(s);
    while(e > s && isspace(e[-1])) {
        --e;
    }
    *e = '\0';
}

static int parse_u32_auto(const char *s, char **end, uint32_t *out) {
    errno = 0;
    // base 0 => accepts 0x/0X hex, leading 0 octal, else decimal.
    unsigned long v = strtoul(s, end, 0);
    if(errno == ERANGE || *end == s) {
        return A2_ERR;
    }
    // SQW was going to use 64K but maybe setting breakpoints in aux coyld be done this way?
    if(v > 0xFFFFFFFFul) {
        return A2_ERR;
    }
    *out = v;
    return A2_OK;
}

// Parse the first field: "<addr>" or "<addr>-<addr>" with optional spaces around '-'
static int parse_addr_field(char *field, uint32_t *start, uint32_t *end) {
    char *p = ltrim(field);
    rtrim(p);

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
int parse_breakpoint_line(const char *val, parsed_t *out) {
    // out->start = 0;
    // out->end = 0;
    // out->mode = MODE_PC;
    // out->speed = SPEED_RESTORE;
    // out->count = 0;
    // out->reset = 0;
    memset(out, 0, sizeof(parsed_t));
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
        char *param = ltrim(rest);
        rtrim(param);

        if(*param != '\0') {
            // Try keywords first
            if(stricmp(param, "pc") == 0) {
                out->mode = MODE_PC;
            } else if(0 == stricmp(param, "read")) {
                out->mode = MODE_READ;
            } else if(0 == stricmp(param, "write")) {
                out->mode = MODE_WRITE;
            } else if(0 == stricmp(param, "access")) {
                out->mode = MODE_ACCESS;
            } else if(0 == stricmp(param, "restore")) {
                out->speed = SPEED_RESTORE;
            } else if(0 == stricmp(param, "fast")) {
                out->speed = SPEED_FAST;
            } else if(0 == stricmp(param, "slow")) {
                out->speed = SPEED_SLOW;
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