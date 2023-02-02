#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <sys/stat.h>
#include <arv.h>
#include <limrendir.h>
#include <arvgvstreamprivate.h>
#include <logroutines.h>


//// These are straight-up copy-pasted form gevCapture
///* return time from timespec-structure in milliseconds */
//long clock_millis(struct timespec t) {
//    return ((long) t.tv_sec) * 1000 + t.tv_nsec / 1000000;
//}
//
///* time difference in milliseconds */
//long clock_diff_millis(struct timespec t1, struct timespec t2) {
//    long d;
//    d = ((long) t1.tv_sec - (long) t2.tv_sec) * 1000
//        + (t1.tv_nsec - t2.tv_nsec) / 1000000;
//    return d;
//}

// RECORDING THINGS
// declare files
// Number of buffers in the buffer queue (more is better)
unsigned n_buffers = 100;
// Memory allowed for buffers, in bytes (approximately)
size_t mem_for_buffers = 200 * (1 << 20);
// Number of FPS to display while recording (less is better)
gint display_fps_recording = 10;
// Time since last update
struct timespec clock_last_snapshot_recording, clock_now_recording;

// SAVE FILES MANAGEMENT
typedef enum {
    FILE_IMAGEFILE,
    FILE_STAMPFILE,
    FILE_METAFILE
} FileType;
FILE *imageFile = NULL;
FILE *stampFile = NULL;
FILE *metaFile = NULL;

char* have_filename(LrdViewer *viewer, FileType file_type) {
    if (viewer->dataset_name == NULL) { return NULL; }

    char *extension = "";
    if (file_type == FILE_IMAGEFILE) {
        extension = ".raw";
    } else if (file_type == FILE_STAMPFILE) {
        extension = ".stamps";
    } else if (file_type == FILE_METAFILE) {
        extension = ".meta";
    }

    size_t const filename_s = strlen(viewer->dataset_name) + strlen(extension) + 1;
    char *filename = (char *) g_malloc(filename_s);
    snprintf(filename, filename_s, "%s%s", viewer->dataset_name, extension);

    return filename;
}

void open_files(LrdViewer *viewer) {
//    const char *imageExt = ".raw";
//    const char *stampExt = ".stamps";
//    const char *metaExt  = ".meta";
//
//
//    size_t const image_filename_s = strlen(imageExt) + strlen(dataset_name) + 1;
//    char *image_filename = (char *) g_malloc(image_filename_s);
//    snprintf(image_filename, image_filename_s, "%s%s", dataset_name, imageExt);
//
//    size_t const stamp_filename_s = strlen(stampExt) + strlen(dataset_name) + 1;
//    char *stamp_filename = (char *) g_malloc(stamp_filename_s);
//    snprintf(stamp_filename, stamp_filename_s, "%s%s", dataset_name, stampExt);
//
//    size_t const meta_filename_s = strlen(metaExt) + strlen(dataset_name) + 1;
//    char *meta_filename = (char *) g_malloc(meta_filename_s);
//    snprintf(meta_filename, meta_filename_s, "%s%s", dataset_name, metaExt);

    imageFile = fopen(have_filename(viewer, FILE_IMAGEFILE), "w");
    stampFile = fopen(have_filename(viewer, FILE_STAMPFILE), "w");
    metaFile = fopen(have_filename(viewer, FILE_METAFILE), "w");
}

void close_files() {
    if (imageFile != NULL)
        fclose(imageFile);
    if (stampFile != NULL)
        fclose(stampFile);
    if (metaFile != NULL)
        fclose(metaFile);
    imageFile = NULL;
    stampFile = NULL;
    metaFile = NULL;
}

void write_meta(LrdViewer *viewer) {

    fprintf(metaFile, "usingROI=%s\n", viewer->show_roi ? "true" : "false");
    fprintf(metaFile, "subRegionX=%d\n", viewer->roi_x);
    fprintf(metaFile, "subRegionY=%d\n", viewer->roi_y);
    fprintf(metaFile, "subRegionWidth=%d\n", viewer->roi_w);
    fprintf(metaFile, "subRegionHeight=%d\n", viewer->roi_h);
    fprintf(metaFile, "captureCameraName=\"%s\"\n", arv_camera_get_model_name(viewer->camera, NULL));
    fprintf(metaFile, "captureFrequency=%f\n", arv_camera_get_frame_rate(viewer->camera, NULL));
    fprintf(metaFile, "captureExposureTime=%f\n", arv_camera_get_exposure_time(viewer->camera, NULL));
    fprintf(metaFile, "captureProg=\"limrendir version %s\"\n", "lol who knows");
}

// FILENAME CONFIRMATION
char *remove_ext (char* myStr, char extSep, char pathSep) {
    char *retStr, *lastExt, *lastPath;

    // Error checks and allocate string.

    if (myStr == NULL) return NULL;
    if ((retStr = malloc (strlen (myStr) + 1)) == NULL) return NULL;

    // Make a copy and find the relevant characters.

    strcpy (retStr, myStr);
    lastExt = strrchr (retStr, extSep);
    lastPath = (pathSep == 0) ? NULL : strrchr (retStr, pathSep);

    // If it has an extension separator.

    if (lastExt != NULL) {
        // and it's to the right of the path separator.

        if (lastPath != NULL) {
            if (lastPath < lastExt) {
                // then remove it.

                *lastExt = '\0';
            }
        } else {
            // Has extension separator with no path separator.

            *lastExt = '\0';
        }
    }

    // Return the modified string.

    return retStr;
}

gboolean does_file_exist(char *filename) {
    if (filename == NULL) { return FALSE; }

    FILE *test_file = fopen(filename, "r");
    if (test_file)
    {
        fclose(test_file);
        return TRUE;
    }
    return FALSE;
}

char *obtain_new_original_filename(LrdViewer *viewer) {
    char *filename;
    GDateTime *date;
    char *date_string;
    date = g_date_time_new_now_local();
    date_string = g_date_time_format(date, "%Y-%m-%d-%H:%M:%S");
    filename = g_strdup_printf("%s-%s",
                               arv_camera_get_model_name(viewer->camera, NULL),
                               date_string);
    g_free(date_string);
    g_date_time_unref(date);
    return filename;
}

char* have_video_filename(LrdViewer *viewer, RecordingType recording_type) {
    if (viewer->dataset_name == NULL) { return NULL; }

    char *extension = "";
    switch (recording_type) {
        case RECORDTYPE_AVI:
            extension = ".avi";
            break;
        case RECORDTYPE_FLV:
            extension = ".flv";
            break;
        case RECORDTYPE_MKV:
            extension = ".mkv";
            break;
        case RECORDTYPE_MP4:
            extension = ".mp4";
            break;
        default:
            return NULL;
            break;
    }

    size_t const filename_s = strlen(viewer->dataset_name) + strlen(extension) + 1;
    char *filename = (char *) g_malloc(filename_s);
    snprintf(filename, filename_s, "%s%s", viewer->dataset_name, extension);

    return filename;
}

