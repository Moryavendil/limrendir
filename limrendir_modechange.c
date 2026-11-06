#include <gtk/gtk.h>
#include <gst/gst.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>
#include <limrendir_settings.h>

// USB memory check

#define USBFS_MEMORY_MB_PATH "/sys/module/usbcore/parameters/usbfs_memory_mb"
#define USBFS_MEMORY_MB_RECOMMENDED 1000

// Linux limits the memory all USB transfers may use at once (usbfs_memory_mb, 16 MB by default).
// This is too little for USB3 Vision cameras at high frame rates or resolutions: frames get lost.
// Warns if the limit is low. Does nothing if it cannot be read (e.g. not on Linux).
static void check_usbfs_memory (void)
{
    char *contents = NULL;

    if (!g_file_get_contents (USBFS_MEMORY_MB_PATH, &contents, NULL, NULL)) {
        log_debug("Could not read %s, the USB memory limit is not checked.", USBFS_MEMORY_MB_PATH);
        return;
    }

    guint64 usbfs_memory_mb = g_ascii_strtoull (g_strstrip (contents), NULL, 10);
    g_free (contents);

    // 0 means no limit
    if (usbfs_memory_mb == 0 || usbfs_memory_mb >= USBFS_MEMORY_MB_RECOMMENDED) {
        log_debug("USB memory limit (usbfs_memory_mb): %" G_GUINT64_FORMAT " MB", usbfs_memory_mb);
        return;
    }

    log_warning("The USB memory limit is low (usbfs_memory_mb = %" G_GUINT64_FORMAT " MB): "
                "USB3 cameras may lose frames at high frame rates.", usbfs_memory_mb);
    log_info("To raise USB memory limit until next reboot: "
                "sudo sh -c 'echo %d > " USBFS_MEMORY_MB_PATH "'", USBFS_MEMORY_MB_RECOMMENDED);
    log_info("To make it permanent, add usbcore.usbfs_memory_mb=%d to the kernel command line "
                "(e.g. GRUB_CMDLINE_LINUX_DEFAULT in /etc/default/grub, then sudo update-grub).",
                USBFS_MEMORY_MB_RECOMMENDED);
}

// MODE CHANGE

// Select mode
void select_mode (LrdViewer *viewer, TrnViewerMode mode)
{
    gboolean video_visibility;
    char *subtitle;
    gint width, height, x, y;

    if (!ARV_IS_CAMERA (viewer->camera))
        mode = TRN_VIEWER_MODE_CAMERA_LIST;

    switch (mode) {
        case TRN_VIEWER_MODE_CAMERA_LIST:
            if (gtk_toggle_button_get_mode(GTK_TOGGLE_BUTTON (viewer->record_button))) {
                gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (viewer->record_button),
                                            FALSE);
                stop_recording (viewer);
            }

            stop_key_listener(viewer);

            video_visibility = FALSE;
            gtk_stack_set_visible_child (GTK_STACK (viewer->main_stack), viewer->camera_box);
            gtk_header_bar_set_title (GTK_HEADER_BAR (viewer->main_headerbar), "Limrendir");
            gtk_header_bar_set_subtitle (GTK_HEADER_BAR (viewer->main_headerbar), NULL);


            stop_video (viewer);
            break;
        case TRN_VIEWER_MODE_VIDEO:
            video_visibility = TRUE;
            arv_camera_get_region (viewer->camera, &x, &y, &width, &height, NULL);
            subtitle = g_strdup_printf ("%s %dx%d@%d,%d %s",
                                        arv_camera_get_model_name (viewer->camera, NULL),
                                        width, height,
                                        x, y,
                                        arv_camera_get_pixel_format_as_string (viewer->camera, NULL));
            gtk_stack_set_visible_child (GTK_STACK (viewer->main_stack), viewer->video_box);
            gtk_header_bar_set_title (GTK_HEADER_BAR (viewer->main_headerbar), viewer->camera_name);
            gtk_header_bar_set_subtitle (GTK_HEADER_BAR (viewer->main_headerbar), subtitle);
            g_free (subtitle);

            start_video (viewer);

            start_key_listener(viewer);
            break;
        default:
            g_assert_not_reached ();
            break;
    }

    gtk_widget_set_visible (viewer->back_button, video_visibility);
    gtk_widget_set_visible (viewer->rotate_cw_button, video_visibility);
    gtk_widget_set_visible (viewer->flip_vertical_toggle, video_visibility);
    gtk_widget_set_visible (viewer->flip_horizontal_toggle, video_visibility);
    gtk_widget_set_visible (viewer->snapshot_button, video_visibility);
    gtk_widget_set_visible (viewer->camera_settings_button, video_visibility);
    gtk_widget_set_visible (viewer->acquisition_settings_button, video_visibility);
    gtk_widget_set_visible (viewer->help_button, video_visibility);

    // Custom G2L
    gtk_widget_set_visible (viewer->record_button, video_visibility);

}

