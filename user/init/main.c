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

    while (1)
    {
        _yield();
    }
    return 0;
}