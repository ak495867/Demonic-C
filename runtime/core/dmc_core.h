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
#define DMC_MIN_HANDLES 16u
#define DMC_MAX_HANDLES 1048576u

extern dmc_slot *dmc_slots;
extern size_t dmc_slots_cap;
extern size_t dmc_slots_used;
extern void dmc_set_error(const char *message);
extern dmc_handle dmc_reserve(dmc_kind kind, void *value, size_t size);
extern dmc_slot *dmc_get(dmc_handle handle, dmc_kind kind);
extern int dmc_release(dmc_handle handle);
extern size_t dmc_handle_count(void);
extern size_t dmc_handle_capacity(void);
extern void wsa_init(void);
const char *dmc_last_error(void);
const char *dmc_error_code_text(int code);

#ifdef __cplusplus
}
#endif

#endif