#include "zuzu.h"
#include <zmalloc.h>

int main()
{
    int ns_port = _port_create();
    int ns_pid  = _spawn("bin/nametable", 13);
    _port_grant(ns_port, ns_pid);

    int zuart_pid = _spawn("bin/zuart", 9);
    _port_grant(ns_port, zuart_pid);

    int zzsh_pid = _spawn("bin/zzsh", 8);
    _port_grant(ns_port, zzsh_pid);

    int *malloced_mem = zmalloc(sizeof(int) * 9);
    if (malloced_mem) {
        for (int i = 0; i < 9; i++) {
            malloced_mem[i] = i;
            malloced_mem++;
        }
        for (int i = 10; i > 0; i--) {
            char c = '0' + malloced_mem[i];
            _log(&c, 1);
        }
        zfree(malloced_mem);
    } else {
        _log("Whoops chile", 12);
    }

    while (1)
    {
        _yield();
    }
    return 0;
}