/**
 * @file
 * @brief ARMv7-A ASID allocator with generations.
 */

#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <string.h>

#define ASID_COUNT 256 /**< Number of hardware ASIDs. */
#define ASID_BITMAP_BYTES 32 /**< Bytes in a bitmap of all ASIDs. */

/** @brief ASIDs handed out in the current generation. */
static Asid asid_bitmap[ASID_BITMAP_BYTES];
/** @brief ASIDs that may still have TLB entries since the last full flush. */
static uint8_t dirty_bitmap[ASID_BITMAP_BYTES];
/** @brief Current generation; bumped each time the ASIDs run out and are recycled. */
uint32_t asid_generation = 1;
/** @brief Where the next allocation starts searching. */
static Asid next_asid = 1;
/** @brief ASID loaded on the CPU, kept across a rollover. */
static Asid active_asid;

/**
 * @brief Tests a bit in an ASID bitmap.
 *
 * @param[in] bitmap Bitmap.
 * @param[in] i      ASID.
 * @return true if set.
 */
static inline bool AsidBitTest(uint8_t *bitmap, int i) { return bitmap[i / 8] & (1 << (i % 8)); }
/**
 * @brief Sets a bit in an ASID bitmap.
 *
 * @param[in,out] bitmap Bitmap.
 * @param[in]     i      ASID.
 */
static inline void AsidBitSet(uint8_t *bitmap, int i)
{
    bitmap[i / 8] = (Asid)(bitmap[i / 8] | (1U << (i % 8)));
}
/**
 * @brief Clears a bit in an ASID bitmap.
 *
 * @param[in,out] bitmap Bitmap.
 * @param[in]     i      ASID.
 */
static inline void AsidBitClear(uint8_t *bitmap, int i)
{
    bitmap[i / 8] = (Asid)(bitmap[i / 8] & ~(1U << (i % 8)));
}

void AsidSetActive(Asid a)
{
    active_asid = a;
    AsidBitSet(dirty_bitmap, a);
}

/**
 * @brief Claims the first free ASID in [@p lo, @p hi), flushing its old TLB entries if it was used.
 *
 * @param[in] lo First ASID to try.
 * @param[in] hi One past the last.
 * @return The ASID, or 0 if none is free.
 */
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