RecordingType confirm_dataset_name(LrdViewer *viewer)  {
    char *filename;
    if (viewer->dataset_name == NULL) {
        log_debug("No name specified, we need to set one.", viewer->dataset_name);
        filename = obtain_new_original_filename(viewer);
    } else {
        // Is this a valid copy method ?
        // Will it generate a segfault ?
        // Memory loss ?
        // Hatred from grumpy c programmers ?
        // Who knows ¯\_(ツ)_/¯
        filename = malloc (strlen(viewer->dataset_name)+1);
        memcpy(filename, viewer->dataset_name, strlen(viewer->dataset_name)+1);
        char *image_filename = have_filename(viewer, FILE_IMAGEFILE);
        if (does_file_exist(have_filename(viewer, FILE_IMAGEFILE))) {
            log_debug("Name %s is taken, generating another one.", viewer->dataset_name);
            filename = obtain_new_original_filename(viewer);
        } else {
            log_debug("Name %s seems to be free (no file named %s).", viewer->dataset_name, image_filename);
        }
        g_free(image_filename);
    }

    log_debug("Asking for a name choice, default is %s.", filename);

    GtkFileFilter *filter;
    GtkFileFilter *filter_gcv;
    GtkFileFilter *filter_all;
    GtkWidget *dialog;
    char *path;

    dialog = gtk_file_chooser_dialog_new("Save Video", GTK_WINDOW (viewer->main_window),
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         "_Cancel",
                                         GTK_RESPONSE_CANCEL,
                                         "_Save",
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER (dialog), TRUE);

    log_debug("The special video dir is %s", g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS));

    // Si le dossier existe deja, le mkdir echoue et osef.
    char* lrd_videos_path = NULL;
    if (g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS) != NULL) {
        lrd_videos_path = g_build_filename(g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS), "Limrendir-videos", NULL);
    } else {
        lrd_videos_path = g_build_filename(".", "Limrendir-videos", NULL);
    }
    mkdir(lrd_videos_path, S_IRWXU | S_IRWXG | S_IRWXO);

    log_debug("The saving dir is %s", lrd_videos_path);

    path = g_build_filename(lrd_videos_path, filename, NULL);
    g_free(lrd_videos_path);

    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (dialog), path);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (dialog), filename);

    const gchar *any_filter_name = "Supported video formats";
    filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, any_filter_name);

    const gchar *raw_filter_name = "GevCapture style video (*.raw)";
    filter_gcv = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_gcv, raw_filter_name);
    gtk_file_filter_add_pattern(filter_gcv, "*.raw");
    gtk_file_filter_add_pattern(filter_all, "*.raw");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter_gcv);

    const gchar *flv_filter_name = "FLV video (H264 encoding) (*.flv)";
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, flv_filter_name);
    gtk_file_filter_add_pattern(filter, "*.flv");
    gtk_file_filter_add_pattern(filter_all, "*.flv");
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton))) {
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter);
    }

    const gchar *avi_filter_name = "AVI video (*.avi)";
//    filter = gtk_file_filter_new();
//    gtk_file_filter_set_name(filter, avi_filter_name);
//    gtk_file_filter_add_pattern(filter, "*.avi");
//    gtk_file_filter_add_pattern(filter_all, "*.avi");
//    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter);

    const gchar *mkv_filter_name = "MKV video (*.mkv)";
//    filter = gtk_file_filter_new();
//    gtk_file_filter_set_name(filter, mkv_filter_name);
//    gtk_file_filter_add_pattern(filter, "*.mkv");
//    gtk_file_filter_add_pattern(filter_all, "*.mkv");
//    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter);

    const gchar *mp4_filter_name = "MP4 video (*.mp4)";
//    filter = gtk_file_filter_new();
//    gtk_file_filter_set_name(filter, mp4_filter_name);
//    gtk_file_filter_add_pattern(filter, "*.mp4");
//    gtk_file_filter_add_pattern(filter_all, "*.mp4");
//    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter);

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter_all);

    // The default filter
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER (dialog), filter_gcv);

    g_free(path);
    g_free(filename);

    gint result = gtk_dialog_run(GTK_DIALOG (dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        log_debug("Dialog accepted.");

        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
        viewer->dataset_name = remove_ext(filename, '.', '/');


        const gchar *extension = strrchr(filename, '.');

        log_trace("File name: %s", filename);
        log_trace("dataset name: %s", viewer->dataset_name);
        log_trace("Extension: %s", extension);

        // GTK3 file chooser does not allow for automatic modification of the extension when a different filter is selected
        // I don't know why and I think it is a shame. cf https://gitlab.gnome.org/GNOME/gtk/-/issues/4626
        // anyway, we have to do black-dirty magic here to make sure we get the extension right
        //
        // At the beginning, we set a filename with *no* estension.
        // If the file selected has an extension, then the user put it themselves.
        // Thus we trust that it is the extension they want.
        // If there is no extension (extension==NULL), we see if a filter have been set.
        RecordingType chosen_filetype = RECORDTYPE_NONE;
        if (extension == NULL) {
            const gchar *current_filter_name = gtk_file_filter_get_name (gtk_file_chooser_get_filter (GTK_FILE_CHOOSER (dialog)));
            log_trace("No extension. Current filter: %s", current_filter_name);
            if (strcmp(current_filter_name, raw_filter_name) == 0) {
                chosen_filetype = RECORDTYPE_GEVCAPTURE;
            } else if (strcmp(current_filter_name, avi_filter_name) == 0) {
                chosen_filetype = RECORDTYPE_AVI;
            } else if (strcmp(current_filter_name, flv_filter_name) == 0) {
                chosen_filetype = RECORDTYPE_FLV;
            } else if (strcmp(current_filter_name, mkv_filter_name) == 0) {
                chosen_filetype = RECORDTYPE_MKV;
            } else if (strcmp(current_filter_name, mp4_filter_name) == 0) {
                chosen_filetype = RECORDTYPE_MP4;
            } else {
                log_info("Video type wanted not understood. Aborting recording.");
                chosen_filetype = RECORDTYPE_NONE;
            }
        } else if (strcmp(extension, ".raw") == 0) {
            chosen_filetype = RECORDTYPE_GEVCAPTURE;
        } else if (strcmp(extension, ".avi") == 0) {
            chosen_filetype = RECORDTYPE_AVI;
        } else if (strcmp(extension, ".flv") == 0) {
            chosen_filetype = RECORDTYPE_FLV;
        } else if (strcmp(extension, ".mkv") == 0) {
            chosen_filetype = RECORDTYPE_MKV;
        } else if (strcmp(extension, ".mp4") == 0) {
            chosen_filetype = RECORDTYPE_MP4;
        } else {
            log_info("Unsupported video extension chosen : '%s'. Aborting recording.", extension);
            chosen_filetype = RECORDTYPE_NONE;
        }
        gtk_widget_destroy(dialog);
        return chosen_filetype;

    } else if (result == GTK_RESPONSE_CANCEL) {
        log_debug("Dialog cancelled.");
        gtk_widget_destroy(dialog);
        return RECORDTYPE_NONE;
    } else {
        log_debug("Dialog response is not accept nor cancel. Treating it as a cancel.");
        gtk_widget_destroy(dialog);
        return RECORDTYPE_NONE;
    }
}

