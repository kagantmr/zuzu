#ifndef ZUZU_HASH_H
#define ZUZU_HASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint32_t hashno_t;

typedef struct hash_entry {
    const char *key; // null-terminated string
    void       *val; // pointer to the value associated with the key
    hashno_t    hash; // precomputed hash of the key for faster lookups
} hash_entry_t;

typedef struct {
    hash_entry_t *entries; // pointer to the array of hash entries
    uint32_t       cap;    // power of 2
    uint32_t       count; // number of entries currently in the hash table
} hash_t;

/**
 * @brief Initializes a hash table with the specified backing array and capacity.
 * 
 * @param h Pointer to the hash table structure to initialize.
 * @param backing Pointer to the array of hash entries that will serve as the backing store for the hash table.
 * @param cap The capacity of the hash table, which must be a power of 2.
 * 
 * @return int Returns 0 on success, or -1 if the initialization fails (e.g., if h or backing is NULL, or if cap is 0).
 */
int   hash_init(hash_t *h, hash_entry_t *backing, uint32_t cap);

/**
 * @brief Sets a key-value pair in the hash table. If the key already exists, its value is updated.
 * 
 * @param h Pointer to the hash table.
 * @param key The null-terminated string key to set in the hash table.
 * @param val Pointer to the value associated with the key.
 * 
 * @return int Returns 0 on success, or -1 if the hash table is full or if h or key is NULL.
 */
int   hash_set(hash_t *h, const char *key, void *val);

/**
 * @brief Retrieves the value associated with a given key from the hash table.
 * 
 * @param h Pointer to the hash table.
 * @param key The null-terminated string key to look up in the hash table.
 * 
 * @return void* Returns a pointer to the value associated with the key, or NULL if the key is not found or if h or key is NULL.
 */
void *hash_get(const hash_t *h, const char *key);

/**
 * @brief Deletes a key-value pair from the hash table.
 * 
 * @param h Pointer to the hash table.
 * @param key The null-terminated string key to delete from the hash table.
 * 
 * @return int Returns 0 on success, or -1 if the key is not found or if h or key is NULL.
 */
int   hash_del(hash_t *h, const char *key);

/**
 * @brief Iterates over all key-value pairs in the hash table and applies a user-defined function to each pair.
 * 
 * @param h Pointer to the hash table.
 * @param fn A pointer to a user-defined function that takes a key, value, and context pointer as arguments. This function will be called for each key-value pair in the hash table.
 * @param ctx A pointer to user-defined context data that will be passed to the user-defined function for each key-value pair.
 */
void  hash_iter(const hash_t *h, void (*fn)(const char *key, void *val, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_HASH_H