#include <math.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video.h>
#include <arv.h>
#include <limrendir.h>
#include <memory.h>
#include <logroutines.h>

#define ARV_VIEWER_NOTIFICATION_TIMEOUT 10

gboolean has_autovideo_sink = FALSE;
gboolean has_gtksink = FALSE;
gboolean has_gtkglsink = FALSE;
gboolean has_bayer2rgb = FALSE;

static gboolean create_fake_camera = FALSE;

static gboolean
gstreamer_plugin_check (void)
{
	static gsize check_done = 0;
	static gboolean check_success = FALSE;

	if (g_once_init_enter (&check_done)) {
		GstRegistry *registry;
		GstPluginFeature *feature;
		unsigned int i;
		gboolean success = TRUE;

		static char *plugins[] = {
			"appsrc",
			"videoconvert",
			"videoflip"
		};

		registry = gst_registry_get ();

		for (i = 0; i < G_N_ELEMENTS (plugins); i++) {
			feature = gst_registry_lookup_feature (registry, plugins[i]);
			if (!GST_IS_PLUGIN_FEATURE (feature)) {
				g_print ("Gstreamer plugin '%s' is missing.\n", plugins[i]);
				success = FALSE;
			}
			else

				g_object_unref (feature);
		}

		feature = gst_registry_lookup_feature (registry, "autovideosink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_autovideo_sink = TRUE;
			g_object_unref (feature);
		}
        log_debug ("GStreamer video output plugin 'autovideosink' present: %s", has_autovideo_sink ? "yes" : "no");

		feature = gst_registry_lookup_feature (registry, "gtksink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_gtksink = TRUE;
			g_object_unref (feature);
		}
        log_debug ("GStreamer video output plugin 'gtksink' present: %s", has_gtksink ? "yes" : "no");

		feature = gst_registry_lookup_feature (registry, "gtkglsink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_gtkglsink = TRUE;
			g_object_unref (feature);
		}
        log_debug ("GStreamer video output plugin 'gtkglsink' present: %s", has_gtkglsink ? "yes" : "no");

		feature = gst_registry_lookup_feature (registry, "bayer2rgb");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			has_bayer2rgb = TRUE;
			g_object_unref (feature);
		}

		if (!has_autovideo_sink && !has_gtkglsink && !has_gtksink) {
			g_print ("Missing GStreamer video output plugin (autovideosink, gtksink or gtkglsink)\n");
			success = FALSE;
		}

		if (!success)
			g_print ("Check your gstreamer installation.\n");

		/* Kludge, prevent autoloading of coglsink, which doesn't seem to work for us */
		feature = gst_registry_lookup_feature (registry, "coglsink");
		if (GST_IS_PLUGIN_FEATURE (feature)) {
			gst_plugin_feature_set_rank (feature, GST_RANK_NONE);
			g_object_unref (feature);
		}

		check_success = success;

		g_once_init_leave (&check_done, 1);
	}

	return check_success;
}

typedef GtkApplicationClass LrdViewerClass;

G_DEFINE_TYPE (LrdViewer, arv_viewer, GTK_TYPE_APPLICATION)

// OPTIONS APPLYING

void
arv_viewer_set_options (LrdViewer *viewer,
                        gboolean auto_socket_buffer,
                        gboolean packet_resend,
                        guint initial_packet_timeout,
                        guint packet_timeout,
                        guint frame_retention,
                        ArvRegisterCachePolicy register_cache_policy,
                        ArvRangeCheckPolicy range_check_policy,
                        ArvUvUsbMode usb_mode) {
    g_return_if_fail (viewer != NULL);

    viewer->auto_socket_buffer = auto_socket_buffer;
    viewer->packet_resend = packet_resend;
    viewer->initial_packet_timeout = initial_packet_timeout;
    viewer->packet_timeout = packet_timeout;
    viewer->frame_retention = frame_retention;
    viewer->register_cache_policy = register_cache_policy;
    viewer->range_check_policy = range_check_policy;
    viewer->usb_mode = usb_mode;
}


