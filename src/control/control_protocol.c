#include "control_protocol.h"

#include "runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void control_request_release(control_request *request)
{
    if (request == NULL) {
        return;
    }
    free(request->payload);
    request->payload = NULL;
    request->payload_size = 0;
}

void control_response_release(control_response *response)
{
    if (response == NULL) {
        return;
    }
    free(response->payload);
    response->payload = NULL;
    response->payload_size = 0;
}

void control_protocol_format_ok(
    control_response *response,
    uint32_t id,
    const char *text)
{
    if (response == NULL) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->id = id;
    response->type = CONTROL_RESPONSE_OK;
    if (text != NULL) {
        strncpy(response->text, text, CONTROL_RESPONSE_TEXT_MAX - 1);
    }
}

void control_protocol_format_error(
    control_response *response,
    uint32_t id,
    const char *code,
    const char *message,
    bool close_client)
{
    if (response == NULL) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->id = id;
    response->type = CONTROL_RESPONSE_ERROR;
    response->close_client = close_client;
    snprintf(
        response->text,
        sizeof(response->text),
        "%s %s",
        code != NULL ? code : "error",
        message != NULL ? message : "");
}

void control_protocol_format_data(
    control_response *response,
    uint32_t id,
    const char *data_type,
    const char *metadata,
    uint8_t *payload,
    size_t payload_size)
{
    if (response == NULL) {
        return;
    }
    memset(response, 0, sizeof(*response));
    response->id = id;
    response->type = CONTROL_RESPONSE_DATA;
    if (data_type != NULL) {
        strncpy(response->data_type, data_type, sizeof(response->data_type) - 1);
    }
    if (metadata != NULL) {
        strncpy(response->metadata, metadata, sizeof(response->metadata) - 1);
    }
    response->payload = payload;
    response->payload_size = payload_size;
}

bool control_protocol_write_response_line(
    char *out,
    size_t out_size,
    const control_response *response)
{
    int n;

    if (out == NULL || out_size == 0 || response == NULL) {
        return false;
    }

    if (response->type == CONTROL_RESPONSE_OK) {
        if (response->text[0] != '\0') {
            n = snprintf(out, out_size, "%u ok %s\n", response->id, response->text);
        } else {
            n = snprintf(out, out_size, "%u ok\n", response->id);
        }
        return n > 0 && (size_t)n < out_size;
    }

    if (response->type == CONTROL_RESPONSE_ERROR) {
        n = snprintf(out, out_size, "%u error %s\n", response->id, response->text);
        return n > 0 && (size_t)n < out_size;
    }

    if (response->type == CONTROL_RESPONSE_DATA) {
        if (response->metadata[0] != '\0') {
            n = snprintf(
                out,
                out_size,
                "%u data %s %zu %s\n",
                response->id,
                response->data_type,
                response->payload_size,
                response->metadata);
        } else {
            n = snprintf(
                out,
                out_size,
                "%u data %s %zu\n",
                response->id,
                response->data_type,
                response->payload_size);
        }
        return n > 0 && (size_t)n < out_size;
    }

    return false;
}

static const char *skip_ws(const char *s)
{
    while (s != NULL && (*s == ' ' || *s == '\t')) {
        s++;
    }
    return s;
}

static bool parse_number(const char *s, char **end, unsigned long *out)
{
    const char *p = s;
    int base = 0;

    if (s == NULL || out == NULL) {
        return false;
    }
    if (*p == '$') {
        p++;
        base = 16;
    }
    *out = strtoul(p, end, base);
    if (end != NULL && *end == p) {
        return false;
    }
    return true;
}

