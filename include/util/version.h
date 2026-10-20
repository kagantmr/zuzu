/**
 * @file
 * @brief zuzuOS (userspace) version, separate from the kernel's version.
 */

#ifndef ZUZUOS_VERSION_H
#define ZUZUOS_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief zuzuOS major version. */
#define ZUZUOS_VERSION_MAJOR 0
/** @brief zuzuOS minor version. */
#define ZUZUOS_VERSION_MINOR 7
/** @brief zuzuOS patch version. */
#define ZUZUOS_VERSION_PATCH 0
/** @brief zuzuOS version as a display string. */
#define ZUZUOS_VERSION "v0.7.0"
/** @brief Oldest kernel major version this userspace runs on. */
#define ZUZUOS_MIN_KERNEL_MAJOR 1

#ifdef __cplusplus
}
#endif

#endif