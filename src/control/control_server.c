#include "control_server.h"

#include "message_queue.h"
#include "mutex.h"
#include "platform_socket.h"
#include "thread.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CONTROL_QUEUE_CAPACITY = 32,
    CONTROL_RESPONSE_LINE_MAX = 512,
    /* Poll cadence while waiting for a deferred reply: short enough to notice
       peer close quickly, long enough to avoid a hot spin. */
    CONTROL_RESPONSE_WAIT_SLICE_MS = 50u,
    /* After peer disconnect, briefly drain late posts before accepting again. */
    CONTROL_DISCONNECT_DRAIN_MS = 250u
};

struct control_server {
    uint16_t port;
    bool enabled;
    bool started;
    bool stopping;
    bool owns_self; /* true if create() allocated */
    mutex *lock;
    platform_socket_listener *listener;
    /* Active client socket; only touch under lock. stop() closes it to wake
       blocked I/O; the worker still owns destroy after handle_connection. */
    platform_socket_connection *connection;
    message_queue *requests;
    message_queue *responses;
    thread *worker;
    uint64_t connection_epoch;
    bool has_client;
    control_server_wake_fn wake_hook;
};

static bool control_server_is_stopping(control_server_t *server)
{
    bool stopping;
    if (server == NULL || server->lock == NULL) {
        return true;
    }
    mutex_lock(server->lock);
    stopping = server->stopping;
    mutex_unlock(server->lock);
    return stopping;
}

static void control_server_set_has_client(control_server_t *server, bool has)
{
    if (server == NULL || server->lock == NULL) {
        return;
    }
    mutex_lock(server->lock);
    server->has_client = has;
    mutex_unlock(server->lock);
}

/* Peer gone: free the single client slot and bump epoch so dispatch cancels
   deferred work (wait-paused etc.) immediately via check_session. */
static void control_server_note_disconnect(control_server_t *server)
{
    if (server == NULL || server->lock == NULL) {
        return;
    }
    mutex_lock(server->lock);
    server->has_client = false;
    server->connection_epoch += 1u;
    if (server->connection_epoch == 0u) {
        server->connection_epoch = 1u;
    }
    mutex_unlock(server->lock);
}

static void control_server_discard_pending_responses(control_server_t *server)
{
    control_response response;

    if (server == NULL || server->responses == NULL) {
        return;
    }
    while (message_queue_try_pop(server->responses, &response)) {
        if (response.payload != NULL) {
            free(response.payload);
            response.payload = NULL;
        }
    }
}

/* True if the peer closed or the socket is in error. Does not consume
   application data unless EOF is detected (recv returns 0). */
static bool control_server_peer_gone(platform_socket_connection *connection)
{
    int ready;
    char ch;
    int n;

    if (connection == NULL) {
        return true;
    }
    ready = platform_socket_wait_readable(connection, 0u);
    if (ready < 0) {
        return true; /* error / HUP */
    }
    if (ready == 0) {
        return false;
    }
    /* Readable: FIN/RST → 0/-1; unexpected payload while waiting is a protocol
       violation for sequential A2M (treat as drop to free the port). */
    n = platform_socket_read(connection, &ch, 1);
    if (n == -2) {
        return false; /* would-block after poll race */
    }
    if (n <= 0) {
        return true; /* EOF or hard error */
    }
    /* Unexpected byte mid-response-wait: desync risk → drop session. */
    return true;
}

static bool control_server_send_response(
    platform_socket_connection *connection,
    const control_response *response);

static bool control_server_flush_unsolicited(
    control_server_t *server,
    platform_socket_connection *connection)
{
    control_response response;

    if (server == NULL || connection == NULL || server->responses == NULL) {
        return true;
    }
    while (message_queue_try_pop(server->responses, &response)) {
        if (!control_server_send_response(connection, &response)) {
            if (response.payload != NULL) {
                free(response.payload);
            }
            return false;
        }
        if (response.payload != NULL) {
            free(response.payload);
            response.payload = NULL;
        }
    }
    return true;
}

static bool control_server_read_line(
    control_server_t *server,
    platform_socket_connection *connection,
    char *out,
    size_t out_size)
{
    size_t used = 0;

    if (connection == NULL || out == NULL || out_size == 0) {
        return false;
    }

    while (used + 1 < out_size) {
        char ch;
        int n = platform_socket_read(connection, &ch, 1);
        if (n == -2) {
            /* Would block: flush events, then wait for peer data or close. */
            if (!control_server_flush_unsolicited(server, connection)) {
                return false;
            }
            if (platform_socket_wait_readable(connection, 50u) < 0) {
                return false;
            }
            continue;
        }
        if (n <= 0) {
            return false; /* EOF or error */
        }
        out[used++] = ch;
        if (ch == '\n') {
            out[used] = '\0';
            return true;
        }
    }
    return false;
}

