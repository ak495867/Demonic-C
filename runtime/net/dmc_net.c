#include <limits.h>

#ifdef _WIN32
#define DMC_SOCKET_TYPE SOCKET
#define DMC_BAD_SOCKET INVALID_SOCKET
#else
#define DMC_SOCKET_TYPE int
#define DMC_BAD_SOCKET (-1)
#endif

#define DMC_INVALID_HANDLE ((dmc_handle)-1)

dmc_handle dmc_tcp_connect(const char *host, unsigned short port) {
    if (!host) {
        dmc_set_error("tcp_connect: null host");
        return DMC_INVALID_HANDLE;
    }
#ifdef _WIN32
    wsa_init();
#endif
    char service[16];
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    struct addrinfo *result = NULL;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, service, &hints, &result) != 0) {
        dmc_set_error("address lookup failed");
        return DMC_INVALID_HANDLE;
    }
    DMC_SOCKET_TYPE socket_fd = DMC_BAD_SOCKET;
    for (struct addrinfo *candidate = result; candidate; candidate = candidate->ai_next) {
        DMC_SOCKET_TYPE fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd == DMC_BAD_SOCKET) continue;
        if (connect(fd, candidate->ai_addr, (int)candidate->ai_addrlen) == 0) {
            socket_fd = fd;
            break;
        }
        DMC_CLOSE(fd);
    }
    freeaddrinfo(result);
    if (socket_fd == DMC_BAD_SOCKET) {
        dmc_set_error("TCP connection failed");
        return DMC_INVALID_HANDLE;
    }
    dmc_handle handle = dmc_reserve(DMC_SOCKET, (void *)(intptr_t)socket_fd, 0);
    if (handle == DMC_INVALID_HANDLE) DMC_CLOSE(socket_fd);
    return handle;
}

static DMC_SOCKET_TYPE slot_fd(const dmc_slot *slot) {
    return (DMC_SOCKET_TYPE)(intptr_t)slot->value;
}

static int dmc_clamp_io(size_t length) {
    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

int dmc_tcp_send(dmc_handle handle, const void *data, size_t length, size_t *sent) {
    dmc_slot *slot = dmc_get(handle, DMC_SOCKET);
    if (!slot || !data || !sent) return -1;
    int result = (int)send(slot_fd(slot), (const char *)data, dmc_clamp_io(length), 0);
    *sent = result < 0 ? 0 : (size_t)result;
    return result < 0 ? -1 : 0;
}

int dmc_tcp_recv(dmc_handle handle, void *buffer, size_t capacity, size_t *received) {
    dmc_slot *slot = dmc_get(handle, DMC_SOCKET);
    if (!slot || !buffer || !received) return -1;
    int result = (int)recv(slot_fd(slot), (char *)buffer, dmc_clamp_io(capacity), 0);
    *received = result < 0 ? 0 : (size_t)result;
    return result < 0 ? -1 : 0;
}

int dmc_tcp_close(dmc_handle handle) {
    dmc_slot *slot = dmc_get(handle, DMC_SOCKET);
    if (!slot) return -1;
    int result = DMC_CLOSE(slot_fd(slot));
    dmc_release(handle);
    return result == 0 ? 0 : -1;
}
