#include "drivers/uart.h"

void start(void) {
    uart_printf("Booting zuzuMicrokernel...\n", 1);
    while (1);      // Loop forever
}
