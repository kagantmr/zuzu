#include "zuzu.h"

int big_array[256];  // forces a BSS segment

int main() {
    _log("init: starting\n", 15);

    int pid = _spawn("bin/hello", 9);
    _log("init: spawned hello\n", 20);

    int status = _wait(pid);
    // status has hello's exit code

    _log("init: done\n", 11);
    return 0;
}