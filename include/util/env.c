/**
 * @file
 * @brief The environment a zcrt program was started with.
 */

#include <string.h>
#include <util/env.h>
#include <util/stdio_core.h>

#pragma weak StdioSetTtyAlias

/** @brief The environment array that follows argv. */
static char **g_envp;

void __zcrt_env_init(int argc, char **argv)
{
    if (argc > 0 && argv)
        g_envp = argv + argc + 1;
    const char *tty = EnvGet("TTY");
    if (tty && tty[0] && StdioSetTtyAlias)
        StdioSetTtyAlias(tty);
}

const char *EnvGet(const char *name)
{
    size_t len = strlen(name);
    for (uint32_t i = 0; g_envp && g_envp[i]; i++) {
        if (strncmp(g_envp[i], name, len) == 0 && g_envp[i][len] == '=')
            return g_envp[i] + len + 1;
    }
    return NULL;
}

const char *EnvAt(uint32_t index)
{
    if (!g_envp)
        return NULL;
    for (uint32_t i = 0; i < index; i++) {
        if (!g_envp[i])
            return NULL;
    }
    return g_envp[index];
}
