#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CONTROL_LINE_MAX = 512,
    CONTROL_RESPONSE_TEXT_MAX = 512,
    CONTROL_PROTOCOL_NAME_MAX = 32
};

/* Product wire identity. Bump when scripts must learn new behaviour. */
#define CONTROL_PROTOCOL_VERSION "A2M/13"
#define CONTROL_PROTOCOL_APP_NAME "a2m"

typedef enum control_command_type {
    CONTROL_COMMAND_NONE = 0,
    CONTROL_COMMAND_HELLO,
    CONTROL_COMMAND_VERSION,
    CONTROL_COMMAND_CAPABILITIES,
    CONTROL_COMMAND_PING,
    CONTROL_COMMAND_QUIT_CLIENT,
    CONTROL_COMMAND_RESET,
    CONTROL_COMMAND_RUN,
    CONTROL_COMMAND_PAUSE,
    CONTROL_COMMAND_STEP_CYCLE,
    CONTROL_COMMAND_STEP_INSTRUCTION,
    CONTROL_COMMAND_STEP_OVER,
    CONTROL_COMMAND_STEP_OUT,
    CONTROL_COMMAND_GET_STATE,
    CONTROL_COMMAND_GET_CPU,
    CONTROL_COMMAND_GET_SOFTSWITCHES,
    CONTROL_COMMAND_GET_MEMORY,
    CONTROL_COMMAND_SET_MEMORY,
    CONTROL_COMMAND_GET_FRAME,
    CONTROL_COMMAND_FRAME_RING_INFO,
    CONTROL_COMMAND_FRAME_RING_RECORD,
    CONTROL_COMMAND_FRAME_RING_CLEAR,
    CONTROL_COMMAND_GET_FRAME_AT,
    CONTROL_COMMAND_SET_REG,
    CONTROL_COMMAND_SET_TURBO,
    CONTROL_COMMAND_BREAK_EXEC,
    CONTROL_COMMAND_BREAK_CLEAR,
    CONTROL_COMMAND_BREAK_CLEAR_ALL,
    CONTROL_COMMAND_BREAK_ENABLE,
    CONTROL_COMMAND_BREAK_LIST,
    CONTROL_COMMAND_BREAK_CREATE,
    CONTROL_COMMAND_BREAK_UPDATE,
    CONTROL_COMMAND_REARM_ONESHOTS,
    CONTROL_COMMAND_WAIT_PAUSED,
    CONTROL_COMMAND_WAIT_RUNNING,
    CONTROL_COMMAND_WAIT_FRAME,
    CONTROL_COMMAND_WAIT_EVENT,
    CONTROL_COMMAND_SAVE_STATE,
    CONTROL_COMMAND_LOAD_STATE,
    CONTROL_COMMAND_KEY,
    CONTROL_COMMAND_MOUNT_DISK,
    CONTROL_COMMAND_MOUNT,
    CONTROL_COMMAND_UNMOUNT,
    CONTROL_COMMAND_SELECT_DISK,
    CONTROL_COMMAND_SET_DISK_WRITABLE,
    CONTROL_COMMAND_HISTORY_INFO,
    CONTROL_COMMAND_HISTORY_RECORD,
    CONTROL_COMMAND_HISTORY_CLEAR,
    CONTROL_COMMAND_HISTORY_FIND,
    CONTROL_COMMAND_HISTORY_NEXT,
    CONTROL_COMMAND_HISTORY_READ,
    CONTROL_COMMAND_HISTORY_CLOSE,
    CONTROL_COMMAND_ASSEMBLE,
    CONTROL_COMMAND_FIND_SYMBOL,
    CONTROL_COMMAND_LEAVE_INSPECTOR
} control_command_type;

typedef enum control_memory_mode {
    CONTROL_MEMORY_MODE_MAP = 0,
    CONTROL_MEMORY_MODE_MAIN = 1,
    CONTROL_MEMORY_MODE_AUX = 2,
    CONTROL_MEMORY_MODE_LC1 = 3,
    CONTROL_MEMORY_MODE_LC2 = 4,
    CONTROL_MEMORY_MODE_ROM = 5
} control_memory_mode;

