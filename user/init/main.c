#include "zuzu.h"

int main() {
    const char hello_string[] = "Hello from zuzu ELF!\n";
    _log(hello_string, sizeof(hello_string) - 1);
    return 0;
}