/* Low-memory-pressure notification: subscribers register an Event and get
 * signalled when free frames cross the low/high watermarks. Independent
 * of the actual allocator algorithm in pmm.c. */

#include "pmm_internal.h"

#include "kernel/ipc/event.h"
#include "kernel/mm/alloc.h"

#include <zuzu/err.h>

#define LOW_WATER_PCT 20  // fire when free < 20%
#define HIGH_WATER_PCT 30 // clear when free > 30%

#define LOG_FMT(fmt) "(pmm) " fmt
#include "zuzu/log.h"

typedef struct {
    ListNode node;
    EventObject *ev;
} PmmSubscriber;

ListHead pmm_subscribers;

void PmmKEventSignal(void)
{
    size_t free_pct = (pmm_state.free_frames * 100) / pmm_state.total_frames;

    if (!pmm_state.in_pressure && free_pct < LOW_WATER_PCT) {
        pmm_state.in_pressure = true;
        KWARN("Memory usage exceeded low-water mark, signalling subscribers");

        // walk subscribers and signal them
        // clean dead ntfns: refcount--, free-if-zero, kfree(subscriber)
        ListNode *pos, *tmp;
        list_for_each_safe(pos, tmp, &pmm_subscribers.node)
        {
            PmmSubscriber *sub = container_of(pos, PmmSubscriber, node);
            // safe to remove sub from list here
            if (!sub->ev->alive) {
                list_remove(pos);
                EventDropReference(sub->ev);
                KFree(sub);
                continue;
            }

            EventSignal(sub->ev, KEVENT_MEMMGMT_BIT);
        }
    } else if (pmm_state.in_pressure && free_pct > HIGH_WATER_PCT) {
        pmm_state.in_pressure = false;
    }
}

int PmmSubscribe(EventObject *ev)
{
    if (!ev)
        return ERR_BADARG;

    PmmSubscriber *new_node = KZAlloc(sizeof(PmmSubscriber));
    if (!new_node)
        return ERR_NOMEM;
    new_node->ev = ev;
    ev->irq_bind_count++;
    list_add_tail(&new_node->node, &pmm_subscribers.node);
    ev->ref_count++;

    return ZUZU_OK;
}
