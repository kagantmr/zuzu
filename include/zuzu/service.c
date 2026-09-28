#include "zuzu/cap.h"
#include "zuzu/err.h"
#include "zuzu/msg.h"
#include "zuzu/protocols/nametable.h"
#include <util/channel.h>
#include <util/msg.h>
#include <zuzu/service.h>

Err RegisterService(const char *name, Handle port)
{
    if (!name || port < 0)
        return ERR_BADARG;

    NtOpcode op = NT_REGISTER;
    Handle handle = ZuzuGrant(port, NT_PID, GRANT_REGRANTABLE);

    if (handle < 0)
        return handle;

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, op);     // cmd first
    MsgPutU32(&w, handle); // handle
    MsgPutU32(&w, 0);      // pid field unused for register
    MsgPutStr(&w, name);

    Message reply = ZuzuMsgLcall(NT_PORT,
                                 w.off); // ns will reply with return code in w1

    return reply.w1;
}

Handle LookupService(const char *name)
{
    if (!name)
        return ERR_BADARG;

    NtOpcode op = NT_LOOKUP;

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, op); // cmd first
    MsgPutU32(&w, 0);  // handle
    MsgPutU32(&w, 0);  // pid field unused
    MsgPutStr(&w, name);

    Message reply = ZuzuMsgLcall(NT_PORT,
                                 w.off); // ns will reply with return code in w1

    if (reply.w1 != ZUZU_OK)
        return reply.w1;
    return (Handle)reply.w2;
}

Handle LookupServiceWithPid(const char *name, Spid *out_pid)
{
    if (!name)
        return ERR_BADARG;

    NtOpcode op = NT_LOOKUP;

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, op); // cmd first
    MsgPutU32(&w, 0);  // handle
    MsgPutU32(&w, 0);  // pid field unused
    MsgPutStr(&w, name);

    Message reply = ZuzuMsgLcall(NT_PORT,
                                 w.off); // ns will reply with return code in w1

    if (reply.w1 != ZUZU_OK)
        return reply.w1;
    if (out_pid) *out_pid = reply.w3;
    return (Handle)reply.w2;
}

Handle LookupServicePid(Spid pid)
{
    if (pid <= 0)
        return ERR_BADARG;

    NtOpcode op = NT_LOOKUP_PID;

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, op);  // cmd first
    MsgPutU32(&w, 0);   // handle
    MsgPutU32(&w, pid); // pid field
    MsgPutStr(&w, "");

    Message reply = ZuzuMsgLcall(NT_PORT,
                                 w.off); // ns will reply with return code in w1

    if (reply.w1 != ZUZU_OK)
        return reply.w1;
    return (Handle)reply.w2;
}

Err ScrubServicePid(Spid pid)
{
    if (pid <= 0)
        return ERR_BADARG;

    NtOpcode op = NT_SCRUB_PID;

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, op);  // cmd first
    MsgPutU32(&w, 0);   // handle
    MsgPutU32(&w, pid); // pid field
    MsgPutStr(&w, "");

    Message reply = ZuzuMsgLcall(NT_PORT,
                                 w.off); // ns will reply with return code in w1

    return (Err)reply.w1;
}
