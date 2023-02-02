#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>  // for GDK_WINDOW_XID
#endif
#ifdef GDK_WINDOWING_WIN32
#include <gdk/gdkwin32.h>  // for GDK_WINDOW_HWND
#endif


// These are straight-up copy-pasted form gevCapture
/* return time from timespec-structure in milliseconds */
long clock_millis(struct timespec t) {
    return ((long) t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

/* time difference in milliseconds */
long clock_diff_millis(struct timespec t1, struct timespec t2) {
    long d;
    d = ((long) t1.tv_sec - (long) t2.tv_sec) * 1000
        + (t1.tv_nsec - t2.tv_nsec) / 1000000;
    return d;
}


// RECORDING THINGS
// declare files
// Number of buffers in the buffer queue (more is better)
unsigned n_buffers_streaming = 10;
// Number of FPS to display while recording (less is better)
gint display_fps_streaming = 50;
// Time since last update
struct timespec clock_last_snapshot_streaming, clock_now_streaming;

void video_frame_realize_cb (GtkWidget * widget, LrdViewer *viewer)
{
#ifdef GDK_WINDOWING_X11
    viewer->video_window_xid = GDK_WINDOW_XID (gtk_widget_get_window (widget));
#endif
#ifdef GDK_WINDOWING_WIN32
    viewer->video_window_xid = (guintptr) GDK_WINDOW_HWND (gtk_widget_get_window (widget));
#endif
}

void remove_widget (GtkWidget *widget, gpointer data)
{
    gtk_container_remove (data, widget);
    g_object_unref (widget);
}

// GRID DRAWING
gboolean draw_horizontal_line(int linenumber, uint8_t * buffer_data, int width, int height) {
    if ((linenumber < 0) || (linenumber >= height)) {
        return FALSE;
    }
    for (int i = 0 ; i < width ; i+=1) {
        buffer_data[linenumber*width + i] = 0;
    }
    return TRUE;
}

gboolean draw_vertical_line(int linenumber, uint8_t * buffer_data, int width, int height) {
    if ((linenumber < 0) || (linenumber >= width)) {
        return FALSE;
    }
    for (int i = 0 ; i < height ; i+=1) {
        buffer_data[i*width + linenumber] = 0;
    }
    return TRUE;
}

void draw_grid_on_buffer(uint8_t *buffer_data, int width, int height, GridType type_of_grid) {
    // operations
    int midline;
    int n_line;
    int lines_spacing = 100;

    // HORIZONTAL
    if ((type_of_grid == GRID_HORIZONTAL) || (type_of_grid == GRID_BOTH)) {
        midline = height / 2;
        n_line = 0;
        while (draw_horizontal_line(midline + n_line * lines_spacing, buffer_data, width, height)) {
            n_line += 1;
        }
        n_line = -1;
        while (draw_horizontal_line(midline + n_line * lines_spacing, buffer_data, width, height)) {
            n_line -= 1;
        }
    }

    // VERTICAL
    if ((type_of_grid == GRID_VERTICAL) || (type_of_grid == GRID_BOTH)) {
        midline = width / 2;
        n_line = 0;
        while (draw_vertical_line(midline + n_line * lines_spacing, buffer_data, width, height)) {
            n_line += 1;
        }
        n_line = -1;
        while (draw_vertical_line(midline + n_line * lines_spacing, buffer_data, width, height)) {
            n_line -= 1;
        }
    }
}

// ROI DRAWING
void draw_roi_buffer(uint8_t *buffer_data, int width, int height, LrdViewer *viewer) {

    if (viewer->show_roi) {
        constrain_roi_to_field_of_view(viewer, width, height);
        uint8_t attenuation_factor = 3;

        // Lines over
        for (int i = 0 ; i < width * viewer->roi_y ; i+=1) {
            buffer_data[i] = buffer_data[i] / attenuation_factor;
        }

        // Lines in between
        for (int i_hline = viewer->roi_y ; i_hline < viewer->roi_y + viewer->roi_h ; i_hline+=1) {
            for (int i_vline = 0 ; i_vline < viewer->roi_x ; i_vline += 1) {
                buffer_data[i_hline*width + i_vline] = buffer_data[i_hline*width + i_vline] / attenuation_factor;
            }
            for (int i_vline = viewer->roi_x + viewer->roi_w ; i_vline < width ; i_vline += 1) {
                buffer_data[i_hline*width + i_vline] = buffer_data[i_hline*width + i_vline] / attenuation_factor;
            }
        }

        // Lines under
        for (int i = width * (viewer->roi_y + viewer->roi_h) ; i < width * height ; i+=1) {
            buffer_data[i] = buffer_data[i] / attenuation_factor;
        }
    }

}


// SHOW AN ARAVIS BUFFER VIA GSTREAMER
typedef struct {
    GWeakRef stream;
    ArvBuffer* arv_buffer;
    void *data;
} ArvGstBufferReleaseData;

static void
gst_buffer_release_cb (void *user_data)
{
    ArvGstBufferReleaseData* release_data = user_data;

    ArvStream* stream = g_weak_ref_get (&release_data->stream);

    g_free (release_data->data);

    if (stream) {
//		gint n_input_buffers, n_output_buffers;
//
//		arv_stream_get_n_buffers (stream, &n_input_buffers, &n_output_buffers);
////		arv_debug_viewer ("push buffer (%d,%d)", n_input_buffers, n_output_buffers);

        arv_stream_push_buffer (stream, release_data->arv_buffer);
        g_object_unref (stream);
    } else {
//		arv_info_viewer ("invalid stream object");
        g_object_unref (release_data->arv_buffer);
    }

    g_weak_ref_clear (&release_data->stream);
    g_free (release_data);
}

GstBuffer *
arv_to_gst_buffer (ArvBuffer *arv_buffer, ArvStream *stream, LrdViewer *viewer)
{
    ArvGstBufferReleaseData* release_data;
    int arv_row_stride;
    int width, height;
    uint8_t *buffer_data_uint8;
    char *buffer_data;
    size_t buffer_size;
    size_t size;
    void *data;

    // get image size
    arv_buffer_get_image_region (arv_buffer, NULL, NULL, &width, &height);

    buffer_data_uint8 = (uint8_t *) arv_buffer_get_data (arv_buffer, &buffer_size);

    // DRAWING
    draw_grid_on_buffer(buffer_data_uint8, width, height, viewer->grid_type);
    draw_roi_buffer(buffer_data_uint8, width, height, viewer);

//    printf("%hhu\n", buffer_data_uint8[0]);

    buffer_data = (char *) buffer_data_uint8;

    release_data = g_new0 (ArvGstBufferReleaseData, 1);

    g_weak_ref_init (&release_data->stream, stream);
    release_data->arv_buffer = arv_buffer;

    /* Gstreamer requires row stride to be a multiple of 4 */
    arv_row_stride = width * ARV_PIXEL_FORMAT_BIT_PER_PIXEL (arv_buffer_get_image_pixel_format (arv_buffer)) / 8;
    if ((arv_row_stride & 0x3) != 0) {
        int gst_row_stride;
        int i;

        gst_row_stride = (arv_row_stride & ~(0x3)) + 4;

        size = height * gst_row_stride;
        data = g_malloc (size);

        for (i = 0; i < height; i++)
            memcpy (((char *) data) + i * gst_row_stride, buffer_data + i * arv_row_stride, arv_row_stride);

        release_data->data = data;

    } else {
        data = buffer_data;
        size = buffer_size;
    }

    GstBuffer *gst_buffer;
    gst_buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
                                              data, size, 0, size,
                                              release_data, gst_buffer_release_cb);
//    GstBuffer *w_gst_buffer;
//    w_gst_buffer = gst_buffer_make_writable(gst_buffer);
//
//    GstVideoOverlayRectangle * rectangle;
//    rectangle = gst_video_overlay_rectangle_new_raw (GstBuffer * pixels,
//                                                     100, 100, 100, 100, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
//
//    GstVideoOverlayComposition *composition;
//    composition = gst_video_overlay_composition_new (rectangle);
//
//    gst_buffer_add_video_overlay_composition_meta (gst_buffer,
//                                                   composition);

    return gst_buffer;
//	return gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
//					    data, size, 0, size,
//					    release_data, gst_buffer_release_cb);
}

// the callback called each time an aravis buffer is available
static void new_buffer_cb (ArvStream *stream, LrdViewer *viewer)
{
    ArvBuffer *arv_buffer;
    gint n_input_buffers, n_output_buffers;

    arv_buffer = arv_stream_pop_buffer (stream);
    if (arv_buffer == NULL)
        return;

    // Get the time of the grab
    clock_gettime(CLOCK_MONOTONIC, &clock_now_streaming);

//    arv_stream_get_n_buffers (stream, &n_input_buffers, &n_output_buffers);
//	arv_debug_viewer ("pop buffer (%d,%d)", n_input_buffers, n_output_buffers);

    if (arv_buffer_get_status(arv_buffer) != ARV_BUFFER_STATUS_SUCCESS) {
        arv_stream_push_buffer (stream, arv_buffer);
    } else {


        // Push back the buffer (in the right queue)
        if (clock_diff_millis(clock_now_streaming, clock_last_snapshot_streaming) * display_fps_streaming > 1000) {
            clock_last_snapshot_streaming = clock_now_streaming;
            g_clear_object(&viewer->last_buffer);
            viewer->last_buffer = g_object_ref(arv_buffer);
            gst_app_src_push_buffer(GST_APP_SRC (viewer->appsrc), arv_to_gst_buffer(arv_buffer, stream, viewer));
        } else {
            arv_stream_push_buffer(stream, arv_buffer);
        }
    }
}

// handles the bus... not sure what that does exactly
GstBusSyncReply bus_sync_handler (GstBus *bus, GstMessage *message, gpointer user_data)
{
    LrdViewer *viewer = user_data;

    if (!gst_is_video_overlay_prepare_window_handle_message(message))
        return GST_BUS_PASS;

    if (viewer->video_window_xid != 0) {
        GstVideoOverlay *videooverlay;

        videooverlay = GST_VIDEO_OVERLAY (GST_MESSAGE_SRC (message));
        gst_video_overlay_set_window_handle (videooverlay, viewer->video_window_xid);
    } else {
        g_warning ("Should have obtained video_window_xid by now!");
    }

    gst_message_unref (message);

    return GST_BUS_DROP;
}


gboolean start_video (LrdViewer *viewer)
{
    log_debug("Start the video stream.");
    GstElement *videoconvert;
    GstCaps *caps;
    ArvPixelFormat pixel_format;
    unsigned payload;
    unsigned i;
    gint width, height;
    const char *caps_string;

    if (!ARV_IS_CAMERA (viewer->camera))
        return FALSE;

    stop_video (viewer);

    viewer->rotation = 0;
    viewer->stream = arv_camera_create_stream(viewer->camera, stream_video_cb, NULL, NULL);
    if (!ARV_IS_STREAM (viewer->stream)) {
        g_object_unref (viewer->camera);
        viewer->camera = NULL;
        return FALSE;
    }

    if (ARV_IS_GV_STREAM (viewer->stream)) {
        if (viewer->auto_socket_buffer)
            g_object_set (viewer->stream,
                          "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
                          "socket-buffer-size", 0,
                          NULL);
        if (!viewer->packet_resend)
            g_object_set (viewer->stream,
                          "packet-resend", ARV_GV_STREAM_PACKET_RESEND_NEVER,
                          NULL);
        g_object_set (viewer->stream,
                      "initial-packet-timeout", (unsigned) viewer->initial_packet_timeout * 1000,
                      "packet-timeout", (unsigned) viewer->packet_timeout * 1000,
                      "frame-retention", (unsigned) viewer->frame_retention * 1000,
                      NULL);
    }

    arv_stream_set_emit_signals (viewer->stream, TRUE);
    payload = arv_camera_get_payload (viewer->camera, NULL);
    for (i = 0; i < n_buffers_streaming; i++)
        arv_stream_push_buffer (viewer->stream, arv_buffer_new (payload, NULL));

    set_camera_control_widgets(viewer);
    pixel_format = arv_camera_get_pixel_format (viewer->camera, NULL);

    caps_string = arv_pixel_format_to_gst_caps_string (pixel_format);
    if (caps_string == NULL) {
        g_message ("GStreamer cannot understand this camera pixel format: 0x%x!", (int) pixel_format);
        stop_video (viewer);
        return FALSE;
    } else if (g_str_has_prefix (caps_string, "video/x-bayer") && !has_bayer2rgb) {
        g_message ("GStreamer bayer plugin is required for pixel format: 0x%x!", (int) pixel_format);
        stop_video (viewer);
        return FALSE;
    }

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);
    arv_camera_start_acquisition (viewer->camera, NULL);

    viewer->pipeline = gst_pipeline_new ("pipeline");

    viewer->appsrc = gst_element_factory_make ("appsrc", NULL);
    videoconvert = gst_element_factory_make ("videoconvert", NULL);
    viewer->transform = gst_element_factory_make ("videoflip", NULL);

    gst_bin_add_many (GST_BIN (viewer->pipeline), viewer->appsrc, videoconvert, viewer->transform, NULL);

    if (g_str_has_prefix (caps_string, "video/x-bayer")) {
        GstElement *bayer2rgb;

        bayer2rgb = gst_element_factory_make ("bayer2rgb", NULL);
        gst_bin_add (GST_BIN (viewer->pipeline), bayer2rgb);
        gst_element_link_many (viewer->appsrc, bayer2rgb, videoconvert, viewer->transform, NULL);
    } else {
        gst_element_link_many (viewer->appsrc, videoconvert, viewer->transform, NULL);
    }

    if (has_gtksink || has_gtkglsink) {
        GtkWidget *video_widget;

#if 0 /* Disable glsink for now, it crashes when we come back to camera list with:
	(lt-arv-viewer:29151): Gdk-WARNING **: eglMakeCurrent failed
	(lt-arv-viewer:29151): Gdk-WARNING **: eglMakeCurrent failed
	(lt-arv-viewer:29151): Gdk-WARNING **: eglMakeCurrent failed
	Erreur de segmentation (core dumped)
	*/

        videosink = gst_element_factory_make ("gtkglsink", NULL);

		if (GST_IS_ELEMENT (videosink)) {
			GstElement *glupload;

			glupload = gst_element_factory_make ("glupload", NULL);
			gst_bin_add_many (GST_BIN (viewer->pipeline), glupload, videosink, NULL);
			gst_element_link_many (viewer->transform, glupload, videosink, NULL);
		} else {
#else
        {
#endif
            viewer->videosink = gst_element_factory_make ("gtksink", NULL);
            gst_bin_add_many (GST_BIN (viewer->pipeline), viewer->videosink, NULL);
            gst_element_link_many (viewer->transform, viewer->videosink, NULL);
        }

        g_object_get (viewer->videosink, "widget", &video_widget, NULL);
        gtk_container_add (GTK_CONTAINER (viewer->video_frame), video_widget);
        gtk_widget_show (video_widget);
        g_object_set(G_OBJECT (video_widget), "force-aspect-ratio", TRUE, NULL);
        gtk_widget_set_size_request (video_widget, 640, 480);
    } else {
        viewer->videosink = gst_element_factory_make ("autovideosink", NULL);
        gst_bin_add (GST_BIN (viewer->pipeline), viewer->videosink);
        gst_element_link_many (viewer->transform, viewer->videosink, NULL);
    }

    g_object_set(G_OBJECT (viewer->videosink), "sync", FALSE, NULL);

    caps = gst_caps_from_string (caps_string);
    arv_camera_get_region (viewer->camera, NULL, NULL, &width, &height, NULL);
    gst_caps_set_simple (caps,
                         "width", G_TYPE_INT, width,
                         "height", G_TYPE_INT, height,
                         "framerate", GST_TYPE_FRACTION, 0, 1,
                         NULL);
    gst_app_src_set_caps (GST_APP_SRC (viewer->appsrc), caps);
    gst_caps_unref (caps);

    g_object_set(G_OBJECT (viewer->appsrc), "format", GST_FORMAT_TIME, "is-live", TRUE, "do-timestamp", TRUE, NULL);

    if (!has_gtkglsink && !has_gtksink) {
        GstBus *bus;

        bus = gst_pipeline_get_bus (GST_PIPELINE (viewer->pipeline));
        gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, viewer, NULL);
        gst_object_unref (bus);
    }

    gst_element_set_state (viewer->pipeline, GST_STATE_PLAYING);

    viewer->last_status_bar_update_time_ms = g_get_real_time () / 1000;
    viewer->last_n_images = 0;
    viewer->last_n_bytes = 0;
    viewer->status_bar_update_event = g_timeout_add_seconds (1, update_status_bar_cb, viewer);

    viewer->new_buffer_available_video = g_signal_connect (viewer->stream, "new-buffer", G_CALLBACK (new_buffer_cb), viewer);

    // The time for the video snapshot update
    clock_gettime(CLOCK_MONOTONIC, &clock_last_snapshot_streaming);

    return TRUE;
}

