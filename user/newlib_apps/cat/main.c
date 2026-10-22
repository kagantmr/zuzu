#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd;
    char buf[1024];
    ssize_t n;

    if (argc < 2) {
        write(2, "Usage: cat <file>\n", 20);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        write(2, "Error opening file\n", 19);
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }

    close(fd);
    return 0;
}
