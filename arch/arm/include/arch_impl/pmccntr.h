#ifndef ZUZU_ARM_IMPL_PMCCNTR_H
#define ZUZU_ARM_IMPL_PMCCNTR_H

#include <arch/barrier.h>
#include <zuzu/types.h>

static inline uint32_t ArchReadCycles(void)
{
	uint32_t cycles;
	__asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles) : : "memory");
	return cycles;
}

static inline __attribute__((always_inline)) uint32_t ArchMeasure(void)
{
	uint32_t cycles;
	ArchIsb();
	__asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles) : : "memory");
	ArchIsb();
	return cycles;
}

static inline __attribute__((always_inline)) uint32_t ArchMeasureEmpty(void)
{
	uint32_t cycles1, cycles2;
	ArchIsb();
	__asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles1) : : "memory");
	ArchIsb();

    // nothing...
    
	ArchIsb();
	__asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(cycles2) : : "memory");
	ArchIsb();
    
	return cycles2 - cycles1;
} 

#define ArchStartMeasurement ArchMeasure
#define ArchEndMeasurement ArchMeasure

#endif
