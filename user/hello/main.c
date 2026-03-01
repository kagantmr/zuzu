#include "zuzu.h"

int main() {
    _log("Hello from hello!\n", 18);

   __asm__ volatile ("mrc p15, #0, r0, c1, c1, #0");

    return 42;
}