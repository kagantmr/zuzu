#include "zzsh.h"

int main(void) {
    // This is a placeholder for the main function of the zzsh shell.
    // You can implement the shell logic here, such as reading user input,
    // parsing commands, and executing them.

    // For now, we'll just print a welcome message and enter an infinite loop.
    _log("ZZSH " ZZSH_VER "\n", 24 + sizeof(ZZSH_VER) - 1);

    while (1)
    {
        _yield(); // Yield to other processes
    }

    return 0;
}