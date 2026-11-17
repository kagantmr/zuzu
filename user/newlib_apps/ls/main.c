#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Entries per line. */
#define LS_COLUMNS 5

/* Lists one directory, or names a file. Returns 0, or 1 if it cannot be read. */
static int ls_one(const char *path, int show_title)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        printf("ls: cannot access '%s': No such file or directory\n", path);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("%s\n", path);
        return 0;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        printf("ls: cannot open '%s'\n", path);
        return 1;
    }

    if (show_title)
        printf("%s:\n", path);

    int n = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("%-10s\t", entry->d_name);
        if (++n % LS_COLUMNS == 0)
            printf("\n");
    }
    if (n % LS_COLUMNS != 0 || n == 0)
        printf("\n");

    closedir(dir);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return ls_one(".", 0);

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            printf("\n");
        rc |= ls_one(argv[i], argc > 2);
    }
    return rc;
}