static bool parse_u32(const char *s, char **end, uint32_t *out)
{
    unsigned long v;
    if (!parse_number(s, end, &v)) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool parse_u16_addr(const char *s, char **end, uint16_t *out)
{
    unsigned long v;
    if (!parse_number(s, end, &v)) {
        return false;
    }
    if (v > 0xFFFFul) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

static bool parse_memory_mode(const char *token, uint8_t *out_mode)
{
    if (token == NULL || out_mode == NULL) {
        return false;
    }
    if (strcmp(token, "map") == 0) {
        *out_mode = CONTROL_MEMORY_MODE_MAP;
        return true;
    }
    if (strcmp(token, "main") == 0 || strcmp(token, "ram") == 0) {
        *out_mode = CONTROL_MEMORY_MODE_MAIN;
        return true;
    }
    if (strcmp(token, "aux") == 0) {
        *out_mode = CONTROL_MEMORY_MODE_AUX;
        return true;
    }
    if (strcmp(token, "lc1") == 0) {
        *out_mode = CONTROL_MEMORY_MODE_LC1;
        return true;
    }
    if (strcmp(token, "lc2") == 0) {
        *out_mode = CONTROL_MEMORY_MODE_LC2;
        return true;
    }
    if (strcmp(token, "rom") == 0) {
        *out_mode = CONTROL_MEMORY_MODE_ROM;
        return true;
    }
    return false;
}

static const char *memory_mode_name(uint8_t mode)
{
    switch (mode) {
    case CONTROL_MEMORY_MODE_MAIN:
        return "main";
    case CONTROL_MEMORY_MODE_AUX:
        return "aux";
    case CONTROL_MEMORY_MODE_LC1:
        return "lc1";
    case CONTROL_MEMORY_MODE_LC2:
        return "lc2";
    case CONTROL_MEMORY_MODE_ROM:
        return "rom";
    case CONTROL_MEMORY_MODE_MAP:
    default:
        return "map";
    }
}

/* Exported for dispatch metadata. */
const char *control_protocol_memory_mode_name(uint8_t mode)
{
    return memory_mode_name(mode);
}

static control_command_type lookup_command(const char *name)
{
    if (strcmp(name, "hello") == 0) return CONTROL_COMMAND_HELLO;
    if (strcmp(name, "version") == 0) return CONTROL_COMMAND_VERSION;
    if (strcmp(name, "capabilities") == 0) return CONTROL_COMMAND_CAPABILITIES;
    if (strcmp(name, "ping") == 0) return CONTROL_COMMAND_PING;
    if (strcmp(name, "quit-client") == 0) return CONTROL_COMMAND_QUIT_CLIENT;
    if (strcmp(name, "reset") == 0) return CONTROL_COMMAND_RESET;
    if (strcmp(name, "run") == 0) return CONTROL_COMMAND_RUN;
    if (strcmp(name, "pause") == 0) return CONTROL_COMMAND_PAUSE;
    if (strcmp(name, "step-cycle") == 0) return CONTROL_COMMAND_STEP_CYCLE;
    if (strcmp(name, "step-instruction") == 0) return CONTROL_COMMAND_STEP_INSTRUCTION;
    if (strcmp(name, "step-over") == 0) return CONTROL_COMMAND_STEP_OVER;
    if (strcmp(name, "step-out") == 0) return CONTROL_COMMAND_STEP_OUT;
    if (strcmp(name, "get-state") == 0) return CONTROL_COMMAND_GET_STATE;
    if (strcmp(name, "get-cpu") == 0) return CONTROL_COMMAND_GET_CPU;
    if (strcmp(name, "get-softswitches") == 0) return CONTROL_COMMAND_GET_SOFTSWITCHES;
    if (strcmp(name, "get-memory") == 0) return CONTROL_COMMAND_GET_MEMORY;
    if (strcmp(name, "set-memory") == 0) return CONTROL_COMMAND_SET_MEMORY;
    if (strcmp(name, "get-frame") == 0) return CONTROL_COMMAND_GET_FRAME;
    if (strcmp(name, "frame-ring-info") == 0) return CONTROL_COMMAND_FRAME_RING_INFO;
    if (strcmp(name, "frame-ring-record") == 0) return CONTROL_COMMAND_FRAME_RING_RECORD;
    if (strcmp(name, "frame-ring-clear") == 0) return CONTROL_COMMAND_FRAME_RING_CLEAR;
    if (strcmp(name, "get-frame-at") == 0) return CONTROL_COMMAND_GET_FRAME_AT;
    if (strcmp(name, "set-reg") == 0) return CONTROL_COMMAND_SET_REG;
    if (strcmp(name, "set-turbo") == 0) return CONTROL_COMMAND_SET_TURBO;
    if (strcmp(name, "break-exec") == 0) return CONTROL_COMMAND_BREAK_EXEC;
    if (strcmp(name, "break-clear") == 0) return CONTROL_COMMAND_BREAK_CLEAR;
    if (strcmp(name, "break-clear-all") == 0) return CONTROL_COMMAND_BREAK_CLEAR_ALL;
    if (strcmp(name, "break-enable") == 0) return CONTROL_COMMAND_BREAK_ENABLE;
    if (strcmp(name, "break-list") == 0) return CONTROL_COMMAND_BREAK_LIST;
    if (strcmp(name, "get-breakpoints") == 0) return CONTROL_COMMAND_BREAK_LIST;
    if (strcmp(name, "break-create") == 0) return CONTROL_COMMAND_BREAK_CREATE;
    if (strcmp(name, "break-update") == 0) return CONTROL_COMMAND_BREAK_UPDATE;
    if (strcmp(name, "rearm-oneshots") == 0) return CONTROL_COMMAND_REARM_ONESHOTS;
    if (strcmp(name, "wait-paused") == 0) return CONTROL_COMMAND_WAIT_PAUSED;
    if (strcmp(name, "wait-running") == 0) return CONTROL_COMMAND_WAIT_RUNNING;
    if (strcmp(name, "wait-frame") == 0) return CONTROL_COMMAND_WAIT_FRAME;
    if (strcmp(name, "wait-event") == 0) return CONTROL_COMMAND_WAIT_EVENT;
    if (strcmp(name, "save-state") == 0) return CONTROL_COMMAND_SAVE_STATE;
    if (strcmp(name, "load-state") == 0) return CONTROL_COMMAND_LOAD_STATE;
    if (strcmp(name, "key") == 0) return CONTROL_COMMAND_KEY;
    if (strcmp(name, "mount-disk") == 0) return CONTROL_COMMAND_MOUNT_DISK;
    if (strcmp(name, "history-info") == 0) return CONTROL_COMMAND_HISTORY_INFO;
    if (strcmp(name, "history-record") == 0) return CONTROL_COMMAND_HISTORY_RECORD;
    if (strcmp(name, "history-clear") == 0) return CONTROL_COMMAND_HISTORY_CLEAR;
    if (strcmp(name, "history-find") == 0) return CONTROL_COMMAND_HISTORY_FIND;
    if (strcmp(name, "history-next") == 0) return CONTROL_COMMAND_HISTORY_NEXT;
    if (strcmp(name, "history-read") == 0) return CONTROL_COMMAND_HISTORY_READ;
    if (strcmp(name, "history-close") == 0) return CONTROL_COMMAND_HISTORY_CLOSE;
    return CONTROL_COMMAND_NONE;
}

bool control_protocol_parse_request(
    const char *line,
    control_request *out_request,
    control_response *out_error)
{
    char buf[CONTROL_LINE_MAX];
    char *cursor;
    char *end = NULL;
    uint32_t id = 0;
    char cmd[64];
    size_t i;

    if (line == NULL || out_request == NULL) {
        return false;
    }

    memset(out_request, 0, sizeof(*out_request));
    out_request->args.timeout_ms = 2000u;
    out_request->args.slot = 6;
    out_request->args.drive = 0;
    out_request->args.memory_mode = CONTROL_MEMORY_MODE_MAP;
    out_request->args.wait_frame_delta = 1u;
    out_request->args.turbo_mode = 1000u; /* 1 MHz default */
    out_request->args.history_limit = 64u;
    out_request->args.history_before = 32u;
    out_request->args.history_after = 8u;

    strncpy(buf, line, sizeof(buf) - 1);
    for (i = 0; buf[i] != '\0'; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') {
            buf[i] = '\0';
            break;
        }
    }

    cursor = (char *)skip_ws(buf);
    if (*cursor == '\0') {
        if (out_error != NULL) {
            control_protocol_format_error(out_error, 0, "bad-request", "empty", false);
        }
        return false;
    }

    if (!parse_u32(cursor, &end, &id) || end == NULL) {
        if (out_error != NULL) {
            control_protocol_format_error(out_error, 0, "bad-id", "missing id", false);
        }
        return false;
    }
    cursor = (char *)skip_ws(end);
    if (*cursor == '\0') {
        if (out_error != NULL) {
            control_protocol_format_error(out_error, id, "bad-request", "missing command", false);
        }
        return false;
    }

    i = 0;
    while (cursor[i] != '\0' && !isspace((unsigned char)cursor[i]) && i + 1 < sizeof(cmd)) {
        cmd[i] = cursor[i];
        i++;
    }
    cmd[i] = '\0';
    cursor = (char *)skip_ws(cursor + i);

    out_request->id = id;
    out_request->type = lookup_command(cmd);
    if (out_request->type == CONTROL_COMMAND_NONE) {
        if (out_error != NULL) {
            control_protocol_format_error(out_error, id, "unknown-command", cmd, false);
        }
        return false;
    }

    switch (out_request->type) {
    case CONTROL_COMMAND_GET_MEMORY:
    case CONTROL_COMMAND_SET_MEMORY: {
        if (!parse_u16_addr(cursor, &end, &out_request->args.address)) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "address", false);
            }
            return false;
        }
        cursor = (char *)skip_ws(end);
        {
            uint32_t length = 0;
            if (!parse_u32(cursor, &end, &length) || length == 0u || length > 65536u) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "length", false);
                }
                return false;
            }
            out_request->args.length = length;
            cursor = (char *)skip_ws(end);
        }
        if (cursor[0] != '\0') {
            char mode_tok[16];
            size_t mi = 0;
            while (cursor[mi] != '\0' && !isspace((unsigned char)cursor[mi]) &&
                   mi + 1 < sizeof(mode_tok)) {
                mode_tok[mi] = (char)tolower((unsigned char)cursor[mi]);
                mi++;
            }
            mode_tok[mi] = '\0';
            if (!parse_memory_mode(mode_tok, &out_request->args.memory_mode)) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "mode", false);
                }
                return false;
            }
        }
        if (out_request->type == CONTROL_COMMAND_SET_MEMORY) {
            if (out_request->args.length > 1024u) {
                if (out_error != NULL) {
                    control_protocol_format_error(
                        out_error, id, "bad-args", "set-memory length max 1024", false);
                }
                return false;
            }
            out_request->payload_size = out_request->args.length;
        }
        break;
    }

    case CONTROL_COMMAND_SET_REG: {
        i = 0;
        while (cursor[i] != '\0' && !isspace((unsigned char)cursor[i]) &&
               i + 1 < sizeof(out_request->args.reg_name)) {
            out_request->args.reg_name[i] = (char)tolower((unsigned char)cursor[i]);
            i++;
        }
        out_request->args.reg_name[i] = '\0';
        cursor = (char *)skip_ws(cursor + i);
        {
            uint32_t v = 0;
            if (out_request->args.reg_name[0] == '\0' || !parse_u32(cursor, &end, &v)) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "set-reg", false);
                }
                return false;
            }
            out_request->args.reg_value = (uint16_t)v;
        }
        break;
    }

    case CONTROL_COMMAND_SET_TURBO: {
        /* Accept MHz number, "max", or "-1" (milli-MHz storage; 0 = max). */
        char token[64];
        size_t ti = 0;
        uint32_t milli = 0;
        const char *s = cursor;

        while (*s != '\0' && !isspace((unsigned char)*s) && ti + 1u < sizeof(token)) {
            token[ti++] = *s++;
        }
        token[ti] = '\0';
        if (ti == 0u || !runtime_turbo_parse_token(token, &milli)) {
            if (out_error != NULL) {
                control_protocol_format_error(
                    out_error, id, "bad-args", "turbo MHz|max|-1", false);
            }
            return false;
        }
        out_request->args.turbo_mode = milli;
        break;
    }

    case CONTROL_COMMAND_BREAK_EXEC: {
        if (!parse_u16_addr(cursor, &end, &out_request->args.address)) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "address", false);
            }
            return false;
        }
        break;
    }

    case CONTROL_COMMAND_BREAK_CLEAR: {
        if (cursor[0] == '\0' || strcmp(cursor, "all") == 0) {
            out_request->args.break_id = 0;
        } else {
            uint32_t bid = 0;
            if (!parse_u32(cursor, &end, &bid)) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "id", false);
                }
                return false;
            }
            out_request->args.break_id = bid;
        }
        break;
    }

    case CONTROL_COMMAND_BREAK_ENABLE: {
        uint32_t bid = 0;
        uint32_t en = 0;
        if (!parse_u32(cursor, &end, &bid)) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "id", false);
            }
            return false;
        }
        cursor = (char *)skip_ws(end);
        if (!parse_u32(cursor, &end, &en) || en > 1u) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "enable 0|1", false);
            }
            return false;
        }
        out_request->args.break_id = bid;
        out_request->args.break_enable = (uint8_t)en;
        break;
    }

    case CONTROL_COMMAND_BREAK_CREATE: {
        if (cursor[0] == '\0') {
            if (out_error != NULL) {
                control_protocol_format_error(
                    out_error, id, "bad-args", "expected breakpoint definition", false);
            }
            return false;
        }
        strncpy(out_request->args.text, cursor, sizeof(out_request->args.text) - 1);
        break;
    }

    case CONTROL_COMMAND_BREAK_UPDATE: {
        uint32_t bid = 0;
        if (!parse_u32(cursor, &end, &bid)) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "id", false);
            }
            return false;
        }
        cursor = (char *)skip_ws(end);
        if (cursor[0] == '\0') {
            if (out_error != NULL) {
                control_protocol_format_error(
                    out_error, id, "bad-args", "expected breakpoint definition", false);
            }
            return false;
        }
        out_request->args.break_id = bid;
        strncpy(out_request->args.text, cursor, sizeof(out_request->args.text) - 1);
        break;
    }

    case CONTROL_COMMAND_WAIT_PAUSED:
    case CONTROL_COMMAND_WAIT_RUNNING: {
        if (cursor[0] != '\0') {
            uint32_t t = 0;
            if (!parse_u32(cursor, &end, &t) || t < 1u || t > 600000u) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "timeout", false);
                }
                return false;
            }
            out_request->args.timeout_ms = t;
        }
        break;
    }

    case CONTROL_COMMAND_WAIT_FRAME: {
        uint32_t delta = 0;
        if (!parse_u32(cursor, &end, &delta) || delta < 1u) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "frame-delta", false);
            }
            return false;
        }
        out_request->args.wait_frame_delta = delta;
        cursor = (char *)skip_ws(end);
        if (cursor[0] != '\0') {
            uint32_t t = 0;
            if (!parse_u32(cursor, &end, &t) || t < 1u || t > 600000u) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "timeout", false);
                }
                return false;
            }
            out_request->args.timeout_ms = t;
        }
        break;
    }

    case CONTROL_COMMAND_WAIT_EVENT: {
        i = 0;
        while (cursor[i] != '\0' && !isspace((unsigned char)cursor[i]) &&
               i + 1 < sizeof(out_request->args.event_name)) {
            out_request->args.event_name[i] = cursor[i];
            i++;
        }
        out_request->args.event_name[i] = '\0';
        if (out_request->args.event_name[0] == '\0') {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "event-name", false);
            }
            return false;
        }
        cursor = (char *)skip_ws(cursor + i);
        if (cursor[0] != '\0') {
            uint32_t t = 0;
            if (!parse_u32(cursor, &end, &t) || t < 1u || t > 600000u) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "timeout", false);
                }
                return false;
            }
            out_request->args.timeout_ms = t;
        }
        break;
    }

    case CONTROL_COMMAND_FRAME_RING_RECORD: {
        if (strcmp(cursor, "on") == 0 || strcmp(cursor, "1") == 0) {
            out_request->args.frame_ring_record_enabled = true;
        } else if (strcmp(cursor, "off") == 0 || strcmp(cursor, "0") == 0) {
            out_request->args.frame_ring_record_enabled = false;
        } else {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "on|off", false);
            }
            return false;
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_RECORD: {
        if (strcmp(cursor, "on") == 0 || strcmp(cursor, "1") == 0) {
            out_request->args.history_record_enabled = true;
        } else if (strcmp(cursor, "off") == 0 || strcmp(cursor, "0") == 0) {
            out_request->args.history_record_enabled = false;
        } else {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "on|off", false);
            }
            return false;
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_FIND: {
        /* Optional key=value tokens; store remainder for dispatch parse. */
        strncpy(
            out_request->args.history_find_text,
            cursor,
            sizeof(out_request->args.history_find_text) - 1);
        break;
    }

    case CONTROL_COMMAND_HISTORY_NEXT: {
        unsigned long cursor_v = 0;
        uint32_t limit = 64;
        if (!parse_number(cursor, &end, &cursor_v) || cursor_v == 0ul) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "cursor", false);
            }
            return false;
        }
        out_request->args.history_cursor = (uint64_t)cursor_v;
        cursor = (char *)skip_ws(end);
        if (cursor[0] != '\0') {
            if (strncmp(cursor, "limit=", 6) == 0) {
                if (!parse_u32(cursor + 6, &end, &limit) || limit < 1u || limit > 256u) {
                    if (out_error != NULL) {
                        control_protocol_format_error(
                            out_error, id, "bad-args", "limit", false);
                    }
                    return false;
                }
            } else if (!parse_u32(cursor, &end, &limit) || limit < 1u || limit > 256u) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", "limit", false);
                }
                return false;
            }
            out_request->args.history_limit = (uint16_t)limit;
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_READ: {
        unsigned long id_v = 0;
        if (!parse_number(cursor, &end, &id_v) || id_v == 0ul) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "id", false);
            }
            return false;
        }
        out_request->args.history_id = (uint64_t)id_v;
        cursor = (char *)skip_ws(end);
        while (cursor[0] != '\0') {
            char key[16];
            size_t ki = 0;
            char *eq = strchr(cursor, '=');
            unsigned long v = 0;
            if (eq == NULL) {
                if (out_error != NULL) {
                    control_protocol_format_error(
                        out_error, id, "bad-args", "key=value", false);
                }
                return false;
            }
            while (cursor[ki] != '=' && ki + 1 < sizeof(key)) {
                key[ki] = cursor[ki];
                ki++;
            }
            key[ki] = '\0';
            if (!parse_number(eq + 1, &end, &v)) {
                if (out_error != NULL) {
                    control_protocol_format_error(out_error, id, "bad-args", key, false);
                }
                return false;
            }
            if (strcmp(key, "epoch") == 0) {
                out_request->args.history_epoch = (uint64_t)v;
            } else if (strcmp(key, "before") == 0) {
                if (v > 256ul) {
                    if (out_error != NULL) {
                        control_protocol_format_error(
                            out_error, id, "bad-args", "before", false);
                    }
                    return false;
                }
                out_request->args.history_before = (uint16_t)v;
            } else if (strcmp(key, "after") == 0) {
                if (v > 256ul) {
                    if (out_error != NULL) {
                        control_protocol_format_error(
                            out_error, id, "bad-args", "after", false);
                    }
                    return false;
                }
                out_request->args.history_after = (uint16_t)v;
            } else {
                if (out_error != NULL) {
                    control_protocol_format_error(
                        out_error, id, "bad-args", "unknown key", false);
                }
                return false;
            }
            cursor = (char *)skip_ws(end);
        }
        break;
    }

    case CONTROL_COMMAND_HISTORY_CLOSE: {
        unsigned long cursor_v = 0;
        if (cursor[0] == '\0') {
            out_request->args.history_cursor = 0;
            break;
        }
        if (!parse_number(cursor, &end, &cursor_v)) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "cursor", false);
            }
            return false;
        }
        out_request->args.history_cursor = (uint64_t)cursor_v;
        break;
    }

    case CONTROL_COMMAND_GET_FRAME_AT: {
        /* Require frame=<n> or cycle=<n> (named, never bare number). */
        char key[16];
        size_t ki = 0;
        char *eq;
        unsigned long v = 0;
        if (cursor[0] == '\0') {
            if (out_error != NULL) {
                control_protocol_format_error(
                    out_error, id, "bad-args", "frame=<n>|cycle=<n>", false);
            }
            return false;
        }
        while (cursor[ki] != '\0' && cursor[ki] != '=' && ki + 1 < sizeof(key)) {
            key[ki] = cursor[ki];
            ki++;
        }
        key[ki] = '\0';
        eq = strchr(cursor, '=');
        if (eq == NULL || !parse_number(eq + 1, &end, &v)) {
            if (out_error != NULL) {
                control_protocol_format_error(
                    out_error, id, "bad-args", "frame=<n>|cycle=<n>", false);
            }
            return false;
        }
        out_request->args.frame_ring_target = (uint64_t)v;
        if (strcmp(key, "frame") == 0) {
            out_request->args.frame_ring_by_cycle = false;
        } else if (strcmp(key, "cycle") == 0) {
            out_request->args.frame_ring_by_cycle = true;
        } else {
            if (out_error != NULL) {
                control_protocol_format_error(
                    out_error, id, "bad-args", "frame=<n>|cycle=<n>", false);
            }
            return false;
        }
        break;
    }

    case CONTROL_COMMAND_SAVE_STATE:
    case CONTROL_COMMAND_LOAD_STATE: {
        if (cursor[0] == '\0') {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "path", false);
            }
            return false;
        }
        strncpy(out_request->args.path, cursor, sizeof(out_request->args.path) - 1);
        break;
    }

    case CONTROL_COMMAND_KEY: {
        uint32_t k = 0;
        if (!parse_u32(cursor, &end, &k)) {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "key", false);
            }
            return false;
        }
        out_request->args.key = (uint8_t)k;
        break;
    }

    case CONTROL_COMMAND_MOUNT_DISK: {
        uint32_t slot = 6;
        char *p1 = cursor;
        char *e1 = NULL;
        if (parse_u32(p1, &e1, &slot) && e1 != p1 && (*e1 == ' ' || *e1 == '\t')) {
            char *p2 = (char *)skip_ws(e1);
            char *e2 = NULL;
            uint32_t d = 0;
            if (parse_u32(p2, &e2, &d) && e2 != p2 && (*e2 == ' ' || *e2 == '\t')) {
                out_request->args.slot = (uint8_t)slot;
                out_request->args.drive = (uint8_t)d;
                cursor = (char *)skip_ws(e2);
            } else {
                out_request->args.slot = (uint8_t)slot;
                cursor = p2;
            }
        }
        if (cursor[0] == '\0') {
            if (out_error != NULL) {
                control_protocol_format_error(out_error, id, "bad-args", "path", false);
            }
            return false;
        }
        strncpy(out_request->args.path, cursor, sizeof(out_request->args.path) - 1);
        break;
    }

    default:
        break;
    }

    return true;
}
