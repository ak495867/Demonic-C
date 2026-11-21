#include "dmc_runtime.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define SMOKE_FILE "dmc_runtime_smoke.txt"
#else
#define SMOKE_FILE "/tmp/dmc-runtime-smoke.txt"
#endif

int main(void) {
    dmc_handle memory = dmc_mem_alloc(2);
    unsigned char value = 0;
    if (memory == (dmc_handle)-1 || dmc_mem_write(memory, 0, 65) != 0 || dmc_mem_read(memory, 0, &value) != 0 || value != 65) return 1;
    if (dmc_mem_free(memory) != 0) return 2;

    dmc_handle file = dmc_file_open(SMOKE_FILE, "w");
    size_t count = 0;
    if (file == (dmc_handle)-1 || dmc_file_write(file, "hellfire", 8, &count) != 0 || count != 8 || dmc_file_close(file) != 0) return 3;
    file = dmc_file_open(SMOKE_FILE, "r");
    char buffer[32];
    if (file == (dmc_handle)-1 || dmc_file_read(file, buffer, sizeof(buffer), &count) != 0 || strcmp(buffer, "hellfire") != 0 || dmc_file_close(file) != 0) return 4;
    remove(SMOKE_FILE);

    for (int i = 0; i < 2100000; ++i) {
        dmc_handle h = dmc_mem_alloc(8);
        if (h == (dmc_handle)-1) return 5;
        if (dmc_mem_free(h) != 0) return 6;
    }
    if (dmc_handle_count() != 0) return 7;

    dmc_handle a = dmc_mem_alloc(4);
    dmc_handle b = dmc_mem_alloc(4);
    if (a == b || dmc_mem_free(a) != 0) return 8;
    if (dmc_mem_free(a) == 0) return 9;
    dmc_handle c = dmc_mem_alloc(4);
    if (c == b || dmc_mem_free(b) != 0 || dmc_mem_free(c) != 0) return 10;
    if (dmc_mem_alloc(0) != (dmc_handle)-1) return 11;
    puts("runtime ok");
    return 0;
}
