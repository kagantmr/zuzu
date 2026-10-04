// asid.c - ASID management for ARM MMU

#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <string.h>

#define ASID_COUNT 256       // ARMv7-A short-descriptor ASID space (8-bit)
#define ASID_BITMAP_BYTES 32 // ASID_COUNT / 8

static Asid asid_bitmap[ASID_BITMAP_BYTES]; // 256 bits
static uint8_t dirty_bitmap[ASID_BITMAP_BYTES];
uint32_t asid_generation = 1;
static Asid next_asid = 1;
static Asid active_asid;

static inline bool AsidBitTest(uint8_t *bitmap, int i) { return bitmap[i / 8] & (1 << (i % 8)); }
static inline void AsidBitSet(uint8_t *bitmap,int i)
{
    bitmap[i / 8] = (Asid)(bitmap[i / 8] | (1U << (i % 8)));
}
static inline void AsidBitClear(uint8_t *bitmap,int i)
{
    bitmap[i / 8] = (Asid)(bitmap[i / 8] & ~(1U << (i % 8)));
}

void AsidSetActive(Asid a)
{
    active_asid = a;
    AsidBitSet(dirty_bitmap, a);
}


// Scan [lo, hi) for a free ASID; claim it and advance next_asid. Returns the
// claimed ASID, or 0 if the range had none free.
static int AsidClaimInRange(int lo, int hi)
{
    for (int i = lo; i < hi; i++)
    {
        if (!AsidBitTest(asid_bitmap, i))
        {
            AsidBitSet(asid_bitmap,i);
            next_asid = (Asid)(i + 1);
            if (AsidBitTest(dirty_bitmap, i)) {          // only flush if it was ever installed
                ArchMmuFlushTlbAsid((uint8_t)i);
                ArchSyncBarrier();
            }
            return i;
        }
    }
    return 0;
}


AsidToken AsidAlloc(void)
{
    // Try the current generation: from next_asid forward, then wrap to the start.
    int i = AsidClaimInRange(next_asid, ASID_COUNT);
    if (!i)
        i = AsidClaimInRange(1, next_asid);
    if (i)
        return (AsidToken){.asid = (Asid)i, .generation = asid_generation};

    // No free ASIDs: flush the whole TLB and start a new generation.
    ArchMmuFlushTlb();
    memset(asid_bitmap, 0, ASID_BITMAP_BYTES);
    memset(dirty_bitmap, 0, ASID_BITMAP_BYTES);
    AsidBitSet(asid_bitmap,0);                             /* kernel */

    if (active_asid) AsidBitSet(asid_bitmap,active_asid);  /* running AS keeps its tag */
    asid_generation++;
    next_asid = 1;
    i = AsidClaimInRange(1, ASID_COUNT);      /* first genuinely free one */

    return (AsidToken){.asid = (Asid)i, .generation = asid_generation};
}

void AsidFree(AsidToken token)
{
    if (token.asid == 0)
        return;

    // If this token is from an old generation, the bitmap was
    // already wiped during the rollover. The bit either belongs
    // to a new process now or is already clear. Don't touch it.
    if (token.generation != asid_generation)
        return;

    AsidBitClear(asid_bitmap,token.asid);
}