// Enables or disables the video mode controls that must not be used during a recording or an exposure search
void set_video_controls_sensitive (LrdViewer *viewer, gboolean sensitive)
{
    gtk_widget_set_sensitive (viewer->camera_settings_button, sensitive);
    gtk_widget_set_sensitive (viewer->acquisition_settings_button, sensitive);
    gtk_widget_set_sensitive (viewer->snapshot_button, sensitive);
    gtk_widget_set_sensitive (viewer->back_button, sensitive);
    gtk_widget_set_sensitive (viewer->rotate_cw_button, sensitive);
    gtk_widget_set_sensitive (viewer->flip_vertical_toggle, sensitive);
    gtk_widget_set_sensitive (viewer->flip_horizontal_toggle, sensitive);
}

// VIDEO MODE
void switch_to_video_mode_cb (GtkToolButton *button, LrdViewer *viewer)
{
    if (!ARV_IS_CAMERA (viewer->camera)) {
        log_warning ("No camera opened: cannot switch to video mode.");
        return;
    }
    camera_region_cb(NULL, viewer);
    select_mode (viewer, TRN_VIEWER_MODE_VIDEO);
}

// Image transforms
gint get_videoflip_method (LrdViewer *viewer)
{
    static const gint methods[4][4] = {
            {0, 1, 2, 3},
            {4, 6, 5, 7},
            {5, 7, 4, 6},
            {2, 3, 0, 1}
    };
    int index = (viewer->flip_horizontal ? 1 : 0) + (viewer->flip_vertical ? 2 : 0);
    return methods[index][viewer->rotation % 4];
}

void update_transform (LrdViewer *viewer)
{
    g_object_set (viewer->transform, "method", get_videoflip_method (viewer), NULL);
    overlay_queue_redraw (viewer);
}

void rotate_cw_cb (GtkButton *button, LrdViewer *viewer)
{
    viewer->rotation = (viewer->rotation + 1) % 4;

    update_transform (viewer);
}

void flip_horizontal_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    viewer->flip_horizontal = gtk_toggle_button_get_active (toggle);

    update_transform (viewer);
}

void flip_vertical_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    viewer->flip_vertical = gtk_toggle_button_get_active (toggle);

    update_transform (viewer);
}

// Back to no rotation and no flip, with the flip buttons released (from aravis c4a70719e).
// Called when a new pipeline is created, since its videoflip element starts untransformed.
void reset_image_transform (LrdViewer *viewer)
{
    viewer->rotation = 0;
    viewer->flip_horizontal = FALSE;
    viewer->flip_vertical = FALSE;

    g_signal_handler_block (viewer->flip_vertical_toggle, viewer->flip_vertical_clicked);
    g_signal_handler_block (viewer->flip_horizontal_toggle, viewer->flip_horizontal_clicked);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->flip_vertical_toggle), FALSE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->flip_horizontal_toggle), FALSE);
    g_signal_handler_unblock (viewer->flip_vertical_toggle, viewer->flip_vertical_clicked);
    g_signal_handler_unblock (viewer->flip_horizontal_toggle, viewer->flip_horizontal_clicked);
}

// Snapshot saving
gboolean _save_gst_sample_to_file (GstSample *sample, const char *path, const char *mime_type, GError **error)
{
    GstSample *converted;
    GstCaps *caps;
    GstBuffer *gst_buffer;
    gboolean success = FALSE;

    g_return_val_if_fail (GST_IS_SAMPLE (sample), FALSE);

    caps = gst_caps_from_string (mime_type);
    converted = gst_video_convert_sample (sample, caps, GST_CLOCK_TIME_NONE, NULL);
    gst_caps_unref (caps);

    gst_buffer = gst_sample_get_buffer (converted);
    if (gst_buffer) {
        GstMapInfo map;

        gst_buffer_map (gst_buffer, &map, GST_MAP_READ);
        success = g_file_set_contents (path, (void *) map.data, map.size, error);
        gst_buffer_unmap (gst_buffer, &map);
    } else
        gst_sample_unref (converted);

    return success;
}

