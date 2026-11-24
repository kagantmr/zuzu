#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

int remove_path(const char *path, int recursive) {

    struct stat path_stat;
    if (lstat(path, &path_stat) < 0) {
        printf("rm: cannot remove '%s': No such file or directory\n", path);
        return -1;
    }

   
    if (!S_ISDIR(path_stat.st_mode)) {
        if (unlink(path) != 0) {
            printf("Error deleting file\n");
            return -1;
        }
        
        return 0;
    }

    if (!recursive) {
        printf("rm: %s: is a directory\n", path);
        return -1;
    }

    DIR *d = opendir(path);
    if (d == NULL) {
        printf("Error opening directory\n");
        return -1;
    }

    int status = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char child[4096];
        size_t need = strlen(path) + 1 + strlen(e->d_name) + 1;  
        if (need > sizeof child) {
            fprintf(stderr, "rm: path too long: %s/%s\n", path, e->d_name);
            status = -1;
            continue;
        }
        strcpy(child, path);
        strcat(child, "/");
        strcat(child, e->d_name);

        if (remove_path(child, recursive) < 0) {
            status = -1;
        }

    }

    closedir(d);

    //if (rmdir(path) < 0) { 
      //      printf("Error removing directory: %s\n", path);
       //     return -1;
       // }

    return status;

}

int main(int argc, char *argv[])
{
    DIR *dir = NULL;


    if (argc < 2) {
        printf("Usage: rm [-r] <file>\n");
        return 1;
    }

    int recursive = 0;
    int i = 1;

    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        
        if (!strcmp(argv[i], "--")) { 
            i++; 
            break; 
        }

        if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-R")){
            recursive = 1;
        }
            
        else {
            printf("rm: invalid option -- '%s'\n", argv[i]);
            return 1;
        }
    }

    const char *path = argv[i];

    struct stat path_stat;
    if (stat(path, &path_stat) < 0) {
        printf("rm: cannot remove '%s': No such file or directory\n", path);
        return 1;
    }

    int status = 0;
    for (; i < argc; i++)
        if (remove_path(argv[i], recursive) < 0)
            status = 1;

    return status;
}