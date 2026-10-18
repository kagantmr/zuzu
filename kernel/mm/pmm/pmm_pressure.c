#include "pmm_internal.h"

#include "kernel/ipc/event.h"
#include "kernel/mm/alloc.h"

#include <zuzu/err.h>

#define LOW_WATER_PCT 20
#define HIGH_WATER_PCT 30

#define LOG_FMT(fmt) "(pmm) " fmt
#include "util/log.h"

typedef struct {
    ListNode node;
    EventObject *ev;
} PmmSubscriber;

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
                KFree(sub);
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

    PmmSubscriber *new_node = KZAlloc(sizeof(PmmSubscriber));
    if (!new_node)
        return ERR_NOMEM;
    new_node->ev = ev;
    EventBindBit(ev, 0);
    ListAddTail(&new_node->node, &pmm_subscribers.node);
    ev->ref_count++;

    return ZUZU_OK;
}
