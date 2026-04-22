#include "asid.h"

#include <string.h>

#include "arch/arm/mmu/mmu.h"

static uint8_t  asid_bitmap[32];    // 256 bits
static uint32_t current_generation = 1;
static uint8_t  next_asid = 1;

asid_token_t asid_alloc(void) {
    // Try to find a free ASID in the current generation
    for (int i = next_asid; i < 256; i++) {
        if (!(asid_bitmap[i/8] & (1 << (i%8)))) {
            asid_bitmap[i/8] |= (1 << (i%8));
            next_asid = i + 1;
            return (asid_token_t){ .asid = (uint8_t)i, .generation = current_generation };
        }
    }
    // Wrapped around from the beginning too
    for (int i = 1; i < next_asid; i++) {
        if (!(asid_bitmap[i/8] & (1 << (i%8)))) {
            asid_bitmap[i/8] |= (1 << (i%8));
            next_asid = i + 1;
            return (asid_token_t){ .asid = (uint8_t)i, .generation = current_generation };
        }
    }

    // no free ASIDs, need to flush the TLB and start a new generation
    arch_mmu_flush_tlb();           // nuke the entire TLB
    memset(asid_bitmap, 0, 32);     // all ASIDs are free again
    asid_bitmap[0] |= 1;           // reserve ASID 0 for kernel
    current_generation++;
    next_asid = 2;

    // Allocate ASID 1 for the caller
    asid_bitmap[0] |= (1 << 1);
    return (asid_token_t){ .asid = 1, .generation = current_generation };
}

void asid_free(asid_token_t token) {
    if (token.asid == 0)
        return;

    // If this token is from an old generation, the bitmap was
    // already wiped during the rollover. The bit either belongs
    // to a new process now or is already clear. Don't touch it.
    if (token.generation != current_generation)
        return;

    asid_bitmap[token.asid / 8] &= ~(1 << (token.asid % 8));
}

uint32_t asid_current_generation(void) {
    return current_generation;
}