// arch/asid.h - Neutral address-space-identifier contract.
//
// ASIDs tag TLB entries with a process identity so context switches avoid full
// TLB flushes. Architectures without ASIDs provide a trivial implementation.
// addrspace_t embeds an asid_token_t by value, so this is a concrete type.

#ifndef ZUZU_ARCH_ASID_H
#define ZUZU_ARCH_ASID_H

#include <stdint.h>

typedef uint8_t asid_t;

typedef struct
{
    asid_t   asid;
    uint32_t generation;
} asid_token_t;

/** Allocate an ASID token for an address space (may roll the generation). */
asid_token_t asid_alloc(void);

/** Release a previously allocated ASID token. */
void asid_free(asid_token_t token);

/** Current ASID generation number. */
extern uint32_t asid_generation;
static inline uint32_t asid_current_generation(void) { return asid_generation; }

/**
 * Record the ASID just installed in the live translation-context register
 * (CONTEXTIDR on ARM). Rollover in asid_alloc() reserves this ASID rather
 * than handing it to someone else, so a process that keeps running across a
 * rollover doesn't collide with a freshly allocated one still using the same
 * tag. Call this exactly where the architecture writes the real ASID into
 * hardware -- not before, and not for the reserved/parking ASID a switch may
 * transit through first.
 */
void AsidSetActive(asid_t asid);

#endif // ZUZU_ARCH_ASID_H
