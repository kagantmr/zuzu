#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd;
    char buf[1024];
    ssize_t n;

    if (argc < 2) {
        while ((n = read(0, buf, sizeof(buf))) > 0) {
            if (write(1, buf, n) < 0)
                return 1;
        }
        return n < 0 ? 1 : 0;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        write(2, "Error opening file\n", 19);
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(1, buf, n) < 0)
            break;
    }

    close(fd);
    return 0;
}
