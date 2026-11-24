#include <limrendir_settings.h>
#include <logroutines.h>

static GKeyFile *settings_keyfile = NULL;
static char *settings_filename = NULL;

void settings_load(void) {
    settings_terminate();

    char *settings_dir = g_build_filename(g_get_user_state_dir(), "limrendir", NULL);
    settings_filename = g_build_filename(settings_dir, "settings", NULL);
    g_free(settings_dir);

    settings_keyfile = g_key_file_new();

    log_trace("Reading the settings file '%s'", settings_filename);
    GError *error = NULL;
    if (g_key_file_load_from_file(settings_keyfile, settings_filename, G_KEY_FILE_KEEP_COMMENTS, &error)) {
        log_debug("Settings loaded from '%s'", settings_filename);
    } else if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        log_debug("No settings file '%s' yet: using defaults.", settings_filename);
    } else {
        log_warning("Could not read the settings file '%s' (%s): using defaults.", settings_filename, error->message);
        // Start from an empty key file (a failed load may leave it partially filled)
        g_key_file_free(settings_keyfile);
        settings_keyfile = g_key_file_new();
    }
    g_clear_error(&error);
}

void settings_terminate(void) {
    g_clear_pointer(&settings_keyfile, g_key_file_free);
    g_clear_pointer(&settings_filename, g_free);
}

static void settings_save(void) {
    g_return_if_fail(settings_keyfile != NULL && settings_filename != NULL);

    char *settings_dir = g_path_get_dirname(settings_filename);
    g_mkdir_with_parents(settings_dir, 0755);
    g_free(settings_dir);

    log_trace("Writing the settings file '%s'", settings_filename);
    GError *error = NULL;
    if (g_key_file_save_to_file(settings_keyfile, settings_filename, &error)) {
        log_trace("Settings saved to '%s'", settings_filename);
    } else {
        log_warning("Could not save the settings file '%s': %s", settings_filename, error->message);
        g_clear_error(&error);
    }
}

char *settings_get_string(const char *group, const char *key) {
    if (settings_keyfile == NULL)
        return NULL;
    return g_key_file_get_string(settings_keyfile, group, key, NULL);
}

void settings_set_string(const char *group, const char *key, const char *value) {
    g_return_if_fail(settings_keyfile != NULL);
    g_return_if_fail(value != NULL);

    char *current_value = settings_get_string(group, key);
    gboolean const unchanged = g_strcmp0(current_value, value) == 0;
    g_free(current_value);
    if (unchanged)
        return;

    g_key_file_set_string(settings_keyfile, group, key, value);
    log_debug("Setting [%s] %s = %s", group, key, value);
    settings_save();
}

char *settings_get_directory(const char *group) {
    char *directory = settings_get_string(group, SETTINGS_KEY_DIRECTORY);
    if (directory != NULL && !g_file_test(directory, G_FILE_TEST_IS_DIR)) {
        log_debug("Remembered %s directory '%s' does not exist anymore: ignored.", group, directory);
        g_clear_pointer(&directory, g_free);
    }
    return directory;
}

void settings_set_directory_of(const char *group, const char *path) {
    g_return_if_fail(path != NULL);
    char *directory = g_path_get_dirname(path);
    settings_set_string(group, SETTINGS_KEY_DIRECTORY, directory);
    g_free(directory);
}