// Arv buffer fail check (when recording, it is interesting to know the precise fail reason if a buffer is lost)
gboolean is_buffer_successful(ArvBuffer *buffer) {
    ArvBufferStatus const buffer_status = arv_buffer_get_status(buffer);
    if (buffer_status == ARV_BUFFER_STATUS_SUCCESS) {
        return TRUE;
    }
    // If we are here, the buffer is not successful. Why?
    char *buffer_status_message = NULL;
    switch (buffer_status) {
        case ARV_BUFFER_STATUS_UNKNOWN:
            buffer_status_message = "Unknown status (ARV_BUFFER_STATUS_UNKNOWN)";
            break;
        case ARV_BUFFER_STATUS_TIMEOUT:
            buffer_status_message = "Timeout was reached before all packets are received (ARV_BUFFER_STATUS_TIMEOUT)";
            break;
        case ARV_BUFFER_STATUS_MISSING_PACKETS:
            buffer_status_message = "stream has missing packets (ARV_BUFFER_STATUS_MISSING_PACKETS)";
            break;
        case ARV_BUFFER_STATUS_CLEARED:
            buffer_status_message = "The buffer is cleared";
            break;
        case ARV_BUFFER_STATUS_WRONG_PACKET_ID:
            buffer_status_message = "Stream has packet with wrong id";
            break;
        case ARV_BUFFER_STATUS_SIZE_MISMATCH:
            buffer_status_message = "The received image didn't fit in the buffer data space";
            break;
        case ARV_BUFFER_STATUS_FILLING:
            buffer_status_message = "The image is currently being filled";
            break;
        case ARV_BUFFER_STATUS_ABORTED:
            buffer_status_message =  "The filling was aborted before completion";
            break;
        default:
            buffer_status_message = "Unrepertoried buffer status";
            break;
    }
    fprintf(stderr, "(X) Buffer unsuccessful: %s.\n", buffer_status_message);
//    log_warning("(X) Buffer unsuccessful: %s.", buffer_status_message);

    return FALSE;
}

// Give max priority to the arv stream
void stream_record_cb (void *user_data, ArvStreamCallbackType type, ArvBuffer *buffer)
{
    if (type == ARV_STREAM_CALLBACK_TYPE_INIT) {

        if (!arv_make_thread_high_priority (-10)) {
            log_warning ("Failed to make stream thread high priority");
        } else {
            log_trace ("Made stream thread high priority");
        }
        if (!arv_make_thread_realtime (10)) {
            log_warning ("Failed to make stream thread realtime. Try running as administrator.");
        } else {
            log_trace ("Made stream thread realtime");
        }
    }
}


// RECORDING LIKE GEVCAPTURE

// The most important function here... This is the callback for the new buffer signal
static void record_buffer (ArvStream *stream, LrdViewer *viewer)
{
    ArvBuffer *buffer;
    gint n_input_buffers, n_output_buffers;

    guint32 frame_id;
    // remember previous frame id to detect overflow
    static guint32 old_frame_id = 0;
    // frame_id that does not wrap around (until guint32 itself overflows)
    static guint32 unwrapped_frame_id = 0;
    // wrapping modulus
    const guint32 frame_id_modulus = 1 << 16;

    uint8_t *image_data;
    size_t size;

    static gboolean show_this_frame = FALSE;

    arv_stream_get_n_buffers (stream, &n_input_buffers, &n_output_buffers);
    // n_input_buffers est le nom de buffers prets a etre ecrits (pushed).
    // n_output_buffers est le nombre de buffers prets à être lus (on peut les poper).
    // Idealement a ce stade, n_input_buffers = n_buffers - 1 et n_output_buffers = 1
//    if (n_input_buffers + n_output_buffers != n_buffers) {
//        log_trace("A buffer is stuck: %d \t(input queue) + %d \t(output queue) = %d \t!= %d)",
//                n_input_buffers, n_output_buffers, n_input_buffers + n_output_buffers, n_buffers);
//    }
    if (n_output_buffers == 0) {
        fprintf(stderr, "No buffer to output (wtf?) - There should be a '0': \n");
    } else if (n_output_buffers > 1) {
        fprintf(stderr, "Several buffers waiting to be saved. Computer time won't be reliable.\n");
        if (n_output_buffers > n_buffers/2) {
            fprintf(stderr, "More than half of the buffers are waiting to be read ! (%d/%d)\n", n_output_buffers, n_buffers);
        }
    }

    for (gint i_buffer = 0 ; i_buffer < n_output_buffers ; i_buffer += 1) {
        // Grab the buffer
        buffer = arv_stream_pop_buffer(stream);

        // Get the time of the grab
        clock_gettime(CLOCK_MONOTONIC, &clock_now_recording);

        // Check that we did indeed grab something
        if (buffer == NULL) {
            // For some reason, we were signaled a buffer but we could not retrieve one. Weird!
            fprintf(stderr, "O\n");
            return;
        }

        // Check if the buffer was successful
        if (!is_buffer_successful(buffer)) {
            arv_stream_push_buffer(stream, buffer);
            return;
        }

        // unwrap running frame number
        frame_id = arv_buffer_get_frame_id(buffer);
        if (unwrapped_frame_id == 0) {
            old_frame_id = frame_id - 1;
        }
        if (frame_id < old_frame_id)
            unwrapped_frame_id += (frame_id + frame_id_modulus) - old_frame_id - 1; // ATTENTION AU -1 cf doc
        else
            unwrapped_frame_id += frame_id - old_frame_id;
        old_frame_id = frame_id;

        // write stamps data
        fprintf(stampFile, "%d\t%ld\t%ld\n",
                unwrapped_frame_id,                 // running frame number, guint32
                arv_buffer_get_timestamp(buffer),   // camera time (ns), guint64
                clock_millis(clock_now_recording));        // computer time (ms)

        // write image data
        image_data = (uint8_t *) arv_buffer_get_data(buffer, &size);
        if (viewer->show_roi) {
            int width, height;
            arv_camera_get_region (viewer->camera, NULL, NULL, &width, &height, NULL);
            if (size == viewer->roi_w*viewer->roi_h) {
                // The roi is useless
                fwrite(image_data, 1, size, imageFile);
            } else if (width == viewer->roi_w) {
                // The roi is used to constrain only in y
                size_t offset = viewer->roi_y * width;
                fwrite(image_data + offset, 1, viewer->roi_w * viewer->roi_h, imageFile);
            } else {
                // general case
                for (int i_line = viewer->roi_y; i_line < viewer->roi_y + viewer->roi_h; i_line++) {
                    size_t offset = i_line * width + viewer->roi_x;
                    fwrite(image_data + offset, 1, viewer->roi_w, imageFile);
                }
            }
        } else {
            fwrite(image_data, 1, size, imageFile);
        }

        // Push back the buffer (in the right queue)
        if (clock_diff_millis(clock_now_recording, clock_last_snapshot_recording) * display_fps_recording > 1000) {
            clock_last_snapshot_recording = clock_now_recording;
            show_this_frame = TRUE;
        }
        if (show_this_frame) {
            g_clear_object(&viewer->last_buffer);
            viewer->last_buffer = g_object_ref(buffer);
            gst_app_src_push_buffer(GST_APP_SRC (viewer->appsrc), arv_to_gst_buffer(buffer, stream, viewer));
            show_this_frame = FALSE;
        } else {
            arv_stream_push_buffer(stream, buffer);
        }
    }
}