// Snapshot formats, attached to the dialog filters (the "Supported image formats" filter has none)
typedef enum {
    SNAPSHOT_FORMAT_NONE,
    SNAPSHOT_FORMAT_PNG,
    SNAPSHOT_FORMAT_JPEG,
    SNAPSHOT_FORMAT_RAW
} SnapshotFormat;

#define SNAPSHOT_FORMAT_KEY "lrd-snapshot-format"

static const char *_snapshot_format_extension (SnapshotFormat format)
{
    switch (format) {
        case SNAPSHOT_FORMAT_PNG: return ".png";
        case SNAPSHOT_FORMAT_JPEG: return ".jpg";
        case SNAPSHOT_FORMAT_RAW: return ".raw";
        default: return NULL;
    }
}

// The format given by the extension of 'name' (case insensitive), or SNAPSHOT_FORMAT_NONE if it is not a known one
static SnapshotFormat _snapshot_format_from_name (const char *name)
{
    g_autofree char *lower = g_ascii_strdown (name, -1);
    if (g_str_has_suffix (lower, ".png"))
        return SNAPSHOT_FORMAT_PNG;
    if (g_str_has_suffix (lower, ".jpg") || g_str_has_suffix (lower, ".jpeg"))
        return SNAPSHOT_FORMAT_JPEG;
    if (g_str_has_suffix (lower, ".raw"))
        return SNAPSHOT_FORMAT_RAW;
    return SNAPSHOT_FORMAT_NONE;
}

// The format remembered in the settings ("png", "jpg" or "raw"), or SNAPSHOT_FORMAT_NONE
static SnapshotFormat _snapshot_remembered_format (void)
{
    g_autofree char *format_name = settings_get_string (SETTINGS_GROUP_SNAPSHOT, SETTINGS_KEY_FORMAT);
    if (format_name == NULL)
        return SNAPSHOT_FORMAT_NONE;
    g_autofree char *dummy_name = g_strdup_printf ("snapshot.%s", format_name);
    return _snapshot_format_from_name (dummy_name);
}

// Asks the user whether to replace an existing file. Returns TRUE to replace it.
static gboolean _snapshot_confirm_overwrite (GtkWindow *parent, const char *filename)
{
    g_autofree char *basename = g_path_get_basename (filename);
    GtkWidget *message = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                                 "A file named \"%s\" already exists. Do you want to replace it?",
                                                 basename);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message),
                                              "Replacing it will overwrite its contents.");
    gtk_dialog_add_buttons (GTK_DIALOG (message), "_Cancel", GTK_RESPONSE_CANCEL,
                            "_Replace", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (message), GTK_RESPONSE_CANCEL);
    gint const response = gtk_dialog_run (GTK_DIALOG (message));
    gtk_widget_destroy (message);
    return response == GTK_RESPONSE_ACCEPT;
}

static SnapshotFormat _snapshot_filter_format (GtkFileFilter *filter)
{
    if (filter == NULL)
        return SNAPSHOT_FORMAT_NONE;
    return (SnapshotFormat) GPOINTER_TO_INT (g_object_get_data (G_OBJECT (filter), SNAPSHOT_FORMAT_KEY));
}

// Returns 'name' (newly allocated) with its known image extension, if any, replaced by the one of 'format'
static char *_snapshot_name_with_format (const char *name, SnapshotFormat format)
{
    size_t base_length = strlen (name);
    SnapshotFormat const current_format = _snapshot_format_from_name (name);
    if (current_format != SNAPSHOT_FORMAT_NONE)
        base_length = strrchr (name, '.') - name;
    return g_strdup_printf ("%.*s%s", (int) base_length, name, _snapshot_format_extension (format));
}

