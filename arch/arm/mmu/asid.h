// asid.h - ASID management interface for ARM MMU
// This file defines the interface for managing ASIDs (Address Space Identifiers) in the ARM MMU.
// ASIDs are used to tag TLB entries with a process identifier, allowing the TLB to hold entries for
// multiple processes without flushing on context switch. This header declares the asid_token_t structure
// and the functions for allocating, freeing, and querying ASIDs.

#ifndef ASID_H
#define ASID_H

#include <stdint.h>

// ASID token struct to track allocated ASIDs and their generation.
typedef struct
{
    uint8_t asid;
    uint32_t generation;
} asid_token_t;

/**
 * Allocate a new ASID token for a process. This function will try to find a free ASID in the current generation. If all ASIDs are in use, it will flush the TLB and start a new generation, effectively freeing all ASIDs. The returned token contains the allocated ASID and the generation it belongs to.
 * @return An asid_token_t containing the allocated ASID and its generation.
 */
asid_token_t asid_alloc(void);

/**
 * Free a previously allocated ASID token.
 */
void asid_free(asid_token_t token);

/**
 * @return The current ASID generation number.
 */
uint32_t asid_current_generation(void);

#endif