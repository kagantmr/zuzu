#include "arch/arm/irq/irq.h"


void disable_interrupts() {
    __asm__ volatile (
        "cpsid i"
    );
};

void enable_interrupts() {
    __asm__ volatile (
        "cpsie i"
    );
}