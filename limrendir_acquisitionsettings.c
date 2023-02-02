#include <math.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>

// ACQUISITION SETTINGS

// ACQUISITION SETTINGS

// Set all the widgets
void set_camera_control_widgets(LrdViewer *viewer)
{
    static gboolean initialization = TRUE;

    g_autofree char *string = NULL;
    gboolean is_frame_rate_available;
    double gain_min, gain_max;
    gboolean is_gain_available;
    gboolean auto_gain;
    double black_level_min, black_level_max;
    gboolean is_black_level_available;
    gboolean auto_black_level;
    gboolean is_exposure_available;
    gboolean auto_exposure;

    g_signal_handler_block (viewer->gain_hscale, viewer->gain_hscale_changed);
    g_signal_handler_block (viewer->gain_spin_button, viewer->gain_spin_changed);
    g_signal_handler_block (viewer->black_level_hscale, viewer->black_level_hscale_changed);
    g_signal_handler_block (viewer->black_level_spin_button, viewer->black_level_spin_changed);
    g_signal_handler_block (viewer->exposure_hscale, viewer->exposure_hscale_changed);
    g_signal_handler_block (viewer->exposure_spin_button, viewer->exposure_spin_changed);

    arv_camera_get_exposure_time_bounds (viewer->camera, &viewer->exposure_min, &viewer->exposure_max, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->exposure_spin_button),
                               viewer->exposure_min, viewer->exposure_max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->exposure_spin_button), 1.0, 100.0);

    arv_camera_get_gain_bounds (viewer->camera, &gain_min, &gain_max, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->gain_spin_button), gain_min, gain_max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->gain_spin_button), 1, 10);

    arv_camera_get_black_level_bounds (viewer->camera, &black_level_min, &black_level_max, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->black_level_spin_button), black_level_min, black_level_max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->black_level_spin_button), 1, 10);

    gtk_range_set_range (GTK_RANGE (viewer->exposure_hscale), 0.0, 1.0);
    gtk_range_set_range (GTK_RANGE (viewer->gain_hscale), gain_min, gain_max);
    gtk_range_set_range (GTK_RANGE (viewer->black_level_hscale), black_level_min, black_level_max);

    is_frame_rate_available = arv_camera_is_frame_rate_available (viewer->camera, NULL);
    gtk_widget_set_sensitive (viewer->frame_rate_entry, is_frame_rate_available);

    string = g_strdup_printf ("%g", arv_camera_get_frame_rate (viewer->camera, NULL));
    gtk_entry_set_text (GTK_ENTRY (viewer->frame_rate_entry), string);

    is_gain_available = arv_camera_is_gain_available (viewer->camera, NULL);
    gtk_widget_set_sensitive (viewer->gain_hscale, is_gain_available);
    gtk_widget_set_sensitive (viewer->gain_spin_button, is_gain_available);

    is_black_level_available = arv_camera_is_black_level_available (viewer->camera, NULL);
    gtk_widget_set_sensitive (viewer->black_level_hscale, is_black_level_available);
    gtk_widget_set_sensitive (viewer->black_level_spin_button, is_black_level_available);

    is_exposure_available = arv_camera_is_exposure_time_available (viewer->camera, NULL);
    gtk_widget_set_sensitive (viewer->exposure_hscale, is_exposure_available);
    gtk_widget_set_sensitive (viewer->exposure_spin_button, is_exposure_available);

    g_signal_handler_unblock (viewer->gain_hscale, viewer->gain_hscale_changed);
    g_signal_handler_unblock (viewer->gain_spin_button, viewer->gain_spin_changed);
    g_signal_handler_unblock (viewer->black_level_hscale, viewer->black_level_hscale_changed);
    g_signal_handler_unblock (viewer->black_level_spin_button, viewer->black_level_spin_changed);
    g_signal_handler_unblock (viewer->exposure_hscale, viewer->exposure_hscale_changed);
    g_signal_handler_unblock (viewer->exposure_spin_button, viewer->exposure_spin_changed);

    auto_gain = arv_camera_get_gain_auto (viewer->camera, NULL) != ARV_AUTO_OFF;
    auto_black_level = arv_camera_get_black_level_auto (viewer->camera, NULL) != ARV_AUTO_OFF;
    auto_exposure = arv_camera_get_exposure_time_auto (viewer->camera, NULL) != ARV_AUTO_OFF;

    update_gain_ui (viewer, auto_gain);
    update_black_level_ui (viewer, auto_black_level);
    update_exposure_ui (viewer, auto_exposure);

    g_signal_handler_block (viewer->auto_gain_toggle, viewer->auto_gain_clicked);
    g_signal_handler_block (viewer->auto_black_level_toggle, viewer->auto_black_level_clicked);
    g_signal_handler_block (viewer->auto_exposure_toggle, viewer->auto_exposure_clicked);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_gain_toggle), auto_gain);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_black_level_toggle), auto_black_level);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_exposure_toggle), auto_exposure);

    gtk_widget_set_sensitive (viewer->auto_gain_toggle,
                              arv_camera_is_gain_auto_available (viewer->camera, NULL));
    gtk_widget_set_sensitive (viewer->auto_black_level_toggle,
                              arv_camera_is_black_level_auto_available (viewer->camera, NULL));
    gtk_widget_set_sensitive (viewer->auto_exposure_toggle,
                              arv_camera_is_exposure_auto_available (viewer->camera, NULL));

    g_signal_handler_unblock (viewer->auto_gain_toggle, viewer->auto_gain_clicked);
    g_signal_handler_unblock (viewer->auto_black_level_toggle, viewer->auto_black_level_clicked);
    g_signal_handler_unblock (viewer->auto_exposure_toggle, viewer->auto_exposure_clicked);

    apply_max_frame_rate_if_wanted (NULL, viewer);

    // BURST
    g_signal_handler_block (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
    g_signal_handler_block (viewer->burst_nframes_spinBox, viewer->burst_nframes_changed);
    g_signal_handler_block (viewer->burst_duration_spinBox, viewer->burst_duration_changed);

    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), 1, 10000000);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), 1, 100);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), TRUE);

    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), 0.000001, 3600);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), 1, 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), FALSE);

    if (initialization) {
        // Default value for burst mode
        gtk_spin_button_set_value(GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), 100);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), 10);

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton), TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->recmode_usercontrolled_radiobutton), TRUE);
    }

    g_signal_handler_unblock (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
    g_signal_handler_unblock (viewer->burst_nframes_spinBox, viewer->burst_nframes_changed);
    g_signal_handler_unblock (viewer->burst_duration_spinBox, viewer->burst_duration_changed);

    burst_control_toggled (NULL, viewer);
    record_mode_toggled (NULL, viewer);

    initialization = FALSE;
}

