#include <math.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>


// CAMERA SETTINGS

// Set all the widgets
void set_camera_control_widgets(LrdViewer *viewer)
{

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

    string = g_strdup_printf ("%g", get_actual_frame_rate (viewer));
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

//    static gboolean initialization = TRUE;
//    // BURST
//    g_signal_handler_block (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
//    g_signal_handler_block (viewer->burst_nframes_spinBox, viewer->burst_nframes_changed);
//    g_signal_handler_block (viewer->burst_duration_spinBox, viewer->burst_duration_changed);
//
//    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), 1, 10000000);
//    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), 1, 100);
//    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), TRUE);
//
//    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), 0.000001, 3600);
//    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), 1, 10);
//    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), FALSE);
//
//    if (initialization) {
//        // Default value for burst mode
//        gtk_spin_button_set_value(GTK_SPIN_BUTTON (viewer->burst_nframes_spinBox), 100);
//        gtk_spin_button_set_value(GTK_SPIN_BUTTON (viewer->burst_duration_spinBox), 10);
//
//        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton), TRUE);
//        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->recmode_usercontrolled_radiobutton), TRUE);
//    }
//
//    g_signal_handler_unblock (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
//    g_signal_handler_unblock (viewer->burst_nframes_spinBox, viewer->burst_nframes_changed);
//    g_signal_handler_unblock (viewer->burst_duration_spinBox, viewer->burst_duration_changed);
//
//    burst_control_toggled (NULL, viewer);
//    record_mode_toggled (NULL, viewer);
//
//    initialization = FALSE;
}

// Frame rate routines

// Returns the frame rate the camera actually delivers.
// AcquisitionFrameRate (what arv_camera_get/set_frame_rate use) is only the requested rate: on many cameras
// (Basler in particular) it is an upper limit, and the real rate is further limited by the exposure time,
// the region, the pixel format and the bandwidth. When the camera tells us the resulting rate, use it.
double get_actual_frame_rate (LrdViewer *viewer) {
    static const char *resulting_frame_rate_features[] = {"ResultingFrameRate", "ResultingFrameRateAbs"};

    if (!ARV_IS_CAMERA (viewer->camera))
        return 0.;

    for (unsigned i = 0; i < G_N_ELEMENTS (resulting_frame_rate_features); i++) {
        const char *feature = resulting_frame_rate_features[i];
        if (arv_camera_is_feature_available (viewer->camera, feature, NULL)) {
            GError *error = NULL;
            double frame_rate = arv_camera_get_float (viewer->camera, feature, &error);
            if (error == NULL)
                return frame_rate;
            log_warning("Could not read %s: %s", feature, error->message);
            g_clear_error (&error);
        }
    }

    // No resulting frame rate available: the requested one is the best guess we have
    return arv_camera_get_frame_rate (viewer->camera, NULL);
}

// Shows the actual frame rate in the entry, and updates what depends on it.
// To be called whenever something that can change the actual frame rate was modified.
void update_frame_rate_display (LrdViewer *viewer) {
    char *text = g_strdup_printf ("%g", get_actual_frame_rate (viewer));
    gtk_entry_set_text (GTK_ENTRY (viewer->frame_rate_entry), text);
    g_free (text);

    burst_harmonize_nframes_and_duration (NULL, viewer);
}

// Requests a frame rate from the camera and returns the frame rate actually obtained.
double impose_frame_rate (LrdViewer *viewer, double new_frame_rate) {
    log_trace("Trying to impose the frame rate set : %g Hz", new_frame_rate);

    GError *error = NULL;
    arv_camera_set_frame_rate (viewer->camera, new_frame_rate, &error);
    if (error != NULL) {
        log_warning("Could not set the frame rate to %g Hz: %s", new_frame_rate, error->message);
        g_clear_error (&error);
    }

    update_frame_rate_display (viewer);

    double actual_frame_rate = get_actual_frame_rate (viewer);
    log_debug("New frame rate set : %g Hz (requested: %g Hz)", actual_frame_rate, new_frame_rate);

    // Warn when a specific rate was asked for and the camera cannot deliver it
    // (requesting the maximum allowed value is the way to ask for 'as fast as possible', so no warning then).
    // 1e-3 relative tolerance: the camera rounds the rate to what its timings allow.
    double max_frame_rate;
    arv_camera_get_frame_rate_bounds (viewer->camera, NULL, &max_frame_rate, NULL);
    if (new_frame_rate < max_frame_rate && actual_frame_rate < new_frame_rate * (1. - 1e-3))
        log_warning("The camera cannot reach %g Hz with the current settings (exposure time, region, "
                    "pixel format...): it runs at %g Hz.", new_frame_rate, actual_frame_rate);

    return actual_frame_rate;
}

