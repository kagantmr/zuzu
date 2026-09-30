#include <zuzu/service.h>
#include <util/msg.h>
#include <zuzu/zuzu.h>
#include <zuzu/err.h>

#define NSVC_PORT 0


Err RegisterServiceGeneric(const char *name, Handle port, Handle nsvc_port)
{
    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, (uint32_t)NS_REGISTER);
    MsgPutStr(&w, name);
    if (w.ovf)
        return ERR_OVERFLOW;

    SvcResult r = Call(nsvc_port, w.off, port);
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;

    Err status;
    MsgRead(&status, sizeof(status));
    return status;
}

Handle LookupServiceGeneric(const char *name, Handle nsvc_port)
{
    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, (uint32_t)NS_LOOKUP);
    MsgPutStr(&w, name);
    if (w.ovf)
        return ERR_OVERFLOW;

    SvcResult r = Call(nsvc_port, w.off, -1);
    if (r.r0 != ZUZU_OK)
        return (Handle)r.r0;
    return (Handle)r.r3; /* nsvc regrants the found port via its Reply */
}

Err RegisterService(const char *name, Handle port)
{
    return RegisterServiceGeneric(name, port, NSVC_PORT);
}

Handle LookupService(const char *name)
{
    return LookupServiceGeneric(name, NSVC_PORT);
}