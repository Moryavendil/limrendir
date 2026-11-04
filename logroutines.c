#include <sys/stat.h>
#include <errno.h>
#include <logroutines.h>

char* log_filename = NULL;
FILE *log_file = NULL;
LogLevel log_level = LOG_ERROR;

void log_trace(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_write(LOG_TRACE, "%s", buffer);
}
void log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_write(LOG_DEBUG, "%s", buffer);
}
void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_write(LOG_INFO, "%s", buffer);
}
void log_warning(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_write(LOG_WARNING, "%s", buffer);
}
void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_write(LOG_ERROR, "%s", buffer);
}

char *log_category_string(LogLevel level) {
    char *message_type_string = NULL;
    switch (level) {
        case LOG_ERROR:
            message_type_string = "ERROR";
            break;
        case LOG_WARNING:
            message_type_string = "WARNING";
            break;
        case LOG_INFO:
            message_type_string = "INFO";
            break;
        case LOG_DEBUG:
            message_type_string = "DEBUG";
            break;
        case LOG_TRACE:
            message_type_string = "TRACE";
            break;
        default:
            message_type_string = "???";
            break;
    }
    return message_type_string;
}

// Terminal color of each log category
static const char *log_category_color(LogLevel level) {
    switch (level) {
        case LOG_ERROR:   return tcRED;
        case LOG_WARNING: return tcYEL;
        case LOG_INFO:    return tcGRN;
        case LOG_DEBUG:   return tcBLU;
        case LOG_TRACE:   return tcCYN;
        default:          return tcNRM;
    }
}

void log_write(LogLevel level, const char *fmt, ...)
{
    // Arguments unpacking
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (level <= log_level) {
        fprintf(stdout, "%s [%s] %s %s\n", log_category_color(level), log_category_string(level), tcNRM, buffer);
    }
    if (log_file != NULL) {
        GDateTime *now = g_date_time_new_now_local ();
        char *date_string = g_date_time_format (now, "%Y-%m-%d-%H:%M:%S");
        fprintf(log_file, "%s - %s %s\n", date_string, log_category_string(level), buffer);
        g_free(date_string);
        g_date_time_unref (now);
    }
}

void log_initialize(LogLevel logging_level) {
    log_level = logging_level;

    // Logs go to the user's state folder (usually ~/.local/state/limrendir/logs), whatever the current folder
    char *log_dir = g_build_filename(g_get_user_state_dir(), "limrendir", "logs", NULL);
    g_mkdir_with_parents(log_dir, 0755);

    GDateTime *now = g_date_time_new_now_local ();
    char *date_string = g_date_time_format (now, "%Y-%m-%d_%H-%M-%S");
    g_date_time_unref (now);
    char *log_basename = g_strdup_printf("logfile_%s.log", date_string);
    g_free(date_string);
    log_filename = g_build_filename(log_dir, log_basename, NULL);
    g_free(log_basename);
    g_free(log_dir);
    log_file = fopen(log_filename, "w");
    if (log_file != NULL) {
        setvbuf(log_file, NULL, _IOLBF, 0);
    } else {
        int const saved_errno = errno;
        // Logging goes on in the terminal only
        log_warning("Could not create the log file '%s' (%s): logs are only shown here.", log_filename, g_strerror(saved_errno));
    }
    if (log_file != NULL)
        log_info("Logs for this session are saved in '%s'", log_filename);

    log_trace("Log level: %d", (int) log_level);
}

void log_terminate() {
    if (log_file != NULL) {
        log_info("Logs for this session were saved in '%s'", log_filename);
        fclose(log_file);
    }
    log_file = NULL;
}