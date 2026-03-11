#include "zuzu.h"
#include <zmalloc.h>

int main() {
    _spawn("bin/nametable", 13);
    _sleep(50);
    _spawn("bin/zuart", 9);
    _spawn("bin/zzsh", 8);
    
    while (1) {
        _yield();
    }
}
