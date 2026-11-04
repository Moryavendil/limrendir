#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <sys/stat.h>
#include <unistd.h>
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
unsigned n_buffers = 250;
// Memory allowed for buffers: enough to hold this many seconds of frames at the actual data rate
// (frame size x actual frame rate), so that the disk can stall that long without losing frames.
const double buffered_seconds = 3.;
// ... but never less than this (3 s of a GigE link, 118 MB/s), in bytes
const size_t min_mem_for_buffers = 3 * 118 * (1 << 20);
// ... and never more than this fraction of the physical memory of the computer
const double max_mem_for_buffers_fraction = 0.25;
// Number of FPS to display while recording (less is better)
gint display_fps_recording = 10;
// Time since last update
struct timespec clock_last_snapshot_recording, clock_now_recording;

// SAVE FILES MANAGEMENT
typedef enum {
    FILE_IMAGEFILE,
    FILE_STAMPFILE,
    FILE_METAFILE,
    FILE_GCVDIR
} FileType;
FILE *imageFile = NULL;
FILE *stampFile = NULL;
FILE *metaFile = NULL;

// combine to add dir to filename, shamelessly taken from stackoverflow
void combine(char* destination, const char* path1, const char* path2)
{
    if(path1 == NULL && path2 == NULL) {
        strcpy(destination, "");;
    }
    else if(path2 == NULL || strlen(path2) == 0) {
        strcpy(destination, path1);
    }
    else if(path1 == NULL || strlen(path1) == 0) {
        strcpy(destination, path2);
    }
    else {
        char directory_separator[] = "/";
#ifdef WIN32
        directory_separator[0] = '\\';
#endif
        const char *last_char = path1;
        while(*last_char != '\0')
            last_char++;
        int append_directory_separator = 0;
        if(strcmp(last_char, directory_separator) != 0) {
            append_directory_separator = 1;
        }
        strcpy(destination, path1);
        if(append_directory_separator)
            strcat(destination, directory_separator);
        strcat(destination, path2);
    }
}

void close_files();

// Opens one of the files of a .gcv dataset. On failure, tells the user and returns NULL.
static FILE *open_dataset_file(LrdViewer *viewer, const char *dirname, const char *filename) {
    char path[strlen(dirname) + strlen(filename) + 2];
    combine(path, dirname, filename);
    log_trace("%s path: %s", filename, path);

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        int const saved_errno = errno;
        log_error("Could not create the file '%s': %s", path, g_strerror(saved_errno));
        arv_viewer_show_notification(viewer, "Could not create the recording file", g_strerror(saved_errno));
    }
    return file;
}

