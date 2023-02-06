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

// Set all the widgets
void set_acquisition_settings_widgets(LrdViewer *viewer)
{
    static gboolean initialization = TRUE;

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

    change_default_acquisition_name (NULL, viewer);

    initialization = FALSE;
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

// RECORD NAME

void change_default_acquisition_name (GtkToggleButton *button, LrdViewer *viewer)
{
    if (gtk_entry_get_text_length(GTK_ENTRY( viewer->acquisition_name_entry)) == 0) {
        gtk_entry_set_placeholder_text(GTK_ENTRY( viewer->acquisition_name_entry), obtain_new_original_filename(viewer));
    }
}

void acquisition_name_changed_cb (GtkEditable *entry, LrdViewer *viewer)
{
    if (gtk_entry_get_text_length(GTK_ENTRY( viewer->acquisition_name_entry)) != 0) {
        viewer->dataset_name = strdup( gtk_entry_get_text(GTK_ENTRY( viewer->acquisition_name_entry)) );
    } else {
        g_free(viewer->dataset_name);
        viewer->dataset_name = NULL;
    }
}