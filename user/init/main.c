#include "zuzu.h"

int main()
{
    _log("init: starting\n", 15);

    _spawn("bin/zuart", 9);
    _log("init: spawned zuart\n", 20);

    int pid = _spawn("bin/hello", 9);
    _log("init: spawned hello\n", 20);

    long int status;
    _wait(pid, &status);
    // status has hello's exit code

    _log("init: done\n", 11);

    while (1)
    {
        _yield();
    }
    return 0;
}