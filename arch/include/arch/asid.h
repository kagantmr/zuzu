// arch/asid.h - Neutral address-space-identifier contract.
//
// ASIDs tag TLB entries with a process identity so context switches avoid full
// TLB flushes. Architectures without ASIDs provide a trivial implementation.
// AddressSpace embeds an AsidToken by value, so this is a concrete type.

#ifndef ARCH_ASID_H
#define ARCH_ASID_H

#include <stdint.h>

typedef uint8_t Asid;

typedef struct
{
    Asid   asid;
    uint32_t generation;
} AsidToken;

/** Allocate an ASID token for an address space (may roll the generation). */
AsidToken AsidAlloc(void);

/** Release a previously allocated ASID token. */
void AsidFree(AsidToken token);

/** Current ASID generation number. */
extern uint32_t asid_generation;
static inline uint32_t AsidCurrentGeneration(void) { return asid_generation; }

/**
 * Record the ASID just installed in the live translation-context register
 * (CONTEXTIDR on ARM). Rollover in AsidAlloc() reserves this ASID rather
 * than handing it to someone else, so a process that keeps running across a
 * rollover doesn't collide with a freshly allocated one still using the same
 * tag. Call this exactly where the architecture writes the real ASID into
 * hardware -- not before, and not for the reserved/parking ASID a switch may
 * transit through first.
 */
void AsidSetActive(Asid asid);

#endif // ARCH_ASID_H