// Creates the .gcv folder and its files. Returns FALSE (with everything closed) if this is not possible.
gboolean open_files(LrdViewer *viewer) {

    char *extension = ".gcv";
    size_t const filename_s = strlen(viewer->dataset_name) + strlen(extension) + 1;
    char *dirname = (char *) g_malloc(filename_s);
    snprintf(dirname, filename_s, "%s%s", viewer->dataset_name, extension);

    log_trace("dirpath: %s", dirname);

    // An already existing folder is fine (e.g. chosen in the dialog), anything else is an error
    if (mkdir(dirname, S_IRWXU | S_IRWXG | S_IRWXO) != 0 && errno != EEXIST) {
        int const saved_errno = errno;
        log_error("Could not create the recording folder '%s': %s", dirname, g_strerror(saved_errno));
        arv_viewer_show_notification(viewer, "Could not create the recording folder", g_strerror(saved_errno));
        g_free(dirname);
        return FALSE;
    }

    imageFile = open_dataset_file(viewer, dirname, "rawvideo.raw");
    if (imageFile != NULL)
        stampFile = open_dataset_file(viewer, dirname, "stamps.stamps");
    if (stampFile != NULL)
        metaFile = open_dataset_file(viewer, dirname, "metainfo.meta");
    g_free(dirname);

    if (imageFile == NULL || stampFile == NULL || metaFile == NULL) {
        close_files();
        return FALSE;
    }
    return TRUE;
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
    int x_offset, y_offset, width, height;
    arv_camera_get_region (viewer->camera, &x_offset, &y_offset, &width, &height, NULL);

    fprintf(metaFile, "cameraRegionX=%d\n", x_offset);
    fprintf(metaFile, "cameraRegionY=%d\n", y_offset);
    fprintf(metaFile, "cameraRegionWidth=%d\n", width);
    fprintf(metaFile, "cameraRegionHeight=%d\n", height);
    fprintf(metaFile, "usingROI=%s\n", viewer->show_roi ? "true" : "false");
    fprintf(metaFile, "viewerRegionX=%d\n", viewer->roi_x);
    fprintf(metaFile, "viewerRegionY=%d\n", viewer->roi_y);
    fprintf(metaFile, "viewerRegionWidth=%d\n", viewer->roi_w);
    fprintf(metaFile, "viewerRegionHeight=%d\n", viewer->roi_h);
    fprintf(metaFile, "captureCameraName=\"%s\"\n", arv_camera_get_model_name(viewer->camera, NULL));
    fprintf(metaFile, "pixelFormat=\"%s\"\n", arv_camera_get_pixel_format_as_string(viewer->camera, NULL));
    fprintf(metaFile, "captureFrequency=%f\n", get_actual_frame_rate(viewer));
    fprintf(metaFile, "autoExposureTime=%u\n", arv_camera_get_exposure_time_auto(viewer->camera, NULL));
    fprintf(metaFile, "captureExposureTime=%f\n", arv_camera_get_exposure_time(viewer->camera, NULL));
    fprintf(metaFile, "autoGain=%u\n", arv_camera_get_gain_auto(viewer->camera, NULL));
    fprintf(metaFile, "captureGain=%f\n", arv_camera_get_gain(viewer->camera, NULL));
    fprintf(metaFile, "autoBlackLevel=%u\n", arv_camera_get_black_level_auto(viewer->camera, NULL));
    fprintf(metaFile, "captureBlackLevel=%f\n", arv_camera_get_black_level(viewer->camera, NULL));
    fprintf(metaFile, "captureProg=\"Limrendir version %s\"\n", LIMRENDIR_VERSION);
    fprintf(metaFile, "xxxxx_legacy_xxxxx=0\n");
    fprintf(metaFile, "subRegionX=%d\n", x_offset);
    fprintf(metaFile, "subRegionY=%d\n", y_offset);
    fprintf(metaFile, "subRegionWidth=%d\n", width);
    fprintf(metaFile, "subRegionHeight=%d\n", height);
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

gboolean does_dir_exist(char *dirname) {
    if (dirname == NULL) { return FALSE; }

    DIR* dir = opendir(dirname);
    if (dir) {
        closedir(dir);
        return TRUE;
    } else if (ENOENT == errno) {
        /* Directory does not exist. */
        return FALSE;
    } else {
        /* opendir() failed for some other reason. */
        return TRUE;
    }
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
        log_debug("No name specified, we need to set one.");
        filename = obtain_new_original_filename(viewer);
    } else {
        filename = g_strdup(viewer->dataset_name);

        // This part is useless but la flemme de l'enlever
        char *extension = ".gcv";
        size_t const filename_s = strlen(viewer->dataset_name) + strlen(extension) + 1;
        char *dirname = (char *) g_malloc(filename_s);
        snprintf(dirname, filename_s, "%s%s", viewer->dataset_name, extension);

        if (does_file_exist(dirname) || does_dir_exist(dirname)) {
            log_debug("Name %s is taken, generating another one.", viewer->dataset_name);
            g_free(filename);
            filename = obtain_new_original_filename(viewer);
        } else {
            log_debug("Name %s seems to be free (nothing named %s).", viewer->dataset_name, dirname);
        }
        g_free(dirname);
    }

    log_debug("Asking for a name choice, default is %s.", filename);

    GtkFileFilter *filter;
    GtkFileFilter *filter_gcv;
    GtkFileFilter *filter_all;
    GtkWidget *dialog;
    char *path;

    dialog = gtk_file_chooser_dialog_new("Save Video", GTK_WINDOW (viewer->main_window),
//                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER,
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

    const gchar *raw_filter_name = "GevCapture style video (*.gcv)";
    filter_gcv = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_gcv, raw_filter_name);
//    gtk_file_filter_add_pattern(filter_gcv, "*.gcv");
//    gtk_file_filter_add_pattern(filter_all, "*.gcv");
    gtk_file_filter_add_pattern(filter_gcv, "*");
    gtk_file_filter_add_pattern(filter_all, "*");
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
        g_free(viewer->dataset_name);
        viewer->dataset_name = remove_ext(filename, '.', '/');


        const gchar *extension = strrchr(filename, '.');

        log_trace("\tFile name: %s", filename);
        log_trace("\tdataset name: %s", viewer->dataset_name);
        log_trace("\tExtension: %s", extension);

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
            // Remove the empty folder created with action GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER
            rmdir(filename);
        } else if (strcmp(extension, ".gcv") == 0) {
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
        g_free(filename); // 'extension' pointed into it
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
            log_warning("(X) Buffer unsuccessful: Buffer %d x %d = %d.", arv_buffer_get_image_width(buffer),
                        arv_buffer_get_image_height(buffer),
                        arv_buffer_get_image_width(buffer)*arv_buffer_get_image_height(buffer));
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
//    fprintf(stderr, "(X) Buffer unsuccessful: %s.\n", buffer_status_message);
    log_warning("(X) Buffer unsuccessful: %s.", buffer_status_message);

    return FALSE;
}

