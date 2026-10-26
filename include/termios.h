/**
 * @file
 * @brief POSIX terminal interface, backed by ttysvc through the console.
 * Only the flags ttysvc implements change behaviour: ICANON, ECHO, ISIG, ICRNL, OPOST and ONLCR.
 */

#ifndef ZUZU_TERMIOS_H
#define ZUZU_TERMIOS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Type of the flag words in a termios. */
typedef unsigned int tcflag_t;
/** @brief Type of a control character. */
typedef unsigned char cc_t;
/** @brief Type of a baud rate. */
typedef unsigned int speed_t;

/** @brief Number of control characters in a termios. */
#define NCCS 32

/** @brief A terminal's settings. */
struct termios {
    tcflag_t c_iflag;  /**< Input flags. */
    tcflag_t c_oflag;  /**< Output flags. */
    tcflag_t c_cflag;  /**< Control flags. */
    tcflag_t c_lflag;  /**< Local flags. */
    cc_t c_cc[NCCS];   /**< Control characters, indexed by the V* constants. */
    speed_t c_ispeed;  /**< Input baud rate. */
    speed_t c_ospeed;  /**< Output baud rate. */
};

/** @brief Input: ignore a break condition. */
#define IGNBRK 0000001
/** @brief Input: a break condition raises an interrupt. */
#define BRKINT 0000002
/** @brief Input: ignore bytes with parity errors. */
#define IGNPAR 0000004
/** @brief Input: mark parity errors in the input stream. */
#define PARMRK 0000010
/** @brief Input: check parity. */
#define INPCK 0000020
/** @brief Input: strip the eighth bit. */
#define ISTRIP 0000040
/** @brief Input: translate newline to carriage return. */
#define INLCR 0000100
/** @brief Input: ignore carriage return. */
#define IGNCR 0000200
/** @brief Input: translate carriage return to newline. */
#define ICRNL 0000400
/** @brief Input: ^S and ^Q control output flow. */
#define IXON 0002000
/** @brief Input: any character restarts output. */
#define IXANY 0004000
/** @brief Input: the terminal may send ^S and ^Q to control input flow. */
#define IXOFF 0010000

/** @brief Output: process output. */
#define OPOST 0000001
/** @brief Output: translate newline to carriage return and newline. */
#define ONLCR 0000004
/** @brief Output: translate carriage return to newline. */
#define OCRNL 0000010

/** @brief Control: mask for the character size bits. */
#define CSIZE 0000060
/** @brief Control: 5 data bits. */
#define CS5 0000000
/** @brief Control: 6 data bits. */
#define CS6 0000020
/** @brief Control: 7 data bits. */
#define CS7 0000040
/** @brief Control: 8 data bits. */
#define CS8 0000060
/** @brief Control: two stop bits. */
#define CSTOPB 0000100
/** @brief Control: enable the receiver. */
#define CREAD 0000200
/** @brief Control: generate and check parity. */
#define PARENB 0000400
/** @brief Control: odd parity. */
#define PARODD 0001000
/** @brief Control: hang up on last close. */
#define HUPCL 0002000
/** @brief Control: ignore modem control lines. */
#define CLOCAL 0004000

/** @brief Local: ^C, ^\\ and ^Z raise signals. */
#define ISIG 0000001
/** @brief Local: canonical mode, input is collected a line at a time. */
#define ICANON 0000002
/** @brief Local: echo typed characters. */
#define ECHO 0000010
/** @brief Local: erase echoes as backspace, space, backspace. */
#define ECHOE 0000020
/** @brief Local: kill character echoes a newline. */
#define ECHOK 0000040
/** @brief Local: echo newline even without ECHO. */
#define ECHONL 0000100
/** @brief Local: do not flush queues on interrupt. */
#define NOFLSH 0000200
/** @brief Local: background output raises a signal. */
#define TOSTOP 0000400
/** @brief Local: extended input processing. */
#define IEXTEN 0100000

/** @brief Control character index: interrupt (^C). */
#define VINTR 0
/** @brief Control character index: quit. */
#define VQUIT 1
/** @brief Control character index: erase. */
#define VERASE 2
/** @brief Control character index: kill line (^U). */
#define VKILL 3
/** @brief Control character index: end of file (^D). */
#define VEOF 4
/** @brief Control character index: read timeout in tenths of a second. */
#define VTIME 5
/** @brief Control character index: bytes a read waits for. */
#define VMIN 6
/** @brief Control character index: resume output (^Q). */
#define VSTART 8
/** @brief Control character index: stop output (^S). */
#define VSTOP 9
/** @brief Control character index: suspend. */
#define VSUSP 10
/** @brief Control character index: extra end of line. */
#define VEOL 11
/** @brief Control character index: erase word (^W). */
#define VWERASE 14

/** @brief Baud rate: hang up. */
#define B0 0000000
/** @brief Baud rate: 9600. */
#define B9600 0000015
/** @brief Baud rate: 115200. */
#define B115200 0010002

/** @brief tcsetattr() action: change at once. */
#define TCSANOW 0
/** @brief tcsetattr() action: change after output drains. */
#define TCSADRAIN 1
/** @brief tcsetattr() action: change after output drains, discarding input. */
#define TCSAFLUSH 2

/**
 * @brief Reads the terminal's settings.
 *
 * @param[in]  fd File descriptor 0, 1 or 2.
 * @param[out] t  Receives the settings.
 * @return 0 on success, or -1 with errno set (ENOTTY if @p fd is not the console).
 */
int tcgetattr(int fd, struct termios *t);
/**
 * @brief Changes the terminal's settings.
 *
 * Only the flags ttysvc implements take effect. The change is always made at once.
 *
 * @param[in] fd     File descriptor 0, 1 or 2.
 * @param[in] action TCSANOW, TCSADRAIN or TCSAFLUSH.
 * @param[in] t      The new settings.
 * @return 0 on success, or -1 with errno set.
 */
int tcsetattr(int fd, int action, const struct termios *t);
/**
 * @brief Sets raw mode: no line editing, echo, signals or output processing, 8 bits.
 *
 * @param[in,out] t The settings to change.
 */
void cfmakeraw(struct termios *t);
/**
 * @brief Gives the input baud rate.
 *
 * @param[in] t The settings.
 * @return The rate.
 */
speed_t cfgetispeed(const struct termios *t);
/**
 * @brief Gives the output baud rate.
 *
 * @param[in] t The settings.
 * @return The rate.
 */
speed_t cfgetospeed(const struct termios *t);
/**
 * @brief Sets the input baud rate.
 *
 * @param[in,out] t     The settings to change.
 * @param[in]     speed The rate.
 * @return 0.
 */
int cfsetispeed(struct termios *t, speed_t speed);
/**
 * @brief Sets the output baud rate.
 *
 * @param[in,out] t     The settings to change.
 * @param[in]     speed The rate.
 * @return 0.
 */
int cfsetospeed(struct termios *t, speed_t speed);

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_TERMIOS_H */
