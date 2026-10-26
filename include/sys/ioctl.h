#ifndef ZUZU_SYS_IOCTL_H
#define ZUZU_SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#define TIOCGWINSZ 0x5413

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_SYS_IOCTL_H */