// Frame rate routines

void apply_frame_rate (GtkEntry *entry, LrdViewer *viewer, gboolean grab_focus) {
    char *text;

    text = (char *) gtk_entry_get_text(entry);

    arv_camera_set_frame_rate(viewer->camera, g_strtod(text, NULL), NULL);

    double new_frame_rate = arv_camera_get_frame_rate(viewer->camera, NULL);
    text = g_strdup_printf("%g", new_frame_rate);
    gtk_entry_set_text(GTK_ENTRY(viewer->frame_rate_entry), text);

    if (grab_focus)
        gtk_widget_grab_focus(GTK_WIDGET(entry));

    g_free(text);

    burst_harmonize_nframes_and_duration(NULL, viewer);

    double max_frame_rate;
    arv_camera_get_frame_rate_bounds(viewer->camera, NULL, &max_frame_rate, NULL);
    if (max_frame_rate != new_frame_rate) {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->max_frame_rate_button), FALSE);
    }
}

void frame_rate_entry_cb (GtkEntry *entry, LrdViewer *viewer)
{
    apply_frame_rate(entry, viewer, TRUE);
}

gboolean frame_rate_entry_focus_cb (GtkEntry *entry, GdkEventFocus *event, LrdViewer *viewer)
{
    apply_frame_rate(entry, viewer, FALSE);

    return FALSE;
}

