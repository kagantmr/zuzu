#include <dev/protocols/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

static Err GfxCall(Handle gfx, uint32_t cmd, uint32_t arg, FbInfo *info)
{
    FbRequest req = {.cmd = cmd, .arg = arg};
    MsgWrite(&req, sizeof(req));
    uint32_t got = 0;
    Err rc = CallPort(gfx, sizeof(req), -1, &got, NULL);
    if (rc != ZUZU_OK)
        return rc;
    if (got < sizeof(Err))
        return ERR_MALFORMED;
    Err status;
    memcpy(&status, GetMessageBox(), sizeof(status));
    if (info && got >= sizeof(*info))
        memcpy(info, GetMessageBox(), sizeof(*info));
    return status;
}

int main(int argc, char **argv)
{
    Handle gfx = LookupService("/svc/gfx");
    if (gfx < 0) {
        printf("vt: no display service\n");
        return 1;
    }

    if (argc < 2) {
        FbInfo info = {0};
        Err rc = GfxCall(gfx, FB_CMD_GET_INFO, 0, &info);
        if (rc != ZUZU_OK) {
            printf("vt: %s\n", StrToError(rc));
            return 1;
        }
        printf("vt%u\n", info.vt);
        return 0;
    }

    char *end;
    unsigned long n = strtoul(argv[1], &end, 10);
    if (*end != '\0' || n == 0 || n > FB_MAX_VTS) {
        printf("usage: vt [1-%u]\n", FB_MAX_VTS);
        return 1;
    }
    Err rc = GfxCall(gfx, FB_CMD_SWITCH, (uint32_t)n, NULL);
    if (rc != ZUZU_OK) {
        printf("vt: %s\n", StrToError(rc));
        return 1;
    }
    return 0;
}
