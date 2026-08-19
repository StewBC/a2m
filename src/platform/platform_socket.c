#include "platform_socket.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET a2m_socket_handle;
#define A2M_INVALID_SOCKET INVALID_SOCKET
#define a2m_close_socket closesocket
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int a2m_socket_handle;
#define A2M_INVALID_SOCKET (-1)
#define a2m_close_socket close
#endif

struct platform_socket_listener {
    a2m_socket_handle handle;
};

struct platform_socket_connection {
    a2m_socket_handle handle;
};

bool platform_socket_startup(void)
{
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void platform_socket_shutdown(void)
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

platform_socket_listener *platform_socket_listen_localhost(uint16_t port)
{
    platform_socket_listener *listener;
    a2m_socket_handle handle;
    struct sockaddr_in addr;
    int one = 1;

    handle = socket(AF_INET, SOCK_STREAM, 0);
    if (handle == A2M_INVALID_SOCKET) {
        return NULL;
    }

    setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(handle, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(handle, 1) != 0) {
        a2m_close_socket(handle);
        return NULL;
    }

    listener = (platform_socket_listener *)calloc(1, sizeof(*listener));
    if (listener == NULL) {
        a2m_close_socket(handle);
        return NULL;
    }
    listener->handle = handle;
    return listener;
}

void platform_socket_listener_close(platform_socket_listener *listener)
{
    if (listener == NULL || listener->handle == A2M_INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    shutdown(listener->handle, SD_BOTH);
#else
    shutdown(listener->handle, SHUT_RDWR);
#endif
    a2m_close_socket(listener->handle);
    listener->handle = A2M_INVALID_SOCKET;
}

void platform_socket_listener_destroy(platform_socket_listener *listener)
{
    if (listener == NULL) {
        return;
    }
    platform_socket_listener_close(listener);
    free(listener);
}

platform_socket_connection *platform_socket_accept(platform_socket_listener *listener)
{
    platform_socket_connection *connection;
    a2m_socket_handle handle;

    if (listener == NULL || listener->handle == A2M_INVALID_SOCKET) {
        return NULL;
    }

    handle = accept(listener->handle, NULL, NULL);
    if (handle == A2M_INVALID_SOCKET) {
        return NULL;
    }

    connection = (platform_socket_connection *)calloc(1, sizeof(*connection));
    if (connection == NULL) {
        a2m_close_socket(handle);
        return NULL;
    }
    connection->handle = handle;
    return connection;
}

int platform_socket_read(platform_socket_connection *connection, void *buffer, size_t size)
{
    int received;

    if (connection == NULL || connection->handle == A2M_INVALID_SOCKET ||
        buffer == NULL || size == 0) {
        return -1;
    }

    received = (int)recv(connection->handle, (char *)buffer, (int)size, 0);
    if (received == 0) {
        return 0;
    }
    if (received < 0) {
#if defined(_WIN32)
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return -2; /* would block */
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -2; /* would block */
        }
#endif
        return -1;
    }
    return received;
}

bool platform_socket_write_all(
    platform_socket_connection *connection,
    const void *buffer,
    size_t size)
{
    const char *cursor = (const char *)buffer;
    size_t remaining = size;

    if (connection == NULL || connection->handle == A2M_INVALID_SOCKET ||
        (buffer == NULL && size > 0)) {
        return false;
    }

    while (remaining > 0) {
        int sent = (int)send(connection->handle, cursor, (int)remaining, 0);
        if (sent > 0) {
            cursor += sent;
            remaining -= (size_t)sent;
            continue;
        }
        if (sent == 0) {
            return false;
        }
#if defined(_WIN32)
        {
            int err = WSAGetLastError();
            if (err == WSAEINTR) {
                continue;
            }
            if (err == WSAEWOULDBLOCK) {
                fd_set write_set;
                struct timeval timeout;
                int ready;
                FD_ZERO(&write_set);
                FD_SET(connection->handle, &write_set);
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                ready = select(0, NULL, &write_set, NULL, &timeout);
                if (ready > 0) {
                    continue;
                }
            }
        }
#else
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd pfd;
            int ready;
            pfd.fd = connection->handle;
            pfd.events = POLLOUT | POLLERR | POLLHUP;
            pfd.revents = 0;
            ready = poll(&pfd, 1, 5000);
            if (ready > 0 &&
                (pfd.revents & POLLOUT) != 0 &&
                (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0) {
                continue;
            }
        }
#endif
        return false;
    }
    return true;
}

void platform_socket_connection_close(platform_socket_connection *connection)
{
    if (connection == NULL || connection->handle == A2M_INVALID_SOCKET) {
        return;
    }
#if defined(_WIN32)
    shutdown(connection->handle, SD_BOTH);
#else
    shutdown(connection->handle, SHUT_RDWR);
#endif
    a2m_close_socket(connection->handle);
    connection->handle = A2M_INVALID_SOCKET;
}

void platform_socket_connection_destroy(platform_socket_connection *connection)
{
    if (connection == NULL) {
        return;
    }
    platform_socket_connection_close(connection);
    free(connection);
}

bool platform_socket_set_nonblocking(platform_socket_connection *connection, bool enabled)
{
    if (connection == NULL || connection->handle == A2M_INVALID_SOCKET) {
        return false;
    }
#if defined(_WIN32)
    u_long mode = enabled ? 1ul : 0ul;
    return ioctlsocket(connection->handle, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(connection->handle, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(connection->handle, F_SETFL, flags) == 0;
#endif
}

int platform_socket_wait_readable(
    platform_socket_connection *connection,
    uint32_t timeout_ms)
{
    if (connection == NULL || connection->handle == A2M_INVALID_SOCKET) {
        return -1;
    }
#if defined(_WIN32)
    fd_set read_fds;
    struct timeval tv;
    int result;

    FD_ZERO(&read_fds);
    FD_SET(connection->handle, &read_fds);
    tv.tv_sec = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
    result = select(0, &read_fds, NULL, NULL, &tv);
    if (result < 0) {
        return -1;
    }
    if (result == 0) {
        return 0;
    }
    return 1;
#else
    struct pollfd pfd;
    int result;

    pfd.fd = connection->handle;
    pfd.events = POLLIN | POLLERR | POLLHUP;
    pfd.revents = 0;
    result = poll(&pfd, 1, (int)timeout_ms);
    if (result < 0) {
        return -1;
    }
    if (result == 0) {
        return 0;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return -1;
    }
    if (pfd.revents & POLLIN) {
        return 1;
    }
    return 0;
#endif
}
