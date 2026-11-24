#include "netmq.h"

#include <arch/barrier.h>
#include <string.h>

void NetMqInit(NetdMessageQueue *q)
{
    q->head = 0;
    q->tail = 0;
}

Err NetMqPush(NetdMessageQueue *q, const NetdMsg *msg)
{
    size_t head = q->head;
    if (head - q->tail == NETMQ_DEPTH)
        return ERR_BUSY;

    memcpy(&q->queue[head & (NETMQ_DEPTH - 1)], msg, sizeof(*msg));
    ArchDmb();
    q->head = head + 1;
    return ZUZU_OK;
}

Err NetMqPop(NetdMessageQueue *q, NetdMsg *out)
{
    size_t tail = q->tail;
    if (q->head == tail)
        return ERR_NOENT;

    ArchDmb();
    memcpy(out, &q->queue[tail & (NETMQ_DEPTH - 1)], sizeof(*out));
    ArchDmb();
    q->tail = tail + 1;
    return ZUZU_OK;
}