void apply_frame_rate (GtkEntry *entry, LrdViewer *viewer, gboolean grab_focus) {
    const char *text = gtk_entry_get_text (entry);

    // The entry shows the actual frame rate. If it was not edited, there is nothing to do
    // (re-applying it would e.g. replace a 'maximum' request by the current, exposure-dependent rate).
    char *displayed = g_strdup_printf ("%g", get_actual_frame_rate (viewer));
    gboolean unchanged = g_str_equal (text, displayed);
    g_free (displayed);

    if (!unchanged) {
        impose_frame_rate (viewer, g_strtod (text, NULL));

        // A frame rate was set by hand: we are no longer in 'maximum frame rate' mode
        double max_frame_rate;
        arv_camera_get_frame_rate_bounds (viewer->camera, NULL, &max_frame_rate, NULL);
        if (arv_camera_get_frame_rate (viewer->camera, NULL) != max_frame_rate) {
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->max_frame_rate_button), FALSE);
        }
    }

    if (grab_focus)
        gtk_widget_grab_focus (GTK_WIDGET (entry));
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

        // Request the highest rate the camera accepts; the camera then runs as fast as its settings allow
        double max_frame_rate;
        arv_camera_get_frame_rate_bounds(viewer->camera, NULL, &max_frame_rate, NULL);

        double actual_frame_rate = impose_frame_rate(viewer, max_frame_rate);

        log_info("New frame rate set (maximum possible): %g Hz", actual_frame_rate);
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
    update_frame_rate_display (viewer);

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
    update_frame_rate_display (viewer);

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

    // The exposure time (e.g. in auto mode) limits the actual frame rate
    update_frame_rate_display (viewer);

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
//
// Finds the longest exposure time for which the image is not saturated, by a binary search on a logarithmic scale
// between the shortest exposure time of the camera and the longest one compatible with the current frame rate
// (1 / frame rate), with a precision of 1 %.
// Each measurement uses all the frames received during 100 ms: the image is saturated if the average, over these
// frames, of the maximum pixel value of each frame reaches 255 (8-bit pixels).

static const double search_measurement_duration_s = 0.1;
static const double search_precision = 0.01;
// Number of frames dropped after an exposure change (they may have been exposed with the previous exposure time)
static const int search_frames_to_skip = 2;

// Current bounds of the search: lo is not saturated, hi is saturated (once tested; before, they are the limits)
static double lo_exposure_bs, hi_exposure_bs;
static gboolean lo_tested_bs, hi_tested_bs;
static int n_steps_bs;
static double initial_exposure_bs;
static gint64 search_start_time_bs;
// Requested frame rate before the search (restored at the end; e.g. the maximum value in 'max frame rate' mode)
static double initial_frame_rate;

void initialize_best_exposure_search (LrdViewer *viewer) {
    log_info("Searching for the best exposure time...");
    stop_key_listener(viewer);
    g_signal_handler_block (viewer->stream, viewer->new_buffer_available_video);
    set_video_controls_sensitive(viewer, FALSE);
    gtk_widget_set_sensitive(viewer->record_button, FALSE);
    gtk_widget_set_sensitive(viewer->help_button, FALSE);
    update_exposure_cb(viewer);
    log_trace("Starting best exposure search.");
}

