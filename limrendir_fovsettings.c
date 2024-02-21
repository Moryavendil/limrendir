#include <gtk/gtk.h>
#include <gst/gst.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>

// FOV SETTINGS

// Pixel format
void pixel_format_combo_cb (GtkComboBoxText *combo, LrdViewer *viewer)
{
    char *pixel_format;

    pixel_format = gtk_combo_box_text_get_active_text (combo);
    arv_camera_set_pixel_format_from_string (viewer->camera, pixel_format, NULL);
    g_free (pixel_format);
}

void set_sensitive (GtkCellLayout *cell_layout,
                    GtkCellRenderer *cell,
                    GtkTreeModel *tree_model,
                    GtkTreeIter *iter,
                    gpointer data)
{
    gboolean valid;

    gtk_tree_model_get (tree_model, iter, 1, &valid, -1);

    g_object_set(cell, "sensitive", valid, NULL);
}

// Region
void camera_region_cb (GtkSpinButton *spin_button, LrdViewer *viewer)
{
    int x = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_x));
    int y = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_y));
    int width = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_width));
    int height = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_height));

    log_trace("Trying to set the region to (x:%d, y:%d, w:%d, h:%d)", x, y, width, height);
    arv_camera_set_region (viewer->camera, x, y, width, height, NULL);

    update_camera_region (viewer);
}

// Binning
void camera_binning_cb (GtkSpinButton *spin_button, LrdViewer *viewer) {
    int dx = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_binning_x));
    int dy = gtk_spin_button_get_value (GTK_SPIN_BUTTON (viewer->camera_binning_y));

    log_trace("Trying to set the binning to (binX:%d, binY:%d)", dx, dy);
    arv_camera_set_binning (viewer->camera, dx, dy, NULL);

    update_camera_region (viewer);
}

// Updates the region and binning spin boxes
void update_camera_region (LrdViewer *viewer)
{
    log_debug("Updating the camera region");
    gint x, y, width, height;
    gint dx, dy;
    gint min, max;
    gint inc;

    // Block signals to avoid infinite recursion
    g_signal_handler_block (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_block (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_block (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_block (viewer->camera_height, viewer->camera_height_changed);
    g_signal_handler_block (viewer->camera_binning_x, viewer->camera_binning_x_changed);
    g_signal_handler_block (viewer->camera_binning_y, viewer->camera_binning_y_changed);

    // Get the real region
    arv_camera_get_region (viewer->camera, &x, &y, &width, &height, NULL);
    arv_camera_get_binning (viewer->camera, &dx, &dy, NULL);
    log_trace("The region is (x:%d, y:%d, w:%d, h:%d)", x, y, width, height);
    log_trace("The binning is (binX:%d, binY:%d)", dx, dy);

    // Bin x
    arv_camera_get_x_binning_bounds (viewer->camera, &min, &max, NULL);
    inc = arv_camera_get_x_binning_increment (viewer->camera, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_binning_x), min, max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_binning_x), inc, inc * 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_binning_x), TRUE);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_binning_x), dx);

    // Bin y
    arv_camera_get_y_binning_bounds (viewer->camera, &min, &max, NULL);
    inc = arv_camera_get_y_binning_increment (viewer->camera,  NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_binning_y), min, max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_binning_y), inc, inc * 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_binning_y), TRUE);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_binning_y), dy);

    // Offset x
    arv_camera_get_x_offset_bounds (viewer->camera, &min, &max, NULL);
    inc = arv_camera_get_x_offset_increment (viewer->camera, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_x), min, max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_x), inc, inc * 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_x), TRUE);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_x), x);

    // Offset y
    arv_camera_get_y_offset_bounds (viewer->camera, &min, &max, NULL);
    inc = arv_camera_get_y_offset_increment (viewer->camera, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_y), min, max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_y), inc, inc * 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_y), TRUE);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_y), y);

    // Width
    arv_camera_get_width_bounds (viewer->camera, &min, &max, NULL);
    inc = arv_camera_get_width_increment (viewer->camera, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_width), min, max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_width), inc, inc * 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_width), TRUE);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_width), width);

    // Height
    arv_camera_get_height_bounds (viewer->camera, &min, &max, NULL);
    inc = arv_camera_get_height_increment (viewer->camera, NULL);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_height), min, max);
    gtk_spin_button_set_increments (GTK_SPIN_BUTTON (viewer->camera_height), inc, inc * 10);
    gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (viewer->camera_height), TRUE);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (viewer->camera_height), height);

    // Re-enable signals
    g_signal_handler_unblock (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_unblock (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_unblock (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_unblock (viewer->camera_height, viewer->camera_height_changed);
    g_signal_handler_unblock (viewer->camera_binning_x, viewer->camera_binning_x_changed);
    g_signal_handler_unblock (viewer->camera_binning_y, viewer->camera_binning_y_changed);

    constrain_roi_to_field_of_view(viewer, width, height);
}