// GTK3 does not change the extension of the file name when another filter is chosen: we do it ourselves
static void _snapshot_filter_changed_cb (GtkFileChooser *chooser, GParamSpec *pspec, gpointer user_data)
{
    SnapshotFormat const format = _snapshot_filter_format (gtk_file_chooser_get_filter (chooser));
    if (format == SNAPSHOT_FORMAT_NONE)
        return; // "Supported image formats": keep the name as it is

    g_autofree char *current_name = gtk_file_chooser_get_current_name (chooser);
    if (current_name == NULL || current_name[0] == '\0')
        return;
    g_autofree char *new_name = _snapshot_name_with_format (current_name, format);
    gtk_file_chooser_set_current_name (chooser, new_name);
}

static GtkFileFilter *_snapshot_add_filter (GtkFileChooser *chooser, GtkFileFilter *filter_all, const char *name,
                                            const char *mime_type, const char *pattern, SnapshotFormat format)
{
    GtkFileFilter *filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, name);
    if (mime_type != NULL) {
        gtk_file_filter_add_mime_type (filter, mime_type);
        gtk_file_filter_add_mime_type (filter_all, mime_type);
    }
    if (pattern != NULL) {
        gtk_file_filter_add_pattern (filter, pattern);
        gtk_file_filter_add_pattern (filter_all, pattern);
    }
    g_object_set_data (G_OBJECT (filter), SNAPSHOT_FORMAT_KEY, GINT_TO_POINTER (format));
    gtk_file_chooser_add_filter (chooser, filter);
    return filter;
}

