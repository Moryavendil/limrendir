#include <gtk/gtk.h>
#include <gst/gst.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>

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

// VIDEO MODE
void switch_to_video_mode_cb (GtkToolButton *button, LrdViewer *viewer)
{
    camera_region_cb(NULL, viewer);
    select_mode (viewer, TRN_VIEWER_MODE_VIDEO);
}

// Image transforms
void update_transform (LrdViewer *viewer)
{
    static const gint methods[4][4] = {
            {0, 1, 2, 3},
            {4, 6, 5, 7},
            {5, 7, 4, 6},
            {2, 3, 0, 1}
    };
    int index = (viewer->flip_horizontal ? 1 : 0) + (viewer->flip_vertical ? 2 : 0);

    g_object_set (viewer->transform, "method", methods[index][viewer->rotation % 4], NULL);
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

void snapshot_cb (GtkButton *button, LrdViewer *viewer)
{
    GtkFileFilter *filter;
    GtkFileFilter *filter_all;
    GtkWidget *dialog;
    GstSample *sample = NULL;
    ArvBuffer *buffer = NULL;
    char *path;
    char *filename;
    GDateTime *date;
    char *date_string;
    int width, height;
    const char *data;
    const char *pixel_format;
    size_t size;
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
    data = arv_buffer_get_data (buffer, &size);

    date = g_date_time_new_now_local ();
    date_string = g_date_time_format (date, "%Y-%m-%d-%H:%M:%S");
    filename = g_strdup_printf ("%s-%s-%d-%d-%s-%s.raw",
                                arv_camera_get_vendor_name (viewer->camera, NULL),
                                arv_camera_get_device_serial_number (viewer->camera, NULL),
                                width,
                                height,
                                pixel_format != NULL ? pixel_format : "Unknown",
                                date_string);
    g_free (date_string);
    g_date_time_unref (date);

    path = g_build_filename (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES), filename, NULL);

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
        filter = gtk_file_filter_new ();
        gtk_file_filter_add_mime_type (filter, "image/png");
        gtk_file_filter_add_mime_type (filter_all, "image/png");
        gtk_file_filter_set_name (filter, "PNG (*.png)");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

        filter = gtk_file_filter_new ();
        gtk_file_filter_add_mime_type (filter, "image/jpeg");
        gtk_file_filter_add_mime_type (filter_all, "image/jpeg");
        gtk_file_filter_set_name (filter, "JPEG (*.jpeg)");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
    }

    if (ARV_IS_BUFFER (buffer)) {
        filter = gtk_file_filter_new ();
        gtk_file_filter_add_pattern (filter, "*.raw");
        gtk_file_filter_add_pattern (filter_all, "*.raw");
        gtk_file_filter_set_name (filter, "Raw images (*.raw)");
        gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
    }

    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter_all);
    gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter_all);

    g_free (path);
    g_free (filename);

    result = gtk_dialog_run (GTK_DIALOG (dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        g_autoptr (GError) error = NULL;
        g_autofree char * content_type = NULL;
        gboolean success = FALSE;

        filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

        content_type = g_content_type_guess (filename, NULL, 0, NULL);

        if (GST_IS_SAMPLE (sample) && g_content_type_is_mime_type (content_type, "image/png")) {
            success = _save_gst_sample_to_file (sample, filename, "image/png", NULL);
        } else if (GST_IS_SAMPLE (sample) && g_content_type_is_mime_type (content_type, "image/jpeg")) {
            success = _save_gst_sample_to_file (sample, filename, "image/jpeg", NULL);
        } else if (ARV_IS_BUFFER (buffer)) {
            success = g_file_set_contents (filename, data, size, &error);
            g_free (filename);
        }

        if (!success)
            arv_viewer_show_notification (viewer, "Failed to save image to file",
                                          error != NULL ? error->message : NULL);
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

    viewer->camera = arv_camera_new (camera_id, NULL);

    if (!ARV_IS_CAMERA (viewer->camera))
        return FALSE;

    arv_device_set_register_cache_policy (arv_camera_get_device (viewer->camera),
                                          viewer->register_cache_policy);
    arv_device_set_range_check_policy (arv_camera_get_device (viewer->camera),
                                       viewer->range_check_policy);

    if (arv_camera_is_uv_device (viewer->camera))
        arv_camera_uv_set_usb_mode (viewer->camera, viewer->usb_mode);

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
    for (i = 0; i < n_pixel_formats; i++) {
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
