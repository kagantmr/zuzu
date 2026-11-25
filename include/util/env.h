/**
 * @file
 * @brief The environment a zcrt program was started with (NAME=value strings after argv).
 */

#ifndef UTIL_ENV_H
#define UTIL_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Records the environment and attaches stdio to $TTY when it is set.
 *
 * zcrt's start code calls this before constructors run.
 *
 * @param[in] argc Argument count passed to the program.
 * @param[in] argv Argument vector; the environment follows its NULL terminator.
 */
void __zcrt_env_init(int argc, char **argv);

/**
 * @brief Looks up an environment variable.
 *
 * @param[in] name Variable name.
 * @return Its value, or NULL if it is not set.
 */
const char *EnvGet(const char *name);

/**
 * @brief Returns one environment entry.
 *
 * @param[in] index Entry number, from 0.
 * @return The "NAME=value" string, or NULL past the last entry.
 */
const char *EnvAt(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_ENV_H */
