/**
 * @file
 * @brief Device control requests; only the terminal window size is supported.
 */

#ifndef ZUZU_SYS_IOCTL_H
#define ZUZU_SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ioctl() request: read the terminal's window size into a struct winsize. */
#define TIOCGWINSZ 0x5413

/** @brief A terminal's size. */
struct winsize {
    unsigned short ws_row;    /**< Rows of characters. */
    unsigned short ws_col;    /**< Columns of characters. */
    unsigned short ws_xpixel; /**< Width in pixels; always 0. */
    unsigned short ws_ypixel; /**< Height in pixels; always 0. */
};

/**
 * @brief Sends a control request to a device.
 *
 * @param[in]     fd      File descriptor 0, 1 or 2.
 * @param[in]     request TIOCGWINSZ.
 * @param[in,out] ...     Pointer to the request's argument.
 * @return 0 on success, or -1 with errno set (ENOTTY if the request or the descriptor is not supported,
 *         or no window size is known).
 */
int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_SYS_IOCTL_H */
