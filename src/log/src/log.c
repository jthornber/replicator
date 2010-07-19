#include "log.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/*----------------------------------------------------------------*/

/*
 * FIXME: add a lock, so only one replicator can log to a particular
 * directory at a time.  At this point should we drop the race condition
 * loop in next_file ?
 */
struct log {
        char *dir;
        enum log_level min_level;
        enum log_level flush_level;

        FILE *file;
};

static int next_filename(const char *dir, char *result, size_t len)
{
        /* FIXME: include timestamps */
        snprintf(result, len, "%s/log.log", dir);
        return 1;
}

static FILE *next_file(const char *dir)
{
        FILE *fp;
        char filename[PATH_MAX];

        for (;;) {
                if (!next_filename(dir, filename, sizeof(filename)))
                        return NULL;

                /* we never append to a log, always opening a new one */
                fp = fopen(filename, "w");

                if (fp)
                        return fp;

                else if (errno != EEXIST)
                        return NULL;
        }

        /* never get here */
}

struct log *log_create(const char *dir,
                       enum log_level min_level,
                       enum log_level flush_level)
{
        FILE *fp;
        struct log *l;

        l = malloc(sizeof(*l));
        if (!l) {
                fprintf(stderr, "couldn't allocate log");
                exit(1);
        }

        l->dir = strdup(dir);
        l->min_level = min_level;
        l->flush_level = flush_level;

        fp = next_file(dir);
        if (!fp) {
                free(l);
                fprintf(stderr, "couldn't open log");
                exit(1);
        }
        l->file = fp;

        return l;
}

void log_destroy(struct log *l)
{
        fflush(l->file);
        fclose(l->file);
        free(l->dir);
        free(l);
}

static void check_for_rollover(struct log *l)
{
        /* FIXME: finish */
}

/*
 * FIXME: add milliseconds, also cache the second string to save
 * reformatting it for every message.
 */
static void format_time(struct timeval *now, char *buffer, size_t len)
{
        struct tm expanded;
        localtime_r(&now->tv_sec, &expanded);
        strftime(buffer, len,
                 "%Y/%m/%d %H:%M:%S", &expanded);
}

void print_prefix(struct log *l, enum log_level level)
{
        const char *levels[FATAL + 1] = {
                "DEBUG",
                "INFO",
                "WARN",
                "EVENT",
                "ERROR",
                "FATAL"
        };

        struct timeval now;
        struct timezone tz; // not used
        char time_buffer[64];

        gettimeofday(&now, &tz);
        format_time(&now, time_buffer, sizeof(time_buffer));

        fprintf(l->file, "%s %s ", time_buffer, levels[level]);
}

void log_vprintf(struct log *l, enum log_level level, const char *format, va_list ap)
{
        check_for_rollover(l);

        print_prefix(l, level);
        vfprintf(l->file, format, ap);
        fprintf(l->file, "\n");

        if (level >= l->flush_level)
                fflush(l->file);
}

/*----------------------------------------------------------------*/

/*
 * Global log, we could move this to a separate file, except we'd incur
 * extra function call overhead.
 */
static struct log *log_ = NULL;

void log_init(const char *dir,
              enum log_level min_level,
              enum log_level flush_level)
{
        assert(!log_);
        log_ = log_create(dir, min_level, flush_level);
}

void log_exit()
{
        log_destroy(log_);
        log_ = NULL;
}

void log_printf(enum log_level level, const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        log_vprintf(log_, level, fmt, ap);
        va_end(ap);
}

/*----------------------------------------------------------------*/
