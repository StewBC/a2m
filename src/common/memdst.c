// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common_lib.h"

VIEW_FLAGS cmn_parse_mem_dest_string(const char *val) {
    VIEW_FLAGS vf = 0;
    if(!val || !*val) {
        return vf;
    }

    if(0 == stricmp(val, "6502")) {
        return vf;
    }
    if(0 == stricmp(val, "64K")) {
        vf_set_ram(&vf, A2SEL48K_MAIN);
        return vf;
    }
    if(0 == stricmp(val, "128K")) {
        vf_set_ram(&vf, A2SEL48K_AUX);
        return vf;
    }
    if(0 == stricmp(val, "LC Bank") || 0 == stricmp(val, "LCBank")) {
        vf_set_d000(&vf, A2SELD000_LC_B2);
        return vf;
    }

    char buf[128];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = NULL;
    char *token_ctx = NULL;
    token = util_strtok_r(buf, ",", &token_ctx);
    while(token) {
        while(*token == ' ' || *token == '\t') {
            token++;
        }
        char *end = token + strlen(token);
        while(end > token && (end[-1] == ' ' || end[-1] == '\t')) {
            *(--end) = '\0';
        }

        if(0 == stricmp(token, "map") || 0 == stricmp(token, "mapped")) {
            vf = 0;
        } else if(0 == stricmp(token, "main")) {
            vf_set_ram(&vf, A2SEL48K_MAIN);
        } else if(0 == stricmp(token, "aux")) {
            vf_set_ram(&vf, A2SEL48K_AUX);
        } else if(0 == stricmp(token, "c100map") || 0 == stricmp(token, "c100mapped")) {
            vf_set_c100(&vf, A2SELC100_MAPPED);
        } else if(0 == stricmp(token, "c100rom")) {
            vf_set_c100(&vf, A2SELC100_ROM);
        } else if(0 == stricmp(token, "d000map") || 0 == stricmp(token, "d000mapped")) {
            vf_set_d000(&vf, A2SELD000_MAPPED);
        } else if(0 == stricmp(token, "lc1")) {
            vf_set_d000(&vf, A2SELD000_LC_B1);
        } else if(0 == stricmp(token, "lc2")) {
            vf_set_d000(&vf, A2SELD000_LC_B2);
        } else if(0 == stricmp(token, "rom")) {
            vf_set_d000(&vf, A2SELD000_ROM);
        }

        token = util_strtok_r(NULL, ",", &token_ctx);
    }

    return vf;
}

void cmn_mem_dest_to_string(VIEW_FLAGS vf, char *out, size_t out_len) {
    char buf[64] = {0};
    char *p = buf;
    size_t remain = sizeof(buf);
    int first = 1;

    A2SEL_48K ram = vf_get_ram(vf);
    A2SEL_C100 c100 = vf_get_c100(vf);
    A2SEL_D000 d000 = vf_get_d000(vf);

    if(ram == A2SEL48K_MAIN) {
        int n = snprintf(p, remain, "%s%s", first ? "" : ",", "main");
        if(n > 0 && (size_t)n < remain) {
            p += n;
            remain -= n;
        }
        first = 0;
    } else if(ram == A2SEL48K_AUX) {
        int n = snprintf(p, remain, "%s%s", first ? "" : ",", "aux");
        if(n > 0 && (size_t)n < remain) {
            p += n;
            remain -= n;
        }
        first = 0;
    }

    if(c100 == A2SELC100_ROM) {
        int n = snprintf(p, remain, "%s%s", first ? "" : ",", "c100rom");
        if(n > 0 && (size_t)n < remain) {
            p += n;
            remain -= n;
        }
        first = 0;
    }

    if(d000 != A2SELD000_MAPPED) {
        switch(d000) {
            case A2SELD000_LC_B1:
                {
                    int n = snprintf(p, remain, "%s%s", first ? "" : ",", "lc1");
                    if(n > 0 && (size_t)n < remain) {
                        p += n;
                        remain -= n;
                    }
                }
                break;
            case A2SELD000_LC_B2:
                {
                    int n = snprintf(p, remain, "%s%s", first ? "" : ",", "lc2");
                    if(n > 0 && (size_t)n < remain) {
                        p += n;
                        remain -= n;
                    }
                }
                break;
            case A2SELD000_ROM:
                {
                    int n = snprintf(p, remain, "%s%s", first ? "" : ",", "rom");
                    if(n > 0 && (size_t)n < remain) {
                        p += n;
                        remain -= n;
                    }
                }
                break;
            case A2SELD000_MAPPED:
                break;
        }
        first = 0;
    }

    if(first) {
        strncpy(out, "map", out_len - 1);
        out[out_len - 1] = '\0';
    } else {
        strncpy(out, buf, out_len - 1);
        out[out_len - 1] = '\0';
    }
}
