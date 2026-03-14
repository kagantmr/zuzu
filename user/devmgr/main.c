#include "devmgr.h"

typedef struct {
    uint32_t dev_class;
    const char *compatible;
    size_t compat_len;
} class_entry_t;

static const class_entry_t class_table[] = {
    { DEV_CLASS_SERIAL, "arm,pl011", 9 },
};

#define CLASS_TABLE_LEN (sizeof(class_table) / sizeof(class_table[0]))

static int resolve_compatible(uint32_t dev_class, const char **out_compatible, size_t *out_len)
{
    for (size_t i = 0; i < CLASS_TABLE_LEN; i++) {
        if (class_table[i].dev_class == dev_class) {
            *out_compatible = class_table[i].compatible;
            *out_len = class_table[i].compat_len;
            return 0;
        }
    }
    return ERR_NOENT;
}

static int request_and_grant_class(uint32_t dev_class, int32_t target_pid, uint32_t *out_granted)
{
    const char *compatible = NULL;
    size_t compat_len = 0;
    int rc = resolve_compatible(dev_class, &compatible, &compat_len);
    if (rc < 0) {
        return rc;
    }

    int32_t dev_handle = _getdev(compatible, compat_len);
    if (dev_handle < 0) {
        return dev_handle;
    }

    int32_t granted_handle = _port_grant(dev_handle, target_pid);
    if (granted_handle < 0) {
        return granted_handle;
    }

    if (out_granted) {
        *out_granted = (uint32_t)granted_handle;
    }
    return 0;
}

static void handle_dev_request(uint32_t sender, uint32_t dev_class)
{
    uint32_t granted_serial = 0;
    int rc = request_and_grant_class(dev_class, (int32_t)sender, &granted_serial);
    if (rc < 0) {
        _reply(sender, (uint32_t)rc, 0, 0);
        return;
    }

    _reply(sender, 0, granted_serial, 0);
}

int devmgr_setup(void)
{
    int32_t my_port = _port_create();
    if (my_port < 0) {
        return my_port;
    }

    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    if (nt_slot < 0) {
        return nt_slot;
    }

    zuzu_ipcmsg_t reg = _call(NT_PORT, NT_REGISTER, nt_pack(DEVMGR_NAME), (uint32_t)nt_slot);
    if ((int32_t)reg.r1 != NT_REG_OK) {
        return ERR_BUSY;
    }

    return my_port;
}

void devmgr_loop(int32_t port_handle)
{
    while (1) {
        zuzu_ipcmsg_t msg = _recv(port_handle);

        if (msg.r1 == DEV_REQUEST) {
            handle_dev_request((uint32_t)msg.r0, msg.r2);
        } else {
            _reply((uint32_t)msg.r0, ERR_BADCMD, 0, 0);
        }
    }
}

int main(void)
{
    int32_t port = devmgr_setup();
    if (port < 0) {
        return 1;
    }

    devmgr_loop(port);
    return 0;
}