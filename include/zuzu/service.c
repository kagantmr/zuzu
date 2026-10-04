#include <util/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

Err RegisterService(const char *name, Handle port)
{
    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, (uint32_t)NS_REGISTER);
    MsgPutStr(&w, name);
    if (w.ovf)
        return ERR_OVERFLOW;

    SvcResult r = Call(NSVC_PORT, w.off, port);
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;

    Err status;
    MsgRead(&status, sizeof(status));
    return status;
}

Handle LookupService(const char *name)
{
    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, (uint32_t)NS_LOOKUP);
    MsgPutStr(&w, name);
    if (w.ovf)
        return ERR_OVERFLOW;

    SvcResult r = Call(NSVC_PORT, w.off, -1);
    if (r.r0 != ZUZU_OK)
        return (Handle)r.r0;
    return (Handle)r.r3; /* nsvc regrants the found port via its Reply */
}
