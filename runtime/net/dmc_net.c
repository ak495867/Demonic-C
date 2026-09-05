#define _POSIX_C_SOURCE 200112L
#include "dmc_net.h"
#include "core/dmc_core.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define DMC_CLOSE closesocket
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#define DMC_CLOSE close
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

dmc_handle dmc_tcp_connect(const char *host, unsigned short port) {
#ifdef _WIN32
    wsa_init();
#endif
    char service[16];
    snprintf(service, sizeof(service), "%u", port);
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &result) != 0) { dmc_set_error("address lookup failed"); return -1; }
    int socket_fd = (int)socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (socket_fd >= 0 && connect(socket_fd, result->ai_addr, (int)result->ai_addrlen) < 0) { DMC_CLOSE(socket_fd); socket_fd = -1; }
    freeaddrinfo(result);
    if (socket_fd < 0) { dmc_set_error("TCP connection failed"); return -1; }
    dmc_handle handle = dmc_reserve(DMC_SOCKET, (void *)(intptr_t)socket_fd, 0);
    if (handle == (dmc_handle)-1) DMC_CLOSE(socket_fd);
    return handle;
}

static int slot_fd(const dmc_slot *slot) {
    return (int)(intptr_t)slot->value;
}

int dmc_tcp_send(dmc_handle handle, const void *data, size_t length, size_t *sent) {
    dmc_slot *slot = dmc_get(handle, DMC_SOCKET);
    if (!slot || !data || !sent) return -1;
    int result = (int)send(slot_fd(slot), data, (int)length, 0);
    *sent = result < 0 ? 0 : (size_t)result;
    return result < 0 ? -1 : 0;
}

int dmc_tcp_recv(dmc_handle handle, void *buffer, size_t capacity, size_t *received) {
    dmc_slot *slot = dmc_get(handle, DMC_SOCKET);
    if (!slot || !buffer || !received) return -1;
    int result = (int)recv(slot_fd(slot), buffer, (int)capacity, 0);
    *received = result < 0 ? 0 : (size_t)result;
    return result < 0 ? -1 : 0;
}

int dmc_tcp_close(dmc_handle handle) {
    dmc_slot *slot = dmc_get(handle, DMC_SOCKET);
    if (!slot) return -1;
    int result = DMC_CLOSE(slot_fd(slot));
    *slot = (dmc_slot){0};
    return result;
}