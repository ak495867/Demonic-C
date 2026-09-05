#ifndef DMC_NET_H
#define DMC_NET_H

#include "core/dmc_core.h"
#include <stddef.h>

dmc_handle dmc_tcp_connect(const char *host, unsigned short port);
int dmc_tcp_send(dmc_handle handle, const void *data, size_t length, size_t *sent);
int dmc_tcp_recv(dmc_handle handle, void *buffer, size_t capacity, size_t *received);
int dmc_tcp_close(dmc_handle handle);

#endif