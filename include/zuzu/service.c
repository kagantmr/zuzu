#include <ipc/msg.h>
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

    Err rc = CallPort(NSVC_PORT, w.off, port, NULL, NULL);
    if (rc != ZUZU_OK)
        return rc;

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

    Handle found = -1;
    Err rc = CallPort(NSVC_PORT, w.off, -1, NULL, &found);
    if (rc != ZUZU_OK)
        return (Handle)rc;
    return found;
}
