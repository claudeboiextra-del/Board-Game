#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>   /* PSL1GHT filesystem API (sysFsOpen, sysFsMkdir, etc.) */
#include <sys/timer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_MAX_PATH 256
#define LOG_BUF_SIZE 512

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

void logger_init(void);
void logger_shutdown(void);
void logger_set_level(LogLevel level);
void logger_log(LogLevel level, const char *func, int line, const char *fmt, ...);
void logger_flush(void);

/* Convenience macros */
#define LOG_D(...) logger_log(LOG_LEVEL_DEBUG, __func__, __LINE__, __VA_ARGS__)
#define LOG_I(...) logger_log(LOG_LEVEL_INFO,  __func__, __LINE__, __VA_ARGS__)
#define LOG_W(...) logger_log(LOG_LEVEL_WARN,  __func__, __LINE__, __VA_ARGS__)
#define LOG_E(...) logger_log(LOG_LEVEL_ERROR, __func__, __LINE__, __VA_ARGS__)
#define LOG_F(...) logger_log(LOG_LEVEL_FATAL, __func__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
