#include "ksym.h"
#include <stddef.h>

__attribute__((weak)) const ksym_entry_t ksym_table[] = {};
__attribute__((weak)) const uint32_t ksym_count = 0;

const char *ksym_lookup(uint32_t addr) {
    if (ksym_count == 0) {
        return NULL;
    }

    size_t lo = 0;
    size_t hi = ksym_count - 1;
    const char *result = NULL;
    while (lo <= hi) {
        size_t mid = (lo + hi) / 2;
        if (ksym_table[mid].addr <= addr) {
            result = ksym_table[mid].name;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}
