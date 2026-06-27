// asid.c - ASID management for ARM MMU

#include <arch/asid.h>
#include <string.h>
#include <arch/mmu.h>

#define ASID_COUNT 256          // ARMv7-A short-descriptor ASID space (8-bit)
#define ASID_BITMAP_BYTES 32    // ASID_COUNT / 8

static asid_t asid_bitmap[ASID_BITMAP_BYTES]; // 256 bits
static uint32_t current_generation = 1;
static asid_t next_asid = 1;

static inline bool asid_bit_test(int i) { return asid_bitmap[i / 8] & (1 << (i % 8)); }
static inline void asid_bit_set(int i) { asid_bitmap[i / 8] |= (1 << (i % 8)); }
static inline void asid_bit_clear(int i) { asid_bitmap[i / 8] &= ~(1 << (i % 8)); }

// Scan [lo, hi) for a free ASID; claim it and advance next_asid. Returns the
// claimed ASID, or 0 if the range had none free.
static int asid_claim_in_range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
    {
        if (!asid_bit_test(i))
        {
            asid_bit_set(i);
            next_asid = i + 1;
            return i;
        }
    }
    return 0;
}

asid_token_t asid_alloc(void)
{
    // Try the current generation: from next_asid forward, then wrap to the start.
    int i = asid_claim_in_range(next_asid, ASID_COUNT);
    if (!i)
        i = asid_claim_in_range(1, next_asid);
    if (i)
        return (asid_token_t){.asid = (asid_t)i, .generation = current_generation};

    // No free ASIDs: flush the whole TLB and start a new generation.
    arch_mmu_flush_tlb();                          // nuke the entire TLB
    memset(asid_bitmap, 0, ASID_BITMAP_BYTES);     // all ASIDs are free again
    asid_bit_set(0);                               // reserve ASID 0 for kernel
    current_generation++;
    next_asid = 2;

    // Allocate ASID 1 for the caller.
    asid_bit_set(1);
    return (asid_token_t){.asid = 1, .generation = current_generation};
}

void asid_free(asid_token_t token)
{
    if (token.asid == 0)
        return;

    // If this token is from an old generation, the bitmap was
    // already wiped during the rollover. The bit either belongs
    // to a new process now or is already clear. Don't touch it.
    if (token.generation != current_generation)
        return;

    asid_bit_clear(token.asid);
}

uint32_t asid_current_generation(void)
{
    return current_generation;
}