void finalize_best_exposure_search (LrdViewer *viewer) {
    log_trace("Ending best exposure search.");
    update_exposure_cb(viewer);
    log_info("Best exposure time: %.1f us (%d measurements, %.1f s)", arv_camera_get_exposure_time(viewer->camera, NULL),
             n_steps_bs, (double) (g_get_monotonic_time() - search_start_time_bs) / G_USEC_PER_SEC);
    g_signal_handler_unblock(viewer->stream, viewer->new_buffer_available_video);
    set_video_controls_sensitive(viewer, TRUE);
    gtk_widget_set_sensitive(viewer->record_button, TRUE);
    gtk_widget_set_sensitive(viewer->help_button, TRUE);
    start_key_listener(viewer);
    impose_frame_rate(viewer, initial_frame_rate);
    apply_max_frame_rate_if_wanted(NULL, viewer);
}

// Waits for the next frame during the search. Returns NULL if the camera sends nothing for too long
// (1 s plus two exposure times plus two frame periods), so that a stalled camera cannot hang the application.
static ArvBuffer *pop_search_buffer (LrdViewer *viewer) {
    double exposure_us = arv_camera_get_exposure_time (viewer->camera, NULL);
    double frame_rate = get_actual_frame_rate (viewer);
    double timeout_us = G_USEC_PER_SEC + 2 * exposure_us;
    if (frame_rate > 0)
        timeout_us += 2. / frame_rate * G_USEC_PER_SEC;
    return arv_stream_timeout_pop_buffer (viewer->stream, (guint64) timeout_us);
}

// Stops the search when the camera does not send frames anymore, and puts the exposure time back
static void abort_best_exposure_search (LrdViewer *viewer) {
    log_warning("No frame received from the camera: best exposure search aborted, exposure time restored.");
    arv_camera_set_exposure_time (viewer->camera, initial_exposure_bs, NULL);
    finalize_best_exposure_search (viewer);
}

// Maximum pixel value of a frame (8-bit pixels)
static uint8_t frame_max_pixel_value (ArvBuffer *buffer) {
    size_t size;
    const uint8_t *image_data = arv_buffer_get_data (buffer, &size);
    uint8_t max_pixel_value = 0;
    for (size_t i = 0; i < size; i++) {
        if (image_data[i] > max_pixel_value)
            max_pixel_value = image_data[i];
    }
    return max_pixel_value;
}

