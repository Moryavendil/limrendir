#include <gtk/gtk.h>
#include <gst/gst.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>

// HELP (h)
void help(LrdViewer *viewer) {
        printf("%s***** Available key bindings *****%s\n", tcCYN, tcNRM);

        printf("%s*** Quality Of Life ***%s                                                     \n", tcMAG, tcNRM);
        printf("h .................. Show help                                                  \n");
        printf("q  ................. Quit                                                       \n");
        printf("F11 ................ Toggle fullscreen                                          \n");
        printf("m .................. Toggle window maximization                                 \n");
        printf("g .................. Toggle grid                                                \n");
    if (!is_record_button_active(viewer)) {
        printf("%s*** Camera Control ***%s                                                      \n", tcMAG, tcNRM);
        printf("+/- ................ Change exposure time (1 us)                                \n");
        printf("Pg Up/Pg Down ...... Change exposure time (100 us)                              \n");
        printf("b .................. Toggle use the maximum available frame rate                \n");
        printf("n .................. Set the exposure to have a maximum contrast (experimental) \n");

        printf("%s*** Field Of View Manipulation ***%s                                          \n", tcMAG, tcNRM);
        printf("a .................. See the largest possible field of view                     \n");
        printf("z .................. Zoom in (x2) to the center of the field of view            \n");
        printf("Z .................. Zoom out (x2) from the center of the field of view         \n");
        printf("r .................. Toggle Region Of Interest mode                             \n");
        printf("e,s,d,f (E,S,D,F)... Move upper left corner of ROI                              \n");
        printf("i,j,k,l (I,J,K,L)... Move lower right corner of ROI                             \n");
        printf("arrows (keypad) .... Move ROI as a whole                                        \n");
        printf("mouse drag ......... Draw a new ROI (in ROI mode)                               \n");
        printf("c .................. Crop to the Region Of Interest                             \n");
    }
        printf("%s*** Video Acquisition ***%s                                                   \n", tcMAG, tcNRM);
        printf("SPACE .............. Toggle video acquisition                                   \n");
}

void setup_help_popover (LrdViewer *viewer) {
    // QOL
    gtk_label_set_markup (GTK_LABEL (viewer->help_key_1), "<b>General Commands</b>\n"
                                                          "h\n"
                                                          "q\n"
                                                          "F11\n"
                                                          "m\n"
                                                          "g");
    gtk_label_set_markup (GTK_LABEL (viewer->help_description_1), "\n"
                                                                  "Print help\n"
                                                                  "Quit\n"
                                                                  "Toggle fullscreen\n"
                                                                  "Toggle window maximization\n"
                                                                  "Toggle grid");
    // CAMERA CONTROL
    gtk_label_set_markup (GTK_LABEL (viewer->help_key_2), "<b>Camera Control</b>\n"
                                                          "+/-\n"
                                                          "Pg Up/Pg Down\n"
                                                          "b\n"
                                                          "n");
    gtk_label_set_markup (GTK_LABEL (viewer->help_description_2), "\n"
                                                                  "Change exposure time (1 us)\n"
                                                                  "Change exposure time (100 us)\n"
                                                                  "Toggle maximum frame rate \n"
                                                                  "Optimize exposure (experimental)");
    // ROI
    gtk_label_set_markup (GTK_LABEL (viewer->help_key_3), "<b>Field Of View Manipulation</b>\n"
                                                          "a\n"
                                                          "z\n"
                                                          "Z\n"
                                                         "r\n"
                                                         "e,s,d,f \t\t(E,S,D,F)\n"
                                                         "i,j,k,l \t\t(I,J,K,L)\n"
                                                         "↑←↓→ \t(keypad)\n"
                                                         "mouse drag\n"
                                                         "c");
    gtk_label_set_markup (GTK_LABEL (viewer->help_description_3), "\n"
                                                                  "See the largest possible field of view\n"
                                                                  "Zoom in (x2)\n"
                                                                  "Zoom out (x2)\n"
                                                                 "Toggle Region Of Interest mode\n"
                                                                 "Move upper left corner of ROI\n"
                                                                 "Move lower right corner of ROI\n"
                                                                 "Move ROI as a whole\n"
                                                                 "Draw a new ROI (in ROI mode)\n"
                                                                 "Crop to the Region Of Interest");
    // VIDEO ACQU
    gtk_label_set_markup (GTK_LABEL (viewer->help_key_4), "<b>Video Acquisition</b>\n"
                                                         "SPACE");
    gtk_label_set_markup (GTK_LABEL (viewer->help_description_4), "\n"
                                                                 "Toggle video acquisition");
}

