/**
 * @file main.c
 *
 * @brief zzsh: the zuzu shell.
 */

#include "zzsh.h"
#include <ansi.h>
#include <stdio.h>
#include <string.h>
#include <util/env.h>
#include <util/stdio_core.h>
#include <util/path.h>

/** @brief The startup file, relative to $HOME, run before the first prompt. */
#define RC_FILE ".zzshrc"

/**
 * @brief Runs the startup file if there is one.
 */
static void RunRc(void)
{
    const char *home = VarGet("HOME");
    char rel[PATH_MAX_LEN], path[PATH_MAX_LEN];
    if (snprintf(rel, sizeof(rel), "%s/%s", home ? home : "/", RC_FILE) < (int)sizeof(rel) &&
        PathResolve("/", rel, path, sizeof(path)))
        ShSource(path);
}

/** @brief Releases the file service connection at exit. */
static void __attribute__((destructor)) ShellDetachFsd(void) { FsdDetach(ShFsd()); }

/** @brief Exports every variable the shell was started with. */
static void ImportEnv(void)
{
    for (uint32_t i = 0;; i++) {
        const char *e = EnvAt(i);
        if (!e)
            break;
        const char *eq = strchr(e, '=');
        if (!eq || eq == e || (size_t)(eq - e) >= 64)
            continue;
        char name[64];
        memcpy(name, e, (size_t)(eq - e));
        name[eq - e] = '\0';
        if (strcmp(name, "PWD") != 0)
            VarExport(name, eq + 1);
    }
}

int main(int argc, char **argv)
{
    ShInit();
    ImportEnv();

    if (argc >= 2) {
        /* zzsh <script>: run a file and leave; no terminal editing needed. */
        char path[PATH_MAX_LEN];
        if (!PathResolve(g_cwd, argv[1], path, sizeof(path)) || !ShEnsureFsd()) {
            printf("zzsh: %s: cannot open\n", argv[1]);
            return 1;
        }
        int rc = ShSource(path);
        if (rc < 0) {
            printf("zzsh: %s: cannot read\n", argv[1]);
            return 1;
        }
        return rc;
    }

    stdio_set_raw(1);
    if (stdio_open_tty() != 0)
        return 1;
    g_interactive = true;
    if (!VarGet("TTY") && StdioTtyAlias()[0])
        VarExport("TTY", StdioTtyAlias());

    printf("%s", ANSI_BOLD ANSI_CYAN "zzsh " ZZSH_VER "\n" ANSI_RESET);
    RunRc();

    while (!g_exit) {
        char line[LINE_BUFFER_SIZE];
        if (!ReadLine(line, sizeof(line)))
            break;

        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            continue;
        HistoryPush(line);
        ShRunLine(line);
    }

    printf("\n");
    return g_status;
}