void
gevCapture_set_options (LrdViewer *viewer,
                        char *gc_option_camera_name,
                        double gc_option_frame_rate,
                        double gc_option_exposure_time_us,
                        double gc_option_gain,
                        int gc_option_horizontal_binning,
                        int gc_option_vertical_binning ,
                        int gc_option_width,
                        int gc_option_height,
                        int gc_option_x_offset,
                        int gc_option_y_offset) {
    g_return_if_fail (viewer != NULL);

    viewer->gc_option_gain = gc_option_gain;
    viewer->gc_option_frame_rate = gc_option_frame_rate;
    viewer->gc_option_exposure_time_us = gc_option_exposure_time_us;
    viewer->gc_option_camera_name = gc_option_camera_name;
    viewer->gc_option_horizontal_binning = gc_option_horizontal_binning;
    viewer->gc_option_vertical_binning = gc_option_vertical_binning;
    viewer->gc_option_x_offset = gc_option_x_offset;
    viewer->gc_option_y_offset = gc_option_y_offset;
    viewer->gc_option_width = gc_option_width;
    viewer->gc_option_height = gc_option_height;
}

// This returns TRUE if a camera is selected and FALSE if none is
gboolean smart_camera_selection(LrdViewer *viewer) {
    log_debug("Smart camera selection");
    GtkTreeModel *tree_model = gtk_tree_view_get_model (GTK_TREE_VIEW (viewer->camera_tree));
    int const number_of_available_cameras = gtk_tree_model_iter_n_children (tree_model, NULL);
    GtkTreeIter iter;
    char *camera_id = NULL;

    if (viewer->gc_option_camera_name != NULL) {
        gboolean success = FALSE;
        for (int i_camera = 0; i_camera < number_of_available_cameras; i_camera++) {
            gtk_tree_model_iter_nth_child(tree_model, &iter, NULL, i_camera);
            gtk_tree_model_get(tree_model, &iter, 0, &camera_id, -1);
            if (g_str_equal(viewer->gc_option_camera_name, camera_id)) {
                gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW (viewer->camera_tree)),
                                               &iter);
                log_info("Selected camera %s", viewer->gc_option_camera_name);
                return TRUE;
            }
        }
        // If we are here, we did not return
        log_warning("Could not select camera %s.", viewer->gc_option_camera_name);
        if (number_of_available_cameras ==0) {
            log_info("No available cameras.");
        } else {
            log_info("Available cameras are:");
            for (int i_camera = 0; i_camera < number_of_available_cameras; i_camera++) {
                gtk_tree_model_iter_nth_child(tree_model, &iter, NULL, i_camera);
                gtk_tree_model_get(tree_model, &iter, 0, &camera_id, -1);
                log_info("    - %s", camera_id);
            }
        }
    } else {
        if (number_of_available_cameras == 1) {
            gtk_tree_model_get_iter_first(gtk_tree_view_get_model (GTK_TREE_VIEW (viewer->camera_tree)), &iter);
            gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), &iter);

            gtk_tree_model_get(tree_model, &iter, 0, &camera_id, -1);
            log_info("Auto-selected the only camera available: %s", camera_id);
            return TRUE;
        }
    }
    return FALSE;
}