// ROI MANIPULATION
// The ROI is drawn as an overlay, whatever the pixel format. But GCV recordings crop the images to the ROI byte by
// byte, which needs a whole number of bytes per pixel (not possible with packed formats such as Mono12Packed).
gboolean is_roi_possible(LrdViewer *viewer) {
    const ArvPixelFormat pixel_format = arv_camera_get_pixel_format (viewer->camera, NULL);
    if (ARV_PIXEL_FORMAT_BIT_PER_PIXEL (pixel_format) % 8 == 0) {
        return TRUE;
    } else {
        log_warning("The ROI cannot be used with a packed pixel format (%s).",
                    arv_camera_get_pixel_format_as_string (viewer->camera, NULL));
        return FALSE;
    }
}

void constrain_roi(LrdViewer *viewer) {
    gint width, height;
    arv_camera_get_region (viewer->camera, NULL, NULL, &width, &height, NULL);
    constrain_roi_to_field_of_view(viewer, width, height);
}

void constrain_roi_to_field_of_view(LrdViewer *viewer, int width, int height) {
// If no roi is defined, make default
    if (viewer->roi_x == -1) {
        viewer->roi_x = 0;
    }
    if (viewer->roi_y == -1) {
        viewer->roi_y = 0;
    }
    if (viewer->roi_w == -1) {
        viewer->roi_w = width;
    }
    if (viewer->roi_h == -1) {
        viewer->roi_h = height;
    }

// Make sure that the roi is included in the field
    if (viewer->roi_x < 0) {
        viewer->roi_x = 0;
    } else if (viewer->roi_x >= width) {
        viewer->roi_x = width -1;
    }
    if (viewer->roi_y < 0) {
        viewer->roi_y = 0;
    } else if (viewer->roi_y >= height) {
        viewer->roi_y = height -1;
    }
    if (viewer->roi_w < 1) {
        viewer->roi_w = 1;
    } else if (viewer->roi_w > width - viewer->roi_x) {
        viewer->roi_w = width - viewer->roi_x;
    }
    if (viewer->roi_h < 1) {
        viewer->roi_h = 1;
    } else if (viewer->roi_h > height - viewer->roi_y) {
        viewer->roi_h = height - viewer->roi_y;
    }
}

// TOGGLE ROI VISIBILITY (r)
void toggle_roi(LrdViewer *viewer) {
    if (is_roi_possible(viewer))
        viewer->show_roi = !(viewer->show_roi);
    constrain_roi(viewer);
    update_status_bar_cb(viewer);
    overlay_queue_redraw(viewer);
}

// ROI MOVEMENT
enum roiMovementDirection{ROI_DIRECTION_NONE, ROI_DIRECTION_RIGHT, ROI_DIRECTION_LEFT, ROI_DIRECTION_UP, ROI_DIRECTION_DOWN};
enum roiMovementAmplitude{ROI_AMPLITUDE_NONE = 0, ROI_AMPLITUDE_SMALL = 1, ROI_AMPLITUDE_LARGE = 20};

void move_roi_whole(LrdViewer *viewer, enum roiMovementDirection direction, enum roiMovementAmplitude amplitude) {
    if (viewer->show_roi) {
        if (direction == ROI_DIRECTION_RIGHT) {
            viewer->roi_x += (int) amplitude;
        } else if (direction == ROI_DIRECTION_LEFT) {
            viewer->roi_x -= (int) amplitude;
        } else if (direction == ROI_DIRECTION_UP) {
            viewer->roi_y -= (int) amplitude;
        } else if (direction == ROI_DIRECTION_DOWN) {
            viewer->roi_y += (int) amplitude;
        }
        constrain_roi(viewer);
        update_status_bar_cb(viewer);
        overlay_queue_redraw(viewer);
    }
}

