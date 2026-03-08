#include "zuzu.h"

int main()
{
    _log("init: starting\n", 15);

    int pid = _getpid();

    int counter = 0;

    int ns_port = _port_create();
    int ns_pid  = _spawn("bin/nametable", 13);
    _port_grant(ns_port, ns_pid);

    int zuart_pid = _spawn("bin/zuart", 9);
    _port_grant(ns_port, zuart_pid);

    int hello_pid = _spawn("bin/hello", 9);
    _port_grant(ns_port, hello_pid);



    int32_t status;
    _wait(hello_pid, &status);

    _log("init: done\n", 11);

    while (1)
    {
        _yield();
        _sleep(1000); // sleep for a sec
        _log("init: i'm still up!\n", 20);
        counter++;
        if (counter == 5) {
            int shmem_pid = _spawn("bin/shmem_test", 14);
            _port_grant(ns_port, shmem_pid);
        }

    }
    return 0;
}