void
apply_gc_options(LrdViewer *viewer) {

    const gboolean there_is_a_camera = smart_camera_selection(viewer);

    g_signal_handler_block (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_block (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_block (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_block (viewer->camera_height, viewer->camera_height_changed);

    if (viewer->gc_option_x_offset > -1)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_x), viewer->gc_option_x_offset);
    if (viewer->gc_option_y_offset > -1)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_y), viewer->gc_option_y_offset);
    if (viewer->gc_option_width > 0)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_width), viewer->gc_option_width);
    if (viewer->gc_option_height > 0)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_height), viewer->gc_option_height);

    g_signal_handler_unblock (viewer->camera_x, viewer->camera_x_changed);
    g_signal_handler_unblock (viewer->camera_y, viewer->camera_y_changed);
    g_signal_handler_unblock (viewer->camera_width, viewer->camera_width_changed);
    g_signal_handler_unblock (viewer->camera_height, viewer->camera_height_changed);

    g_signal_handler_block (viewer->camera_binning_x, viewer->camera_binning_x_changed);
    g_signal_handler_block (viewer->camera_binning_y, viewer->camera_binning_y_changed);

    if (viewer->gc_option_horizontal_binning > 0)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_binning_x), viewer->gc_option_horizontal_binning);
    if (viewer->gc_option_vertical_binning > 0)
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(viewer->camera_binning_y), viewer->gc_option_vertical_binning);

    g_signal_handler_unblock (viewer->camera_binning_x, viewer->camera_binning_x_changed);
    g_signal_handler_unblock (viewer->camera_binning_y, viewer->camera_binning_y_changed);

    // this only works if a camera is selected
    if (there_is_a_camera) {
        camera_region_cb(NULL, viewer);
        camera_binning_cb(NULL, viewer);

        // Exposure time
        if (viewer->gc_option_exposure_time_us != gc_option_exposure_time_us_default) {
            arv_camera_set_exposure_time (viewer->camera, viewer->gc_option_exposure_time_us, NULL);
            update_exposure_cb(viewer);
        }

        // gain
        if (viewer->gc_option_gain != gc_option_gain_default) {
            arv_camera_set_gain (viewer->camera, viewer->gc_option_gain, NULL);
            update_gain_cb(viewer);
        }

        // frame rate (acquisition frequency)
        if (viewer->gc_option_frame_rate != gc_option_frequency_default) {
            arv_camera_set_frame_rate(viewer->camera, viewer->gc_option_frame_rate, NULL);
            const double actual_frame_rate = arv_camera_get_frame_rate (viewer->camera, NULL);
            char *text = g_strdup_printf ("%g", actual_frame_rate);
            gtk_entry_set_text (GTK_ENTRY(viewer->frame_rate_entry), text);
        }

    } else if ((viewer->gc_option_frame_rate != gc_option_frequency_default) ||
               (viewer->gc_option_exposure_time_us != gc_option_exposure_time_us_default) ||
               (viewer->gc_option_gain != gc_option_gain_default) ||
               (viewer->gc_option_horizontal_binning != gc_option_horizontal_binning_default) ||
               (viewer->gc_option_vertical_binning != gc_option_vertical_binning_default) ||
               (viewer->gc_option_width != gc_option_width_default) ||
               (viewer->gc_option_height != gc_option_height_default) ||
               (viewer->gc_option_x_offset != gc_option_x_offset_default) ||
               (viewer->gc_option_y_offset != gc_option_y_offset_default)) {
        log_warning("No camera selected : some gevCapture options were not taken into account.");
    }
}

void
limrendir_set_options (LrdViewer *viewer,
                       gboolean lrd_option_gevcapture_mode,
                       char *lrd_option_dataset_name,
                       gboolean lrd_option_create_fake_camera) {
    g_return_if_fail (viewer != NULL);

    viewer->gevcapture_mode = lrd_option_gevcapture_mode;
    viewer->dataset_name = lrd_option_dataset_name;
    create_fake_camera = lrd_option_create_fake_camera;
}

// NOTIFICATION
void arv_viewer_show_notification (LrdViewer *viewer, const char *message, const char *details)
{
    g_return_if_fail (ARV_IS_VIEWER (viewer));
    g_return_if_fail (message != NULL);

    if (viewer->notification_timeout > 0)
        g_source_remove (viewer->notification_timeout);

    gtk_revealer_set_reveal_child (GTK_REVEALER (viewer->notification_revealer), FALSE);
    gtk_label_set_text (GTK_LABEL (viewer->notification_label), message);
    if (details != NULL) {
        g_autofree char *text = g_strdup_printf ("<small>%s</small>", details);
        gtk_widget_show (viewer->notification_details);
        gtk_label_set_markup (GTK_LABEL (viewer->notification_details), text);
    } else {
        gtk_widget_hide (viewer->notification_details);
    }
    gtk_revealer_set_reveal_child (GTK_REVEALER (viewer->notification_revealer), TRUE);

    viewer->notification_timeout = g_timeout_add_seconds (ARV_VIEWER_NOTIFICATION_TIMEOUT, hide_notification, viewer);
}

gboolean hide_notification (gpointer user_data)
{
    LrdViewer *viewer = user_data;

    gtk_revealer_set_reveal_child (GTK_REVEALER (viewer->notification_revealer), FALSE);
    viewer->notification_timeout = 0;

    return G_SOURCE_REMOVE;
}

