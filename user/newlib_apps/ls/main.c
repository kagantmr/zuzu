#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>


int main(int argc, char *argv[])
{
    DIR *dir = NULL;
    struct dirent *entry;

    if (argc < 2) {
        printf("Usage: ls <directory>\n");
        return 1;
    }

    dir = opendir(argv[1]);

    if (dir == NULL) {
        printf("Error opening directory\n");
        return 1;
    }

    int i = 0;

    do {
        entry = readdir(dir);

        if (entry == NULL && i == 0) {
            printf("ls:  No such file or directory\n");
            return 0;
        }
        i++;

        if (entry == NULL) {
            break;
        }

        printf("%-10s\t", entry->d_name);

        if (i % 5 == 0) {
            printf("\n");
        }

    } while (entry != NULL);

    printf("\n");

    closedir(dir);
    return 0;
}