void move_roi_lower_right_corner(LrdViewer *viewer, enum roiMovementDirection direction, enum roiMovementAmplitude amplitude) {
    if (viewer->show_roi) {
        if (direction == ROI_DIRECTION_RIGHT) {
            viewer->roi_w += (int) amplitude;
        } else if (direction == ROI_DIRECTION_LEFT) {
            viewer->roi_w -= (int) amplitude;
        } else if (direction == ROI_DIRECTION_UP) {
            viewer->roi_h -= (int) amplitude;
        } else if (direction == ROI_DIRECTION_DOWN) {
            viewer->roi_h += (int) amplitude;
        }
        constrain_roi(viewer);
        update_status_bar_cb(viewer);
        overlay_queue_redraw(viewer);
    }
}

void move_roi_upper_left_corner(LrdViewer *viewer, enum roiMovementDirection direction, enum roiMovementAmplitude amplitude) {
    if (viewer->show_roi) {
        if (direction == ROI_DIRECTION_RIGHT) {
            viewer->roi_x += (int) amplitude;
            viewer->roi_w -= (int) amplitude;
        } else if (direction == ROI_DIRECTION_LEFT) {
            viewer->roi_x -= (int) amplitude;
            viewer->roi_w += (int) amplitude;
        } else if (direction == ROI_DIRECTION_UP) {
            viewer->roi_y -= (int) amplitude;
            viewer->roi_h += (int) amplitude;
        } else if (direction == ROI_DIRECTION_DOWN) {
            viewer->roi_y += (int) amplitude;
            viewer->roi_h -= (int) amplitude;
        }
        constrain_roi(viewer);
        update_status_bar_cb(viewer);
        overlay_queue_redraw(viewer);
    }
}

// ROI QOL ROUTINES
void crop_to_roi(LrdViewer *viewer) {
    if (!(viewer->show_roi)) {
        return;
    }
    log_debug("Cropping to ROI");
    log_trace("====== CROP Start");

    gint old_x, old_y, old_width, old_height;
    arv_camera_get_region (viewer->camera, &old_x, &old_y, &old_width, &old_height, NULL);
    log_trace("Old region: x: %d ; y: %d ; w: %d ; h: %d", old_x, old_y, old_width, old_height);

    log_trace("Old ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);

    int new_x = old_x + viewer->roi_x;
    int new_y = old_y + viewer->roi_y;
    int new_width = viewer->roi_w;
    int new_height = viewer->roi_h;
    log_trace("New target region: x: %d ; y: %d ; w: %d ; h: %d", new_x, new_y, new_width, new_height);

    new_x = new_x - new_x%arv_camera_get_x_offset_increment (viewer->camera, NULL);
    new_y = new_y - new_y%arv_camera_get_y_offset_increment (viewer->camera, NULL);
    new_width = new_width - new_width%arv_camera_get_width_increment (viewer->camera, NULL);
    new_height = new_height - new_height%arv_camera_get_height_increment (viewer->camera, NULL);

    log_trace("New target region: x: %d ; y: %d ; w: %d ; h: %d (INCREMENT COMPLIANT)", new_x, new_y, new_width, new_height);

    log_trace("=== CROP GoToCameraListMode");
    select_mode (viewer, TRN_VIEWER_MODE_CAMERA_LIST);

    log_trace("=== CROP ChangeWidth");
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_width), new_width);
    log_trace("=== CROP ChangeHeight");
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_height), new_height);
    log_trace("=== CROP ChangeX");
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_x), new_x);
    log_trace("=== CROP ChangeY");
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_y), new_y);

    // DEFAULT ROI
    log_trace("=== CROP UpdateROI");
    viewer->roi_x = 0;
    viewer->roi_y = 0;
    viewer->roi_w = new_width;
    viewer->roi_h = new_height;
    if (is_roi_possible(viewer))
        viewer->show_roi = TRUE;

    log_trace("=== CROP BackToVideoMode");
    select_mode (viewer, TRN_VIEWER_MODE_VIDEO);

    apply_max_frame_rate_if_wanted (NULL, viewer);

    log_trace("====== CROP End");
}

