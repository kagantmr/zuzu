#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/** @brief Prints the usage line. */
static void Usage(void) { fputs("usage: logger [-p priority] [-t tag] [-s] message...\n", stderr); }

/**
 * @brief Parses a priority name or number.
 * @param[in] s The text, such as "err" or "3".
 * @return The priority, or -1 if @p s is not one.
 */
static int ParsePriority(const char *s)
{
    static const char *const names[] = {"emerg", "alert", "crit", "err",
                                        "warning", "notice", "info", "debug"};
    for (int i = 0; i < 8; i++) {
        if (strcmp(s, names[i]) == 0)
            return i;
    }
    if (s[0] >= '0' && s[0] <= '7' && s[1] == '\0')
        return s[0] - '0';
    return -1;
}

int main(int argc, char *argv[])
{
    int pri = LOG_INFO;
    int option = 0;
    const char *tag = "logger";
    int i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            pri = ParsePriority(argv[++i]);
            if (pri < 0) {
                Usage();
                return 2;
            }
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            tag = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0) {
            option |= LOG_PERROR;
        } else {
            Usage();
            return 2;
        }
    }
    if (i >= argc) {
        Usage();
        return 2;
    }

    char msg[256] = "";
    for (; i < argc; i++) {
        if (msg[0])
            strncat(msg, " ", sizeof(msg) - strlen(msg) - 1);
        strncat(msg, argv[i], sizeof(msg) - strlen(msg) - 1);
    }

    openlog(tag, option, LOG_USER);
    syslog(pri, "%s", msg);
    closelog();
    return 0;
}
