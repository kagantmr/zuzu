/**
 * @file
 * @brief Board bring-up hooks the kernel calls during early boot.
 */

#ifndef ARCH_PLATFORM_H
#define ARCH_PLATFORM_H

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

#endif // ARCH_PLATFORM_H
