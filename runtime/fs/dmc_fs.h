#ifndef DMC_FS_H
#define DMC_FS_H

#include "core/dmc_core.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

dmc_handle dmc_file_open(const char *path, const char *mode);
int dmc_file_read(dmc_handle handle, char *buffer, size_t capacity, size_t *written);
int dmc_file_write(dmc_handle handle, const char *text, size_t length, size_t *written);
int dmc_file_close(dmc_handle handle);

#ifdef __cplusplus
}
#endif

#endif
