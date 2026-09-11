#define _POSIX_C_SOURCE 200112L
#include "dmc_fs.h"
#include "core/dmc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

dmc_handle dmc_file_open(const char *path, const char *mode) {
    if (!path || !mode) {
        dmc_set_error("file_open: null argument");
        return (dmc_handle)-1;
    }
    FILE *file = fopen(path, mode);
    if (!file) {
        dmc_set_error("file open failed");
        return (dmc_handle)-1;
    }
    dmc_handle handle = dmc_reserve(DMC_FILE, file, 0);
    if (handle == (dmc_handle)-1) fclose(file);
    return handle;
}

int dmc_file_read(dmc_handle handle, char *buffer, size_t capacity, size_t *written) {
    if (!buffer || capacity == 0 || !written) {
        dmc_set_error("file_read: invalid argument");
        return -1;
    }
    dmc_slot *slot = dmc_get(handle, DMC_FILE);
    if (!slot) return -1;
    FILE *fp = (FILE *)slot->value;
    if (!fp) {
        dmc_set_error("file_read: invalid file");
        return -1;
    }
    /* Reserve one byte for null terminator; never overflow buffer */
    size_t to_read = capacity - 1;
    size_t got = fread(buffer, 1, to_read, fp);
    buffer[got] = '\0';
    *written = got;
    if (ferror(fp)) {
        dmc_set_error("file_read: read error");
        return -1;
    }
    return 0;
}

int dmc_file_write(dmc_handle handle, const char *text, size_t length, size_t *written) {
    if (!text || !written) {
        dmc_set_error("file_write: invalid argument");
        return -1;
    }
    dmc_slot *slot = dmc_get(handle, DMC_FILE);
    if (!slot) return -1;
    FILE *fp = (FILE *)slot->value;
    if (!fp) {
        dmc_set_error("file_write: invalid file");
        return -1;
    }
    size_t wrote = fwrite(text, 1, length, fp);
    if (fflush(fp) != 0) {
        dmc_set_error("file_write: flush failed");
        return -1;
    }
    *written = wrote;
    return wrote == length ? 0 : -1;
}

int dmc_file_close(dmc_handle handle) {
    dmc_slot *slot = dmc_get(handle, DMC_FILE);
    if (!slot) return -1;
    FILE *fp = (FILE *)slot->value;
    if (!fp) {
        *slot = (dmc_slot){0};
        return 0;
    }
    int result = fclose(fp);
    *slot = (dmc_slot){0};
    return result == 0 ? 0 : -1;
}