void apply_max_frame_rate_if_wanted (GtkButton *button, LrdViewer *viewer) {
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->max_frame_rate_button))) {
        log_trace("Setting frame rate to the maximum available");
        double max_frame_rate;
        arv_camera_get_frame_rate_bounds(viewer->camera, NULL, &max_frame_rate, NULL);

        arv_camera_set_frame_rate(viewer->camera, max_frame_rate, NULL);

        double actual_frame_rate = arv_camera_get_frame_rate(viewer->camera, NULL);
        char *text = g_strdup_printf("%g", actual_frame_rate);
        gtk_entry_set_text(GTK_ENTRY(viewer->frame_rate_entry), text);
        log_info("New frame rate set (maximum possible): %g Hz", actual_frame_rate);

        burst_harmonize_nframes_and_duration(NULL, viewer);
    }
}


// Mathematics routines for the sliders
double arv_viewer_value_to_log (double value, double min, double max)
{
    if (min >= max)
        return 1.0;

    if (value < min)
        return 0.0;

    return (log10 (value) - log10 (min)) / (log10 (max) - log10 (min));
}

double arv_viewer_value_from_log (double value, double min, double max)
{
    if (min <= 0.0 || max <= 0)
        return 0.0;

    if (value > 1.0)
        return max;
    if (value < 0.0)
        return min;

    return pow (10.0, (value * (log10 (max) - log10 (min)) + log10 (min)));
}


// Exposure routines
void exposure_spin_cb (GtkSpinButton *spin_button, LrdViewer *viewer)
{
    double exposure = gtk_spin_button_get_value (spin_button);
    double log_exposure = arv_viewer_value_to_log (exposure, viewer->exposure_min, viewer->exposure_max);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_exposure_toggle), FALSE);

    arv_camera_set_exposure_time (viewer->camera, exposure, NULL);

    g_signal_handler_block (viewer->exposure_hscale, viewer->exposure_hscale_changed);
    gtk_range_set_value (GTK_RANGE (viewer->exposure_hscale), log_exposure);
    gtk_widget_grab_focus (GTK_WIDGET (spin_button));
    g_signal_handler_unblock (viewer->exposure_hscale, viewer->exposure_hscale_changed);
}

void exposure_scale_cb (GtkRange *range, LrdViewer *viewer)
{
    double log_exposure = gtk_range_get_value (range);
    double exposure = arv_viewer_value_from_log (log_exposure, viewer->exposure_min, viewer->exposure_max);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_exposure_toggle), FALSE);

    arv_camera_set_exposure_time (viewer->camera, exposure, NULL);

    g_signal_handler_block (viewer->exposure_spin_button, viewer->exposure_spin_changed);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->exposure_spin_button), exposure);
    g_signal_handler_unblock (viewer->exposure_spin_button, viewer->exposure_spin_changed);
}

gboolean update_exposure_cb (void *data)
{
    log_trace("Updating the exposure spinBox and slider.");
    LrdViewer *viewer = data;
    double exposure;
    double log_exposure;

    exposure = arv_camera_get_exposure_time (viewer->camera, NULL);
    log_exposure = arv_viewer_value_to_log (exposure, viewer->exposure_min, viewer->exposure_max);

    g_signal_handler_block (viewer->exposure_hscale, viewer->exposure_hscale_changed);
    g_signal_handler_block (viewer->exposure_spin_button, viewer->exposure_spin_changed);
    gtk_range_set_value (GTK_RANGE (viewer->exposure_hscale), log_exposure);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->exposure_spin_button), exposure);
    g_signal_handler_unblock (viewer->exposure_spin_button, viewer->exposure_spin_changed);
    g_signal_handler_unblock (viewer->exposure_hscale, viewer->exposure_hscale_changed);

    return TRUE;
}

void update_exposure_ui (LrdViewer *viewer, gboolean is_auto)
{
    update_exposure_cb (viewer);

    if (viewer->exposure_update_event > 0) {
        g_source_remove (viewer->exposure_update_event);
        viewer->exposure_update_event = 0;
    }

    if (is_auto)
        viewer->exposure_update_event = g_timeout_add_seconds (1, update_exposure_cb, viewer);
}