// sets up the display part (we want to see the video we're recording
static gboolean setup_video_display_for_gevcapture_style_recording(LrdViewer *viewer) {
    log_debug("Preparing to video display (gstreamer videostream).");
    GstElement *videoconvert;
    GstCaps *caps;
    gint width, height; // image geometry
    ArvPixelFormat pixel_format; // The pixel format used
    const char *caps_string;

    log_trace("    Retrieving pixel format.");
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

    log_trace("    Creating gstreamer pipeline.");
    viewer->pipeline = gst_pipeline_new ("pipeline");
    viewer->appsrc = gst_element_factory_make ("appsrc", NULL);
    videoconvert = gst_element_factory_make ("videoconvert", NULL);
    viewer->transform = gst_element_factory_make ("videoflip", NULL);

    log_trace("    Adding elements to the gstreamer pipeline.");
    gst_bin_add_many (GST_BIN (viewer->pipeline), viewer->appsrc, videoconvert, viewer->transform, NULL);

    if (g_str_has_prefix (caps_string, "video/x-bayer")) {
        log_trace("    The pixel format has prefix 'video/x-bayer', adding a bayer2rgb element to the pipeline.");
        GstElement *bayer2rgb;

        bayer2rgb = gst_element_factory_make ("bayer2rgb", NULL);
        gst_bin_add (GST_BIN (viewer->pipeline), bayer2rgb);
        gst_element_link_many (viewer->appsrc, bayer2rgb, videoconvert, viewer->transform, NULL);
    } else {
        gst_element_link_many (viewer->appsrc, videoconvert, viewer->transform, NULL);
    }

    if (has_gtksink || has_gtkglsink) {
        log_trace("    A video sink of type 'gtksink' is used.");
        GtkWidget *video_widget;
        viewer->videosink = gst_element_factory_make ("gtksink", NULL);
        gst_bin_add_many (GST_BIN (viewer->pipeline), viewer->videosink, NULL);
        gst_element_link_many (viewer->transform, viewer->videosink, NULL);

        g_object_get (viewer->videosink, "widget", &video_widget, NULL);
        gtk_container_add (GTK_CONTAINER (viewer->video_frame), video_widget);
        gtk_widget_show (video_widget);
        g_object_set(G_OBJECT (video_widget), "force-aspect-ratio", TRUE, NULL);
        gtk_widget_set_size_request (video_widget, 640, 480);
    } else {
        log_trace("    A video sink of type 'autovideosink'is used.");
        viewer->videosink = gst_element_factory_make ("autovideosink", NULL);
        gst_bin_add (GST_BIN (viewer->pipeline), viewer->videosink);
        gst_element_link_many (viewer->transform, viewer->videosink, NULL);
    }

    g_object_set(G_OBJECT (viewer->videosink), "sync", FALSE, NULL);

    log_trace("    Setting source caps.");
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

    log_trace("    Bus management.");
    if (!has_gtkglsink && !has_gtksink) {
        GstBus *bus;

        bus = gst_pipeline_get_bus (GST_PIPELINE (viewer->pipeline));
        gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, viewer, NULL);
        gst_object_unref (bus);
    }

    log_trace("    Pipeline in playing state.");
    gst_element_set_state (viewer->pipeline, GST_STATE_PLAYING);

    viewer->last_status_bar_update_time_ms = g_get_real_time () / 1000;
    viewer->last_n_images = 0;
    viewer->last_n_bytes = 0;
    viewer->status_bar_update_event = g_timeout_add_seconds (1, update_status_bar_cb, viewer);

    return TRUE;
}

// sets up the arv stream we are gonna record from
static gboolean setup_stream_for_gevcapture_style_recording(LrdViewer *viewer) {
    log_debug("Preparing to acquisition stream (aravis GigE Vision stream).");
    unsigned payload; // The size of the buffers

    // The time for the video snapshot update
    clock_gettime(CLOCK_MONOTONIC, &clock_last_snapshot_recording);

    // Files writing
    open_files(viewer);

    write_meta(viewer);

    // Create the stream
    viewer->stream = arv_camera_create_stream(viewer->camera, stream_record_cb, NULL, NULL);
    if (!ARV_IS_STREAM (viewer->stream)) {
        g_object_unref (viewer->camera);
        viewer->camera = NULL;
        return FALSE;
    }

    // Set some stream properties (buffer size, packet resent, timeouts, frame retention time)
    // only possible for GV (GigE Vision) streams. Notably, doesn't work for Aravis Fake Camera.
    if (ARV_IS_GV_STREAM (viewer->stream)) {

        log_debug("Auto socket buffer size: %s", viewer->auto_socket_buffer ? "yes" : "no");
        if (viewer->auto_socket_buffer) {
            g_object_set(viewer->stream,
                         "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
                         "socket-buffer-size", 0,
                         NULL);
        } else {
            log_info("Auto socket buffer size is off. You may want to try it on for better performances.");
        }

        log_debug("Packet resend: %s", viewer->packet_resend ? "yes" : "no");
        if (!viewer->packet_resend) {
            g_object_set (viewer->stream,
                          "packet-resend", ARV_GV_STREAM_PACKET_RESEND_NEVER,
                          NULL);
        }

        unsigned int initial_packet_timeout = (unsigned) (viewer->initial_packet_timeout * 1000);
        unsigned int packet_timeout = (unsigned) (viewer->packet_timeout * 1000);
        unsigned int frame_retention = (unsigned) (viewer->frame_retention * 1000);

        g_object_set (viewer->stream,
                      "initial-packet-timeout", initial_packet_timeout,
                      "packet-timeout", packet_timeout,
                      "frame-retention", frame_retention,
                      NULL);


        const char* default_initial_packet_timeout_message = g_strdup_printf("(default value is %u us)",
                                                                             ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT);
        const char* default_packet_timeout_message = g_strdup_printf("(default value is %u us)",
                                                                     ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT);
        const char* default_frame_retention_message = g_strdup_printf("(default value is %u us)",
                                                                      ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT);
        log_debug("Initial packet timeout: %u us %s", initial_packet_timeout,
                  initial_packet_timeout == ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT ? "(default)" : default_initial_packet_timeout_message);
        log_debug("Packet timeout: %u us %s", packet_timeout,
                  packet_timeout == ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT ? "(default)" : default_packet_timeout_message);
        log_debug("Frame retention: %u us %s", frame_retention,
                  frame_retention == ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT ? "(default)" : default_frame_retention_message);
    } else {
        log_info("This Aravis stream is not a GigE Vision stream. Some properties could not be set.");
    }

    // Add buffers
    payload = arv_camera_get_payload (viewer->camera, NULL);
    for (unsigned i = 0; i < n_buffers; i++) {
        arv_stream_push_buffer (viewer->stream, arv_buffer_new (payload, NULL));
    }

    g_signal_connect (viewer->stream, "new-buffer", G_CALLBACK (record_buffer), viewer);

    arv_stream_set_emit_signals (viewer->stream, TRUE);

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);

    arv_camera_start_acquisition (viewer->camera, NULL);

    return TRUE;
}


// RECORDING WITH PURE GSTREAMER

