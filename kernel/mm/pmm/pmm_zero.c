/**
 * @file pmm_zero.c
 *
 * @brief Pool of frames zeroed ahead of time by lbx, so hot paths get a zeroed frame without
 * clearing it themselves.
 */

#include "pmm.h"
#include "kernel/lbx/lbx.h"
#include "kernel/mm/vmm/vmm.h"
#include <string.h>

/** @brief Zeroed frames the pool holds when full. */
#define ZERO_POOL_SIZE 32U
/** @brief Free-memory percentage below which the pool stops refilling. */
#define ZERO_POOL_MIN_FREE_PCT 20U

/** @brief The zeroed frames. */
static PhysAddr zero_pool[ZERO_POOL_SIZE];
/** @brief Frames in @c zero_pool. */
static uint32_t zero_count;
/** @brief Set once lbx can refill the pool. */
static bool zero_started;
/** @brief The pool's place in the lbx queue. */
static LbxNode zero_node;

/**
 * @brief Tells whether free memory is high enough to keep frames in the pool.
 *
 * @return true if the pool may take another frame.
 */
static bool PmmZeroPoolMayGrow(void)
{
    PmmStats st = PmmGetStats();
    return st.free_frames * 100U > st.total_frames * ZERO_POOL_MIN_FREE_PCT;
}

/**
 * @brief lbx step that zeroes one frame into the pool.
 *
 * @param[in,out] n The pool's node.
 * @param[in] budget Unused; one frame per step keeps each step short.
 * @return LBX_MORE until the pool is full or memory runs out.
 */
static LbxResult PmmZeroLbxStep(LbxNode *n, uint32_t budget)
{
    (void)n;
    (void)budget;
    if (zero_count >= ZERO_POOL_SIZE || !PmmZeroPoolMayGrow())
        return LBX_DONE;
    PhysAddr pa = PmmAllocFrame();
    if (pa == PA_NULL)
        return LBX_DONE;
    memset((void *)PA_TO_VA(pa), 0, PAGE_SIZE);
    zero_pool[zero_count++] = pa;
    return zero_count < ZERO_POOL_SIZE ? LBX_MORE : LBX_DONE;
}

void PmmZeroPoolStart(void)
{
    zero_started = true;
    LbxQueueIdle(&zero_node, PmmZeroLbxStep);
}

PhysAddr PmmAllocZeroedFrame(void)
{
    PhysAddr pa = zero_count ? zero_pool[--zero_count] : PA_NULL;
    if (zero_started && zero_count < ZERO_POOL_SIZE && PmmZeroPoolMayGrow())
        LbxQueueIdle(&zero_node, PmmZeroLbxStep);
    if (pa != PA_NULL)
        return pa;
    pa = PmmAllocFrame();
    if (pa != PA_NULL)
        memset((void *)PA_TO_VA(pa), 0, PAGE_SIZE);
    return pa;
}

bool PmmZeroPoolRelease(void)
{
    if (!zero_count)
        return false;
    while (zero_count)
        PmmFreeFrame(zero_pool[--zero_count]);
    return true;
}