void snapshot_cb (GtkButton *button, LrdViewer *viewer)
{
    GtkFileFilter *filter_all;
    GtkFileFilter *filter_png = NULL;
    GtkFileFilter *filter_jpeg = NULL;
    GtkFileFilter *filter_raw = NULL;
    GtkFileFilter *default_filter;
    SnapshotFormat default_format;
    GtkWidget *dialog;
    GstSample *sample = NULL;
    ArvBuffer *buffer = NULL;
    char *path;
    char *filename;
    GDateTime *date;
    char *date_string;
    int width, height;
    g_autofree char *data = NULL;
    const char *pixel_format;
    size_t size = 0;
    gint result;

    if (GST_IS_ELEMENT (viewer->videosink))
        sample = gst_base_sink_get_last_sample (GST_BASE_SINK (viewer->videosink));
    if (ARV_IS_BUFFER (viewer->last_buffer))
        buffer = g_object_ref (viewer->last_buffer);

    if (!ARV_IS_BUFFER (buffer) && !GST_IS_SAMPLE (sample)) {
        arv_viewer_show_notification (viewer, "No buffer available", NULL);
        return;
    }

    g_return_if_fail (ARV_IS_CAMERA (viewer->camera));

    pixel_format = arv_camera_get_pixel_format_as_string (viewer->camera, NULL);
    arv_buffer_get_image_region (buffer, NULL, NULL, &width, &height);
    // Copy the raw data now: the stream may reuse the buffer memory while the dialog is open
    if (ARV_IS_BUFFER (buffer)) {
        const void *buffer_data = arv_buffer_get_data (buffer, &size);
        data = g_memdup2 (buffer_data, size);
    }

    date = g_date_time_new_now_local ();
    date_string = g_date_time_format (date, "%Y-%m-%d_%H-%M-%S");
    // Default format: the one of the last saved snapshot if it can be used here,
    // else PNG (a viewable image), or RAW if only the camera buffer is available
    default_format = _snapshot_remembered_format ();
    if (default_format == SNAPSHOT_FORMAT_RAW ? !ARV_IS_BUFFER (buffer)
                                              : (default_format == SNAPSHOT_FORMAT_NONE || !GST_IS_SAMPLE (sample)))
        default_format = GST_IS_SAMPLE (sample) ? SNAPSHOT_FORMAT_PNG : SNAPSHOT_FORMAT_RAW;
    filename = g_strdup_printf ("%s-%s-%d-%d-%s-%s%s",
                                arv_camera_get_vendor_name (viewer->camera, NULL),
                                arv_camera_get_device_serial_number (viewer->camera, NULL),
                                width,
                                height,
                                pixel_format != NULL ? pixel_format : "Unknown",
                                date_string,
                                _snapshot_format_extension (default_format));
    g_free (date_string);
    g_date_time_unref (date);

    // Default folder: the one where the last snapshot was saved, else the user's pictures folder
    g_autofree char *snapshot_dir = settings_get_directory (SETTINGS_GROUP_SNAPSHOT);
    if (snapshot_dir == NULL)
        snapshot_dir = g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES) != NULL
                                 ? g_get_user_special_dir (G_USER_DIRECTORY_PICTURES) : g_get_home_dir ());
    path = g_build_filename (snapshot_dir, filename, NULL);

    dialog = gtk_file_chooser_dialog_new ("Save Snapshot", GTK_WINDOW (viewer->main_window),
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          "_Cancel",
                                          GTK_RESPONSE_CANCEL,
                                          "_Save",
                                          GTK_RESPONSE_ACCEPT,
                                          NULL);
    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (dialog), path);
    gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), filename);

    filter_all = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter_all, "Supported image formats");

    if (GST_IS_SAMPLE (sample)) {
        filter_png = _snapshot_add_filter (GTK_FILE_CHOOSER (dialog), filter_all, "PNG (*.png)",
                                           "image/png", NULL, SNAPSHOT_FORMAT_PNG);
        filter_jpeg = _snapshot_add_filter (GTK_FILE_CHOOSER (dialog), filter_all, "JPEG (*.jpg)",
                                            "image/jpeg", NULL, SNAPSHOT_FORMAT_JPEG);
    }

    if (ARV_IS_BUFFER (buffer)) {
        filter_raw = _snapshot_add_filter (GTK_FILE_CHOOSER (dialog), filter_all, "Raw images (*.raw)",
                                           NULL, "*.raw", SNAPSHOT_FORMAT_RAW);
    }

    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter_all);
    switch (default_format) {
        case SNAPSHOT_FORMAT_PNG: default_filter = filter_png; break;
        case SNAPSHOT_FORMAT_JPEG: default_filter = filter_jpeg; break;
        default: default_filter = filter_raw; break;
    }
    gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), default_filter);
    g_signal_connect (dialog, "notify::filter", G_CALLBACK (_snapshot_filter_changed_cb), NULL);

    g_free (path);
    g_free (filename);

    SnapshotFormat format = SNAPSHOT_FORMAT_NONE;
    filename = NULL;
    while ((result = gtk_dialog_run (GTK_DIALOG (dialog))) == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

        // The extension typed decides the format. Without a known one, the selected filter does
        // (the default format for "Supported image formats"), and its extension is added.
        format = _snapshot_format_from_name (filename);
        if (format != SNAPSHOT_FORMAT_NONE)
            break; // the dialog already asked before overwriting this exact name

        format = _snapshot_filter_format (gtk_file_chooser_get_filter (GTK_FILE_CHOOSER (dialog)));
        if (format == SNAPSHOT_FORMAT_NONE)
            format = default_format;
        char *filename_with_extension = _snapshot_name_with_format (filename, format);
        g_free (filename);
        filename = filename_with_extension;

        // The dialog did not check the name with the added extension: ask before replacing it
        if (!g_file_test (filename, G_FILE_TEST_EXISTS) || _snapshot_confirm_overwrite (GTK_WINDOW (dialog), filename))
            break;

        // Back to the dialog, showing the full name so that the user can change it
        g_autofree char *basename = g_path_get_basename (filename);
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), basename);
        g_clear_pointer (&filename, g_free);
    }

    if (result == GTK_RESPONSE_ACCEPT) {
        g_autoptr (GError) error = NULL;
        gboolean success = FALSE;

        if (GST_IS_SAMPLE (sample) && format == SNAPSHOT_FORMAT_PNG) {
            success = _save_gst_sample_to_file (sample, filename, "image/png", NULL);
        } else if (GST_IS_SAMPLE (sample) && format == SNAPSHOT_FORMAT_JPEG) {
            success = _save_gst_sample_to_file (sample, filename, "image/jpeg", NULL);
        } else if (ARV_IS_BUFFER (buffer)) {
            success = g_file_set_contents (filename, data, size, &error);
        }

        if (success) {
            log_debug ("Snapshot saved to '%s'", filename);
            settings_set_directory_of (SETTINGS_GROUP_SNAPSHOT, filename);
            // Remember the format, without the dot ("png", "jpg" or "raw")
            settings_set_string (SETTINGS_GROUP_SNAPSHOT, SETTINGS_KEY_FORMAT, _snapshot_format_extension (format) + 1);
        } else
            arv_viewer_show_notification (viewer, "Failed to save image to file",
                                          error != NULL ? error->message : NULL);
        g_free (filename);
    }

    gtk_widget_destroy (dialog);
    g_clear_pointer (&sample, gst_sample_unref);
    g_clear_object (&buffer);
}