void auto_exposure_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    gboolean is_auto;

    is_auto = gtk_toggle_button_get_active (toggle);

    arv_camera_set_exposure_time_auto (viewer->camera, is_auto ? ARV_AUTO_CONTINUOUS : ARV_AUTO_OFF, NULL);
    update_exposure_ui (viewer, is_auto);
}


// Gain routines
void gain_spin_cb (GtkSpinButton *spin_button, LrdViewer *viewer)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_gain_toggle), FALSE);

    arv_camera_set_gain (viewer->camera, gtk_spin_button_get_value (spin_button), NULL);

    g_signal_handler_block (viewer->gain_hscale, viewer->gain_hscale_changed);
    gtk_range_set_value (GTK_RANGE (viewer->gain_hscale), gtk_spin_button_get_value (spin_button));
    gtk_widget_grab_focus (GTK_WIDGET (spin_button));
    g_signal_handler_unblock (viewer->gain_hscale, viewer->gain_hscale_changed);
}

void gain_scale_cb (GtkRange *range, LrdViewer *viewer)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_gain_toggle), FALSE);

    arv_camera_set_gain (viewer->camera, gtk_range_get_value (range), NULL);

    g_signal_handler_block (viewer->gain_spin_button, viewer->gain_spin_changed);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->gain_spin_button), gtk_range_get_value (range));
    g_signal_handler_unblock (viewer->gain_spin_button, viewer->gain_spin_changed);
}

gboolean update_gain_cb (void *data)
{
    LrdViewer *viewer = data;
    double gain;

    gain = arv_camera_get_gain (viewer->camera, NULL);

    g_signal_handler_block (viewer->gain_hscale, viewer->gain_hscale_changed);
    g_signal_handler_block (viewer->gain_spin_button, viewer->gain_spin_changed);
    gtk_range_set_value (GTK_RANGE (viewer->gain_hscale), gain);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->gain_spin_button), gain);
    g_signal_handler_unblock (viewer->gain_spin_button, viewer->gain_spin_changed);
    g_signal_handler_unblock (viewer->gain_hscale, viewer->gain_hscale_changed);

    return TRUE;
}

void update_gain_ui (LrdViewer *viewer, gboolean is_auto)
{
    update_gain_cb (viewer);

    if (viewer->gain_update_event > 0) {
        g_source_remove (viewer->gain_update_event);
        viewer->gain_update_event = 0;
    }

    if (is_auto)
        viewer->gain_update_event = g_timeout_add_seconds (1, update_gain_cb, viewer);

}

void auto_gain_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    gboolean is_auto;

    is_auto = gtk_toggle_button_get_active (toggle);

    arv_camera_set_gain_auto (viewer->camera, is_auto ? ARV_AUTO_CONTINUOUS : ARV_AUTO_OFF, NULL);
    update_gain_ui (viewer, is_auto);
}


// Black level routines
void black_level_spin_cb (GtkSpinButton *spin_button, LrdViewer *viewer)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_black_level_toggle), FALSE);

    arv_camera_set_black_level (viewer->camera, gtk_spin_button_get_value (spin_button), NULL);

    g_signal_handler_block (viewer->black_level_hscale, viewer->black_level_hscale_changed);
    gtk_range_set_value (GTK_RANGE (viewer->black_level_hscale), gtk_spin_button_get_value (spin_button));
    gtk_widget_grab_focus (GTK_WIDGET (spin_button));
    g_signal_handler_unblock (viewer->black_level_hscale, viewer->black_level_hscale_changed);
}

void black_level_scale_cb (GtkRange *range, LrdViewer *viewer)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->auto_black_level_toggle), FALSE);

    arv_camera_set_black_level (viewer->camera, gtk_range_get_value (range), NULL);

    g_signal_handler_block (viewer->black_level_spin_button, viewer->black_level_spin_changed);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->black_level_spin_button), gtk_range_get_value (range));
    g_signal_handler_unblock (viewer->black_level_spin_button, viewer->black_level_spin_changed);
}

