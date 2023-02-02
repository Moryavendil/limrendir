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
        printf("r .................. Toggle Region Of Interest mode                             \n");
        printf("e,s,d,f (E,S,D,F)... Move upper left corner of ROI                              \n");
        printf("i,j,k,l (I,J,K,L)... Move lower right corner of ROI                             \n");
        printf("arrows (keypad) .... Move ROI as a whole                                        \n");
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
                                                         "r\n"
                                                         "e,s,d,f \t\t(E,S,D,F)\n"
                                                         "i,j,k,l \t\t(I,J,K,L)\n"
                                                         "↑←↓→ \t(keypad)\n"
                                                         "c");
    gtk_label_set_markup (GTK_LABEL (viewer->help_description_3), "\n"
                                                                 "See the largest possible field of view\n"
                                                                 "Toggle Region Of Interest mode\n"
                                                                 "Move upper left corner of ROI\n"
                                                                 "Move lower right corner of ROI\n"
                                                                 "Move ROI as a whole\n"
                                                                 "Crop to the Region Of Interest");
    // VIDEO ACQU
    gtk_label_set_markup (GTK_LABEL (viewer->help_key_4), "<b>Video Acquisition</b>\n"
                                                         "SPACE");
    gtk_label_set_markup (GTK_LABEL (viewer->help_description_4), "\n"
                                                                 "Toggle video acquisition");
}

// ROI MANIPULATION
gboolean is_roi_possible(LrdViewer *viewer) {
    const ArvPixelFormat pixel_format = arv_camera_get_pixel_format (viewer->camera, NULL);
    const char *caps_string = arv_pixel_format_to_gst_caps_string (pixel_format);
    if (pixel_format == ARV_PIXEL_FORMAT_MONO_8) {
        return TRUE;
    } else {
        log_warning("This pixel format (%s) do not allow for ROI utilization (yet!). Use Mono8 instead.", caps_string);
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
    }
}

// ROI QOL ROUTINES
void crop_to_roi(LrdViewer *viewer) {
    if (!(viewer->show_roi)) {
        return;
    }
    log_debug("Cropping to ROI");

    gint old_x, old_y, old_width, old_height;
    arv_camera_get_region (viewer->camera, &old_x, &old_y, &old_width, &old_height, NULL);
    log_trace("Old region: x: %d ; y: %d ; w: %d ; h: %d", old_x, old_y, old_width, old_height);

    log_trace("Old ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);

    int new_x = old_x + viewer->roi_x;
    int new_y = old_y + viewer->roi_y;
    int new_width = viewer->roi_w;
    int new_height = viewer->roi_h;
    log_trace("New region: x: %d ; y: %d ; w: %d ; h: %d", new_x, new_y, new_width, new_height);

    select_mode (viewer, TRN_VIEWER_MODE_CAMERA_LIST);

    // Setting the range to be sure that the new values will be accepted.
    // This is of no consequence since these ranges are redined after in update_camera_region (called by camera_region_cb)
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (viewer->camera_x), 0, new_x + 1);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON(viewer->camera_y), 0, new_y + 1);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON(viewer->camera_width), 0, new_width + 1);
    gtk_spin_button_set_range (GTK_SPIN_BUTTON(viewer->camera_height), 0, new_height + 1);

    g_signal_handler_block (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_block (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_block (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_block (viewer->camera_height, viewer->camera_height_changed);

    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_x), new_x);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_y), new_y);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_width), new_width);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_height), new_height);

    g_signal_handler_unblock (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_unblock (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_unblock (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_unblock (viewer->camera_height, viewer->camera_height_changed);

    // DEFAULT ROI
    viewer->roi_x = 0;
    viewer->roi_y = 0;
    viewer->roi_w = new_width;
    viewer->roi_h = new_height;
    if (is_roi_possible(viewer))
        viewer->show_roi = TRUE;

    camera_region_cb(NULL, viewer);

    select_mode (viewer, TRN_VIEWER_MODE_VIDEO);
}

void see_whole_field_of_view(LrdViewer *viewer) {
    log_debug("Seeing all the field of view");

    gint old_x, old_y, old_w, old_h;
    arv_camera_get_region (viewer->camera, &old_x, &old_y, &old_w, &old_h, NULL);
    log_trace("Old region: x: %d ; y: %d ; w: %d ; h: %d", old_x, old_y, old_w, old_h);

    // This ROI is the whole current field of view
    int new_roi_x = old_x;
    int new_roi_y = old_y;
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
    log_trace("New ROI: x: %d ; y: %d ; w: %d ; h: %d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);

    gint min_x, min_y, max_w, max_h;
    arv_camera_get_x_offset_bounds(viewer->camera, &min_x, NULL, NULL);
    arv_camera_get_y_offset_bounds(viewer->camera, &min_y, NULL, NULL);
    arv_camera_get_width_bounds(viewer->camera, NULL, &max_w, NULL);
    arv_camera_get_height_bounds (viewer->camera, NULL, &max_h, NULL);
    /*
     * FOR SOME CAMERAS (Arv-Fake par ex), arv_camera_get_width_bounds renvoie les bornes ABSOLUES
     * POUR D'AUTRES (la manta par ex), arv_camera_get_width_bounds renvoie les bornes possibles sachant l'offset x.
     * DANS LE DOUTE on blinde et on met un gros truc dans la max region. Au pire ca ne sera pas pris en compte.
     */
    max_w += old_x;
    max_h += old_y;
    log_trace("Max region: x: %d ; y: %d ; w: %d ; h: %d", min_x, min_y, max_w, max_h);

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
    log_debug("Switched to a new grid : %d.", viewer->grid_type);

    const ArvPixelFormat pixel_format = arv_camera_get_pixel_format (viewer->camera, NULL);
    if (pixel_format != ARV_PIXEL_FORMAT_MONO_8) {
        const char *caps_string = arv_pixel_format_to_gst_caps_string (pixel_format);
        printf("This pixel format (%s) do not allow for grid utilization (yet!). Use Mono8 instead.", caps_string);
    }
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
                apply_max_frame_rate_if_wanted(NULL, viewer);
                break;

            case GDK_KEY_a:
                see_whole_field_of_view(viewer);
                break;
            case GDK_KEY_c:
                crop_to_roi(viewer);
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