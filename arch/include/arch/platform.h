/**
 * @file
 * @brief Board bring-up hooks the kernel calls during early boot.
 */

#ifndef ARCH_PLATFORM_H
#define ARCH_PLATFORM_H

#include <types.h>

/** @brief Probes and starts the board's devices from the device tree. */
void ArchPlatformInitDevices(void);

/**
 * @brief Writes one character to the boot UART by polling, before any console driver runs.
 *
 * A '\n' is sent as "\r\n".
 *
 * @param[in] c Character to write.
 */
void ArchEarlyPutc(char c);

/**
 * @brief Gives the physical range of the stack that early boot and kmain() run on.
 *
 * Exception-mode stacks are not part of it. Nothing uses it after kmain()'s last Schedule().
 *
 * @param[out] base Page-aligned physical start.
 * @param[out] end  Page-aligned physical end.
 */
void ArchBootStackRange(PhysAddr *base, PhysAddr *end);

#endif // ARCH_PLATFORM_H
