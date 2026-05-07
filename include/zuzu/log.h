#ifndef ZUZU_LOG_H
#define ZUZU_LOG_H

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
} log_level_t;

void log_set_level(log_level_t min_level);
void log_write(log_level_t level, const char *tag, const char *fmt, ...);

#define LOG_DEBUG(tag, fmt, ...) log_write(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  log_write(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  log_write(LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) log_write(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#endif // ZUZU_LOG_H