// Give max priority to the arv stream
void stream_record_cb (void *user_data, ArvStreamCallbackType type, ArvBuffer *buffer)
{
    if (type == ARV_STREAM_CALLBACK_TYPE_INIT) {

        if (!arv_make_thread_high_priority (-19)) {
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


// FRAME NUMBERING
// The stamps file contains a running frame number, starting at 1 for the first recorded frame,
// that keeps track of lost frames (gaps) and does not wrap around like the camera frame id may.
typedef struct {
    gboolean first_frame;   // TRUE until the first frame of the recording is numbered
    guint64 last_frame_id;  // camera frame id of the previous frame
    guint32 running_number; // running frame number of the previous frame
} FrameNumbering;

static void frame_numbering_reset (FrameNumbering *numbering) {
    numbering->first_frame = TRUE;
    numbering->last_frame_id = 0;
    numbering->running_number = 0;
}

static guint32 frame_numbering_next (FrameNumbering *numbering, guint64 frame_id) {
    // GigE Vision (v1) frame ids are 16 bits and wrap around, skipping 0
    const guint64 frame_id_modulus = 1 << 16;

    if (numbering->first_frame) {
        numbering->first_frame = FALSE;
        numbering->running_number = 1;
    } else if (frame_id < numbering->last_frame_id) {
        numbering->running_number += (frame_id + frame_id_modulus) - numbering->last_frame_id - 1; // ATTENTION AU -1 cf doc
    } else {
        numbering->running_number += frame_id - numbering->last_frame_id;
    }
    numbering->last_frame_id = frame_id;
    return numbering->running_number;
}

// Numbering of the frames of the current user-controlled recording (record_buffer)
static FrameNumbering record_numbering;


// RECORDING LIKE GEVCAPTURE

// The most important function here... This is the callback for the new buffer signal
static void record_buffer (ArvStream *stream, LrdViewer *viewer)
{
    ArvBuffer *buffer;
    gint n_input_buffers, n_output_buffers;

    guint32 running_number;

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
            fprintf(stderr, "More than half of the buffers are waiting to be read ! (%d/%u)\n", n_output_buffers, n_buffers);
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
            // The queue is empty, so there is nothing left to do.
            fprintf(stderr, "O\n");
            break;
        }

        // Check if the buffer was successful. If not, give it back and handle the next waiting ones.
        if (!is_buffer_successful(buffer)) {
            arv_stream_push_buffer(stream, buffer);
            continue;
        }

        running_number = frame_numbering_next(&record_numbering, arv_buffer_get_frame_id(buffer));

        // write stamps data
        fprintf(stampFile, "%u\t%" G_GUINT64_FORMAT "\t%ld\n",
                running_number,                     // running frame number, guint32
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

    const char *caps_string = get_display_caps_string (viewer);
    if (caps_string == NULL)
        return FALSE;

    create_display_pipeline (viewer, caps_string);
    add_display_sink (viewer);
    gst_element_link (viewer->transform, viewer->videosink);
    start_display_pipeline (viewer, caps_string);

    return TRUE;
}

// sets up the arv stream we are gonna record from
static gboolean setup_stream_for_gevcapture_style_recording(LrdViewer *viewer) {
    log_debug("Preparing to acquisition stream (aravis GigE Vision stream).");

    // The time for the video snapshot update
    clock_gettime(CLOCK_MONOTONIC, &clock_last_snapshot_recording);

    // Each recording numbers its frames from 1
    frame_numbering_reset(&record_numbering);

    // Files writing
    if (!open_files(viewer))
        return FALSE;

    write_meta(viewer);

    // Create the stream
    viewer->stream = create_camera_stream (viewer, stream_record_cb);
    if (viewer->stream == NULL) {
        close_files();
        return FALSE;
    }

    configure_stream (viewer);


    push_new_buffers (viewer, n_buffers);

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

// Links a pad to the "sink" pad of an element
static GstPadLinkReturn link_pad_to_element (GstPad *src_pad, GstElement *element) {
    GstPad *sink_pad = gst_element_get_static_pad (element, "sink");
    GstPadLinkReturn result = gst_pad_link (src_pad, sink_pad);
    gst_object_unref (sink_pad);
    return result;
}

// jhfdejhgfuyswfiuse
static gboolean setup_video_display_for_gstreamer_recording(LrdViewer *viewer) {

    g_autofree char *filename = have_video_filename(viewer, viewer->record_type);
    if (filename == NULL) {
        log_info("Could not get filename");
        return FALSE;
    }

    log_debug("Preparing to video display (gstreamer-style) recording.");
    gboolean link_success; // boolean to know if a link creation went ok

    const char *caps_string = get_display_caps_string (viewer);
    if (caps_string == NULL)
        return FALSE;

    create_display_pipeline (viewer, caps_string);

    // tee element
    GstElement *tee = gst_element_factory_make ("tee", NULL);
    gst_bin_add (GST_BIN (viewer->pipeline), tee);
    gst_element_link (viewer->transform, tee);


    // DISPLAY PIPELINE
    GstPad *tee_display_src_pad = gst_element_get_request_pad (tee, "src_%u");

    add_display_sink (viewer);

    // Fuck this line.
    // For some reason if we don't add it everything freezes .
    // It took me 3 hours to randomly discover the existence of this parameter.
    // I still have no idea why or how it works.
    // I just tried it out of despair.
    // It works.
    // But fuck it.
    g_object_set(G_OBJECT (viewer->videosink), "async", FALSE, NULL);

    link_success = link_pad_to_element (tee_display_src_pad, viewer->videosink);
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
            link_success = link_pad_to_element (tee_record_src_pad, videoconvert_rec);
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
            GstPad *mp4mux_sink_pad = gst_element_get_request_pad (mp4mux, "video_%u");
            filesink = gst_element_factory_make ("filesink", NULL);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), videoconvert_rec, queue, x264enc, mp4mux, filesink, NULL);
            // Linking the elements
            link_success = link_pad_to_element (tee_record_src_pad, videoconvert_rec);
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
            link_success = link_pad_to_element (tee_record_src_pad, videoconvert_rec);
            if (GST_PAD_LINK_FAILED (link_success)) {
                log_debug("Link from the tee to the videoconvert_rec for recording failed.");
                return FALSE;
            }
            gst_element_link_many (videoconvert_rec, avenc_ffv1, matroskamux, filesink, NULL);
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
            link_success = link_pad_to_element (tee_record_src_pad, videoconvert_rec);
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

    start_display_pipeline (viewer, caps_string);

    return TRUE;
}

static gboolean setup_stream_for_gstreamer_recording(LrdViewer *viewer) {
    log_debug("Preparing to acquisition stream (aravis GigE Vision stream).");

    // Create the stream
    viewer->stream = create_camera_stream (viewer, stream_record_cb);
    if (viewer->stream == NULL)
        return FALSE;

    configure_stream (viewer);


    push_new_buffers (viewer, n_buffers);

    g_signal_connect (viewer->stream, "new-buffer", G_CALLBACK (forward_buffer_to_gst), viewer);

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);

    arv_stream_set_emit_signals (viewer->stream, TRUE);

    arv_camera_start_acquisition (viewer->camera, NULL);

    return TRUE;
}

// RECORDING LIKE A BOURRIN


// sets up the arv stream we are gonna record from
// static gboolean bourrin_recording(LrdViewer *viewer) {
static gboolean bourrin_recording(void *data)
{
    LrdViewer *viewer = data;

    log_debug("Preparing the acquisition stream (aravis GigE Vision stream).");

    // The time for the video snapshot update
    clock_gettime(CLOCK_MONOTONIC, &clock_last_snapshot_recording);

    // Files writing
    if (!open_files(viewer)) {
        // Puts the interface back in its normal state
        stop_recording (viewer);
        gtk_widget_set_sensitive(viewer->help_button, TRUE);
        return FALSE;
    }

    write_meta(viewer);

    // Create the stream
    viewer->stream = create_camera_stream (viewer, stream_record_cb);
    if (viewer->stream == NULL) {
        // Closes the files and puts the interface back in its normal state
        stop_recording (viewer);
        gtk_widget_set_sensitive(viewer->help_button, TRUE);
        return FALSE;
    }

    configure_stream (viewer);

    push_new_buffers (viewer, n_buffers);

    arv_stream_set_emit_signals (viewer->stream, FALSE);

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);

    arv_camera_start_acquisition (viewer->camera, NULL);

    gint number_of_frames_to_record = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(viewer->burst_nframes_spinBox));

    ArvBuffer *buffer;

    FrameNumbering burst_numbering;
    frame_numbering_reset(&burst_numbering);
    guint32 running_number;

    uint8_t *image_data;
    size_t size;

    // Maximum time to wait for a frame before giving up: 10 s, or 10 frame periods if that is longer
    // (very low frame rates), so that a camera that stops sending cannot hang the application.
    double frame_rate = get_actual_frame_rate(viewer);
    guint64 pop_timeout_us = 10 * G_USEC_PER_SEC;
    if (frame_rate > 0 && 10. / frame_rate * G_USEC_PER_SEC > pop_timeout_us)
        pop_timeout_us = (guint64) (10. / frame_rate * G_USEC_PER_SEC);

    // Only successful frames count towards the number of frames to record
    int n_recorded_frames = 0;
    int n_failed_frames = 0;

    while (n_recorded_frames < number_of_frames_to_record) {

        buffer = arv_stream_timeout_pop_buffer(viewer->stream, pop_timeout_us);

        // Get the time of the grab
        clock_gettime(CLOCK_MONOTONIC, &clock_now_recording);

        // Check that we did indeed grab something
        if (buffer == NULL) {
            log_warning("No frame received for %.1f s: burst aborted after %d frames out of %d.",
                        (double) pop_timeout_us / G_USEC_PER_SEC, n_recorded_frames, number_of_frames_to_record);
            break;
        }

        // Check if the buffer was successful. If not, give it back: it does not count.
        if (!is_buffer_successful(buffer)) {
            n_failed_frames++;
            arv_stream_push_buffer(viewer->stream, buffer);
            continue;
        }

        running_number = frame_numbering_next(&burst_numbering, arv_buffer_get_frame_id(buffer));

        // write stamps data
        fprintf(stampFile, "%u\t%" G_GUINT64_FORMAT "\t%ld\n",
                running_number,                              // running frame number, guint32
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
        n_recorded_frames++;

        // Push back the buffer
        arv_stream_push_buffer(viewer->stream, buffer);
    }

    log_info("Burst: %d frames recorded (%d failed frames skipped).", n_recorded_frames, n_failed_frames);

    stop_recording (viewer);
    gtk_widget_set_sensitive(viewer->help_button, TRUE);

    return FALSE;
}

