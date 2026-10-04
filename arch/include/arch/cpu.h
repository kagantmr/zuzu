// arch/cpu.h - Neutral CPU / interrupt-flag control contract.
//
// Global interrupt enable/disable and save/restore for critical sections.
// The active architecture supplies the inline implementation.

#ifndef ZUZU_ARCH_CPU_H
#define ZUZU_ARCH_CPU_H

#include <stdint.h>

/* void     ArchGlobalIrqDisable(void);
 * void     ArchGlobalIrqEnable(void);
 * uint32_t ArchIrqSave(void);            -- disable IRQs, return prior state
 * void     ArchIrqRestore(uint32_t s);   -- restore prior IRQ state          */
#include <arch_impl/cpu.h>

#endif // ZUZU_ARCH_CPU_H
