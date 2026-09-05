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
    if (memory < 0 || dmc_mem_write(memory, 0, 65) != 0 || dmc_mem_read(memory, 0, &value) != 0 || value != 65) return 1;
    if (dmc_mem_free(memory) != 0) return 2;

    dmc_handle file = dmc_file_open(SMOKE_FILE, "w");
    size_t count = 0;
    if (file < 0 || dmc_file_write(file, "hellfire", 8, &count) != 0 || count != 8 || dmc_file_close(file) != 0) return 3;
    file = dmc_file_open(SMOKE_FILE, "r");
    char buffer[32];
    if (file < 0 || dmc_file_read(file, buffer, sizeof(buffer), &count) != 0 || strcmp(buffer, "hellfire") != 0 || dmc_file_close(file) != 0) return 4;
    remove(SMOKE_FILE);
    puts("runtime ok");
    return 0;
}