// Largest region size (in binned pixels), whatever the current offsets. The Width/Height bounds can not be used:
// depending on the camera, their maximum is either absolute or reduced by the current offset.
static void get_max_region_size (LrdViewer *viewer, gint *max_w, gint *max_h) {
    if (arv_camera_is_feature_available (viewer->camera, "WidthMax", NULL) &&
        arv_camera_is_feature_available (viewer->camera, "HeightMax", NULL)) {
        // Standard (SFNC) features, with the binning applied and the offsets ignored
        *max_w = (gint) arv_camera_get_integer (viewer->camera, "WidthMax", NULL);
        *max_h = (gint) arv_camera_get_integer (viewer->camera, "HeightMax", NULL);
        log_trace("Max region size from WidthMax/HeightMax: %d x %d", *max_w, *max_h);
    } else {
        // The sensor size is in unbinned pixels
        gint sensor_w, sensor_h, binning_x = 1, binning_y = 1;
        arv_camera_get_sensor_size (viewer->camera, &sensor_w, &sensor_h, NULL);
        if (arv_camera_is_binning_available (viewer->camera, NULL))
            arv_camera_get_binning (viewer->camera, &binning_x, &binning_y, NULL);
        *max_w = sensor_w / MAX (binning_x, 1);
        *max_h = sensor_h / MAX (binning_y, 1);
        log_trace("Max region size from the sensor size (%d x %d) and binning (%d x %d): %d x %d",
                  sensor_w, sensor_h, binning_x, binning_y, *max_w, *max_h);
    }
    if (*max_w <= 0 || *max_h <= 0) {
        // Neither is available: the Width/Height maximum may be too small (if reduced by the offsets), but it is
        // always accepted by the camera
        arv_camera_get_width_bounds (viewer->camera, NULL, max_w, NULL);
        arv_camera_get_height_bounds (viewer->camera, NULL, max_h, NULL);
        log_warning("Unknown sensor size: the largest field of view may not be reached (%d x %d).", *max_w, *max_h);
    }
}