// CAMERA LIST MODE
gboolean select_camera_list_mode (gpointer user_data)
{
    LrdViewer *viewer = user_data;

    select_mode (viewer, TRN_VIEWER_MODE_CAMERA_LIST);
    update_device_list_cb (GTK_TOOL_BUTTON (viewer->refresh_button), viewer);

    return FALSE;
}

// Go back to list mode if camera control is lost
void control_lost_cb (ArvCamera *camera, LrdViewer *viewer)
{
    g_main_context_invoke (NULL, select_camera_list_mode, viewer);
}
void switch_to_camera_list_cb (GtkToolButton *button, LrdViewer *viewer)
{
    select_mode (viewer, TRN_VIEWER_MODE_CAMERA_LIST);
}

// update the camera list
void update_device_list_cb (GtkToolButton *button, LrdViewer *viewer)
{
    log_debug("Update device list");
    GtkListStore *list_store;
    GtkTreeIter iter;
    unsigned int n_devices;
    unsigned int i;

    gtk_widget_set_sensitive (viewer->video_mode_button, FALSE);
    gtk_widget_set_sensitive (viewer->camera_parameters, FALSE);

    g_signal_handler_block (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), viewer->camera_selected);
    list_store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (viewer->camera_tree)));
    gtk_list_store_clear (list_store);
    arv_update_device_list ();
    n_devices = arv_get_n_devices ();
    for (i = 0; i < n_devices; i++) {
        GString *protocol;

        protocol = g_string_new (NULL);
        g_string_append_printf (protocol, "aravis-%s-symbolic", arv_get_device_protocol (i));
        g_string_ascii_down (protocol);

        gtk_list_store_append (list_store, &iter);
        gtk_list_store_set (list_store, &iter,
                            0, arv_get_device_id (i),
                            1, protocol->str,
                            2, arv_get_device_vendor (i),
                            3, arv_get_device_model (i),
                            4, arv_get_device_serial_nbr (i),
                            -1);

        g_string_free (protocol, TRUE);
    }
    g_signal_handler_unblock (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), viewer->camera_selected);
}

// Manage the camera selection change: start and stop the right camera
void camera_selection_changed_cb (GtkTreeSelection *selection, LrdViewer *viewer)
{
    GtkTreeIter iter;
    GtkTreeModel *tree_model;
    char *camera_id = NULL;

    if (gtk_tree_selection_get_selected (selection, &tree_model, &iter)) {
        gtk_tree_model_get (tree_model, &iter, 0, &camera_id, -1);
        start_camera (viewer, camera_id);
        g_free (camera_id);
    } else {
        stop_camera(viewer);
    }
}

