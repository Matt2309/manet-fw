#include "runfile.h"

#include <stdio.h>
#include <string.h>

int run_number(const char* name) {
    if (name == nullptr) return -1;
    if (name[0] == '/') name++;
    // "run_" + 3 cifre + ".csv"
    if (strlen(name) != 11) return -1;
    if (strncmp(name, "run_", 4) != 0 || strcmp(name + 7, ".csv") != 0) return -1;
    int n = 0;
    for (int i = 4; i < 7; i++) {
        if (name[i] < '0' || name[i] > '9') return -1;
        n = n * 10 + (name[i] - '0');
    }
    return (n >= 1 && n <= RUN_MAX) ? n : -1;
}

bool run_name(int n, char* out, size_t cap) {
    if (n < 1 || n > RUN_MAX) return false;
    int w = snprintf(out, cap, "/run_%03d.csv", n);
    return w > 0 && static_cast<size_t>(w) < cap;
}

int run_first_free(const int* used, size_t count) {
    for (int n = 1; n <= RUN_MAX; n++) {
        bool presente = false;
        for (size_t i = 0; i < count; i++) {
            if (used[i] == n) {
                presente = true;
                break;
            }
        }
        if (!presente) return n;
    }
    return -1;
}
