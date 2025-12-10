#include "irq.h"


void disable_interrupts() {
    __asm__ volatile (
        "cpsid i\n"
    );
};

void enable_interrupts() {
    __asm__ volatile (
        "cpsie i\n"
    );
}