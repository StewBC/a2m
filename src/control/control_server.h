#pragma once

#include "control_protocol.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct control_server control_server_t;

/* Legacy stub-compatible init: creates nothing if port==0. */
bool control_server_init(control_server_t *server, uint16_t port);
void control_server_shutdown(control_server_t *server);

/* Heap API (preferred). */
control_server_t *control_server_create(uint16_t port);
void control_server_destroy(control_server_t *server);

bool control_server_start(control_server_t *server);
void control_server_stop(control_server_t *server);

bool control_server_poll_request(control_server_t *server, control_request *out_request);
bool control_server_post_response(control_server_t *server, const control_response *response);

uint64_t control_server_connection_epoch(control_server_t *server);
bool control_server_has_client(control_server_t *server);
bool control_server_enabled(const control_server_t *server);
uint16_t control_server_port(const control_server_t *server);

typedef void (*control_server_wake_fn)(void);
void control_server_set_wake_hook(control_server_t *server, control_server_wake_fn fn);