// START AND STOP RECORDING
void abort_recording(LrdViewer *viewer) {
    g_signal_handler_block (viewer->record_button, viewer->recording_button_changed);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->record_button), FALSE);
    g_signal_handler_unblock (viewer->record_button, viewer->recording_button_changed);
    g_clear_pointer(&viewer->dataset_name, g_free);
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

    // Each buffer holds a full frame as sent by the camera (the payload), whatever the ROI or the pixel format
    size_t buffer_size = arv_camera_get_payload (viewer->camera, NULL);
    if (buffer_size == 0) {
        log_warning("Could not get the frame size from the camera.");
        buffer_size = 1;
    }
    log_debug("Size of one buffer: %zu bytes (%zu kB)", buffer_size, buffer_size / (1 << 10));

    // Change the buffer size
    size_t mem_for_buffers = (size_t) (buffer_size * get_actual_frame_rate (viewer) * buffered_seconds);
    if (mem_for_buffers < min_mem_for_buffers)
        mem_for_buffers = min_mem_for_buffers;
    long n_pages = sysconf (_SC_PHYS_PAGES);
    long page_size = sysconf (_SC_PAGE_SIZE);
    if (n_pages > 0 && page_size > 0) {
        size_t max_mem_for_buffers = (size_t) ((double) n_pages * page_size * max_mem_for_buffers_fraction);
        if (mem_for_buffers > max_mem_for_buffers) {
            log_warning("Not enough memory to buffer %g s of recording: limited to %zu MB.",
                        buffered_seconds, max_mem_for_buffers / (1 << 20));
            mem_for_buffers = max_mem_for_buffers;
        }
    }
    log_debug("Size allowed for buffers: %zu bytes (%zu MB)", mem_for_buffers, mem_for_buffers / (1 << 20));
    n_buffers = mem_for_buffers / buffer_size;
    if (n_buffers < 10) {
        n_buffers = 10;
    }
    log_debug("Number of buffers: %u", n_buffers);
    log_debug("Size of all buffers stack: %zu bytes (%zu MB)", n_buffers * buffer_size, n_buffers * buffer_size / (1 << 20));

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

        set_video_controls_sensitive(viewer, FALSE);
        gtk_widget_set_sensitive(viewer->help_key_2, FALSE);
        gtk_widget_set_sensitive(viewer->help_description_2, FALSE);
        gtk_widget_set_sensitive(viewer->help_key_3, FALSE);
        gtk_widget_set_sensitive(viewer->help_description_3, FALSE);

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

                set_video_controls_sensitive(viewer, FALSE);
                gtk_widget_set_sensitive(viewer->record_button, FALSE);
                gtk_widget_set_sensitive(viewer->help_button, FALSE);


                g_timeout_add (500, bourrin_recording, viewer);
                return TRUE;
                break;
            default:
                log_info("Unsupported video format");

                abort_recording(viewer);
                return FALSE;
                break;
        }

    }

    log_warning("No recording mode selected.");
    abort_recording(viewer);
    return FALSE;
}

