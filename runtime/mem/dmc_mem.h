#ifndef DMC_MEM_H
#define DMC_MEM_H

#include <stddef.h>

dmc_handle dmc_mem_alloc(size_t size);
int dmc_mem_size(dmc_handle handle, size_t *size);
int dmc_mem_read(dmc_handle handle, size_t index, unsigned char *value);
int dmc_mem_write(dmc_handle handle, size_t index, unsigned char value);
int dmc_mem_free(dmc_handle handle);

#endif