/* mount/unmount card selection (0 = infer / resolve uniquely). */
typedef enum control_media_kind {
    CONTROL_MEDIA_KIND_UNSPECIFIED = 0,
    CONTROL_MEDIA_KIND_DISKII = 1,
    CONTROL_MEDIA_KIND_SMARTPORT = 2
} control_media_kind;

typedef struct control_args {
    uint32_t timeout_ms;
    uint16_t address;
    uint32_t length;
    uint8_t memory_mode;
    uint8_t key;
    uint8_t slot;
    uint8_t drive;
    uint8_t media_kind; /* control_media_kind */
    uint32_t disk_index; /* select-disk: 1-based queue index */
    uint8_t disk_writable; /* set-disk-writable: 0=RO 1=RW */
    uint32_t break_id; /* 0 = all for break-clear */
    uint8_t break_enable; /* break-enable 0|1 */
    /* set-turbo: milli-MHz (1000 = 1 MHz) or 0 = max. */
    uint32_t turbo_mode;
    uint32_t wait_frame_delta; /* wait-frame positive delta */
    char reg_name[8];
    uint16_t reg_value;
    char event_name[48];
    char path[CONTROL_LINE_MAX];
    /* Remainder of line for break-create / break-update definitions. */
    char text[CONTROL_LINE_MAX];
    /* Frame ring: get-frame-at frame=<n>|cycle=<n>; record on|off. */
    uint64_t frame_ring_target;
    bool frame_ring_by_cycle;
    bool frame_ring_record_enabled;
    /* History control (A2M/5). */
    bool history_record_enabled;
    uint64_t history_cursor;
    uint64_t history_id;
    uint64_t history_epoch;
    uint16_t history_limit;
    uint16_t history_before;
    uint16_t history_after;
    /* Remainder of line for history-find key=value options. */
    char history_find_text[CONTROL_LINE_MAX];
    /* assemble: optional key=value before source path (defaults match Assembler tab). */
    uint16_t run_address;
    bool has_run_address;
    bool auto_run;
    bool mli_launch;
    bool reset_first;
    bool auto_adjust_segments;
} control_args;

typedef enum control_response_type {
    CONTROL_RESPONSE_OK = 0,
    CONTROL_RESPONSE_ERROR,
    CONTROL_RESPONSE_DATA,
    /* Unsolicited out-of-band line: "<id> event <name> [fields...]".
       Request id 0 is the reserved event channel. */
    CONTROL_RESPONSE_EVENT
} control_response_type;

typedef struct control_request {
    uint32_t id;
    control_command_type type;
    control_args args;
    uint8_t *payload;
    size_t payload_size;
} control_request;

typedef struct control_response {
    uint32_t id;
    control_response_type type;
    char text[CONTROL_RESPONSE_TEXT_MAX];
    char data_type[32];
    char metadata[CONTROL_RESPONSE_TEXT_MAX];
    uint8_t *payload;
    size_t payload_size;
    bool close_client;
} control_response;

bool control_protocol_parse_request(
    const char *line,
    control_request *out_request,
    control_response *out_error);

void control_protocol_format_ok(
    control_response *response,
    uint32_t id,
    const char *text);

void control_protocol_format_error(
    control_response *response,
    uint32_t id,
    const char *code,
    const char *message,
    bool close_client);

void control_protocol_format_event(
    control_response *response,
    uint32_t id,
    const char *text);

void control_protocol_format_data(
    control_response *response,
    uint32_t id,
    const char *data_type,
    const char *metadata,
    uint8_t *payload,
    size_t payload_size);

void control_request_release(control_request *request);
void control_response_release(control_response *response);

/* Write one wire response (text or data). Caller retains ownership of payload. */
bool control_protocol_write_response_line(
    char *out,
    size_t out_size,
    const control_response *response);

const char *control_protocol_memory_mode_name(uint8_t mode);
