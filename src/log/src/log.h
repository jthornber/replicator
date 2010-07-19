#ifndef LOG_LOG_H
#define LOG_LOG_H

/*----------------------------------------------------------------*/

enum log_level {
        DEBUG,
        INFO,
        WARN,
        EVENT,
        ERROR,
        FATAL
};

void log_init(const char *dir,
              enum log_level min_level, /* Messages below this level are not written to the log */
              enum log_level flush_level); /* Messages this level and above are flushed immediately */

void log_exit();

void log_printf(enum log_level level, const char *format, ...)
        __attribute__ ((format(printf, 2, 3)));

#define debug(fmt, args...) log_printf(DEBUG, "%s:%d " fmt, __FILE__, __LINE__, ## args)
#define info(fmt, args...) log_printf(INFO, fmt, ## args)
#define warn(fmt, args...) log_printf(WARN, fmt, ## args)
#define event(evt, fmt, args...) log_printf(EVENT, "[%s] " fmt, evt, ## args)
#define error(fmt, args...) log_printf(ERROR, fmt, ## args)
#define fatal(fmt, args...) do { log_printf(FATAL, fmt, ## args); exit(1); } while (0)

/*----------------------------------------------------------------*/

#endif
