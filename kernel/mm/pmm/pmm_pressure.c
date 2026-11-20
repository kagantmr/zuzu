/**
 * @file pmm_pressure.c
 * @brief Low-memory pressure detection and subscriber notification.
 */
#include "pmm_internal.h"

#include "kernel/ipc/event.h"
#include "kernel/mm/alloc.h"

#include <string.h>
#include <zuzu/err.h>

/** @brief Free-memory percentage below which subscribers are signalled. */
#define LOW_WATER_PCT 20
/** @brief Free-memory percentage above which memory pressure is cleared. */
#define HIGH_WATER_PCT 30

#define LOG_FMT(fmt) "(pmm) " fmt
#include "core/log.h"

/** @brief Allocator for pressure subscribers. */
static KSlabCache subscriber_cache;

/** @brief A low-memory subscriber entry. */
typedef struct {
    ListNode node;  /**< Link in pmm_subscribers. */
    EventObject *ev; /**< Event to signal; a reference is held. */
} PmmSubscriber;

/** @brief List of PmmSubscriber entries. */
ListHead pmm_subscribers;

void PmmSignalSubscribers(void)
{
    size_t free_pct = (pmm_state.free_frames * 100) / pmm_state.total_frames;

    if (!pmm_state.in_pressure && free_pct < LOW_WATER_PCT) {
        pmm_state.in_pressure = true;
        KWARN("Memory usage exceeded low-water mark, signalling subscribers");

        ListNode *pos, *tmp;
        LIST_FOR_EACH_SAFE(pos, tmp, &pmm_subscribers.node)
        {
            PmmSubscriber *sub = container_of(pos, PmmSubscriber, node);
            if (!sub->ev->alive) {
                ListRemove(pos);
                EventUnref(sub->ev);
                KSlabFree(&subscriber_cache, sub);
                continue;
            }

            EventSignal(sub->ev, EVENT_MEMMGMT_BIT, false);
        }
    } else if (pmm_state.in_pressure && free_pct > HIGH_WATER_PCT) {
        pmm_state.in_pressure = false;
    }
}

int PmmSubscribe(EventObject *ev)
{
    if (!ev)
        return ERR_BADARG;

    ListNode *pos, *tmp;
    LIST_FOR_EACH_SAFE(pos, tmp, &pmm_subscribers.node)
    {
        PmmSubscriber *sub = container_of(pos, PmmSubscriber, node);
        if (sub->ev == ev)
            return ZUZU_OK;
        if (!sub->ev->alive) {
            ListRemove(pos);
            EventUnref(sub->ev);
            KSlabFree(&subscriber_cache, sub);
        }
    }

    if (!subscriber_cache.obj_size)
        KSlabInit(&subscriber_cache, sizeof(PmmSubscriber));
    PmmSubscriber *new_node = KSlabAlloc(&subscriber_cache);
    if (!new_node)
        return ERR_NOMEM;
    memset(new_node, 0, sizeof(*new_node));
    new_node->ev = ev;
    EventBindBit(ev, 0);
    ListAddTail(&new_node->node, &pmm_subscribers.node);
    ev->ref_count++;

    return ZUZU_OK;
}
