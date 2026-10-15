#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <string.h>

#define ASID_COUNT 256
#define ASID_BITMAP_BYTES 32

static Asid asid_bitmap[ASID_BITMAP_BYTES];
static uint8_t dirty_bitmap[ASID_BITMAP_BYTES];
uint32_t asid_generation = 1;
static Asid next_asid = 1;
static Asid active_asid;

static inline bool AsidBitTest(uint8_t *bitmap, int i) { return bitmap[i / 8] & (1 << (i % 8)); }
static inline void AsidBitSet(uint8_t *bitmap, int i)
{
    bitmap[i / 8] = (Asid)(bitmap[i / 8] | (1U << (i % 8)));
}
static inline void AsidBitClear(uint8_t *bitmap, int i)
{
    bitmap[i / 8] = (Asid)(bitmap[i / 8] & ~(1U << (i % 8)));
}

void AsidSetActive(Asid a)
{
    active_asid = a;
    AsidBitSet(dirty_bitmap, a);
}

static int AsidClaimInRange(int lo, int hi)
{
    for (int i = lo; i < hi; i++) {
        if (!AsidBitTest(asid_bitmap, i)) {
            AsidBitSet(asid_bitmap, i);
            next_asid = (Asid)(i + 1);
            if (AsidBitTest(dirty_bitmap, i)) {
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
    int i = AsidClaimInRange(next_asid, ASID_COUNT);
    if (!i)
        i = AsidClaimInRange(1, next_asid);
    if (i)
        return (AsidToken){.asid = (Asid)i, .generation = asid_generation};

    ArchMmuFlushTlb();
    memset(asid_bitmap, 0, ASID_BITMAP_BYTES);
    memset(dirty_bitmap, 0, ASID_BITMAP_BYTES);
    AsidBitSet(asid_bitmap, 0);

    if (active_asid)
        AsidBitSet(asid_bitmap, active_asid);
    asid_generation++;
    next_asid = 1;
    i = AsidClaimInRange(1, ASID_COUNT);

    return (AsidToken){.asid = (Asid)i, .generation = asid_generation};
}

void AsidFree(AsidToken token)
{
    if (token.asid == 0)
        return;

    if (token.generation != asid_generation)
        return;

    AsidBitClear(asid_bitmap, token.asid);
}
