/*
 * Dual-path logger for PS3 homebrew (PSL1GHT).
 * Writes simultaneously to:
 *   /dev_hdd0/tmp/boardgame.log
 *   /dev_usb000/boardgame.log  (or /dev_usb001 if 000 unavailable)
 */

#include "logger.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>

static FILE *g_log_hdd = NULL;
static FILE *g_log_usb = NULL;
static LogLevel g_level = LOG_LEVEL_DEBUG;
static int g_initialized = 0;
static char g_usb_path[LOG_MAX_PATH] = {0};
static uint32_t g_frame_counter = 0;

static const char *level_str(LogLevel lvl)
{
    switch (lvl) {
        case LOG_LEVEL_DEBUG: return "DBG";
        case LOG_LEVEL_INFO:  return "INF";
        case LOG_LEVEL_WARN:  return "WRN";
        case LOG_LEVEL_ERROR: return "ERR";
        case LOG_LEVEL_FATAL: return "FTL";
        default:              return "???";
    }
}

static int try_open_usb(void)
{
    const char *candidates[] = {
        "/dev_usb000/boardgame.log",
        "/dev_usb001/boardgame.log",
        "/dev_usb002/boardgame.log",
        NULL
    };
    int i;
    for (i = 0; candidates[i]; ++i) {
        char dir[64];
        strncpy(dir, candidates[i], sizeof(dir) - 1);
        dir[sizeof(dir)-1] = '\0';
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        mkdir(dir, 0777); /* ignore error */

        FILE *f = fopen(candidates[i], "a");
        if (f) {
            g_log_usb = f;
            strncpy(g_usb_path, candidates[i], sizeof(g_usb_path) - 1);
            g_usb_path[sizeof(g_usb_path)-1] = '\0';
            return 1;
        }
    }
    return 0;
}

void logger_init(void)
{
    if (g_initialized) return;

    /* HDD path – always try */
    mkdir("/dev_hdd0/tmp", 0777);
    g_log_hdd = fopen("/dev_hdd0/tmp/boardgame.log", "a");

    /* USB path – first available */
    try_open_usb();

    g_initialized = 1;

    LOG_I("=== Board Game logger started ===");
    if (g_log_hdd) LOG_I("HDD log: /dev_hdd0/tmp/boardgame.log");
    else           LOG_W("HDD log unavailable");
    if (g_log_usb) LOG_I("USB log: %s", g_usb_path);
    else           LOG_W("USB log unavailable (no stick or write-protected)");
}

void logger_shutdown(void)
{
    if (!g_initialized) return;
    LOG_I("=== Logger shutting down ===");
    logger_flush();
    if (g_log_hdd) { fclose(g_log_hdd); g_log_hdd = NULL; }
    if (g_log_usb) { fclose(g_log_usb); g_log_usb = NULL; }
    g_initialized = 0;
}

void logger_set_level(LogLevel level)
{
    g_level = level;
}

void logger_log(LogLevel level, const char *func, int line, const char *fmt, ...)
{
    if (!g_initialized || level < g_level) return;

    char buf[LOG_BUF_SIZE];
    char msg[LOG_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    struct timeval tv;
    double sec;
    if (gettimeofday(&tv, NULL) == 0)
        sec = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
    else
        sec = (double)(++g_frame_counter) * 0.016;

    snprintf(buf, sizeof(buf), "[%.3f][%s] %.32s:%d | %.400s\n",
             sec, level_str(level), func ? func : "?", line, msg);

    if (g_log_hdd) {
        fputs(buf, g_log_hdd);
        fflush(g_log_hdd);
    }
    if (g_log_usb) {
        fputs(buf, g_log_usb);
        fflush(g_log_usb);
    }
}

void logger_flush(void)
{
    if (g_log_hdd) fflush(g_log_hdd);
    if (g_log_usb) fflush(g_log_usb);
}
