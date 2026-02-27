#include "zuzu.h"

int big_array[256];  // forces a BSS segment

int main() {
    const char hello_string[] = "Hello from zuzu ELF!\n";
    _log(hello_string, sizeof(hello_string) - 1);
    if (big_array[0] == 0 && big_array[255] == 0)
        _log("BSS OK!\n", 8);
    else
        _log("BSS FAIL\n", 9);
    return 0;
}