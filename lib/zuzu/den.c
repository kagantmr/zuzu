#include <zuzu/den.h>

#include <zuzu/err.h>
#include <zuzu/msg.h>
#include <zuzu/protocols/nt_protocol.h>

static uint32_t den_pack_name(const char *name) {
    char packed[4] = {0, 0, 0, 0};

    if (name) {
        for (int i = 0; i < 4 && name[i] != '\0'; i++)
            packed[i] = name[i];
    }

    return nt_pack(packed);
}

__attribute__((weak)) DenID den_create(const char *name, uint32_t cap) {
    msg_t reply = zuzu_msg_call(NT_PORT, DEN_CREATE, den_pack_name(name), cap);
    if (reply.r1 == DEN_OK)
        return (DenID)reply.r2;
    return (DenID)reply.r1;
}

__attribute__((weak)) int den_destroy(DenID id) {
    (void)id;
    return ERR_NOSYS;
}

__attribute__((weak)) DenID den_myden(const char *name) {
    (void)name;
    msg_t reply = zuzu_msg_call(NT_PORT, DEN_MYDEN, 0, 0);
    if (reply.r1 == DEN_OK)
        return (DenID)reply.r2;
    return 0;
}
