#define _POSIX_C_SOURCE 200112L
#include "dmc_fs.h"
#include "core/dmc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

dmc_handle dmc_file_open(const char *path, const char *mode) {
    FILE *file = fopen(path, mode);
    if (!file) { dmc_set_error("file open failed"); return -1; }
    dmc_handle handle = dmc_reserve(DMC_FILE, file, 0);
    if (handle == (dmc_handle)-1) fclose(file);
    return handle;
}

int dmc_file_read(dmc_handle handle, char *buffer, size_t capacity, size_t *written) {
    dmc_slot *slot = dmc_get(handle, DMC_FILE);
    if (!slot || !buffer || capacity == 0 || !written) return -1;
    *written = fread(buffer, 1, capacity - 1, (FILE *)slot->value);
    buffer[*written] = 0;
    return ferror((FILE *)slot->value) ? -1 : 0;
}

int dmc_file_write(dmc_handle handle, const char *text, size_t length, size_t *written) {
    dmc_slot *slot = dmc_get(handle, DMC_FILE);
    if (!slot || !text || !written) return -1;
    *written = fwrite(text, 1, length, (FILE *)slot->value);
    fflush((FILE *)slot->value);
    return *written == length ? 0 : -1;
}

int dmc_file_close(dmc_handle handle) {
    dmc_slot *slot = dmc_get(handle, DMC_FILE);
    if (!slot) return -1;
    int result = fclose((FILE *)slot->value);
    *slot = (dmc_slot){0};
    return result;
}