gboolean update_black_level_cb (void *data)
{
    LrdViewer *viewer = data;
    double black_level;

    black_level = arv_camera_get_black_level (viewer->camera, NULL);

    g_signal_handler_block (viewer->black_level_hscale, viewer->black_level_hscale_changed);
    g_signal_handler_block (viewer->black_level_spin_button, viewer->black_level_spin_changed);
    gtk_range_set_value (GTK_RANGE (viewer->black_level_hscale), black_level);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->black_level_spin_button), black_level);
    g_signal_handler_unblock (viewer->black_level_spin_button, viewer->black_level_spin_changed);
    g_signal_handler_unblock (viewer->black_level_hscale, viewer->black_level_hscale_changed);

    return TRUE;
}

void update_black_level_ui (LrdViewer *viewer, gboolean is_auto)
{
    update_black_level_cb (viewer);

    if (viewer->black_level_update_event > 0) {
        g_source_remove (viewer->black_level_update_event);
        viewer->black_level_update_event = 0;
    }

    if (is_auto)
        viewer->black_level_update_event = g_timeout_add_seconds (1, update_black_level_cb, viewer);

}

void auto_black_level_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    gboolean is_auto;

    is_auto = gtk_toggle_button_get_active (toggle);

    arv_camera_set_black_level_auto (viewer->camera, is_auto ? ARV_AUTO_CONTINUOUS : ARV_AUTO_OFF, NULL);
    update_black_level_ui (viewer, is_auto);
}



// Best exposure selection
void initialize_best_exposure_search (LrdViewer *viewer) {
    log_info("Searching for the best exposure time...");
    stop_key_listener(viewer);
    g_signal_handler_block (viewer->stream, viewer->new_buffer_available_video);
    gtk_widget_set_sensitive (viewer->acquisition_button, FALSE);
    gtk_widget_set_sensitive (viewer->back_button, FALSE);
    gtk_widget_set_sensitive (viewer->snapshot_button, FALSE);
    gtk_widget_set_sensitive (viewer->rotate_cw_button, FALSE);
    gtk_widget_set_sensitive (viewer->flip_vertical_toggle, FALSE);
    gtk_widget_set_sensitive (viewer->flip_horizontal_toggle, FALSE);
    gtk_widget_set_sensitive (viewer->record_button, FALSE);
    gtk_widget_set_sensitive (viewer->help_button, FALSE);
    update_exposure_cb(viewer);
    log_trace("Starting best exposure search.");
}

void finalize_best_exposure_search (LrdViewer *viewer) {
    log_trace("Ending best exposure search.");
    update_exposure_cb(viewer);
    log_info("Best exposure time: %f us", arv_camera_get_exposure_time(viewer->camera, NULL));
    g_signal_handler_unblock(viewer->stream, viewer->new_buffer_available_video);
    gtk_widget_set_sensitive(viewer->acquisition_button, TRUE);
    gtk_widget_set_sensitive(viewer->back_button, TRUE);
    gtk_widget_set_sensitive(viewer->snapshot_button, TRUE);
    gtk_widget_set_sensitive(viewer->rotate_cw_button, TRUE);
    gtk_widget_set_sensitive(viewer->flip_vertical_toggle, TRUE);
    gtk_widget_set_sensitive(viewer->flip_horizontal_toggle, TRUE);
    gtk_widget_set_sensitive (viewer->record_button, TRUE);
    gtk_widget_set_sensitive (viewer->help_button, TRUE);
    start_key_listener(viewer);
}