static bool control_server_read_exact(
    platform_socket_connection *connection,
    uint8_t *out,
    size_t size)
{
    size_t used = 0;
    while (used < size) {
        int n = platform_socket_read(connection, out + used, size - used);
        if (n == -2) {
            if (platform_socket_wait_readable(connection, 2000u) <= 0) {
                return false;
            }
            continue;
        }
        if (n <= 0) {
            return false;
        }
        used += (size_t)n;
    }
    return true;
}

static bool control_server_send_response(
    platform_socket_connection *connection,
    const control_response *response)
{
    char line[CONTROL_RESPONSE_LINE_MAX];

    if (!control_protocol_write_response_line(line, sizeof(line), response)) {
        return false;
    }
    if (!platform_socket_write_all(connection, line, strlen(line))) {
        return false;
    }
    if (response->type == CONTROL_RESPONSE_DATA) {
        /* Counted payload may be empty (e.g. break-list count=0); still send
           the trailing newline so clients stay in sync. */
        if (response->payload_size > 0) {
            if (response->payload == NULL) {
                return false;
            }
            if (!platform_socket_write_all(
                    connection, response->payload, response->payload_size)) {
                return false;
            }
        }
        if (!platform_socket_write_all(connection, "\n", 1)) {
            return false;
        }
    }
    return true;
}

static void control_server_handle_connection(
    control_server_t *server,
    platform_socket_connection *connection)
{
    char line[CONTROL_LINE_MAX];
    bool peer_disconnected = false;

    /* Drop any orphaned replies from a previous session before serving. */
    control_server_discard_pending_responses(server);
    control_server_set_has_client(server, true);

    /* Nonblocking so peer-close checks never stall on recv. */
    (void)platform_socket_set_nonblocking(connection, true);

    while (!control_server_is_stopping(server)) {
        control_request request;
        control_response error;
        control_response response;
        bool got_response = false;

        memset(&request, 0, sizeof(request));
        memset(&error, 0, sizeof(error));
        memset(&response, 0, sizeof(response));

        if (!control_server_read_line(server, connection, line, sizeof(line))) {
            peer_disconnected = true;
            break;
        }

        if (!control_protocol_parse_request(line, &request, &error)) {
            (void)control_server_send_response(connection, &error);
            if (error.close_client) {
                break;
            }
            continue;
        }

        if (request.payload_size > 0) {
            char newline;
            request.payload = (uint8_t *)malloc(request.payload_size);
            if (request.payload == NULL ||
                !control_server_read_exact(
                    connection, request.payload, request.payload_size) ||
                !control_server_read_exact(connection, (uint8_t *)&newline, 1) ||
                newline != '\n') {
                control_request_release(&request);
                control_protocol_format_error(
                    &error, request.id, "bad-payload", "framing", true);
                (void)control_server_send_response(connection, &error);
                break;
            }
        }

        if (request.type == CONTROL_COMMAND_QUIT_CLIENT) {
            control_protocol_format_ok(&response, request.id, "bye");
            response.close_client = true;
            (void)control_server_send_response(connection, &response);
            control_request_release(&request);
            break;
        }

        /* Immediate identity commands handled on socket thread. */
        if (request.type == CONTROL_COMMAND_HELLO) {
            control_protocol_format_ok(
                &response,
                request.id,
                "name=" CONTROL_PROTOCOL_APP_NAME " protocol=" CONTROL_PROTOCOL_VERSION);
            (void)control_server_send_response(connection, &response);
            control_request_release(&request);
            continue;
        }
        if (request.type == CONTROL_COMMAND_VERSION) {
            control_protocol_format_ok(
                &response,
                request.id,
                "protocol=" CONTROL_PROTOCOL_VERSION " app=" CONTROL_PROTOCOL_APP_NAME);
            (void)control_server_send_response(connection, &response);
            control_request_release(&request);
            continue;
        }
        if (request.type == CONTROL_COMMAND_CAPABILITIES) {
            control_protocol_format_ok(
                &response,
                request.id,
                "connection introspection execution state softswitches step "
                "turbo frame frame-ring memory breakpoints wait key disk "
                "snapshot history assemble symbols sessions state-changed "
                "inspector");
            (void)control_server_send_response(connection, &response);
            control_request_release(&request);
            continue;
        }
        if (request.type == CONTROL_COMMAND_PING) {
            control_protocol_format_ok(&response, request.id, "");
            (void)control_server_send_response(connection, &response);
            control_request_release(&request);
            continue;
        }

        if (!message_queue_push(server->requests, &request)) {
            control_request_release(&request);
            control_protocol_format_error(
                &error, request.id, "busy", "request-queue-full", false);
            (void)control_server_send_response(connection, &error);
            continue;
        }
        if (server->wake_hook != NULL) {
            server->wake_hook();
        }

        /* Wait for deferred reply OR peer close — never spin only on the
           response queue (that wedges the single client slot until timeout).
           Unsolicited EVENT lines (id 0) may arrive first; flush them and keep
           waiting for the matching request reply. */
        while (!control_server_is_stopping(server)) {
            if (message_queue_wait_pop_timeout(
                    server->responses,
                    &response,
                    CONTROL_RESPONSE_WAIT_SLICE_MS)) {
                if (response.type == CONTROL_RESPONSE_EVENT || response.id == 0u) {
                    if (!control_server_send_response(connection, &response)) {
                        if (response.payload != NULL) {
                            free(response.payload);
                            response.payload = NULL;
                        }
                        peer_disconnected = true;
                        break;
                    }
                    if (response.payload != NULL) {
                        free(response.payload);
                        response.payload = NULL;
                    }
                    continue;
                }
                got_response = true;
                break;
            }
            if (control_server_peer_gone(connection)) {
                peer_disconnected = true;
                break;
            }
        }

        if (peer_disconnected) {
            break;
        }
        if (!got_response) {
            break; /* stopping */
        }

        if (!control_server_send_response(connection, &response)) {
            if (response.payload != NULL) {
                free(response.payload);
                response.payload = NULL;
            }
            peer_disconnected = true;
            break;
        }
        if (response.payload != NULL) {
            free(response.payload);
            response.payload = NULL;
        }
        if (response.close_client) {
            break;
        }
    }

    if (peer_disconnected) {
        control_server_note_disconnect(server);
        /* Brief drain so a near-simultaneous deferred completion is not left
           for the next client; main check_session cancels epoch-mismatched work. */
        {
            uint32_t start = (uint32_t)SDL_GetTicks();
            while ((uint32_t)SDL_GetTicks() - start < CONTROL_DISCONNECT_DRAIN_MS) {
                control_server_discard_pending_responses(server);
                if (control_server_is_stopping(server)) {
                    break;
                }
                SDL_Delay(5u);
            }
        }
        control_server_discard_pending_responses(server);
    } else {
        control_server_set_has_client(server, false);
        control_server_discard_pending_responses(server);
    }

    /* Close only: worker destroys under the lock so stop() cannot free the
       same pointer concurrently (see control_server_worker / stop). */
    platform_socket_connection_close(connection);
}

