#ifndef ZUZU_HASH_H
#define ZUZU_HASH_H

#include <stdint.h>

typedef uint32_t hash_t;

typedef struct hash_entry {
    const char *key;
    void       *val;
    hash_t    hash;
} hash_entry_t;

typedef struct {
    hash_entry_t *entries;
    uint32_t       cap;    // power of 2
    uint32_t       count;
} hash_t;

int   hash_init(hash_t *h, hash_entry_t *backing, uint32_t cap);
int   hash_set(hash_t *h, const char *key, void *val);
void *hash_get(const hash_t *h, const char *key);
int   hash_del(hash_t *h, const char *key);
void  hash_iter(const hash_t *h, void (*fn)(const char *key, void *val, void *ctx), void *ctx);

#endif // ZUZU_HASH_H