void notification_dismiss_clicked_cb (GtkButton *dismiss, LrdViewer *viewer)
{
    if (viewer->notification_timeout > 0)
        g_source_remove (viewer->notification_timeout);

    gtk_revealer_set_reveal_child (GTK_REVEALER (viewer->notification_revealer), FALSE);

    viewer->notification_timeout = 0;
}

// QUITTING
static void
arv_viewer_quit_cb (GtkApplicationWindow *window, LrdViewer *viewer)
{
    log_debug("Quit callback called.");
    log_info("Au revoir!");
    stop_camera (viewer);
    g_application_quit (G_APPLICATION (viewer));
}

static void
viewer_shutdown (GApplication *application)
{
    G_APPLICATION_CLASS (arv_viewer_parent_class)->shutdown (application);

    arv_shutdown ();
}

// CREATING
static void
activate (GApplication *application)
{
	LrdViewer *viewer = (LrdViewer *) application;
	g_autoptr (GtkBuilder) builder = NULL;
    g_autoptr (GtkListStore) list_store = NULL;
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    // Key listener
    viewer->keypress_handler_id = 0;
    viewer->is_fullscreen = FALSE;

    // Grid
    viewer->grid_type = GRID_NOGRID;

// FOR THE LINE BELOW TO WORK, PUT THE FILE "limrendir.ui" IN THE FOLDER WITH THE EXECUTABLE
    builder = gtk_builder_new_from_file ("limrendir.ui");

	viewer->main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));
	viewer->main_stack = GTK_WIDGET (gtk_builder_get_object (builder, "main_stack"));
	viewer->main_headerbar = GTK_WIDGET (gtk_builder_get_object (builder, "main_headerbar"));
	viewer->camera_box = GTK_WIDGET (gtk_builder_get_object (builder, "camera_box"));
	viewer->refresh_button = GTK_WIDGET (gtk_builder_get_object (builder, "refresh_button"));
	viewer->video_mode_button = GTK_WIDGET (gtk_builder_get_object (builder, "video_mode_button"));
	viewer->back_button = GTK_WIDGET (gtk_builder_get_object (builder, "back_button"));
	viewer->snapshot_button = GTK_WIDGET (gtk_builder_get_object (builder, "snapshot_button"));
	viewer->camera_tree = GTK_WIDGET (gtk_builder_get_object (builder, "camera_tree"));
	viewer->camera_parameters = GTK_WIDGET (gtk_builder_get_object (builder, "camera_parameters"));
	viewer->pixel_format_combo = GTK_WIDGET (gtk_builder_get_object (builder, "pixel_format_combo"));
	viewer->camera_x = GTK_WIDGET (gtk_builder_get_object (builder, "camera_x"));
	viewer->camera_y = GTK_WIDGET (gtk_builder_get_object (builder, "camera_y"));
	viewer->camera_binning_x = GTK_WIDGET (gtk_builder_get_object (builder, "camera_binning_x"));
	viewer->camera_binning_y = GTK_WIDGET (gtk_builder_get_object (builder, "camera_binning_y"));
	viewer->camera_width = GTK_WIDGET (gtk_builder_get_object (builder, "camera_width"));
	viewer->camera_height = GTK_WIDGET (gtk_builder_get_object (builder, "camera_height"));
	viewer->video_box = GTK_WIDGET (gtk_builder_get_object (builder, "video_box"));
	viewer->video_frame = GTK_WIDGET (gtk_builder_get_object (builder, "video_frame"));
    viewer->fps_label = GTK_WIDGET (gtk_builder_get_object (builder, "fps_label"));
    viewer->roi_label = GTK_WIDGET (gtk_builder_get_object (builder, "roi_label"));
	viewer->image_label = GTK_WIDGET (gtk_builder_get_object (builder, "image_label"));
	viewer->trigger_combo_box = GTK_WIDGET (gtk_builder_get_object (builder, "trigger_combobox"));
	viewer->frame_rate_entry = GTK_WIDGET (gtk_builder_get_object (builder, "frame_rate_entry"));
	viewer->exposure_spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "exposure_spinbutton"));
	viewer->exposure_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "exposure_hscale"));
	viewer->auto_exposure_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "auto_exposure_togglebutton"));
	viewer->gain_spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "gain_spinbutton"));
	viewer->gain_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "gain_hscale"));
	viewer->auto_gain_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "auto_gain_togglebutton"));
	viewer->black_level_spin_button = GTK_WIDGET (gtk_builder_get_object (builder, "black_level_spinbutton"));
	viewer->black_level_hscale = GTK_WIDGET (gtk_builder_get_object (builder, "black_level_hscale"));
	viewer->auto_black_level_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "auto_black_level_togglebutton"));

	viewer->rotate_cw_button = GTK_WIDGET (gtk_builder_get_object (builder, "rotate_cw_button"));
	viewer->flip_vertical_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "flip_vertical_togglebutton"));
	viewer->flip_horizontal_toggle = GTK_WIDGET (gtk_builder_get_object (builder, "flip_horizontal_togglebutton"));
    viewer->camera_settings_button = GTK_WIDGET (gtk_builder_get_object (builder, "camera_settings_button"));
    viewer->acquisition_settings_button = GTK_WIDGET (gtk_builder_get_object (builder, "acquisition_settings_button"));

    // Record mode
    viewer->recmode_usercontrolled_radiobutton = GTK_WIDGET (gtk_builder_get_object (builder, "recmode_usercontrolled_radiobutton"));
    viewer->recmode_burst_radiobutton = GTK_WIDGET (gtk_builder_get_object (builder, "recmode_burst_radiobutton"));
    viewer->burst_nframes_radiobutton = GTK_WIDGET (gtk_builder_get_object (builder, "burst_nframes_radiobutton"));
    viewer->burst_duration_radiobutton = GTK_WIDGET (gtk_builder_get_object (builder, "burst_duration_radiobutton"));
    viewer->burst_nframes_spinBox = GTK_WIDGET (gtk_builder_get_object (builder, "burst_nframes_spinBox"));
    viewer->burst_duration_spinBox = GTK_WIDGET (gtk_builder_get_object (builder, "burst_duration_spinBox"));
    viewer->acquisition_name_entry = GTK_WIDGET (gtk_builder_get_object (builder, "acquisition_name_entry"));

    // Help popover
    viewer->help_button = GTK_WIDGET (gtk_builder_get_object (builder, "help_button"));
    viewer->help_key_1 = GTK_WIDGET (gtk_builder_get_object (builder, "help_key_1"));
    viewer->help_description_1 = GTK_WIDGET (gtk_builder_get_object (builder, "help_description_1"));
    viewer->help_key_2 = GTK_WIDGET (gtk_builder_get_object (builder, "help_key_2"));
    viewer->help_description_2 = GTK_WIDGET (gtk_builder_get_object (builder, "help_description_2"));
    viewer->help_key_3 = GTK_WIDGET (gtk_builder_get_object (builder, "help_key_3"));
    viewer->help_description_3 = GTK_WIDGET (gtk_builder_get_object (builder, "help_description_3"));
    viewer->help_key_4 = GTK_WIDGET (gtk_builder_get_object (builder, "help_key_4"));
    viewer->help_description_4 = GTK_WIDGET (gtk_builder_get_object (builder, "help_description_4"));

        // Custom G2L
    viewer->record_button = GTK_WIDGET (gtk_builder_get_object (builder, "record_button"));
    viewer->max_frame_rate_button = GTK_WIDGET (gtk_builder_get_object (builder, "max_frame_rate_button"));

    viewer->notification_revealer = GTK_WIDGET (gtk_builder_get_object (builder, "notification_revealer"));
    viewer->notification_label = GTK_WIDGET (gtk_builder_get_object (builder, "notification_label"));
    viewer->notification_details = GTK_WIDGET (gtk_builder_get_object (builder, "notification_details"));
    viewer->notification_dismiss = GTK_WIDGET (gtk_builder_get_object (builder, "notification_dismiss"));

    g_signal_connect (viewer->notification_dismiss, "clicked",
                      G_CALLBACK (notification_dismiss_clicked_cb), viewer);

    list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_BOOLEAN);
    gtk_combo_box_set_model (GTK_COMBO_BOX (viewer->pixel_format_combo), GTK_TREE_MODEL (list_store));

    gtk_cell_layout_clear (GTK_CELL_LAYOUT (viewer->pixel_format_combo));
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (viewer->pixel_format_combo), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (viewer->pixel_format_combo), renderer, "text", 0, NULL);
    gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (viewer->pixel_format_combo), renderer, set_sensitive, NULL, NULL);

	gtk_widget_set_no_show_all (viewer->trigger_combo_box, TRUE);

	gtk_widget_show_all (viewer->main_window);

	gtk_application_add_window (GTK_APPLICATION (application), GTK_WINDOW (viewer->main_window));

	g_signal_connect (viewer->refresh_button, "clicked", G_CALLBACK (update_device_list_cb), viewer);
	g_signal_connect (viewer->video_mode_button, "clicked", G_CALLBACK (switch_to_video_mode_cb), viewer);
	g_signal_connect (viewer->back_button, "clicked", G_CALLBACK (switch_to_camera_list_cb), viewer);
	g_signal_connect (viewer->main_window, "destroy", G_CALLBACK (arv_viewer_quit_cb), viewer);
	g_signal_connect (viewer->snapshot_button, "clicked", G_CALLBACK (snapshot_cb), viewer);
	g_signal_connect (viewer->rotate_cw_button, "clicked", G_CALLBACK (rotate_cw_cb), viewer);
	g_signal_connect (viewer->flip_horizontal_toggle, "clicked", G_CALLBACK (flip_horizontal_cb), viewer);
	g_signal_connect (viewer->flip_vertical_toggle, "clicked", G_CALLBACK (flip_vertical_cb), viewer);
	g_signal_connect (viewer->frame_rate_entry, "activate", G_CALLBACK (frame_rate_entry_cb), viewer);
	g_signal_connect (viewer->frame_rate_entry, "focus-out-event", G_CALLBACK (frame_rate_entry_focus_cb), viewer);

    g_signal_connect (viewer->acquisition_settings_button, "toggled", G_CALLBACK (change_default_acquisition_name), viewer);
    g_signal_connect (viewer->acquisition_name_entry, "changed", G_CALLBACK (acquisition_name_changed_cb), viewer);

    // record mode
    g_signal_connect (viewer->recmode_usercontrolled_radiobutton, "toggled", G_CALLBACK (record_mode_toggled), viewer);

    viewer->burst_control_changed = g_signal_connect (viewer->burst_nframes_radiobutton, "toggled", G_CALLBACK (burst_control_toggled), viewer);

    viewer->burst_nframes_changed = g_signal_connect (viewer->burst_nframes_spinBox, "value-changed", G_CALLBACK (burst_harmonize_nframes_and_duration), viewer);
    viewer->burst_duration_changed = g_signal_connect (viewer->burst_duration_spinBox, "value-changed", G_CALLBACK (burst_harmonize_nframes_and_duration), viewer);

    // Custom G2L
    viewer->recording_button_changed = g_signal_connect (viewer->record_button, "clicked", G_CALLBACK (record_button_cb), viewer);
    g_signal_connect (viewer->max_frame_rate_button, "toggled", G_CALLBACK(apply_max_frame_rate_if_wanted), viewer);

	if (!has_gtksink && !has_gtkglsink) {
		g_signal_connect (viewer->video_frame, "realize", G_CALLBACK (video_frame_realize_cb), viewer);
	}

	viewer->camera_selected = g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (viewer->camera_tree)), "changed",
						    G_CALLBACK (camera_selection_changed_cb), viewer);
	viewer->exposure_spin_changed = g_signal_connect (viewer->exposure_spin_button, "value-changed",
							  G_CALLBACK (exposure_spin_cb), viewer);
	viewer->exposure_hscale_changed = g_signal_connect (viewer->exposure_hscale, "value-changed",
							    G_CALLBACK (exposure_scale_cb), viewer);
	viewer->auto_exposure_clicked = g_signal_connect (viewer->auto_exposure_toggle, "clicked",
							  G_CALLBACK (auto_exposure_cb), viewer);
	viewer->gain_spin_changed = g_signal_connect (viewer->gain_spin_button, "value-changed",
						      G_CALLBACK (gain_spin_cb), viewer);
	viewer->gain_hscale_changed = g_signal_connect (viewer->gain_hscale, "value-changed",
							G_CALLBACK (gain_scale_cb), viewer);
	viewer->auto_gain_clicked = g_signal_connect (viewer->auto_gain_toggle, "clicked",
						      G_CALLBACK (auto_gain_cb), viewer);
	viewer->black_level_spin_changed = g_signal_connect (viewer->black_level_spin_button, "value-changed",
						      G_CALLBACK (black_level_spin_cb), viewer);
	viewer->black_level_hscale_changed = g_signal_connect (viewer->black_level_hscale, "value-changed",
							G_CALLBACK (black_level_scale_cb), viewer);
	viewer->auto_black_level_clicked = g_signal_connect (viewer->auto_black_level_toggle, "clicked",
						      G_CALLBACK (auto_black_level_cb), viewer);
	viewer->pixel_format_changed = g_signal_connect (viewer->pixel_format_combo, "changed",
							 G_CALLBACK (pixel_format_combo_cb), viewer);
	viewer->camera_x_changed = g_signal_connect (viewer->camera_x, "value-changed",
						     G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_y_changed = g_signal_connect (viewer->camera_y, "value-changed",
						     G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_width_changed = g_signal_connect (viewer->camera_width, "value-changed",
							 G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_height_changed = g_signal_connect (viewer->camera_height, "value-changed",
							  G_CALLBACK (camera_region_cb), viewer);
	viewer->camera_binning_x_changed = g_signal_connect (viewer->camera_binning_x, "value-changed",
							     G_CALLBACK (camera_binning_cb), viewer);
	viewer->camera_binning_y_changed = g_signal_connect (viewer->camera_binning_y, "value-changed",
							     G_CALLBACK (camera_binning_cb), viewer);

	gtk_widget_set_sensitive (viewer->camera_parameters, FALSE);
	select_mode (viewer, TRN_VIEWER_MODE_CAMERA_LIST);
	update_device_list_cb (GTK_TOOL_BUTTON (viewer->refresh_button), viewer);

    // Apply gevcapture options
    apply_gc_options(viewer);

    // Do gevcapture mode
    if (viewer->gevcapture_mode) {
        log_info( "gevCapture legacy mode enabled.");

        // Switch to video mode
        switch_to_video_mode_cb(NULL, viewer);

        // Go fullscreen (maximize)
        gtk_window_maximize(GTK_WINDOW (viewer->main_window));
    }


    log_debug("Resetted ROI (initial).");
    viewer->show_roi = FALSE;
    viewer->roi_x = -1;
    viewer->roi_y = -1;
    viewer->roi_w = -1;
    viewer->roi_h = -1;

    setup_help_popover (viewer);

    log_info("Type 'h' to display the available key bindings.");
}

static void
startup (GApplication *application)
{
    if (create_fake_camera) {
        log_debug("Starting fake camera");
        arv_enable_interface ("Fake");
    }

	G_APPLICATION_CLASS (arv_viewer_parent_class)->startup (application);
}

static void
finalize (GObject *object)
{
	G_OBJECT_CLASS (arv_viewer_parent_class)->finalize (object);
}

LrdViewer *
trn_viewer_new (void)
{
  LrdViewer *trn_viewer;

  if (!gstreamer_plugin_check ())
	  return NULL;

  g_set_application_name ("Limrendir");

    trn_viewer = g_object_new (arv_viewer_get_type(),
                               "application-id", "org.aravis.Aravis",
                               "flags", G_APPLICATION_NON_UNIQUE,
                               "inactivity-timeout", 30000,
                               NULL);

  return trn_viewer;
}

static void
arv_viewer_init (LrdViewer *viewer)
{
	viewer->auto_socket_buffer = FALSE;
	viewer->packet_resend = TRUE;
	viewer->packet_timeout = 20;
	viewer->frame_retention = 100;
	viewer->register_cache_policy = ARV_REGISTER_CACHE_POLICY_DEFAULT;
	viewer->range_check_policy = ARV_RANGE_CHECK_POLICY_DEFAULT;
}

static void
arv_viewer_class_init (LrdViewerClass *class)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = finalize;

  application_class->startup = startup;
  application_class->shutdown = viewer_shutdown;
  application_class->activate = activate;

}
