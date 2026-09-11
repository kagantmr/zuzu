// asid.c - ASID management for ARM MMU

#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <string.h>

#define ASID_COUNT 256       // ARMv7-A short-descriptor ASID space (8-bit)
#define ASID_BITMAP_BYTES 32 // ASID_COUNT / 8

static asid_t asid_bitmap[ASID_BITMAP_BYTES]; // 256 bits
static uint8_t dirty_bitmap[ASID_BITMAP_BYTES];
static uint32_t current_generation = 1;
static asid_t next_asid = 1;
static asid_t active_asid;

static inline bool asid_bit_test(uint8_t *bitmap, int i) { return bitmap[i / 8] & (1 << (i % 8)); }
static inline void asid_bit_set(uint8_t *bitmap,int i)
{
    bitmap[i / 8] = (asid_t)(bitmap[i / 8] | (1U << (i % 8)));
}
static inline void asid_bit_clear(uint8_t *bitmap,int i)
{
    bitmap[i / 8] = (asid_t)(bitmap[i / 8] & ~(1U << (i % 8)));
}

void AsidSetActive(asid_t a)
{
    active_asid = a;
    asid_bit_set(dirty_bitmap, a);
}


// Scan [lo, hi) for a free ASID; claim it and advance next_asid. Returns the
// claimed ASID, or 0 if the range had none free.
static int asid_claim_in_range(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
    {
        if (!asid_bit_test(asid_bitmap, i))
        {
            asid_bit_set(asid_bitmap,i);
            next_asid = (asid_t)(i + 1);
            if (asid_bit_test(dirty_bitmap, i)) {          // only flush if it was ever installed
                arch_mmu_flush_tlb_asid((uint8_t)i);
                ArchCtxSync();
            }
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
    arch_mmu_flush_tlb();
    memset(asid_bitmap, 0, ASID_BITMAP_BYTES);
    memset(dirty_bitmap, 0, ASID_BITMAP_BYTES);
    asid_bit_set(asid_bitmap,0);                             /* kernel */

    if (active_asid) asid_bit_set(asid_bitmap,active_asid);  /* running AS keeps its tag */
    current_generation++;
    next_asid = 1;
    i = asid_claim_in_range(1, ASID_COUNT);      /* first genuinely free one */

    return (asid_token_t){.asid = (asid_t)i, .generation = current_generation};
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

    asid_bit_clear(asid_bitmap,token.asid);
}

uint32_t asid_current_generation(void) { return current_generation; }
