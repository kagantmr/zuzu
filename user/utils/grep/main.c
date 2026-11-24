#include <stdio.h>
#include <string.h>

/** @brief Options and the pattern. */
typedef struct {
    const char *pattern; /**< Text to look for. */
    int invert;          /**< -v: select lines that do not match. */
    int count;           /**< -c: print only how many lines are selected. */
    int ignore_case;     /**< -i: ignore case. */
    int number;          /**< -n: prefix each line with its number. */
    int with_name;       /**< Prefix each line with the file name. */
} Opts;

/**
 * @brief Finds a substring.
 * @param[in] line      The line.
 * @param[in] pattern   The text to find.
 * @param[in] icase     Nonzero to ignore case.
 * @return true if @p pattern occurs in @p line.
 */
static int Contains(const char *line, const char *pattern, int icase)
{
    size_t plen = strlen(pattern);
    if (plen == 0)
        return 1;
    for (const char *p = line; *p; p++) {
        size_t k = 0;
        while (k < plen && p[k]) {
            char a = p[k], b = pattern[k];
            if (icase) {
                a = (char)((a >= 'A' && a <= 'Z') ? a + 32 : a);
                b = (char)((b >= 'A' && b <= 'Z') ? b + 32 : b);
            }
            if (a != b)
                break;
            k++;
        }
        if (k == plen)
            return 1;
    }
    return 0;
}

/**
 * @brief Searches one stream.
 * @param[in] f    The stream.
 * @param[in] name Its name for the output prefix.
 * @param[in] o    The options.
 * @return The number of selected lines.
 */
static long Search(FILE *f, const char *name, const Opts *o)
{
    static char line[1024];
    long selected = 0, lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        size_t n = strlen(line);
        int had_newline = n > 0 && line[n - 1] == '\n';
        if (had_newline)
            line[n - 1] = '\0';
        if (Contains(line, o->pattern, o->ignore_case) == o->invert)
            continue;
        selected++;
        if (o->count)
            continue;
        if (o->with_name)
            printf("%s:", name);
        if (o->number)
            printf("%ld:", lineno);
        puts(line);
    }
    return selected;
}

int main(int argc, char *argv[])
{
    Opts o = {0};
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        for (const char *c = argv[i] + 1; *c; c++) {
            if (*c == 'v')
                o.invert = 1;
            else if (*c == 'c')
                o.count = 1;
            else if (*c == 'i')
                o.ignore_case = 1;
            else if (*c == 'n')
                o.number = 1;
            else {
                fputs("usage: grep [-vcin] pattern [file...]\n", stderr);
                return 2;
            }
        }
    }
    if (i >= argc) {
        fputs("usage: grep [-vcin] pattern [file...]\n", stderr);
        return 2;
    }
    o.pattern = argv[i++];
    o.with_name = argc - i > 1;

    long total = 0;
    int status = 0;
    if (i >= argc) {
        long n = Search(stdin, "(stdin)", &o);
        if (o.count)
            printf("%ld\n", n);
        total = n;
    }
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            fprintf(stderr, "grep: %s: No such file or directory\n", argv[i]);
            status = 2;
            continue;
        }
        long n = Search(f, argv[i], &o);
        if (o.count) {
            if (o.with_name)
                printf("%s:", argv[i]);
            printf("%ld\n", n);
        }
        total += n;
        fclose(f);
    }
    return status ? status : (total > 0 ? 0 : 1);
}
