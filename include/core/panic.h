#ifndef PANIC_H
#define PANIC_H

#define panicif(cond, fmt, ...)                                      \
    do {                                                            \
        if (cond) {                                                 \
            panicf("panicif triggered: (%s)\nLocation: %s:%d\n" fmt,\
                   #cond, __FILE__, __LINE__, ##__VA_ARGS__);       \
        }                                                           \
    } while (0)

/**
 * @brief Handle a kernel panic with a message.
 * 
 * @param message The panic message to display.
 */
void panic(const char* message);

/**
 * @brief Handle a kernel panic with a formatted message.
 * 
 * @param fmt The format string.
 * @param ... Additional arguments to be formatted.
 */
void panicf(const char* fmt, ...);

#endif