#include <zuzu/den.h>

#include <zuzu/err.h>
#include <zuzu/ipc.h>
#include <zuzu/protocols/nt_protocol.h>

static uint32_t den_pack_name(const char *name) {
    char packed[4] = {0, 0, 0, 0};

    if (name) {
        for (int i = 0; i < 4 && name[i] != '\0'; i++)
            packed[i] = name[i];
    }

    return nt_pack(packed);
}

__attribute__((weak)) den_id_t den_create(const char *name, uint32_t cap) {
    zuzu_ipcmsg_t reply = _call(NT_PORT, DEN_CREATE, den_pack_name(name), cap);
    if (reply.r1 == DEN_OK)
        return (den_id_t)reply.r2;
    return (den_id_t)reply.r1;
}

__attribute__((weak)) int den_destroy(den_id_t id) {
    (void)id;
    return ZUZU_ERR_NOMATCH;
}

__attribute__((weak)) den_id_t den_myden(const char *name) {
    (void)name;
    zuzu_ipcmsg_t reply = _call(NT_PORT, DEN_MYDEN, 0, 0);
    if (reply.r1 == DEN_OK)
        return (den_id_t)reply.r2;
    return 0;
}
