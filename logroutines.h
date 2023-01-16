#ifndef ARAVIEWFORK_LOGROUTINES_H
#define ARAVIEWFORK_LOGROUTINES_H

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

typedef enum {
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE
} LogLevel;

// Terminal Colors
#define tcNRM  "\x1B[0m"
#define tcRED  "\x1B[31m"
#define tcGRN  "\x1B[32m"
#define tcYEL  "\x1B[33m"
#define tcBLU  "\x1B[34m"
#define tcMAG  "\x1B[35m"
#define tcCYN  "\x1B[36m"
#define tcWHT  "\x1B[37m"

extern char *log_filename;
extern FILE *log_file;
extern LogLevel log_level;

void log_trace(const char *fmt, ...);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warning(const char *fmt, ...);
void log_error(const char *fmt, ...);

void log_write(LogLevel level, const char *fmt, ...);

void log_initialize(LogLevel logging_level);
void log_terminate();


#endif //ARAVIEWFORK_LOGROUTINES_H