void stop_video (LrdViewer *viewer)
{
    log_debug("Stopping the video stream.");

//    log_trace("    Stopping pipeline.");
    if (GST_IS_PIPELINE (viewer->pipeline))
        gst_element_set_state (viewer->pipeline, GST_STATE_NULL);

//    log_trace("    Stopping arv stream.");
    if (ARV_IS_STREAM (viewer->stream))
        arv_stream_set_emit_signals (viewer->stream, FALSE);

//    log_trace("    Destroying stream.");
    g_clear_object (&viewer->stream);
//    log_trace("    Destroying pipeline.");
    g_clear_object (&viewer->pipeline);

    viewer->appsrc = NULL;

//    log_trace("    Destroying last buffer.");
    g_clear_object (&viewer->last_buffer);

//    log_trace("    Stopping camera.");
    if (ARV_IS_CAMERA (viewer->camera))
        arv_camera_stop_acquisition (viewer->camera, NULL);

//    log_trace("    Removing widgets for each videoframe.");
    gtk_container_foreach (GTK_CONTAINER (viewer->video_frame), remove_widget, viewer->video_frame);

//    log_trace("    Resetting events.");
    if (viewer->status_bar_update_event > 0) {
        g_source_remove (viewer->status_bar_update_event);
        viewer->status_bar_update_event = 0;
    }

    if (viewer->exposure_update_event > 0) {
        g_source_remove (viewer->exposure_update_event);
        viewer->exposure_update_event = 0;
    }

    if (viewer->gain_update_event > 0) {
        g_source_remove (viewer->gain_update_event);
        viewer->gain_update_event = 0;
    }

    if (viewer->black_level_update_event > 0) {
        g_source_remove (viewer->black_level_update_event);
        viewer->black_level_update_event = 0;
    }
}