uint8_t get_max_px_value(LrdViewer *viewer, int n_frames, int n_frames_to_skip) {
    ArvStream *stream = viewer->stream;
    uint8_t max_pixel_value = 0;

    ArvBuffer *buffer;
    int i_frame;
    log_trace("Getting max pixel value");

    // skipping frames
    log_trace("\tSkipping frames");
    i_frame = 0;
    while (i_frame < n_frames_to_skip) {
        // take a buffer
        buffer = arv_stream_pop_buffer(stream);

        // Check if the buffer was successful
        if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            log_trace("\t\tBuffer unsuccessful...");
        } else {
            i_frame++;
            log_trace("\t\tSkipped %d frame(s) out of %d", i_frame, n_frames_to_skip);
        }

        arv_stream_push_buffer(stream, buffer);
    }

    // measuring frames
    log_trace("\tTaking frames");
    i_frame = 0;
    while (i_frame < n_frames_to_skip) {
        // take a buffer
        buffer = arv_stream_pop_buffer(stream);

        // Check if the buffer was successful
        if (arv_buffer_get_status(buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            log_trace("\t\tBuffer unsuccessful...");
        } else {
            // Get the data
            uint8_t *image_data;
            size_t size;

            image_data = (uint8_t *) arv_buffer_get_data(buffer, &size);
            for (int i = 0; i < size; i++) {
                if (image_data[i] > max_pixel_value)
                    max_pixel_value = image_data[i];
            }
            i_frame++;
            log_trace("\t\tTaken %d frame(s) out of %d", i_frame, n_frames_to_skip);
        }
        arv_stream_push_buffer(stream, buffer);
    }

    log_debug("Exposure time: %f us, max pixel value: %hhu (%d images skipped then %d images taken)", arv_camera_get_exposure_time (viewer->camera, NULL), max_pixel_value, n_frames_to_skip, n_frames);
    return max_pixel_value;
}

// The non blocking version (multi-threaded so not blocking)
static int n_frames_nb;
static int n_frames_to_skip_nb;
static int phase_nb;
static double min_exposure_nb, max_exposure_nb, current_exposure_nb;

gboolean best_exposure_search_next_step(void *data)
{
    LrdViewer *viewer = data;

    uint8_t max_pixel_value = get_max_px_value(viewer, n_frames_nb, n_frames_to_skip_nb);

    // Display
    log_trace("Display 1 buffer");
    ArvBuffer *buffer = arv_stream_pop_buffer(viewer->stream);
    // Check if the buffer was successful
    if (arv_buffer_get_status(buffer) == ARV_BUFFER_STATUS_SUCCESS) {
        g_clear_object(&viewer->last_buffer);
        viewer->last_buffer = g_object_ref(buffer);
        gst_app_src_push_buffer(GST_APP_SRC (viewer->appsrc), arv_to_gst_buffer(buffer, viewer->stream, viewer));
    } else {
        arv_stream_push_buffer(viewer->stream, buffer);
    }

    // Next step
    switch (phase_nb) {
        case 0:
            if (max_pixel_value >= 255) {
                // If, even at min exposure, we cannot have an image with all pixels under 255: we quit
                finalize_best_exposure_search(viewer);
                return FALSE;
            } else {
                // On passe à la phase 1
                phase_nb = 1;
                log_debug("Determination du max par increment logarithmique.");
                min_exposure_nb = current_exposure_nb;
                current_exposure_nb = current_exposure_nb * 2;
                if (current_exposure_nb > max_exposure_nb)
                    current_exposure_nb = max_exposure_nb;
            }
            break;
        case 1:
            if (max_pixel_value >= 255) {
                // On passe à la phase 2
                max_exposure_nb = current_exposure_nb + 10;
                current_exposure_nb = (max_exposure_nb + min_exposure_nb) / 2;
                phase_nb = 2;
                log_debug("Finding the best exposure time by dichotomy.");
            } else if (current_exposure_nb >= max_exposure_nb) {
                // If, even at max exposure, we cannot have a 255 pixel: we quit
                log_debug("Maximum exposition reached.");
                finalize_best_exposure_search(viewer);
                return FALSE;
            } else {
                // Augment exposure time
                log_trace("Doubling exposure time");
                min_exposure_nb = current_exposure_nb - 10;
                current_exposure_nb = current_exposure_nb * 2;
                if (current_exposure_nb > max_exposure_nb)
                    current_exposure_nb = max_exposure_nb;
            }
            break;
        case 2:
            if (max_exposure_nb - min_exposure_nb < 1) {
                // We reached a fine spot
                arv_camera_set_exposure_time (viewer->camera, (int) current_exposure_nb + 1, NULL);
                log_debug("Final exposure time: %f us", arv_camera_get_exposure_time(viewer->camera, NULL));

                finalize_best_exposure_search(viewer);
                return FALSE;
            } else {
                if (max_pixel_value < 255) {
                    min_exposure_nb = current_exposure_nb;
                } else {
                    max_exposure_nb = current_exposure_nb;
                }
                current_exposure_nb = (max_exposure_nb + min_exposure_nb) / 2;
                log_trace("Trying next exposure time: %f", current_exposure_nb);
            }
            break;
        default:
            break;
    }

    arv_camera_set_exposure_time (viewer->camera, current_exposure_nb, NULL);

    return TRUE;
}

