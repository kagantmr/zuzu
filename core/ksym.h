/**
 * @file ksym.h
 * @brief Kernel symbol table lookup (address to function name) used by panic and logging.
 */
#ifndef KSYM_H
#define KSYM_H

#include <stdint.h>

/** @brief One entry of the kernel symbol table. */
typedef struct {
    uintptr_t addr;     /**< Start address of the symbol. */
    const char *name;  /**< NUL-terminated symbol name. */
} KsymEntry;

/** @brief Number of entries in ::ksym_table (0 when no table was linked in). */
extern volatile const uint32_t ksym_count;

/** @brief Symbol table sorted by ascending address; weakly defined, overridden by the generated table. */
extern const KsymEntry *ksym_table;

/**
 * @brief Find the symbol containing an address.
 *
 * Binary-searches the table for the last entry whose start address is not above @p addr.
 *
 * @param[in] addr Address to resolve.
 * @return Name of the closest preceding symbol, or NULL if the table is empty or @p addr is below the first symbol.
 */
const char *KSymLookup(uintptr_t addr);

/**
 * @brief Find the start address of the symbol containing an address.
 *
 * @param[in] addr Address to resolve.
 * @return Start address of the closest preceding symbol, or 0 if none was found.
 */
uintptr_t KSymLookupBaseAddr(uintptr_t addr);

#endif
