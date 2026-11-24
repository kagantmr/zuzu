#include "ctl.h"
#include "common/globals.h"
#include "common/netmq.h"
#include <string.h>
#include <util/log.h>
#include <zuzu/msg.h>
#include <zuzu/zuzu.h>
#include <ztask/ztask.h>

static NetdMessageQueue g_mq;
static Ticket g_cur_ticket;

static void CtlRefuse(const PortWaitResult *r, Err err)
{
    if (r->granted >= 0)
        HandleClose(r->granted);
    MsgWrite(&err, sizeof(err));
    Reply(sizeof(err), -1);
}

static void CtlMain(void *arg)
{
    (void)arg;
    for (;;) {
        PortWaitResult r = WaitOnPort(g_svc_port, TIMEOUT_INFINITE);
        if (r.status != ZUZU_OK)
            continue;
        if (r.xlen > NETMQ_BODY_MAX) {
            CtlRefuse(&r, ERR_BADARG);
            continue;
        }

        NetdMsg m = {.sender = r.sender, .granted = r.granted, .len = (uint16_t)r.xlen};
        memcpy(m.body, GetMessageBox(), r.xlen);
        m.op = r.xlen ? (uint8_t)m.body[0] : 0;

        Err rc = Hold(&m.ticket);
        if (rc != ZUZU_OK) {
            if (rc == ERR_DEAD) {
                if (r.granted >= 0)
                    HandleClose(r.granted);
            } else {
                CtlRefuse(&r, ERR_BUSY);
            }
            continue;
        }

        if (NetMqPush(&g_mq, &m) != ZUZU_OK) {
            if (r.granted >= 0)
                HandleClose(r.granted);
            Err busy = ERR_BUSY;
            MsgWrite(&busy, sizeof(busy));
            ReplyTo(m.ticket, sizeof(busy), -1);
            continue;
        }
        Signal(g_event, CTL_MBOX_MASK, false);
    }
}

Err CtlStart(void)
{
    NetMqInit(&g_mq);
    Err rc = ZTaskSpawnDetached(CtlMain, NULL, 0);
    if (rc != ZUZU_OK)
        LOG_ERROR(LOG_TAG, "ctl thread failed to start");
    return rc;
}

bool CtlNextCall(PortWaitResult *call)
{
    NetdMsg m;
    if (NetMqPop(&g_mq, &m) != ZUZU_OK)
        return false;
    memcpy(GetMessageBox(), m.body, m.len);
    g_cur_ticket = m.ticket;
    *call = (PortWaitResult){
        .status = ZUZU_OK, .sender = m.sender, .xlen = m.len, .granted = m.granted};
    return true;
}

Err CtlReply(uint32_t xlen, Handle grant) { return ReplyTo(g_cur_ticket, xlen, grant); }