void best_exposure_search_nonblocking (LrdViewer *viewer) {
    // initialize
    initialize_best_exposure_search(viewer);
    n_frames_nb = 20;
    n_frames_to_skip_nb = 5;
    arv_camera_get_exposure_time_bounds (viewer->camera, &min_exposure_nb, &max_exposure_nb, NULL);
    log_trace("Minimum exposure possible: %d us", min_exposure_nb);
    log_trace("Maximum exposure possible: %d us", max_exposure_nb);

    phase_nb = 1;
    current_exposure_nb = arv_camera_get_exposure_time (viewer->camera, NULL);
    log_trace("Current exposure: %d us", current_exposure_nb);

    // start the shit
    double frame_rate = arv_camera_get_frame_rate (viewer->camera, NULL);
    double time_per_frame = 1000 / frame_rate;
    guint time_per_step = (uint) ((n_frames_nb + n_frames_to_skip_nb) * time_per_frame) + 50;

    log_trace("Current frame rate: %f FPS (%f mSPF)", frame_rate, time_per_frame);
    log_trace("Number of steps: %d to skip, %d to measure", n_frames_to_skip_nb, n_frames_nb);

    log_debug("Time per step %u ms", time_per_step);

    g_timeout_add (time_per_step, best_exposure_search_next_step, viewer);
}

// RECORD MODE SETTINGS

void record_mode_toggled (GtkToggleButton *button, LrdViewer *viewer)
{
    gtk_widget_set_sensitive (viewer->burst_duration_radiobutton, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_duration_spinBox, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_nframes_radiobutton, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_nframes_spinBox, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton))) {
        burst_control_toggled(NULL, viewer);
    }
}

void burst_control_toggled (GtkToggleButton *button, LrdViewer *viewer)
{
    gtk_widget_set_sensitive (viewer->burst_duration_spinBox, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->burst_duration_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_nframes_spinBox, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton)));

    burst_harmonize_nframes_and_duration (NULL, viewer);
}

void burst_harmonize_nframes_and_duration (GtkSpinButton *spin_button, LrdViewer *viewer)
{
    g_signal_handler_block (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
    g_signal_handler_block (viewer->burst_nframes_spinBox, viewer->burst_nframes_changed);
    g_signal_handler_block (viewer->burst_duration_spinBox, viewer->burst_duration_changed);

    double frame_rate = arv_camera_get_frame_rate (viewer->camera, NULL);

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton))) {
        // set time
        gint number_of_frames = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(viewer->burst_nframes_spinBox));
        gdouble new_duration = ((double) number_of_frames) / frame_rate;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(viewer->burst_duration_spinBox), new_duration);
    } else {
        // set frames
        gdouble wanted_duration = gtk_spin_button_get_value(GTK_SPIN_BUTTON(viewer->burst_duration_spinBox));
        gdouble new_nframes = ceil(wanted_duration * frame_rate);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(viewer->burst_nframes_spinBox), new_nframes);
        // set time to have a valid time
        gint number_of_frames = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(viewer->burst_nframes_spinBox));
        gdouble new_duration = ((double) number_of_frames) / frame_rate;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(viewer->burst_duration_spinBox), new_duration);
    }

    g_signal_handler_unblock (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
    g_signal_handler_unblock (viewer->burst_nframes_spinBox, viewer->burst_nframes_changed);
    g_signal_handler_unblock (viewer->burst_duration_spinBox, viewer->burst_duration_changed);
}