// the callback called each time an aravis buffer is available
static void forward_buffer_to_gst (ArvStream *stream, LrdViewer *viewer)
{
    ArvBuffer *arv_buffer;

    arv_buffer = arv_stream_pop_buffer (stream);
    if (arv_buffer == NULL)
        return;
    if (!is_buffer_successful(arv_buffer)) {
        arv_stream_push_buffer (stream, arv_buffer);
    } else {
        g_clear_object( &viewer->last_buffer );
        viewer->last_buffer = g_object_ref( arv_buffer );

        gst_app_src_push_buffer (GST_APP_SRC (viewer->appsrc), arv_to_gst_buffer (arv_buffer, stream, viewer));
    }
}

// jhfdejhgfuyswfiuse
static gboolean setup_video_display_for_gstreamer_recording(LrdViewer *viewer) {

    char *filename = have_video_filename(viewer, viewer->record_type);
    if (filename == NULL) {
        log_info("Could not get filename");
        return FALSE;
    }

    log_debug("Preparing to video display (gstreamer-style) recording.");
    GstElement *videoconvert;
    GstCaps *caps;
    gint width, height; // image geometry
    ArvPixelFormat pixel_format; // The pixel format used
    const char *caps_string; // the pixel format in string format
    gboolean link_success; // boolean to know if a link creation went ok

    log_trace("    Retrieving pixel format.");
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

    log_trace("    Creating gstreamer pipeline.");
    viewer->pipeline = gst_pipeline_new ("pipeline");
    viewer->appsrc = gst_element_factory_make ("appsrc", NULL);
    videoconvert = gst_element_factory_make ("videoconvert", NULL);
    viewer->transform = gst_element_factory_make ("videoflip", NULL);

    log_trace("    Adding elements to the gstreamer pipeline.");
    gst_bin_add_many (GST_BIN (viewer->pipeline), viewer->appsrc, videoconvert, viewer->transform, NULL);

    if (g_str_has_prefix (caps_string, "video/x-bayer")) {
        log_trace("    The pixel format has prefix 'video/x-bayer', adding a bayer2rgb element to the pipeline.");
        GstElement *bayer2rgb;

        bayer2rgb = gst_element_factory_make ("bayer2rgb", NULL);
        gst_bin_add (GST_BIN (viewer->pipeline), bayer2rgb);
        gst_element_link_many (viewer->appsrc, bayer2rgb, videoconvert, viewer->transform, NULL);
    } else {
        gst_element_link_many (viewer->appsrc, videoconvert, viewer->transform, NULL);
    }

    // tee element
    GstElement *tee = gst_element_factory_make ("tee", NULL);
    gst_bin_add (GST_BIN (viewer->pipeline), tee);
    gst_element_link (viewer->transform, tee);


    // DISPLAY PIPELINE
    GstPad *tee_display_src_pad = gst_element_get_request_pad (tee, "src_%u");

    if (has_gtksink || has_gtkglsink) {
        log_trace("    A video sink of type 'gtksink' is used.");
        viewer->videosink = gst_element_factory_make ("gtksink", NULL);
        // integrate the video in the window
        GtkWidget *video_widget;
        g_object_get (viewer->videosink, "widget", &video_widget, NULL);
        gtk_container_add (GTK_CONTAINER (viewer->video_frame), video_widget);
        gtk_widget_show (video_widget);
        g_object_set(G_OBJECT (video_widget), "force-aspect-ratio", TRUE, NULL);
        gtk_widget_set_size_request (video_widget, 640, 480);
    } else {
        log_trace("    A video sink of type 'autovideosink'is used.");
        viewer->videosink = gst_element_factory_make ("autovideosink", NULL);
    }
    g_object_set(G_OBJECT (viewer->videosink), "sync", FALSE, NULL);

    // Fuck this line.
    // For some reason if we don't add it everything freezes .
    // It took me 3 hours to randomly discover the existence of this parameter.
    // I still have no idea why or how it works.
    // I just tried it out of despair.
    // It works.
    // But fuck it.
    g_object_set(G_OBJECT (viewer->videosink), "async", FALSE, NULL);

    gst_bin_add (GST_BIN (viewer->pipeline), viewer->videosink);
    link_success = gst_pad_link (tee_display_src_pad, gst_element_get_static_pad (viewer->videosink, "sink"));
    if (GST_PAD_LINK_FAILED (link_success)) {
        log_debug("Link from the tee to the display video sink failed.");
        return FALSE;
    }

    gst_object_unref (GST_OBJECT (tee_display_src_pad));


    // RECORD PIPELINE
    // Todo: try also : multifilesink element to save every buffer in its own file ?
    GstPad *tee_record_src_pad = gst_element_get_request_pad (tee, "src_%u");
    GstElement *filesink;
    GstElement *videoconvert_rec = NULL;
    GstElement *queue = NULL;
    // codecs
    GstElement *x264enc = NULL;
    GstElement *omxh264enc = NULL;
    GstElement *h264parse = NULL;
    GstElement *xvidenc = NULL;
    GstElement *avenc_ffv1 = NULL;
    // filetypes
    GstElement *flvmux = NULL;
    GstElement *avimux = NULL;
    GstElement *mp4mux = NULL;
    GstElement *matroskamux = NULL;

    switch (viewer->record_type) {
        case RECORDTYPE_FLV:
            /*
             * FLV RECORDING
             *
             * Record pipeline:
             *  tee -> videoconvert -> x264enc -> flvmux -> filesink
             */
            videoconvert_rec = gst_element_factory_make ("videoconvert", NULL);
            x264enc = gst_element_factory_make ("x264enc", NULL);
            flvmux = gst_element_factory_make ("flvmux", NULL);
            GstPad *flvmux_sink_pad = gst_element_get_request_pad (flvmux, "video");
            filesink = gst_element_factory_make ("filesink", NULL);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), videoconvert_rec, x264enc, flvmux, filesink, NULL);
            // Linking the elements
            link_success = gst_pad_link (tee_record_src_pad, gst_element_get_static_pad (videoconvert_rec, "sink"));
            if (GST_PAD_LINK_FAILED (link_success)) {
                log_debug("Link from the tee to the videoconvert_rec for recording failed.");
                return FALSE;
            }
            gst_element_link_many (videoconvert_rec, x264enc, flvmux, filesink, NULL);
            // Getting rid of the sink pad
            gst_object_unref (GST_OBJECT (flvmux_sink_pad));
            break;
        case RECORDTYPE_MP4:
            log_warning("mp4 encoding is still experimental. This might not work");
            /*
             * MP4 RECORDING - NOT WORKING RIGHT NOW
             *
             * Record pipeline:
             *  tee -> videoconvert -> queue-> x264enc -> mp4mux -> filesink
             */
            videoconvert_rec = gst_element_factory_make ("videoconvert", NULL);
            queue = gst_element_factory_make ("queue", NULL);
            x264enc = gst_element_factory_make ("x264enc", NULL);
            mp4mux = gst_element_factory_make ("mp4mux", NULL);
            GstPad *mp4mux_sink_pad = gst_element_get_request_pad (flvmux, "video");
            filesink = gst_element_factory_make ("filesink", NULL);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), videoconvert_rec, queue, x264enc, mp4mux, filesink, NULL);
            // Linking the elements
            link_success = gst_pad_link (tee_record_src_pad, gst_element_get_static_pad (videoconvert_rec, "sink"));
            if (GST_PAD_LINK_FAILED (link_success)) {
                log_debug("Link from the tee to the videoconvert_rec for recording failed.");
                return FALSE;
            }
            gst_element_link_many (videoconvert_rec, queue, x264enc, mp4mux, filesink, NULL);
            // Getting rid of the sink pad
            gst_object_unref (GST_OBJECT (mp4mux_sink_pad));
            break;
        case RECORDTYPE_MKV:
            log_warning("mkv encoding is still experimental. This might not work");
            /*
             * MKV RECORDING
             *
             * Record pipeline:
             *  tee -> videoconvert -> avenc_ffv1 -> matroskamux -> filesink
             */
            videoconvert_rec = gst_element_factory_make ("videoconvert", NULL);
            avenc_ffv1 = gst_element_factory_make ("avenc_ffv1", NULL);
            matroskamux = gst_element_factory_make ("matroskamux", NULL);
            GstPad *matroskamux_sink_pad = gst_element_get_request_pad (matroskamux, "video_%u");
            filesink = gst_element_factory_make ("filesink", NULL);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), videoconvert_rec, avenc_ffv1, matroskamux, filesink, NULL);
            // Linking the elements
            link_success = gst_pad_link (tee_record_src_pad, gst_element_get_static_pad (videoconvert_rec, "sink"));
            if (GST_PAD_LINK_FAILED (link_success)) {
                log_debug("Link from the tee to the videoconvert_rec for recording failed.");
                return FALSE;
            }
            gst_element_link_many (videoconvert_rec, videoconvert_rec, avenc_ffv1, matroskamux, filesink, NULL);
            // Getting rid of the sink pad
            gst_object_unref (GST_OBJECT (matroskamux_sink_pad));
            break;
        case RECORDTYPE_AVI:
            log_warning("avi encoding is still experimental. This might not work");
            /*
             * AVI RECORDING
             *
             *
             * Record pipeline:
             *  tee -> videoconvert -> xvidenc -> avimux -> filesink
             */
            videoconvert_rec = gst_element_factory_make ("videoconvert", NULL);
            xvidenc = gst_element_factory_make ("xvidenc", NULL);
            avimux = gst_element_factory_make ("avimux", NULL);
            GstPad *avimux_sink_pad = gst_element_get_request_pad (avimux, "video_%u");
            filesink = gst_element_factory_make ("filesink", NULL);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), videoconvert_rec, xvidenc, avimux, filesink, NULL);
            // Linking the elements
            link_success = gst_pad_link (tee_record_src_pad, gst_element_get_static_pad (videoconvert_rec, "sink"));
            if (GST_PAD_LINK_FAILED (link_success)) {
                log_debug("Link from the tee to the videoconvert_rec for recording failed.");
                return FALSE;
            }
            gst_element_link_many (videoconvert_rec, xvidenc, avimux, filesink, NULL);
            // Getting rid of the sink pad
            gst_object_unref (GST_OBJECT (avimux_sink_pad));
            break;
        default:
            log_info("Unsupported video format.");
            return FALSE;
            break;
    }

    gst_object_unref (GST_OBJECT (tee_record_src_pad));


    log_trace("    Setting source caps.");
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

    log_trace("    Bus management.");
    if (!has_gtkglsink && !has_gtksink) {
        GstBus *bus;

        bus = gst_pipeline_get_bus (GST_PIPELINE (viewer->pipeline));
        gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler, viewer, NULL);
        gst_object_unref (bus);
    }

    log_trace("    Pipeline in playing state.");
    gst_element_set_state (viewer->pipeline, GST_STATE_PLAYING);

    viewer->last_status_bar_update_time_ms = g_get_real_time () / 1000;
    viewer->last_n_images = 0;
    viewer->last_n_bytes = 0;
    viewer->status_bar_update_event = g_timeout_add_seconds (1, update_status_bar_cb, viewer);

    return TRUE;
}

