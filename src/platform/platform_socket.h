#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct platform_socket_listener platform_socket_listener;
typedef struct platform_socket_connection platform_socket_connection;

bool platform_socket_startup(void);
void platform_socket_shutdown(void);

platform_socket_listener *platform_socket_listen_localhost(uint16_t port);
void platform_socket_listener_close(platform_socket_listener *listener);
void platform_socket_listener_destroy(platform_socket_listener *listener);

platform_socket_connection *platform_socket_accept(platform_socket_listener *listener);
int platform_socket_read(platform_socket_connection *connection, void *buffer, size_t size);
bool platform_socket_write_all(
    platform_socket_connection *connection,
    const void *buffer,
    size_t size);
void platform_socket_connection_close(platform_socket_connection *connection);
void platform_socket_connection_destroy(platform_socket_connection *connection);

/* Nonblocking I/O helpers for control-port pipelining (Phase 2b). */
bool platform_socket_set_nonblocking(platform_socket_connection *connection, bool enabled);
/* Wait until readable, or timeout. Returns 1=readable, 0=timeout, -1=error/closed. */
int platform_socket_wait_readable(
    platform_socket_connection *connection,
    uint32_t timeout_ms);

