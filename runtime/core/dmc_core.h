#ifndef DMC_CORE_H
#define DMC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { DMC_NONE, DMC_MEMORY, DMC_FILE, DMC_SOCKET } dmc_kind;
typedef struct { dmc_kind kind; void *value; size_t size; } dmc_slot;
typedef unsigned long long dmc_handle;
#define DMC_MAX_HANDLES 1024

extern dmc_slot dmc_slots[];
extern char dmc_error_text[];
extern void dmc_set_error(const char *message);
extern dmc_handle dmc_reserve(dmc_kind kind, void *value, size_t size);
extern dmc_slot *dmc_get(dmc_handle handle, dmc_kind kind);
extern void wsa_init(void);
const char *dmc_last_error(void);

#ifdef __cplusplus
}
#endif

#endif