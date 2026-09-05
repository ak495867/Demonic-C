#define _POSIX_C_SOURCE 200112L
#include "dmc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

dmc_slot dmc_slots[DMC_MAX_HANDLES];
char dmc_error_text[256];

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

void dmc_set_error(const char *message) {
    snprintf(dmc_error_text, sizeof(dmc_error_text), "%s", message);
}

const char *dmc_last_error(void) {
    return dmc_error_text;
}

dmc_handle dmc_reserve(dmc_kind kind, void *value, size_t size) {
    for (int i = 1; i < DMC_MAX_HANDLES; ++i) {
        if (dmc_slots[i].kind == DMC_NONE) {
            dmc_slots[i] = (dmc_slot){kind, value, size};
            return (dmc_handle)i;
        }
    }
    dmc_set_error("handle table is full");
    return (dmc_handle)-1;
}

dmc_slot *dmc_get(dmc_handle handle, dmc_kind kind) {
    if (handle <= 0 || handle >= DMC_MAX_HANDLES || dmc_slots[handle].kind != kind) {
        dmc_set_error("invalid runtime handle");
        return NULL;
    }
    return &dmc_slots[handle];
}