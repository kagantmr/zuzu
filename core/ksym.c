/**
 * @file ksym.c
 * @brief Kernel symbol table lookup by binary search, with weak defaults for builds without a table.
 */
#include "ksym.h"
#include <stddef.h>

/** @brief Weak default symbol count (0); overridden when a generated symbol table is linked in. */
__attribute__((weak)) volatile const uint32_t ksym_count = 0;
/** @brief Placeholder one-entry table used when no generated table is linked in. */
static const ksym_entry_t KSYM_FALLBACK_TABLE[] = {{0, ""}};
/** @brief Weak default table pointer, referring to the fallback table; overridden by the generated table. */
__attribute__((weak)) const ksym_entry_t *ksym_table = KSYM_FALLBACK_TABLE;

const char *KSymLookup(uint32_t addr)
{
    if (ksym_count == 0) {
        return NULL;
    }

    size_t lo = 0;
    size_t hi = ksym_count - 1;
    const char *result = NULL;
    while (lo <= hi) {
        size_t mid = lo + ((hi - lo) / 2);
        if (ksym_table[mid].addr <= addr) {
            result = ksym_table[mid].name;
            lo = mid + 1;
        } else {
            if (mid == 0)
                break;
            hi = mid - 1;
        }
    }
    return result;
}

uint32_t KSymLookupBaseAddr(uint32_t addr)
{
    if (ksym_count == 0)
        return 0;

    size_t lo = 0;
    size_t hi = ksym_count - 1;
    uint32_t result = 0;
    while (lo <= hi) {
        size_t mid = lo + ((hi - lo) / 2);
        if (ksym_table[mid].addr <= addr) {
            result = ksym_table[mid].addr;
            lo = mid + 1;
        } else {
            if (mid == 0)
                break;
            hi = mid - 1;
        }
    }
    return result;
}