void see_whole_field_of_view(LrdViewer *viewer) {
    log_debug("Seeing all the field of view");
    log_trace("=== Start VIEWALL");

    gint old_x, old_y, old_w, old_h;
    arv_camera_get_region (viewer->camera, &old_x, &old_y, &old_w, &old_h, NULL);
    log_trace("Old region: x: %d ; y: %d ; w: %d ; h: %d", old_x, old_y, old_w, old_h);

    gint min_x, min_y, max_w, max_h;
    arv_camera_get_x_offset_bounds(viewer->camera, &min_x, NULL, NULL);
    arv_camera_get_y_offset_bounds(viewer->camera, &min_y, NULL, NULL);
    get_max_region_size (viewer, &max_w, &max_h);
    log_trace("Max region: x: %d ; y: %d ; w: %d ; h: %d", min_x, min_y, max_w, max_h);

    // This ROI is the whole current field of view (relative to the max region)
    int new_roi_x = old_x - min_x;
    int new_roi_y = old_y - min_y;
    int new_roi_w = old_w;
    int new_roi_h = old_h;
    log_trace("Old ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);
    if (viewer->show_roi) {
        // If there is a used roi, use it instead
        new_roi_x += viewer->roi_x;
        new_roi_y += viewer->roi_y;
        new_roi_w = viewer->roi_w;
        new_roi_h = viewer->roi_h;
    }
    log_trace("New ROI: x: %d ; y: %d ; w: %d ; h: %d", new_roi_x, new_roi_y, new_roi_w, new_roi_h);

    if (old_x == min_x && old_y == min_y && old_w == max_w && old_h == max_h) {
        log_debug("The old fov is already the max fov.");
        return;
    }

    // Setting the range to be sure that the new values will be accepted.
    // This is of no consequence since these ranges are redefined after in update_camera_region (called by camera_region_cb)
    gtk_spin_button_set_range (GTK_SPIN_BUTTON(viewer->camera_width), 0, max_w + 1);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON(viewer->camera_height), 0, max_h + 1);

    select_mode (viewer, TRN_VIEWER_MODE_CAMERA_LIST);

    g_signal_handler_block (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_block (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_block (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_block (viewer->camera_height, viewer->camera_height_changed);


    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_x), min_x);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_y), min_y);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_width), max_w);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_height), max_h);

    g_signal_handler_unblock (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_unblock (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_unblock (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_unblock (viewer->camera_height, viewer->camera_height_changed);

    if (is_roi_possible(viewer)) {
        viewer->roi_x = new_roi_x;
        viewer->roi_y = new_roi_y;
        viewer->roi_w = new_roi_w;
        viewer->roi_h = new_roi_h;
        viewer->show_roi = TRUE;
    }

    camera_region_cb(NULL, viewer);

    select_mode (viewer, TRN_VIEWER_MODE_VIDEO);
//    update_status_bar_cb(viewer);
    log_trace("=== End VIEWALL");
}

void zoomin(LrdViewer *viewer) {
    log_debug("Zooming");
    log_trace("====== ZOOM Start");

    gint old_x, old_y, old_w, old_h;
    arv_camera_get_region (viewer->camera, &old_x, &old_y, &old_w, &old_h, NULL);
    log_trace("Old region: x: %d ; y: %d ; w: %d ; h: %d", old_x, old_y, old_w, old_h);

    viewer->roi_x = old_w/4;
    viewer->roi_y = old_h/4;
    viewer->roi_w = old_w/2;
    viewer->roi_h = old_h/2;

    log_trace("New ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);

    viewer->show_roi = gtk_true();

    crop_to_roi(viewer);

    viewer->show_roi = gtk_false();

    log_trace("====== ZOOM End");
}

// Zooms out by 2, around the center of the current field of view. A ROI in use is kept where it was in the image.
void zoomout(LrdViewer *viewer) {
    log_debug("Unzooming");
    log_trace("====== UNZOOM Start");

    gint old_x, old_y, old_w, old_h;
    arv_camera_get_region (viewer->camera, &old_x, &old_y, &old_w, &old_h, NULL);
    log_trace("Old region: x: %d ; y: %d ; w: %d ; h: %d", old_x, old_y, old_w, old_h);

    int old_roi_x = viewer->roi_x;
    int old_roi_y = viewer->roi_y;
    int old_roi_w = viewer->roi_w;
    int old_roi_h = viewer->roi_h;
    log_trace("Old ROI: x: %d ; y: %d ; w: %d ; h: %d", old_roi_x, old_roi_y, old_roi_w, old_roi_h);
    gboolean using_ROI = viewer->show_roi;

    see_whole_field_of_view(viewer);

    gint full_x, full_y, full_w, full_h;
    arv_camera_get_region (viewer->camera, &full_x, &full_y, &full_w, &full_h, NULL);
    if (full_x == old_x && full_y == old_y && full_w == old_w && full_h == old_h) {
        // see_whole_field_of_view did nothing (and left the ROI untouched)
        log_debug("Already seeing the largest possible field of view.");
        log_trace("====== UNZOOM End");
        return;
    }

    // Twice the old field of view, with the same center (in the coordinates of the whole field of view)
    viewer->roi_w = old_w*2;
    viewer->roi_h = old_h*2;
    viewer->roi_x = old_x - full_x - old_w/2;
    viewer->roi_y = old_y - full_y - old_h/2;
    // Near an edge, shift the window back inside instead of shrinking it
    if (viewer->roi_x + viewer->roi_w > full_w)
        viewer->roi_x = full_w - viewer->roi_w;
    if (viewer->roi_x < 0)
        viewer->roi_x = 0;
    if (viewer->roi_y + viewer->roi_h > full_h)
        viewer->roi_y = full_h - viewer->roi_h;
    if (viewer->roi_y < 0)
        viewer->roi_y = 0;
    constrain_roi(viewer);
    log_trace("New ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);

    // No need to crop (and restart the camera once more) if the window is the whole field of view
    if (viewer->roi_x != 0 || viewer->roi_y != 0 || viewer->roi_w != full_w || viewer->roi_h != full_h) {
        viewer->show_roi = gtk_true();
        crop_to_roi(viewer);
    }

    viewer->show_roi = gtk_false();

    if (is_roi_possible(viewer)) {
        // Same place in the image (the old ROI was relative to the old region). Without a ROI, like 'a', the ROI is
        // the old field of view.
        gint new_x, new_y;
        arv_camera_get_region (viewer->camera, &new_x, &new_y, NULL, NULL, NULL);
        if (using_ROI) {
            viewer->roi_x = old_roi_x + old_x - new_x;
            viewer->roi_y = old_roi_y + old_y - new_y;
            viewer->roi_w = old_roi_w;
            viewer->roi_h = old_roi_h;
        } else {
            viewer->roi_x = old_x - new_x;
            viewer->roi_y = old_y - new_y;
            viewer->roi_w = old_w;
            viewer->roi_h = old_h;
        }
        constrain_roi(viewer);
        viewer->show_roi = gtk_true();
        log_trace("Final ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);
    }

    log_trace("====== UNZOOM End");
}

// TOGGLE GRID (g)
void toggle_grid(LrdViewer *viewer) {
    if (viewer->grid_type == GRID_NOGRID) {
        viewer->grid_type = GRID_HORIZONTAL;
    } else if (viewer->grid_type == GRID_HORIZONTAL) {
        viewer->grid_type = GRID_BOTH;
    } else if (viewer->grid_type == GRID_BOTH) {
        viewer->grid_type = GRID_VERTICAL;
    } else if (viewer->grid_type == GRID_VERTICAL) {
        viewer->grid_type = GRID_NOGRID;
    }
    log_debug("Switched to a new grid : %d.", (int) viewer->grid_type);
    overlay_queue_redraw(viewer);
}

// CHANGE EXPOSURE (+/-/Pg Up/Pg Down)
void change_exposure(LrdViewer *viewer, GtkSpinType direction) {
    gtk_spin_button_spin (GTK_SPIN_BUTTON (viewer->exposure_spin_button), direction, 1);
    exposure_spin_cb (GTK_SPIN_BUTTON (viewer->exposure_spin_button), viewer);
}

// TOGGLE RECORDING (space)
void toggle_recording(LrdViewer *viewer) {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(viewer->record_button),!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(viewer->record_button)));
}

// QUIT (q)
void quit(LrdViewer *viewer) {
    if (is_record_button_active(viewer)) {
        stop_recording (viewer);
    }
    gtk_window_close (GTK_WINDOW (viewer->main_window));
}

// FULLSCREEN (F11)
void toggle_fullscreen(LrdViewer *viewer) {
    if (!viewer->is_fullscreen) {
        gtk_window_fullscreen (GTK_WINDOW (viewer->main_window));
        viewer->is_fullscreen = TRUE;
    } else {
        gtk_window_unfullscreen (GTK_WINDOW (viewer->main_window));
        viewer->is_fullscreen = FALSE;
    }
}

// MAXIMIZE (m)
void toggle_maximize(LrdViewer *viewer) {
    if (!gtk_window_is_maximized(GTK_WINDOW (viewer->main_window))) {
        gtk_window_maximize(GTK_WINDOW (viewer->main_window));
    } else {
        gtk_window_unmaximize(GTK_WINDOW (viewer->main_window));
    }
}

// GENERAL CONTROL
gint key_press_cb(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
    log_trace("The keysim named `%s' was pressed", gdk_keyval_name(event->keyval));

    LrdViewer *viewer = data;

    switch (event->keyval) {

        case GDK_KEY_g:
            toggle_grid(viewer);
            break;
        case GDK_KEY_F11:
            toggle_fullscreen(viewer);
            break;
        case GDK_KEY_m:
            toggle_maximize(viewer);
            break;

        case GDK_KEY_space:
            toggle_recording(viewer);
            break;

        case GDK_KEY_h:
            help(viewer);
            break;
        case GDK_KEY_q:
            quit(viewer);
            break;

        default:
            break;
    }

    if (!is_record_button_active(viewer)) {
        switch (event->keyval) {
            case GDK_KEY_n:
//                best_exposure_search_blocking(viewer);
                best_exposure_search_nonblocking(viewer);
                break;
            case GDK_KEY_b:
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (viewer->max_frame_rate_button), !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (viewer->max_frame_rate_button)));
//                apply_max_frame_rate_if_wanted(NULL, viewer); // redondant
                break;

            case GDK_KEY_a:
                see_whole_field_of_view(viewer);
                break;
            case GDK_KEY_c:
                crop_to_roi(viewer);
                break;
            case GDK_KEY_z:
                zoomin(viewer);
                break;
            case GDK_KEY_Z:
                zoomout(viewer);
                break;

            case GDK_KEY_Up:
                move_roi_whole(viewer, ROI_DIRECTION_UP, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_Left:
                move_roi_whole(viewer, ROI_DIRECTION_LEFT, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_Down:
                move_roi_whole(viewer, ROI_DIRECTION_DOWN, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_Right:
                move_roi_whole(viewer, ROI_DIRECTION_RIGHT, ROI_AMPLITUDE_SMALL);
                break;

            case GDK_KEY_KP_Up:
            case GDK_KEY_KP_8:
                move_roi_whole(viewer, ROI_DIRECTION_UP, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_KP_Left:
            case GDK_KEY_KP_4:
                move_roi_whole(viewer, ROI_DIRECTION_LEFT, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_KP_Down:
            case GDK_KEY_KP_2:
                move_roi_whole(viewer, ROI_DIRECTION_DOWN, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_KP_Right:
            case GDK_KEY_KP_6:
                move_roi_whole(viewer, ROI_DIRECTION_RIGHT, ROI_AMPLITUDE_LARGE);
                break;

            case GDK_KEY_e:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_UP, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_s:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_LEFT, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_d:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_DOWN, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_f:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_RIGHT, ROI_AMPLITUDE_SMALL);
                break;

            case GDK_KEY_E:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_UP, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_S:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_LEFT, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_D:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_DOWN, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_F:
                move_roi_upper_left_corner(viewer, ROI_DIRECTION_RIGHT, ROI_AMPLITUDE_LARGE);
                break;

            case GDK_KEY_i:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_UP, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_j:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_LEFT, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_k:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_DOWN, ROI_AMPLITUDE_SMALL);
                break;
            case GDK_KEY_l:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_RIGHT, ROI_AMPLITUDE_SMALL);
                break;

            case GDK_KEY_I:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_UP, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_J:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_LEFT, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_K:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_DOWN, ROI_AMPLITUDE_LARGE);
                break;
            case GDK_KEY_L:
                move_roi_lower_right_corner(viewer, ROI_DIRECTION_RIGHT, ROI_AMPLITUDE_LARGE);
                break;

            case GDK_KEY_r:
                toggle_roi(viewer);
                break;

            case GDK_KEY_plus:
                change_exposure(viewer, GTK_SPIN_STEP_FORWARD);
                break;
            case GDK_KEY_minus:
                change_exposure(viewer, GTK_SPIN_STEP_BACKWARD);
                break;
            case GDK_KEY_Page_Up:
                change_exposure(viewer, GTK_SPIN_PAGE_FORWARD);
                break;
            case GDK_KEY_Page_Down:
                change_exposure(viewer, GTK_SPIN_PAGE_BACKWARD);
                break;

            default:
                break;
        }
    }

    // Let the key also reach the focused widget (e.g. to type in an entry)
    return FALSE;
}

// Key listener toggle
void start_key_listener(LrdViewer *viewer) {
    gtk_widget_add_events(viewer->main_window, GDK_KEY_PRESS_MASK);
    viewer->keypress_handler_id = g_signal_connect (G_OBJECT (viewer->main_window), "key_press_event",
                                                    G_CALLBACK (key_press_cb), viewer);
}

void stop_key_listener(LrdViewer *viewer) {
    if (viewer->keypress_handler_id != 0) {
//        gtk_widget_add_events(viewer->main_window, GDK_KEY_PRESS_MASK);
        g_signal_handler_disconnect (G_OBJECT (viewer->main_window), viewer->keypress_handler_id);
    }
    viewer->keypress_handler_id = 0;
}