gboolean start_camera (LrdViewer *viewer, const char *camera_id)
{
    GtkTreeIter iter;
    GtkListStore *list_store;
    gint64 *pixel_formats;
    const char *pixel_format_string;
    const char **pixel_format_strings;
    guint n_pixel_formats, n_pixel_format_strings, n_valid_formats;
    gboolean binning_available;
    gboolean region_offset_available;
    gboolean bayer_tooltip = FALSE;
    gint current_format = -1;
    gint i;

    stop_camera (viewer);

    GError *error = NULL;
    viewer->camera = arv_camera_new (camera_id, &error);

    if (!ARV_IS_CAMERA (viewer->camera)) {
        const char *reason = error != NULL ? error->message : "unknown error";
        log_error ("Could not open the camera %s: %s (is it used by another program?)", camera_id, reason);
        arv_viewer_show_notification (viewer, "Could not open the camera (is it used by another program?)", reason);
        g_clear_error (&error);
        g_clear_object (&viewer->camera);
        return FALSE;
    }

    arv_device_set_register_cache_policy (arv_camera_get_device (viewer->camera),
                                          viewer->register_cache_policy);
    arv_device_set_range_check_policy (arv_camera_get_device (viewer->camera),
                                       viewer->range_check_policy);

    if (arv_camera_is_uv_device (viewer->camera)) {
        arv_camera_uv_set_usb_mode (viewer->camera, viewer->usb_mode);
        check_usbfs_memory ();
    }

    viewer->camera_name = g_strdup (camera_id);

    gtk_widget_set_sensitive (viewer->camera_parameters, TRUE);

    arv_camera_set_chunk_mode (viewer->camera, FALSE, NULL);

    update_camera_region (viewer);

    g_signal_handler_block (viewer->pixel_format_combo, viewer->pixel_format_changed);

    list_store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (viewer->pixel_format_combo)));
    gtk_list_store_clear (list_store);
    n_valid_formats = 0;
    pixel_format_strings = arv_camera_dup_available_pixel_formats_as_strings (viewer->camera, &n_pixel_format_strings, NULL);
    pixel_formats = arv_camera_dup_available_pixel_formats (viewer->camera, &n_pixel_formats, NULL);
    g_assert (n_pixel_formats == n_pixel_format_strings);
    pixel_format_string = arv_camera_get_pixel_format_as_string (viewer->camera, NULL);
    for (i = 0; i < (gint) n_pixel_formats; i++) {
        const char *caps_string = arv_pixel_format_to_gst_caps_string (pixel_formats[i]);
        gboolean valid = FALSE;

        gtk_list_store_append (list_store, &iter);

        if (caps_string != NULL && g_str_has_prefix (caps_string, "video/x-bayer") && !has_bayer2rgb) {
            bayer_tooltip = TRUE;
        } else if (caps_string != NULL) {
            if (current_format < 0 ||
                g_strcmp0 (pixel_format_strings[i], pixel_format_string) == 0)
                current_format = i;
            n_valid_formats++;
            valid = TRUE;
        }

        gtk_list_store_set (list_store, &iter,
                            0, pixel_format_strings[i],
                            1, valid,
                            -1);
    }
    g_free (pixel_formats);
    g_free (pixel_format_strings);
    gtk_widget_set_sensitive (viewer->pixel_format_combo, n_valid_formats > 0);
    gtk_widget_set_sensitive (viewer->video_mode_button, n_valid_formats > 0);

    gtk_combo_box_set_active (GTK_COMBO_BOX (viewer->pixel_format_combo), current_format >= 0 ? current_format : 0);

    gtk_widget_set_tooltip_text (GTK_WIDGET (viewer->pixel_format_combo),
                                 bayer_tooltip ?
                                 "Found bayer pixel formats, but the GStreamer bayer plugin "
                                 "is not installed." :
                                 NULL);

    region_offset_available = arv_camera_is_region_offset_available (viewer->camera, NULL);
    gtk_widget_set_sensitive (viewer->camera_x, region_offset_available);
    gtk_widget_set_sensitive (viewer->camera_y, region_offset_available);

    binning_available = arv_camera_is_binning_available (viewer->camera, NULL);
    gtk_widget_set_sensitive (viewer->camera_binning_x, binning_available);
    gtk_widget_set_sensitive (viewer->camera_binning_y, binning_available);

    g_signal_handler_unblock (viewer->pixel_format_combo, viewer->pixel_format_changed);

    g_signal_connect (arv_camera_get_device (viewer->camera), "control-lost", G_CALLBACK (control_lost_cb), viewer);

    return TRUE;
}

void stop_camera (LrdViewer *viewer)
{
    log_debug("Stopping camera.");
    gtk_widget_set_sensitive (viewer->camera_parameters, FALSE);
    gtk_widget_set_sensitive (viewer->video_mode_button, FALSE);
    stop_video (viewer);
    g_clear_object (&viewer->camera);
    g_clear_pointer (&viewer->camera_name, g_free);

    if (viewer->show_roi) {
        log_info("ROI option string: -x %d -y %d -w %d -h %d",
                 viewer->roi_x + (int) gtk_spin_button_get_value ( GTK_SPIN_BUTTON(viewer->camera_x)),
                 viewer->roi_y + (int) gtk_spin_button_get_value ( GTK_SPIN_BUTTON(viewer->camera_y)),
                 viewer->roi_w,
                 viewer->roi_h);
    }

    log_debug("Resetted ROI (stop-camera).");
    viewer->show_roi = FALSE;
    viewer->roi_x = -1;
    viewer->roi_y = -1;
    viewer->roi_w = -1;
    viewer->roi_h = -1;
}