// Measures, with the current exposure time, the average over 100 ms of frames of the maximum pixel value of each
// frame. The last frame is displayed. Returns -1 if the camera stalls.
double measure_mean_max_pixel_value (LrdViewer *viewer) {
    ArvBuffer *buffer;

    // Frames already waiting may have been exposed with the previous exposure time, as well as the next ones
    while ((buffer = arv_stream_try_pop_buffer (viewer->stream)) != NULL)
        arv_stream_push_buffer (viewer->stream, buffer);
    for (int n_skipped = 0; n_skipped < search_frames_to_skip; ) {
        buffer = pop_search_buffer (viewer);
        if (buffer == NULL)
            return -1;
        if (arv_buffer_get_status (buffer) == ARV_BUFFER_STATUS_SUCCESS)
            n_skipped++;
        arv_stream_push_buffer (viewer->stream, buffer);
    }

    // Measurement: all the (successful) frames received during 100 ms, at least one
    ArvBuffer *last_frame = NULL;
    double sum_of_max = 0;
    int n_frames = 0;
    gint64 const start = g_get_monotonic_time ();
    gint64 const duration_us = (gint64) (search_measurement_duration_s * G_USEC_PER_SEC);
    while (n_frames == 0 || g_get_monotonic_time () - start < duration_us) {
        buffer = pop_search_buffer (viewer);
        // No frame for too long, or only failed frames for 5 s: the camera does not work
        if (buffer == NULL || (n_frames == 0 && g_get_monotonic_time () - start > 5 * G_USEC_PER_SEC)) {
            if (buffer != NULL)
                arv_stream_push_buffer (viewer->stream, buffer);
            if (last_frame != NULL)
                arv_stream_push_buffer (viewer->stream, last_frame);
            return -1;
        }
        if (arv_buffer_get_status (buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            arv_stream_push_buffer (viewer->stream, buffer);
            continue;
        }
        sum_of_max += frame_max_pixel_value (buffer);
        n_frames++;
        if (last_frame != NULL)
            arv_stream_push_buffer (viewer->stream, last_frame);
        last_frame = buffer;
    }

    // Display the last frame (the display gives the buffer back to the stream when done), unless the display
    // holds too many buffers
    if (can_display_buffer (viewer, viewer->stream)) {
        g_clear_object (&viewer->last_buffer);
        viewer->last_buffer = g_object_ref (last_frame);
        gst_app_src_push_buffer (GST_APP_SRC (viewer->appsrc), arv_to_gst_buffer (last_frame, viewer->stream, viewer));
    } else {
        arv_stream_push_buffer (viewer->stream, last_frame);
    }

    log_trace("\t%d frames measured", n_frames);
    return sum_of_max / n_frames;
}

// Sets the exposure time and returns the one actually applied by the camera (rounded to its increment)
static double set_search_exposure (LrdViewer *viewer, double exposure_us) {
    arv_camera_set_exposure_time (viewer->camera, exposure_us, NULL);
    return arv_camera_get_exposure_time (viewer->camera, NULL);
}

// Ends the search with the longest exposure time found not to saturate
static void conclude_best_exposure_search (LrdViewer *viewer) {
    set_search_exposure (viewer, lo_exposure_bs);
    if (!lo_tested_bs)
        log_warning("The image is saturated even with the shortest exposure times: using the shortest one.");
    if (!hi_tested_bs)
        log_info("The image is not saturated even with the longest exposure time allowed by the frame rate (%.1f us).",
                 hi_exposure_bs);
    finalize_best_exposure_search (viewer);
}

gboolean best_exposure_search_next_step(void *data)
{
    LrdViewer *viewer = data;

    // Next exposure time: the middle of the bounds, on a logarithmic scale
    double exposure = set_search_exposure (viewer, sqrt (lo_exposure_bs * hi_exposure_bs));
    // The camera cannot be more precise than this: the search is over
    if (exposure <= lo_exposure_bs || exposure >= hi_exposure_bs) {
        conclude_best_exposure_search (viewer);
        return FALSE;
    }

    double mean_max_pixel_value = measure_mean_max_pixel_value (viewer);
    if (mean_max_pixel_value < 0) {
        abort_best_exposure_search (viewer);
        return FALSE;
    }
    n_steps_bs++;

    gboolean const saturated = mean_max_pixel_value >= 254.5;
    log_debug("Exposure %.1f us: mean max pixel value %.1f (%s)", exposure, mean_max_pixel_value,
              saturated ? "saturated" : "not saturated");
    if (saturated) {
        hi_exposure_bs = exposure;
        hi_tested_bs = TRUE;
    } else {
        lo_exposure_bs = exposure;
        lo_tested_bs = TRUE;
    }

    if (hi_exposure_bs / lo_exposure_bs < 1 + search_precision) {
        conclude_best_exposure_search (viewer);
        return FALSE;
    }
    return TRUE;
}

void best_exposure_search_nonblocking (LrdViewer *viewer) {
    initialize_best_exposure_search(viewer);
    search_start_time_bs = g_get_monotonic_time();
    n_steps_bs = 0;

    initial_exposure_bs = arv_camera_get_exposure_time (viewer->camera, NULL);
    initial_frame_rate = arv_camera_get_frame_rate (viewer->camera, NULL);

    // Search limits: the camera minimum, and the longest exposure that keeps the current frame rate
    double min_exposure, max_exposure;
    arv_camera_get_exposure_time_bounds (viewer->camera, &min_exposure, &max_exposure, NULL);
    double frame_rate = get_actual_frame_rate (viewer);
    if (frame_rate > 0 && G_USEC_PER_SEC / frame_rate < max_exposure)
        max_exposure = G_USEC_PER_SEC / frame_rate;
    if (min_exposure <= 0)
        min_exposure = 1;
    lo_exposure_bs = min_exposure;
    hi_exposure_bs = max_exposure;
    lo_tested_bs = FALSE;
    hi_tested_bs = FALSE;
    log_debug("Searching between %.1f us and %.1f us (current frame rate %.2f fps)", min_exposure, max_exposure, frame_rate);

    // One measurement per step; the steps are separated by short pauses so that the window is redrawn
    g_timeout_add (10, best_exposure_search_next_step, viewer);
}
