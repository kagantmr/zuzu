#include "zuzu.h"
#include <zuzu/protocols/devmgr_protocol.h>
#include <zuzu/protocols/nt_protocol.h>

typedef struct {
    uint32_t    dev_class;   /* DEV_CLASS_* constant              */
    const char *compatible;  /* DTB compatible string             */
    size_t      compat_len;
} class_entry_t;

static const class_entry_t class_table[] = {
    { DEV_CLASS_SERIAL, "arm,pl011", 9 },
};
#define CLASS_TABLE_LEN (sizeof(class_table) / sizeof(class_table[0]))

int main(void) {
    int32_t my_port = _port_create();

    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    _call(NT_PORT, NT_REGISTER, nt_pack("devmgr"), nt_slot);

    while (1) {
        zuzu_ipcmsg_t msg = _recv(my_port);

        uint32_t sender   = msg.r0;
        uint32_t command  = msg.r1;

        if (command == DEV_REQUEST) {
            uint32_t dev_class = msg.r2;

            const char *compatible = NULL;
            size_t clen = 0;
            for (size_t i = 0; i < CLASS_TABLE_LEN; i++) {
                if (class_table[i].dev_class == dev_class) {
                    compatible = class_table[i].compatible;
                    clen       = class_table[i].compat_len;
                    break;
                }
            }
            if (!compatible) { _reply(sender, ERR_NOENT, 0, 0); continue; }

            int32_t dev_handle = _getdev(compatible, clen);
            if (dev_handle < 0) { _reply(sender, (uint32_t)dev_handle, 0, 0); continue; }

            int32_t granted = _port_grant(dev_handle, sender);
            if (granted < 0) { _reply(sender, (uint32_t)granted, 0, 0); continue; }

            _reply(sender, 0, (uint32_t)granted, 0);

        } else {
            _reply(sender, ERR_BADCMD, 0, 0);
        }
    }
}