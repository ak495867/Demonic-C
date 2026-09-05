#define _POSIX_C_SOURCE 200112L
#include "dmc_mem.h"
#include "core/dmc_core.h"

dmc_handle dmc_mem_alloc(size_t size) {
    void *memory = calloc(size ? size : 1, 1);
    if (!memory) { dmc_set_error("memory allocation failed"); return -1; }
    dmc_handle handle = dmc_reserve(DMC_MEMORY, memory, size);
    if (handle == (dmc_handle)-1) free(memory);
    return handle;
}

int dmc_mem_size(dmc_handle handle, size_t *size) {
    dmc_slot *slot = dmc_get(handle, DMC_MEMORY);
    if (!slot || !size) return -1;
    *size = slot->size;
    return 0;
}

int dmc_mem_read(dmc_handle handle, size_t index, unsigned char *value) {
    dmc_slot *slot = dmc_get(handle, DMC_MEMORY);
    if (!slot || !value || index >= slot->size) { dmc_set_error("memory read outside buffer"); return -1; }
    *value = ((unsigned char *)slot->value)[index];
    return 0;
}

int dmc_mem_write(dmc_handle handle, size_t index, unsigned char value) {
    dmc_slot *slot = dmc_get(handle, DMC_MEMORY);
    if (!slot || index >= slot->size) { dmc_set_error("memory write outside buffer"); return -1; }
    ((unsigned char *)slot->value)[index] = value;
    return 0;
}

int dmc_mem_free(dmc_handle handle) {
    dmc_slot *slot = dmc_get(handle, DMC_MEMORY);
    if (!slot) return -1;
    free(slot->value);
    *slot = (dmc_slot){0};
    return 0;
}