void stop_recording (LrdViewer *viewer) {
    log_trace("Ending the recording process...");

    if (ARV_IS_STREAM (viewer->stream))
        arv_stream_set_emit_signals (viewer->stream, FALSE);

    arv_camera_stop_acquisition (viewer->camera, NULL);

    // Destroying the stream stops its thread and waits for it. This guarantees that no frame
    // is still being written (record_buffer runs in that thread) when the files are closed below.
    g_clear_object (&viewer->stream);

    // send end of stream signal. Important for certain formats (mp4 for example).
    if (GST_IS_APP_SRC (viewer->appsrc)) {
        GstFlowReturn ret;
        g_signal_emit_by_name (GST_APP_SRC (viewer->appsrc), "end-of-stream", &ret);
    }

    // For video files written by GStreamer, wait for the end of stream to go through the whole pipeline
    // (encoder, muxer, file) before the pipeline is destroyed (in start_video below): the muxer finalizes
    // the file at that moment (e.g. the MP4 index), without it the file can be truncated or unreadable.
    gboolean gstreamer_recording = viewer->record_type == RECORDTYPE_FLV || viewer->record_type == RECORDTYPE_MP4 ||
                                   viewer->record_type == RECORDTYPE_MKV || viewer->record_type == RECORDTYPE_AVI;
    if (gstreamer_recording && GST_IS_PIPELINE (viewer->pipeline)) {
        const GstClockTime eos_timeout = 10 * GST_SECOND;
        log_debug("Waiting for the video file to be finalized...");
        GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (viewer->pipeline));
        GstMessage *message = gst_bus_timed_pop_filtered (bus, eos_timeout, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
        if (message == NULL) {
            log_warning("The video file was not finalized after %" G_GUINT64_FORMAT " s: it may be truncated.",
                        eos_timeout / GST_SECOND);
        } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
            GError *error = NULL;
            gst_message_parse_error (message, &error, NULL);
            log_warning("Error while finalizing the video file: %s", error->message);
            g_clear_error (&error);
        } else {
            log_debug("Video file finalized.");
        }
        g_clear_pointer (&message, gst_message_unref);
        gst_object_unref (bus);
    }

    close_files();

    log_info("End of recording.");

    set_video_controls_sensitive(viewer, TRUE);
    gtk_widget_set_sensitive(viewer->help_key_2, TRUE);
    gtk_widget_set_sensitive(viewer->help_description_2, TRUE);
    gtk_widget_set_sensitive(viewer->help_key_3, TRUE);
    gtk_widget_set_sensitive(viewer->help_description_3, TRUE);
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

    g_clear_pointer(&viewer->dataset_name, g_free);
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

