#include <stdio.h>

int c_compute_factorial(int n) {
    int res = 1;
    for (int i = 2; i <= n; i++) {
        res *= i;
    }
    return res;
}

const char* c_greet(const char* name) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "Hello from C99, %s!", name);
    return buf;
}
