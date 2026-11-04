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

char *terminal_formatted_log_category_string(LogLevel level) {
    char *message_type_string = NULL;
    switch (level) {
        case LOG_ERROR:
            message_type_string = g_strdup_printf ("%s [%s] %s", tcRED, log_category_string(level), tcNRM);
            break;
        case LOG_WARNING:
            message_type_string = g_strdup_printf ("%s [%s] %s", tcYEL, log_category_string(level), tcNRM);
            break;
        case LOG_INFO:
            message_type_string = g_strdup_printf ("%s [%s] %s", tcGRN, log_category_string(level), tcNRM);
            break;
        case LOG_DEBUG:
            message_type_string = g_strdup_printf ("%s [%s] %s", tcBLU, log_category_string(level), tcNRM);
            break;
        case LOG_TRACE:
            message_type_string = g_strdup_printf ("%s [%s] %s", tcCYN, log_category_string(level), tcNRM);
            break;
        default:
            message_type_string = log_category_string(level);
            break;
    }
    return message_type_string;
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
        fprintf(stdout, "%s %s\n", terminal_formatted_log_category_string(level), buffer);
    }
    if (log_file != NULL) {
        char *date_string = g_date_time_format (g_date_time_new_now_local (), "%Y-%m-%d-%H:%M:%S");
        fprintf(log_file, "%s - %s %s\n", date_string, log_category_string(level), buffer);
        g_free(date_string);
    }
}

void log_initialize(LogLevel logging_level) {
    log_level = logging_level;

    // Logs go to the user's state folder (usually ~/.local/state/limrendir/logs), whatever the current folder
    char *log_dir = g_build_filename(g_get_user_state_dir(), "limrendir", "logs", NULL);
    g_mkdir_with_parents(log_dir, 0755);

    char const *date_string = g_date_time_format (g_date_time_new_now_local (), "%Y-%m-%d-%H:%M:%S");
    char *log_basename = g_strdup_printf("logfile_%s.log", date_string);
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

    log_trace("Log level: %d", log_level);
}

void log_terminate() {
    if (log_file != NULL) {
        log_info("Logs for this session were saved in '%s'", log_filename);
        fclose(log_file);
    }
    log_file = NULL;
}