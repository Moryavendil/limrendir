#include <math.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>
#include <limrendir_settings.h>

// ACQUISITION SETTINGS

// Set all the widgets
void set_acquisition_settings_widgets(LrdViewer *viewer)
{
    static gboolean initialization = TRUE;

    if (initialization) {
        g_signal_handler_block (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton), TRUE);
        g_signal_handler_unblock (viewer->burst_nframes_radiobutton, viewer->burst_control_changed);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->recmode_usercontrolled_radiobutton), TRUE);
    }

    burst_control_toggled (NULL, viewer);
    record_mode_toggled (NULL, viewer);

    initialization = FALSE;
}

// RECORDING FORMAT

// Name of a video format in the settings ("mp4", "mkv" or "gcv")
static const char *recording_type_setting_name (RecordingType recording_type) {
    switch (recording_type) {
        case RECORDTYPE_GEVCAPTURE: return "gcv";
        case RECORDTYPE_MKV: return "mkv";
        default: return "mp4";
    }
}

// The format of the last video recorded in user-controlled mode, remembered between sessions (MP4 by default)
static RecordingType remembered_recording_type (void) {
    g_autofree char *type_name = settings_get_string (SETTINGS_GROUP_VIDEO, SETTINGS_KEY_FORMAT);
    const RecordingType candidates[] = {RECORDTYPE_MP4, RECORDTYPE_MKV, RECORDTYPE_GEVCAPTURE};
    for (size_t i = 0; i < G_N_ELEMENTS (candidates); i++) {
        if (g_strcmp0 (type_name, recording_type_setting_name (candidates[i])) == 0)
            return candidates[i];
    }
    if (type_name != NULL)
        log_debug("Unknown remembered video format '%s': ignored.", type_name);
    return RECORDTYPE_MP4;
}

static GtkWidget *recording_format_radiobutton (LrdViewer *viewer, RecordingType recording_type) {
    switch (recording_type) {
        case RECORDTYPE_GEVCAPTURE: return viewer->recformat_gcv_radiobutton;
        case RECORDTYPE_MKV: return viewer->recformat_mkv_radiobutton;
        default: return viewer->recformat_mp4_radiobutton;
    }
}

// Selects a format without remembering it (it is not chosen by the user)
static void show_recording_format (LrdViewer *viewer, RecordingType recording_type) {
    GtkWidget *buttons[] = {viewer->recformat_mp4_radiobutton, viewer->recformat_mkv_radiobutton,
                            viewer->recformat_gcv_radiobutton};
    for (size_t i = 0; i < G_N_ELEMENTS (buttons); i++)
        g_signal_handlers_block_by_func (buttons[i], recording_format_toggled, viewer);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (recording_format_radiobutton (viewer, recording_type)), TRUE);
    for (size_t i = 0; i < G_N_ELEMENTS (buttons); i++)
        g_signal_handlers_unblock_by_func (buttons[i], recording_format_toggled, viewer);
}

RecordingType chosen_recording_type (LrdViewer *viewer) {
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recformat_gcv_radiobutton)) ||
        gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)))
        return RECORDTYPE_GEVCAPTURE;
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recformat_mkv_radiobutton)))
        return RECORDTYPE_MKV;
    return RECORDTYPE_MP4;
}

// A format chosen by the user is remembered between sessions
void recording_format_toggled (GtkToggleButton *button, LrdViewer *viewer)
{
    if (!gtk_toggle_button_get_active (button))
        return;    // the format that was selected before
    settings_set_string (SETTINGS_GROUP_VIDEO, SETTINGS_KEY_FORMAT, recording_type_setting_name (chosen_recording_type (viewer)));
}

// RECORD MODE SETTINGS

void record_mode_toggled (GtkToggleButton *button, LrdViewer *viewer)
{
    // Single burst only records gcv: the other formats cannot be chosen. In user-controlled mode, the remembered
    // format is selected back.
    gboolean const burst_mode = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton));
    show_recording_format (viewer, burst_mode ? RECORDTYPE_GEVCAPTURE : remembered_recording_type ());
    gtk_widget_set_sensitive (viewer->recformat_mp4_radiobutton, !burst_mode);
    gtk_widget_set_sensitive (viewer->recformat_mkv_radiobutton, !burst_mode);

    gtk_widget_set_sensitive (viewer->burst_duration_radiobutton, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_duration_entry, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_nframes_radiobutton, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_nframes_entry, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton)));

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton))) {
        burst_control_toggled(NULL, viewer);
    }
}

void burst_control_toggled (GtkToggleButton *button, LrdViewer *viewer)
{
    gtk_widget_set_sensitive (viewer->burst_duration_entry, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->burst_duration_radiobutton)));
    gtk_widget_set_sensitive (viewer->burst_nframes_entry, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton)));

    burst_harmonize_nframes_and_duration (viewer);
}

// Limits of the single burst
static const gint burst_max_n_frames = 10000000;
static const double burst_min_duration = 0.000001, burst_max_duration = 3600;

// The number typed in an entry (a decimal comma is accepted), or FALSE if it is not a number
static gboolean read_entry_number (GtkWidget *entry, double *value) {
    g_autofree char *text = g_strstrip (g_strdup (gtk_entry_get_text (GTK_ENTRY (entry))));
    g_strdelimit (text, ",", '.');
    char *end = NULL;
    double const number = g_ascii_strtod (text, &end);
    if (end == text || *end != '\0' || !isfinite (number))
        return FALSE;
    *value = number;
    return TRUE;
}

void burst_harmonize_nframes_and_duration (LrdViewer *viewer)
{
    double const frame_rate = get_actual_frame_rate (viewer);
    double typed;

    // The chosen value is read (a text that is not a number is replaced by the previous value), the other one is
    // computed from it. A duration gives a whole number of frames, so it is rounded up to the duration of the frames.
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->burst_nframes_radiobutton))) {
        if (read_entry_number (viewer->burst_nframes_entry, &typed))
            viewer->burst_n_frames = (gint) CLAMP (round (typed), 1, burst_max_n_frames);
    } else {
        if (read_entry_number (viewer->burst_duration_entry, &typed))
            viewer->burst_duration = CLAMP (typed, burst_min_duration, burst_max_duration);
        if (frame_rate > 0)
            viewer->burst_n_frames = (gint) CLAMP (ceil (viewer->burst_duration * frame_rate - 1e-6), 1, burst_max_n_frames);
    }
    if (frame_rate > 0)
        viewer->burst_duration = viewer->burst_n_frames / frame_rate;

    char text[G_ASCII_DTOSTR_BUF_SIZE];
    g_snprintf (text, sizeof (text), "%d", viewer->burst_n_frames);
    gtk_entry_set_text (GTK_ENTRY (viewer->burst_nframes_entry), text);
    g_ascii_formatd (text, sizeof (text), "%.6g", viewer->burst_duration);
    gtk_entry_set_text (GTK_ENTRY (viewer->burst_duration_entry), text);
}

void burst_entry_activate_cb (GtkEntry *entry, LrdViewer *viewer)
{
    burst_harmonize_nframes_and_duration (viewer);
}

gboolean burst_entry_focus_out_cb (GtkWidget *entry, GdkEventFocus *event, LrdViewer *viewer)
{
    burst_harmonize_nframes_and_duration (viewer);
    return FALSE;
}