static gboolean setup_stream_for_gstreamer_recording(LrdViewer *viewer) {
    log_debug("Preparing to acquisition stream (aravis GigE Vision stream).");
    unsigned payload; // The size of the buffers

    // Create the stream
    viewer->stream = arv_camera_create_stream(viewer->camera, stream_record_cb, NULL, NULL);
    if (!ARV_IS_STREAM (viewer->stream)) {
        g_object_unref (viewer->camera);
        viewer->camera = NULL;
        return FALSE;
    }

    // Set some stream properties (buffer size, packet resent, timeouts, frame retention time)
    // only possible for GV (GigE Vision) streams. Notably, doesn't work for Aravis Fake Camera.
    if (ARV_IS_GV_STREAM (viewer->stream)) {

        log_debug("Auto socket buffer size: %s", viewer->auto_socket_buffer ? "yes" : "no");
        if (viewer->auto_socket_buffer) {
            g_object_set(viewer->stream,
                         "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
                         "socket-buffer-size", 0,
                         NULL);
        } else {
            log_info("Auto socket buffer size is off. You may want to try it on for better performances.");
        }

        log_debug("Packet resend: %s", viewer->packet_resend ? "yes" : "no");
        if (!viewer->packet_resend) {
            g_object_set (viewer->stream,
                          "packet-resend", ARV_GV_STREAM_PACKET_RESEND_NEVER,
                          NULL);
        }

        unsigned int initial_packet_timeout = (unsigned) (viewer->initial_packet_timeout * 1000);
        unsigned int packet_timeout = (unsigned) (viewer->packet_timeout * 1000);
        unsigned int frame_retention = (unsigned) (viewer->frame_retention * 1000);

        g_object_set (viewer->stream,
                      "initial-packet-timeout", initial_packet_timeout,
                      "packet-timeout", packet_timeout,
                      "frame-retention", frame_retention,
                      NULL);


        const char* default_initial_packet_timeout_message = g_strdup_printf("(default value is %u us)",
                                                                             ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT);
        const char* default_packet_timeout_message = g_strdup_printf("(default value is %u us)",
                                                                     ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT);
        const char* default_frame_retention_message = g_strdup_printf("(default value is %u us)",
                                                                      ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT);
        log_debug("Initial packet timeout: %u us %s", initial_packet_timeout,
                  initial_packet_timeout == ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT ? "(default)" : default_initial_packet_timeout_message);
        log_debug("Packet timeout: %u us %s", packet_timeout,
                  packet_timeout == ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT ? "(default)" : default_packet_timeout_message);
        log_debug("Frame retention: %u us %s", frame_retention,
                  frame_retention == ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT ? "(default)" : default_frame_retention_message);
    } else {
        log_info("This Aravis stream is not a GigE Vision stream. Some properties could not be set.");
    }

    // Add buffers
    payload = arv_camera_get_payload (viewer->camera, NULL);
    for (unsigned i = 0; i < n_buffers; i++) {
        arv_stream_push_buffer (viewer->stream, arv_buffer_new (payload, NULL));
    }

    g_signal_connect (viewer->stream, "new-buffer", G_CALLBACK (forward_buffer_to_gst), viewer);

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);

    arv_stream_set_emit_signals (viewer->stream, TRUE);

    arv_camera_start_acquisition (viewer->camera, NULL);

    return TRUE;
}

// RECORDING LIKE A BOURRIN


