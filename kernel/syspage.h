/**
 * @file syspage.h
 * @brief The kernel-filled system information page.
 */

#ifndef KERNEL_SYSPAGE_H
#define KERNEL_SYSPAGE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <zuzu/syspage.h>

/** @brief Kernel mapping of the syspage; NULL before SyspageInit(). */
extern Syspage *g_sp;

/**
 * @brief Build the system information page: version, build, machine, CPU, memory totals, timer and device names.
 */
void SyspageInit(void);
/**
 * @brief Get the physical address of the syspage.
 *
 * @return The physical address.
 */
PhysAddr SyspagePa(void);
/**
 * @brief Refresh the free memory figure in the syspage from the frame allocator.
 *
 * Does nothing before SyspageInit().
 */
void SyspageUpdateMem(void);

#endif
