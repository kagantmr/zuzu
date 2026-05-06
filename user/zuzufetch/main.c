#include <stdint.h>
#include <stdio.h>
#include <zuzu/zuzu.h>
#include <ansi.h>
#include "zuzu/syspage.h"

#define LOGO_WIDTH    50
#define INFO_MAX      20
#define INFO_LINE_LEN 80
#define LABEL_WIDTH   10

/* Write "Label:    value" into dst with ANSI color on the label */
static void fmt_kv(char *dst, size_t cap, const char *label, const char *value)
{
    char padded[LABEL_WIDTH + 1];
    snprintf(padded, sizeof(padded), "%-*s", LABEL_WIDTH, label);
    snprintf(dst, cap, ANSI_BLUE "%s" ANSI_RESET "%s", padded, value);
}

/* Calculate visible length of string, excluding ANSI escape sequences */
static int visible_len_ansi(const char *s)
{
    int len = 0;
    while (*s) {
        if (*s == '\033') {
            /* Skip ANSI escape sequence */
            while (*s && *s != 'm')
                s++;
            if (*s == 'm')
                s++;
        } else {
            len++;
            s++;
        }
    }
    return len;
}

static void emit_tiles(char *dst, size_t cap)
{
    snprintf(dst, cap,
        "\033[40m  \033[0m\033[41m  \033[0m\033[42m  \033[0m\033[43m  \033[0m"
        "\033[44m  \033[0m\033[45m  \033[0m\033[46m  \033[0m\033[47m  \033[0m");
}

static int build_info(char info[][INFO_LINE_LEN])
{
    int n = 0;
    char tmp[64];
    zuzu_syspage_t *sp = (zuzu_syspage_t *)SYSPAGE;

    /* blank lines to align with logo top */
    info[n][0] = '\0'; n++;
    info[n][0] = '\0'; n++;

    /* title */
    snprintf(info[n], INFO_LINE_LEN,
             ANSI_CYAN "zuzu" ANSI_RESET " %s", sp->version);
    n++;

    snprintf(info[n], INFO_LINE_LEN,
             ANSI_CYAN "----------" ANSI_RESET);
    n++;

    /* machine */
    fmt_kv(info[n], INFO_LINE_LEN, "Machine:", sp->machine);
    n++;

    /* cpu */
    fmt_kv(info[n], INFO_LINE_LEN, "CPU:", sp->cpu);
    n++;

    /* memory */
    uint32_t ram_mb  = sp->mem_total_kb / 1024;
    uint32_t free_mb = sp->mem_free_kb / 1024;
    snprintf(tmp, sizeof(tmp), "%u MB free / %u MB total", free_mb, ram_mb);
    fmt_kv(info[n], INFO_LINE_LEN, "Memory:", tmp);
    n++;

    /* uptime */
    snprintf(tmp, sizeof(tmp), "%u s", sp->uptime_s);
    fmt_kv(info[n], INFO_LINE_LEN, "Uptime:", tmp);
    n++;

    /* build */
    fmt_kv(info[n], INFO_LINE_LEN, "Build:", sp->build);
    n++;

    /* devices */
    if (sp->dev_count > 0) {
        snprintf(tmp, sizeof(tmp), "%u devices", sp->dev_count);
        fmt_kv(info[n], INFO_LINE_LEN, "Devices:", tmp);
        n++;
    }


    /* blank spacer */
    info[n][0] = '\0'; n++;

    /* color tiles */
    emit_tiles(info[n], INFO_LINE_LEN);
    n++;

    return n;
}


int main(int argc, char *argv[])
{
    static const char *logo[] = {
            "                                                  ",
            "                                                  ",
            "       \033[37m@@@@@@@@@@@\033[0m                                ",
            "       \033[37m@@@@@@@@@@@\033[0m                \033[90m@@\033[0m              ",
            "            \033[37m@@@@\033[0m                \033[90m@@@@@    @@@\033[0m      ",
            "          \033[37m@@@@\033[0m                  \033[90m@@@       @@@\033[0m     ",
            "        \033[37m@@@@\033[0m                    \033[90m@@@      @@@\033[0m      ",
            "      \033[37m@@@@\033[0m                     \033[90m@@@       @@\033[0m       ",
            "    \033[37m@@@@\033[0m           \033[37m@@@@@@@@\033[0m    \033[90m@@@      @@@\033[0m       ",
            "   \033[37m@@@@@@@@@@@@@@@@@\033[0m      \033[37m@@@\033[0m  \033[90m@@@     @@@\033[0m        ",
            "            \033[37m@@\033[0m                 \033[90m@@@@@@@@\033[0m           ",
            "           \033[37m@@@\033[0m                                    ",
            "           \033[37m@@@\033[0m                       \033[90m@@@\033[0m          ",
            "           \033[37m@@@\033[0m      \033[90m@@@@@@@@@@@\033[0m     \033[90m@@@\033[0m           ",
            "           \033[37m@@@\033[0m    \033[90m@@@         @@@\033[0m   \033[90m@@@\033[0m           ",
            "           \033[37m@@@\033[0m    \033[90m@@@         @@@\033[0m   \033[90m@@@\033[0m           ",
            "           \033[37m@@@\033[0m   \033[90m@@@          @@@\033[0m   \033[90m@@@\033[0m           ",
            "            \033[37m@@@@@@@@\033[0m           \033[90m@@@@@@@@\033[0m           ",
            "                                                  ",
            "                                                  ",
            "                                                  "
    };
    static char info[INFO_MAX][INFO_LINE_LEN];
    int info_count = build_info(info);

    int logo_count = (int)(sizeof(logo) / sizeof(logo[0]));
    int lines = logo_count > info_count ? logo_count : info_count;

    for (int i = 0; i < lines; i++) {
        /* logo column */
        if (i < logo_count) {
            printf("%s%s%s", ANSI_CYAN, logo[i], ANSI_RESET);
            int pad = LOGO_WIDTH - visible_len_ansi(logo[i]);
            for (int p = 0; p < pad; p++)
                printf(" ");
        } else {
            for (int p = 0; p < LOGO_WIDTH; p++)
                printf(" ");
        }

        /* info column */
        printf("  ");
        if (i < info_count)
            printf("%s", info[i]);

        printf("\n");
    }

    printf(ANSI_RESET);
    return 0;
}