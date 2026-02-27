#include "zuzu.h"

int big_array[256];  // forces a BSS segment

int main() {
    const char hello_string[] = "Hello from zuzu ELF!\n";
    _log(hello_string, sizeof(hello_string) - 1);
    return 0;
}