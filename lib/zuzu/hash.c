#include <zuzu/hash.h>
#include <string.h>

static uint32_t _djb2(const char *s) {
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (uint8_t)*s++;
    return h;
}

int hash_init(hash_t *h, hash_entry_t *backing, uint32_t cap) {
    if (!h || !backing || cap == 0) return -1;
    h->entries = backing;
    h->cap = cap;
    h->count = 0;
    for (uint32_t i = 0; i < cap; ++i) {
        h->entries[i].key = NULL;
        h->entries[i].val = NULL;
        h->entries[i].hash = 0;
    }
    return 0;
}

int hash_set(hash_t *h, const char *key, void *val) {
    if (!h || !key) return -1;
    uint32_t hash = _djb2(key);
    uint32_t idx = hash & (h->cap - 1);
    for (uint32_t i = 0; i < h->cap; ++i) {
        uint32_t p = (idx + i) & (h->cap - 1);
        if (h->entries[p].key == NULL) {
            h->entries[p].key = key;
            h->entries[p].val = val;
            h->entries[p].hash = hash;
            h->count++;
            return 0;
        }
        if (h->entries[p].hash == hash && strcmp(h->entries[p].key, key) == 0) {
            h->entries[p].val = val;
            return 0;
        }
    }
    return -1; // full
}

void *hash_get(const hash_t *h, const char *key) {
    if (!h || !key) return NULL;
    uint32_t hash = _djb2(key);
    uint32_t idx = hash & (h->cap - 1);
    for (uint32_t i = 0; i < h->cap; ++i) {
        uint32_t p = (idx + i) & (h->cap - 1);
        if (h->entries[p].key == NULL) return NULL;
        if (h->entries[p].hash == hash && strcmp(h->entries[p].key, key) == 0)
            return h->entries[p].val;
    }
    return NULL;
}

int hash_del(hash_t *h, const char *key) {
    if (!h || !key) return -1;
    uint32_t hash = _djb2(key);
    uint32_t idx = hash & (h->cap - 1);
    for (uint32_t i = 0; i < h->cap; ++i) {
        uint32_t p = (idx + i) & (h->cap - 1);
        if (h->entries[p].key == NULL) return -1;
        if (h->entries[p].hash == hash && strcmp(h->entries[p].key, key) == 0) {
            h->entries[p].key = NULL;
            h->entries[p].val = NULL;
            h->entries[p].hash = 0;
            h->count--;
            return 0;
        }
    }
    return -1;
}

void hash_iter(const hash_t *h, void (*fn)(const char *key, void *val, void *ctx), void *ctx) {
    if (!h || !fn) return;
    for (uint32_t i = 0; i < h->cap; ++i) {
        if (h->entries[i].key)
            fn(h->entries[i].key, h->entries[i].val, ctx);
    }
}