static int control_server_worker(void *userdata)
{
    control_server_t *server = (control_server_t *)userdata;

    if (server == NULL) {
        return 1;
    }

    while (!control_server_is_stopping(server)) {
        platform_socket_connection *conn = platform_socket_accept(server->listener);
        if (conn == NULL) {
            /* Listener closed by stop(), or accept error — exit the loop. */
            break;
        }

        mutex_lock(server->lock);
        server->connection_epoch += 1u;
        if (server->connection_epoch == 0u) {
            server->connection_epoch = 1u;
        }
        server->connection = conn;
        mutex_unlock(server->lock);

        control_server_handle_connection(server, conn);

        /* Clear and free under the lock so this destroy cannot race stop()
           closing the same pointer from the main thread. */
        mutex_lock(server->lock);
        server->connection = NULL;
        platform_socket_connection_destroy(conn);
        mutex_unlock(server->lock);
    }

    return 0;
}

static void control_server_zero(control_server_t *server)
{
    memset(server, 0, sizeof(*server));
}

bool control_server_init(control_server_t *server, uint16_t port)
{
    if (server == NULL) {
        return false;
    }
    control_server_zero(server);
    server->port = port;
    server->enabled = port != 0;
    server->owns_self = false;
    return true;
}

void control_server_shutdown(control_server_t *server)
{
    if (server == NULL) {
        return;
    }
    control_server_stop(server);
    if (server->owns_self) {
        /* destroy handles free */
        return;
    }
    control_server_zero(server);
}

control_server_t *control_server_create(uint16_t port)
{
    control_server_t *server = (control_server_t *)calloc(1, sizeof(*server));
    if (server == NULL) {
        return NULL;
    }
    if (!control_server_init(server, port)) {
        free(server);
        return NULL;
    }
    server->owns_self = true;
    return server;
}

void control_server_destroy(control_server_t *server)
{
    if (server == NULL) {
        return;
    }
    control_server_stop(server);
    free(server);
}