void stream_video_cb (void *user_data, ArvStreamCallbackType type, ArvBuffer *buffer)
{
    if (type == ARV_STREAM_CALLBACK_TYPE_INIT) {

        if (!arv_make_thread_high_priority (-10)) {
            log_warning ("Failed to make stream thread high priority");
        } else {
            log_trace ("Made stream thread high priority");
        }
//        if (!arv_make_thread_realtime (10)) {
//            log_warning ("Failed to make stream thread realtime. Try running as administrator.");
//        } else {
//            log_trace ("Made stream thread realtime");
//        }
    }
}

gboolean update_status_bar_cb (void *data)
{
    LrdViewer *viewer = data;
    char *text;
    gint64 time_ms = g_get_real_time () / 1000;
    gint64 elapsed_time_ms = time_ms - viewer->last_status_bar_update_time_ms;
    guint64 n_images = arv_stream_get_info_uint64_by_name (viewer->stream, "n_completed_buffers");
    guint64 n_bytes = arv_stream_get_info_uint64_by_name (viewer->stream, "n_transferred_bytes");
    guint64 n_errors = arv_stream_get_info_uint64_by_name (viewer->stream, "n_failures");

    if (elapsed_time_ms == 0)
        return TRUE;

    // FPS label
    text = g_strdup_printf ("%.1f fps (%.1f MB/s)",
                            1000.0 * (n_images - viewer->last_n_images) / elapsed_time_ms,
                            ((n_bytes - viewer->last_n_bytes) / 1000.0) / elapsed_time_ms);
    gtk_label_set_label (GTK_LABEL (viewer->fps_label), text);
    g_free (text);

    // ROI label
    text = NULL;
    if (viewer->show_roi) {
        text = g_strdup_printf("ROI: +%d +%d %dx%d", viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);
    }
    gtk_label_set_label (GTK_LABEL (viewer->roi_label), text);
    g_free (text);

    // IMAGES label
    text = g_strdup_printf ("%" G_GUINT64_FORMAT " image%s / %" G_GUINT64_FORMAT " error%s",
                            n_images, n_images > 0 ? "s" : "",
                            n_errors, n_errors > 0 ? "s" : "");
    gtk_label_set_label (GTK_LABEL (viewer->image_label), text);
    g_free (text);

    viewer->last_status_bar_update_time_ms = time_ms;
    viewer->last_n_images = n_images;
    viewer->last_n_bytes = n_bytes;

    return TRUE;
}
