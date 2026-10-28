#include "node_id.h"

#include <stddef.h>

#include "config.h"

int parse_node_id(const char* s) {
    if (s == nullptr || s[0] == '\0') return -1;
    int n = 0;
    for (const char* p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return -1;
        n = n * 10 + (*p - '0');
        if (n > MAX_NODES) return -1;
    }
    return n;
}