bool control_server_start(control_server_t *server)
{
    if (server == NULL || !server->enabled || server->started) {
        return false;
    }

    if (!platform_socket_startup()) {
        return false;
    }

    server->lock = mutex_create();
    server->requests = message_queue_create(sizeof(control_request), CONTROL_QUEUE_CAPACITY);
    server->responses = message_queue_create(sizeof(control_response), CONTROL_QUEUE_CAPACITY);
    if (server->lock == NULL || server->requests == NULL || server->responses == NULL) {
        control_server_stop(server);
        platform_socket_shutdown();
        return false;
    }

    server->stopping = false;
    server->connection = NULL;
    /* Listener is created on the starter thread and destroyed only in stop()
       after join — never by the worker (avoids UAF with concurrent close). */
    server->listener = platform_socket_listen_localhost(server->port);
    if (server->listener == NULL) {
        control_server_stop(server);
        platform_socket_shutdown();
        return false;
    }

    server->worker = thread_create("a2m-control", control_server_worker, server);
    if (server->worker == NULL) {
        /* stop() destroys the listener and calls platform_socket_shutdown(). */
        control_server_stop(server);
        return false;
    }
    server->started = true;
    return true;
}

void control_server_stop(control_server_t *server)
{
    control_request req;
    control_response resp;

    if (server == NULL) {
        return;
    }

    if (!server->started && server->worker == NULL && server->listener == NULL &&
        server->lock == NULL && server->requests == NULL && server->responses == NULL) {
        return;
    }

    if (server->lock != NULL) {
        mutex_lock(server->lock);
        server->stopping = true;
        /* Close the active client under the lock so we cannot close a pointer
           the worker is concurrently destroying. Close is idempotent and only
           shuts the socket to wake blocked poll/recv; worker still owns free. */
        if (server->connection != NULL) {
            platform_socket_connection_close(server->connection);
        }
        mutex_unlock(server->lock);
    } else {
        server->stopping = true;
    }

    /* Wake a blocking accept() without freeing the listener object yet. */
    if (server->listener != NULL) {
        platform_socket_listener_close(server->listener);
    }
    if (server->requests != NULL) {
        message_queue_wake_all(server->requests);
    }
    if (server->responses != NULL) {
        message_queue_wake_all(server->responses);
    }

    if (server->worker != NULL) {
        thread_join(server->worker);
        thread_destroy(server->worker);
        server->worker = NULL;
    }

    /* Exclusive ownership after join. */
    if (server->listener != NULL) {
        platform_socket_listener_destroy(server->listener);
        server->listener = NULL;
        platform_socket_shutdown();
    }

    if (server->requests != NULL) {
        while (message_queue_try_pop(server->requests, &req)) {
            control_request_release(&req);
        }
        message_queue_destroy(server->requests);
        server->requests = NULL;
    }
    if (server->responses != NULL) {
        while (message_queue_try_pop(server->responses, &resp)) {
            free(resp.payload);
        }
        message_queue_destroy(server->responses);
        server->responses = NULL;
    }
    if (server->lock != NULL) {
        mutex_destroy(server->lock);
        server->lock = NULL;
    }

    server->connection = NULL;
    server->started = false;
    server->has_client = false;
}

bool control_server_poll_request(control_server_t *server, control_request *out_request)
{
    if (server == NULL || out_request == NULL || server->requests == NULL) {
        return false;
    }
    return message_queue_try_pop(server->requests, out_request);
}

bool control_server_post_response(
    control_server_t *server,
    const control_response *response)
{
    if (server == NULL || response == NULL || server->responses == NULL) {
        return false;
    }
    return message_queue_push(server->responses, response);
}

uint64_t control_server_connection_epoch(control_server_t *server)
{
    uint64_t epoch = 0;
    if (server == NULL || server->lock == NULL) {
        return 0;
    }
    mutex_lock(server->lock);
    epoch = server->connection_epoch;
    mutex_unlock(server->lock);
    return epoch;
}

bool control_server_has_client(control_server_t *server)
{
    bool has = false;
    if (server == NULL || server->lock == NULL) {
        return false;
    }
    mutex_lock(server->lock);
    has = server->has_client;
    mutex_unlock(server->lock);
    return has;
}

bool control_server_enabled(const control_server_t *server)
{
    return server != NULL && server->enabled;
}

uint16_t control_server_port(const control_server_t *server)
{
    return server != NULL ? server->port : 0;
}

void control_server_set_wake_hook(control_server_t *server, control_server_wake_fn fn)
{
    if (server != NULL) {
        server->wake_hook = fn;
    }
}