// sets up the arv stream we are gonna record from
//static gboolean bourrin_recording(LrdViewer *viewer) {
static gboolean bourrin_recording(void *data)
{
    LrdViewer *viewer = data;

    log_debug("Preparing to acquisition stream (aravis GigE Vision stream).");
    unsigned payload; // The size of the buffers

    // The time for the video snapshot update
    clock_gettime(CLOCK_MONOTONIC, &clock_last_snapshot_recording);

    // Files writing
    open_files(viewer);

    write_meta(viewer);

    // Create the stream
    viewer->stream = arv_camera_create_stream(viewer->camera, stream_record_cb, NULL, NULL);
    if (!ARV_IS_STREAM (viewer->stream)) {
        g_object_unref (viewer->camera);
        viewer->camera = NULL;
        return FALSE;
    }

    // Set some stream properties (buffer size, packet resent, timeouts, frame retention time)
    // only possible for GV (GigE Vision) streams. Notably, doesn't work for Aravis Fake Camera.
    if (ARV_IS_GV_STREAM (viewer->stream)) {

        log_debug("Auto socket buffer size: %s", viewer->auto_socket_buffer ? "yes" : "no");
        if (viewer->auto_socket_buffer) {
            g_object_set(viewer->stream,
                         "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
                         "socket-buffer-size", 0,
                         NULL);
        } else {
            log_info("Auto socket buffer size is off. You may want to try it on for better performances.");
        }

        log_debug("Packet resend: %s", viewer->packet_resend ? "yes" : "no");
        if (!viewer->packet_resend) {
            g_object_set (viewer->stream,
                          "packet-resend", ARV_GV_STREAM_PACKET_RESEND_NEVER,
                          NULL);
        }

        unsigned int initial_packet_timeout = (unsigned) (viewer->initial_packet_timeout * 1000);
        unsigned int packet_timeout = (unsigned) (viewer->packet_timeout * 1000);
        unsigned int frame_retention = (unsigned) (viewer->frame_retention * 1000);

        g_object_set (viewer->stream,
                      "initial-packet-timeout", initial_packet_timeout,
                      "packet-timeout", packet_timeout,
                      "frame-retention", frame_retention,
                      NULL);


        const char* default_initial_packet_timeout_message = g_strdup_printf("(default value is %u us)",
                                                                             ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT);
        const char* default_packet_timeout_message = g_strdup_printf("(default value is %u us)",
                                                                     ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT);
        const char* default_frame_retention_message = g_strdup_printf("(default value is %u us)",
                                                                      ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT);
        log_debug("Initial packet timeout: %u us %s", initial_packet_timeout,
                  initial_packet_timeout == ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT ? "(default)" : default_initial_packet_timeout_message);
        log_debug("Packet timeout: %u us %s", packet_timeout,
                  packet_timeout == ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT ? "(default)" : default_packet_timeout_message);
        log_debug("Frame retention: %u us %s", frame_retention,
                  frame_retention == ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT ? "(default)" : default_frame_retention_message);
    } else {
        log_info("This Aravis stream is not a GigE Vision stream. Some properties could not be set.");
    }

    // Add buffers
    payload = arv_camera_get_payload (viewer->camera, NULL);
    for (unsigned i = 0; i < n_buffers; i++) {
        arv_stream_push_buffer (viewer->stream, arv_buffer_new (payload, NULL));
    }

    arv_stream_set_emit_signals (viewer->stream, FALSE);

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);

    arv_camera_start_acquisition (viewer->camera, NULL);

    gint number_of_frames_to_record = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(viewer->burst_nframes_spinBox));

    ArvBuffer *buffer;

    guint32 frame_id;
    // remember previous frame id to detect overflow
    guint32 old_frame_id = 0;
    // frame_id that does not wrap around (until guint32 itself overflows)
    guint32 unwrapped_frame_id = 0;
    // wrapping modulus
    guint32 frame_id_modulus = 1 << 16;

    uint8_t *image_data;
    size_t size;

    gint n_input_buffers, n_output_buffers;

    gboolean record_this_buffer;

    for (int i_frame = 0 ; i_frame < number_of_frames_to_record ; i_frame++) {

        arv_stream_get_n_buffers (viewer->stream, &n_input_buffers, &n_output_buffers);
        if ( (log_level >= LOG_INFO) && (n_output_buffers > 1)) {
            fprintf(stderr, "Several buffers waiting to be saved. Computer time won't be reliable.\n");
        }
        if (n_output_buffers > n_buffers/2) {
            fprintf(stderr, "Buffer queue filled at %.2f %% (%d/%d)\n", ((float) n_output_buffers)/ ((float) n_buffers), n_output_buffers, n_buffers);
        }

        record_this_buffer = TRUE;

        buffer = arv_stream_pop_buffer(viewer->stream);

        // Get the time of the grab
        clock_gettime(CLOCK_MONOTONIC, &clock_now_recording);

        // Check that we did indeed grab something
        if (buffer == NULL) {
            fprintf(stderr, "O\n");
            record_this_buffer = FALSE;
        }

        // Check if the buffer was successful
        if (!is_buffer_successful(buffer)) {
            record_this_buffer = FALSE;
        }

        if (record_this_buffer) {

            // unwrap running frame number
            frame_id = arv_buffer_get_frame_id(buffer);
            if (unwrapped_frame_id == 0) {
                old_frame_id = frame_id - 1;
            }
            if (frame_id < old_frame_id)
                unwrapped_frame_id += (frame_id + frame_id_modulus) - old_frame_id - 1; // ATTENTION AU -1 cf doc
            else
                unwrapped_frame_id += frame_id - old_frame_id;
            old_frame_id = frame_id;

            // write stamps data
            fprintf(stampFile, "%d\t%ld\t%ld\n",
                    unwrapped_frame_id,                          // running frame number, guint32
                    arv_buffer_get_timestamp(buffer),            // camera time (ns), guint64
                    clock_millis(clock_now_recording));       // computer time (ms)

            // write image data
            image_data = (uint8_t *) arv_buffer_get_data(buffer, &size);
            if (viewer->show_roi) {
                int width, height;
                arv_camera_get_region(viewer->camera, NULL, NULL, &width, &height, NULL);
                if (size == viewer->roi_w * viewer->roi_h) {
                    // The roi is useless
                    fwrite(image_data, 1, size, imageFile);
                } else if (width == viewer->roi_w) {
                    // The roi is used to constrain only in y
                    size_t offset = viewer->roi_y * width;
                    fwrite(image_data + offset, 1, viewer->roi_w * viewer->roi_h, imageFile);
                } else {
                    // general case
                    for (int i_line = viewer->roi_y; i_line < viewer->roi_y + viewer->roi_h; i_line++) {
                        size_t offset = i_line * width + viewer->roi_x;
                        fwrite(image_data + offset, 1, viewer->roi_w, imageFile);
                    }
                }
            } else {
                fwrite(image_data, 1, size, imageFile);
            }
        }

        // Push back the buffer
        arv_stream_push_buffer(viewer->stream, buffer);
    }

    stop_recording (viewer);
    gtk_widget_set_sensitive(viewer->help_button, TRUE);

    return FALSE;
}

// START AND STOP RECORDING
void abort_recording(LrdViewer *viewer) {
    g_signal_handler_block (viewer->record_button, viewer->recording_button_changed);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->record_button), FALSE);
    g_signal_handler_unblock (viewer->record_button, viewer->recording_button_changed);
    viewer->dataset_name = NULL;
    viewer->record_type = RECORDTYPE_NONE;
}

