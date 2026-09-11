#define _POSIX_C_SOURCE 200112L
#include "dmc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define DMC_CLOSE closesocket
static int wsa_started = 0;
void wsa_init(void) {
    if (!wsa_started) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2,2), &wsa);
        wsa_started = 1;
    }
}
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#define DMC_CLOSE close
#endif

dmc_slot *dmc_slots = NULL;
size_t dmc_slots_cap = 0;
size_t dmc_slots_used = 0;
char dmc_error_text[256];

#ifdef DMC_RUNTIME_THREADS
#include <pthread.h>
static pthread_mutex_t dmc_mutex = PTHREAD_MUTEX_INITIALIZER;
#define DMC_LOCK() pthread_mutex_lock(&dmc_mutex)
#define DMC_UNLOCK() pthread_mutex_unlock(&dmc_mutex)
#else
#define DMC_LOCK() ((void)0)
#define DMC_UNLOCK() ((void)0)
#endif

void dmc_set_error(const char *message) {
    if (!message) message = "";
    snprintf(dmc_error_text, sizeof(dmc_error_text), "%s", message);
}

const char *dmc_last_error(void) {
    return dmc_error_text;
}

static int dmc_ensure_capacity(size_t needed) {
    if (needed < DMC_MIN_HANDLES) needed = DMC_MIN_HANDLES;
    if (needed > DMC_MAX_HANDLES) {
        dmc_set_error("handle table would exceed maximum size");
        return -1;
    }
    if (needed <= dmc_slots_cap) return 0;
    size_t new_cap = dmc_slots_cap ? dmc_slots_cap * 2 : DMC_MIN_HANDLES;
    if (new_cap < needed) new_cap = needed;
    if (new_cap > DMC_MAX_HANDLES) new_cap = DMC_MAX_HANDLES;
    dmc_slot *grown = (dmc_slot *)realloc(dmc_slots, new_cap * sizeof(dmc_slot));
    if (!grown) {
        dmc_set_error("out of memory growing handle table");
        return -1;
    }
    for (size_t i = dmc_slots_cap; i < new_cap; ++i) {
        grown[i].kind = DMC_NONE;
        grown[i].value = NULL;
        grown[i].size = 0;
    }
    dmc_slots = grown;
    dmc_slots_cap = new_cap;
    return 0;
}

dmc_handle dmc_reserve(dmc_kind kind, void *value, size_t size) {
    if (kind == DMC_NONE) {
        dmc_set_error("invalid handle kind");
        return (dmc_handle)-1;
    }
    DMC_LOCK();
    if (dmc_ensure_capacity(dmc_slots_used + 1) != 0) {
        DMC_UNLOCK();
        return (dmc_handle)-1;
    }
    size_t slot = dmc_slots_used++;
    dmc_slots[slot].kind = kind;
    dmc_slots[slot].value = value;
    dmc_slots[slot].size = size;
    DMC_UNLOCK();
    return (dmc_handle)slot;
}

dmc_slot *dmc_get(dmc_handle handle, dmc_kind kind) {
    if (handle < 0 || (size_t)handle >= dmc_slots_cap || dmc_slots[handle].kind != kind) {
        dmc_set_error("invalid runtime handle");
        return NULL;
    }
    return &dmc_slots[handle];
}

int dmc_release(dmc_handle handle) {
    if (handle < 0 || (size_t)handle >= dmc_slots_cap || dmc_slots[handle].kind == DMC_NONE) {
        dmc_set_error("invalid runtime handle");
        return -1;
    }
    DMC_LOCK();
    dmc_slots[handle].kind = DMC_NONE;
    dmc_slots[handle].value = NULL;
    dmc_slots[handle].size = 0;
    DMC_UNLOCK();
    return 0;
}

size_t dmc_handle_count(void) {
    return dmc_slots_used;
}

size_t dmc_handle_capacity(void) {
    return dmc_slots_cap;
}

const char *dmc_error_code_text(int code) {
    switch (code) {
        case 0: return "success";
        case -1: return "generic failure";
        case -2: return "invalid argument";
        case -3: return "out of memory";
        case -4: return "handle not found";
        case -5: return "resource exhausted";
        case -6: return "io error";
        case -7: return "network error";
        default: return "unknown error";
    }
}