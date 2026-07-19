#include "stdlib.h"
#include <stddef.h>

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const void *elem = (const char *)base + mid * size;
        int c = compar(key, elem);
        if (c == 0) return (void *)elem;
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}