gboolean start_recording (LrdViewer *viewer) {
    // Check that the recording button is pressed
    if (!is_record_button_active(viewer)) {
        g_signal_handler_block (viewer->record_button, viewer->recording_button_changed);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(viewer->record_button), TRUE);
        g_signal_handler_unblock (viewer->record_button, viewer->recording_button_changed);
    }

    // Check that we can record
    viewer->record_type = confirm_dataset_name(viewer);

    // Change the buffer size
    log_debug("Size allowed for buffers: %zu bytes (%zu kB)", mem_for_buffers, mem_for_buffers / (1 << 10));

    size_t buffer_size = 0;
    if (viewer->show_roi) {
        buffer_size = viewer->roi_w * viewer->roi_h;
    } else {
        int width, height;
        arv_camera_get_region (viewer->camera, NULL, NULL, &width, &height, NULL);
        buffer_size = width * height;
    }
    log_debug("Size of one buffer: %zu bytes (%zu kB)", buffer_size, buffer_size / (1 << 10));
    n_buffers = mem_for_buffers / buffer_size;
    if (n_buffers < 10) {
        n_buffers = 10;
    }
    log_debug("Number of buffers: %u", n_buffers);

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (viewer->recmode_usercontrolled_radiobutton))) {
        // user-controlled mode

        gboolean successful_launch = TRUE;
        switch (viewer->record_type) {
            case RECORDTYPE_NONE:
                abort_recording(viewer);
                return FALSE;
                break;
            case RECORDTYPE_GEVCAPTURE:
                stop_video(viewer);
                successful_launch *= setup_video_display_for_gevcapture_style_recording(viewer);
                if (successful_launch) {
                    successful_launch *= setup_stream_for_gevcapture_style_recording(viewer);
                }
                break;
            case RECORDTYPE_FLV:
                stop_video(viewer);
                successful_launch *= setup_video_display_for_gstreamer_recording(viewer);
                if (successful_launch) {
                    successful_launch *= setup_stream_for_gstreamer_recording(viewer);
                }
                break;
            default:
                log_info("Unsupported video format");

                abort_recording(viewer);
                return FALSE;
                break;
        }

        if (!successful_launch) {
            log_debug("Tried to launch a recording but that was unsuccessful.");

            abort_recording(viewer);
            start_video(viewer); // We need to restart the video because if we are here we stopped it
            return FALSE;
        }
        char *dataset_core_name = strrchr(viewer->dataset_name, '/');
        if (dataset_core_name == NULL)
            dataset_core_name = viewer->dataset_name;
        log_info("Beginning to record a video named '%s'", dataset_core_name);

        gtk_widget_set_sensitive(viewer->acquisition_button, FALSE);
        gtk_widget_set_sensitive(viewer->snapshot_button, FALSE);
        gtk_widget_set_sensitive(viewer->help_key_2, FALSE);
        gtk_widget_set_sensitive(viewer->help_description_2, FALSE);
        gtk_widget_set_sensitive(viewer->help_key_3, FALSE);
        gtk_widget_set_sensitive(viewer->help_description_3, FALSE);
        gtk_widget_set_sensitive(viewer->back_button, FALSE);
        gtk_widget_set_sensitive(viewer->rotate_cw_button, FALSE);
        gtk_widget_set_sensitive(viewer->flip_vertical_toggle, FALSE);
        gtk_widget_set_sensitive(viewer->flip_horizontal_toggle, FALSE);

        return successful_launch;
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton))) {
        // burst mode

        switch (viewer->record_type) {
            case RECORDTYPE_NONE:
                abort_recording(viewer);
                return FALSE;
                break;
            case RECORDTYPE_GEVCAPTURE:
                stop_video(viewer);

                char *dataset_core_name = strrchr(viewer->dataset_name, '/');
                if (dataset_core_name == NULL)
                    dataset_core_name = viewer->dataset_name;
                log_info("Beginning to record a video named '%s'", dataset_core_name);

                gtk_widget_set_sensitive(viewer->acquisition_button, FALSE);
                gtk_widget_set_sensitive(viewer->snapshot_button, FALSE);
                gtk_widget_set_sensitive(viewer->record_button, FALSE);
                gtk_widget_set_sensitive(viewer->back_button, FALSE);
                gtk_widget_set_sensitive(viewer->rotate_cw_button, FALSE);
                gtk_widget_set_sensitive(viewer->flip_vertical_toggle, FALSE);
                gtk_widget_set_sensitive(viewer->flip_horizontal_toggle, FALSE);

                gtk_widget_set_sensitive(viewer->help_button, FALSE);


                g_timeout_add (500, bourrin_recording, viewer);
                break;
            default:
                log_info("Unsupported video format");

                abort_recording(viewer);
                return FALSE;
                break;
        }

    }
}

gboolean stop_recording (LrdViewer *viewer) {
    log_trace("Ending the recording process...");

    if (ARV_IS_STREAM (viewer->stream))
        arv_stream_set_emit_signals (viewer->stream, FALSE);

    // send end of stream signal. Important for certain formats (mp4 for example).
    GstFlowReturn ret;
    g_signal_emit_by_name (GST_APP_SRC (viewer->appsrc), "end-of-stream", &ret);

//    if (GST_IS_PIPELINE (viewer->pipeline))
//        gst_element_set_state (viewer->pipeline, GST_STATE_NULL);

    arv_camera_stop_acquisition (viewer->camera, NULL);

    close_files();

    log_info("End of recording.");

    gtk_widget_set_sensitive (viewer->acquisition_button, TRUE);
    gtk_widget_set_sensitive (viewer->snapshot_button, TRUE);
    gtk_widget_set_sensitive(viewer->help_key_2, TRUE);
    gtk_widget_set_sensitive(viewer->help_description_2, TRUE);
    gtk_widget_set_sensitive(viewer->help_key_3, TRUE);
    gtk_widget_set_sensitive(viewer->help_description_3, TRUE);
    gtk_widget_set_sensitive(viewer->back_button, TRUE);
    gtk_widget_set_sensitive(viewer->rotate_cw_button, TRUE);
    gtk_widget_set_sensitive(viewer->flip_vertical_toggle, TRUE);
    gtk_widget_set_sensitive(viewer->flip_horizontal_toggle, TRUE);
    if (is_record_button_active(viewer)) {
        g_signal_handler_block (viewer->record_button, viewer->recording_button_changed);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(viewer->record_button), FALSE);
        g_signal_handler_unblock (viewer->record_button, viewer->recording_button_changed);
    }

    if (viewer->recording_snapshots_update_event > 0) {
        g_source_remove (viewer->recording_snapshots_update_event);
        viewer->recording_snapshots_update_event = 0;
    }


    // This will call stop_video, we have every reason to hope it will clean up all the remaining mess
    start_video (viewer);

    viewer->dataset_name = NULL;
    viewer->record_type = RECORDTYPE_NONE;
}



// MANAGE RECORDING STATE
gboolean is_record_button_active(LrdViewer *viewer) {
    return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(viewer->record_button));
}

void record_button_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    if (gtk_toggle_button_get_active (toggle)) {
        start_recording (viewer);
    } else {
        stop_recording (viewer);
    }
}

