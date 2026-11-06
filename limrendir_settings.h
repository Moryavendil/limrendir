#ifndef LIMRENDIR_SETTINGS_H
#define LIMRENDIR_SETTINGS_H

#include <glib.h>

// Settings remembered between sessions, stored as a key file (INI-like) in the user's state folder
// (usually ~/.local/state/limrendir/settings). Every setter saves the file immediately.

// Groups and keys
#define SETTINGS_GROUP_SNAPSHOT "snapshot"
#define SETTINGS_GROUP_VIDEO "video"
#define SETTINGS_KEY_DIRECTORY "directory"

// Loads the settings file. A missing or unreadable file is not an error: defaults are used.
void settings_load(void);
// Frees the settings (they were already saved by the setters).
void settings_terminate(void);

// Returns a newly allocated string (to be freed with g_free), or NULL if the setting is not set.
char *settings_get_string(const char *group, const char *key);
// Sets a string setting and saves the file.
void settings_set_string(const char *group, const char *key, const char *value);

// Returns the directory remembered for this group (to be freed with g_free),
// or NULL if none is remembered or if it no longer exists.
char *settings_get_directory(const char *group);
// Remembers the directory containing 'path' for this group.
void settings_set_directory_of(const char *group, const char *path);

#endif //LIMRENDIR_SETTINGS_H
