#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arv.h>
#include <limrendir.h>
#include <arvgvstreamprivate.h>
#include <logroutines.h>
#include <limrendir_settings.h>
#include "limrendir_analysis.h"


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

// RECORDED REGION
// The part of the images that is recorded, decided when the recording starts (the ROI cannot change during it):
// the ROI if it is used, constrained to the field of view, else the whole field of view. A ROI equal to the whole
// field of view counts as no ROI.
typedef struct {
    gboolean using_roi;     // a ROI smaller than the field of view is used
    gboolean cropped;       // the recorded region is smaller than the field of view
    int fov_width, fov_height;
    int x, y, width, height;
} RecordedRegion;

static RecordedRegion recorded_region;

// With even_size (needed by H.264 4:2:0 in MP4), the width and height are rounded down to even numbers.
static void prepare_recorded_region (LrdViewer *viewer, gboolean even_size) {
    RecordedRegion *r = &recorded_region;
    arv_camera_get_region (viewer->camera, NULL, NULL, &r->fov_width, &r->fov_height, NULL);
    r->x = 0;
    r->y = 0;
    r->width = r->fov_width;
    r->height = r->fov_height;

    if (viewer->show_roi) {
        constrain_roi_to_field_of_view (viewer, r->fov_width, r->fov_height);
        r->x = viewer->roi_x;
        r->y = viewer->roi_y;
        r->width = viewer->roi_w;
        r->height = viewer->roi_h;
    }

    if (even_size) {
        // Shrink by one pixel if needed (keeping at least 2 pixels, moved back inside the field of view)
        if (r->width % 2 != 0 || r->height % 2 != 0)
            log_info("MP4 videos need an even width and height: the recorded region loses its last odd column / row.");
        r->width = MAX (r->width - r->width % 2, 2);
        r->height = MAX (r->height - r->height % 2, 2);
        r->x = MAX (MIN (r->x, r->fov_width - r->width), 0);
        r->y = MAX (MIN (r->y, r->fov_height - r->height), 0);
    }

    r->cropped = r->x != 0 || r->y != 0 || r->width != r->fov_width || r->height != r->fov_height;
    r->using_roi = viewer->show_roi && r->cropped;
    if (viewer->show_roi && !r->using_roi)
        log_debug("The ROI is the whole field of view: recording as without ROI.");
    log_debug("Recorded region: x: %d ; y: %d ; w: %d ; h: %d (field of view %dx%d, %s)", r->x, r->y, r->width, r->height,
              r->fov_width, r->fov_height, r->using_roi ? "ROI" : "no ROI");
}

// Writes the recorded region of a frame in the .gcv image file
static void write_recorded_region (FILE *file, ArvBuffer *buffer) {
    size_t size;
    const uint8_t *data = arv_buffer_get_data (buffer, &size);
    const RecordedRegion *r = &recorded_region;
    if (!r->cropped) {
        fwrite (data, 1, size, file);
        return;
    }

    gint buffer_width = 0, buffer_height = 0;
    arv_buffer_get_image_region (buffer, NULL, NULL, &buffer_width, &buffer_height);
    guint const bits_per_pixel = ARV_PIXEL_FORMAT_BIT_PER_PIXEL (arv_buffer_get_image_pixel_format (buffer));
    size_t const bpp = bits_per_pixel / 8;
    if (bits_per_pixel % 8 != 0 || bpp == 0 || buffer_width != r->fov_width || buffer_height != r->fov_height ||
        size < (size_t) buffer_width * buffer_height * bpp) {
        // Should not happen (the field of view cannot change during a recording): never read outside the frame
        static gboolean warned = FALSE;
        if (!warned)
            log_warning("Frame of unexpected size or format (%dx%d): the whole frame is recorded, not the ROI.",
                        buffer_width, buffer_height);
        warned = TRUE;
        fwrite (data, 1, size, file);
        return;
    }

    size_t const stride = (size_t) buffer_width * bpp;
    if (r->width == buffer_width) {
        // Whole rows: one block
        fwrite (data + r->y * stride, 1, r->height * stride, file);
    } else {
        for (int row = r->y; row < r->y + r->height; row++)
            fwrite (data + row * stride + r->x * bpp, 1, r->width * bpp, file);
    }
}

// Creates the .gcv folder of the recording (viewer->recording_path) and its files. Returns FALSE (with everything
// closed) if this is not possible.
gboolean open_files(LrdViewer *viewer) {
    const char *dirname = viewer->recording_path;
    log_trace("dirpath: %s", dirname);

    if (mkdir(dirname, S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
        int const saved_errno = errno;
        log_error("Could not create the recording folder '%s': %s", dirname, g_strerror(saved_errno));
        arv_viewer_show_notification(viewer, "Could not create the recording folder", g_strerror(saved_errno));
        return FALSE;
    }

    imageFile = open_dataset_file(viewer, dirname, "rawvideo.raw");
    if (imageFile != NULL)
        stampFile = open_dataset_file(viewer, dirname, "stamps.stamps");
    if (stampFile != NULL)
        metaFile = open_dataset_file(viewer, dirname, "metainfo.meta");

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
    // The recorded region (prepare_recorded_region), relative to the camera region
    fprintf(metaFile, "usingROI=%s\n", recorded_region.using_roi ? "true" : "false");
    fprintf(metaFile, "viewerRegionX=%d\n", recorded_region.x);
    fprintf(metaFile, "viewerRegionY=%d\n", recorded_region.y);
    fprintf(metaFile, "viewerRegionWidth=%d\n", recorded_region.width);
    fprintf(metaFile, "viewerRegionHeight=%d\n", recorded_region.height);
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

// RECORDING FILES

char *obtain_new_original_filename(LrdViewer *viewer) {
    char *filename;
    GDateTime *date;
    char *date_string;
    date = g_date_time_new_now_local();
    date_string = g_date_time_format(date, "%Y-%m-%d_%H-%M-%S");
    filename = g_strdup_printf("%s-%s",
                               arv_camera_get_model_name(viewer->camera, NULL),
                               date_string);
    g_free(date_string);
    g_date_time_unref(date);
    return filename;
}

// A recording is written in a temporary file (a folder for gcv) from the moment it starts. Once it is finished, the
// user chooses where to save it (save_recording) and the temporary file is moved there, or deleted if the user does
// not want to keep it. The temporary file is in the folder where videos are saved by default, with the default name
// followed by ".recording": a recording interrupted by a crash can be found there.

static const char *recording_extension (RecordingType recording_type) {
    switch (recording_type) {
        case RECORDTYPE_GEVCAPTURE: return ".gcv";
        case RECORDTYPE_AVI: return ".avi";
        case RECORDTYPE_FLV: return ".flv";
        case RECORDTYPE_MKV: return ".mkv";
        case RECORDTYPE_MP4: return ".mp4";
        default: return "";
    }
}

// The files of a .gcv folder
static const char *const gcv_file_names[] = {"rawvideo.raw", "stamps.stamps", "metainfo.meta"};

// The folder where videos are saved by default: the one of the last saved video, else Limrendir-videos in the user's
// videos folder (created if needed)
static char *default_videos_folder (void) {
    char *folder = settings_get_directory (SETTINGS_GROUP_VIDEO);
    if (folder == NULL) {
        const char *videos = g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS);
        folder = g_build_filename (videos != NULL ? videos : ".", "Limrendir-videos", NULL);
        g_mkdir_with_parents (folder, 0755);
    }
    return folder;
}

// A free path for the temporary file of a new recording named 'name', in the default videos folder if possible
// (saving the video there or on the same disk is then immediate), else in the user's cache folder
static char *new_temporary_recording_path (const char *name, RecordingType recording_type) {
    g_autofree char *folder = default_videos_folder ();
    if (g_access (folder, W_OK) != 0) {
        log_warning("Cannot write in the folder '%s': the recording is written in the cache folder until it is saved.",
                    folder);
        g_free (folder);
        folder = g_build_filename (g_get_user_cache_dir (), "limrendir", NULL);
        g_mkdir_with_parents (folder, 0700);
    }
    g_autofree char *base_name = g_path_get_basename (name);
    const char *extension = recording_extension (recording_type);
    for (int i = 1; ; i++) {
        g_autofree char *file_name = i == 1 ? g_strdup_printf ("%s.recording%s", base_name, extension)
                                            : g_strdup_printf ("%s-%d.recording%s", base_name, i, extension);
        char *path = g_build_filename (folder, file_name, NULL);
        if (!g_file_test (path, G_FILE_TEST_EXISTS))
            return path;
        g_free (path);
    }
}

// Deletes a recording: a video file, or a .gcv folder with its files
static void delete_recording (const char *path, RecordingType recording_type) {
    if (recording_type == RECORDTYPE_GEVCAPTURE) {
        for (size_t i = 0; i < G_N_ELEMENTS (gcv_file_names); i++) {
            g_autofree char *file = g_build_filename (path, gcv_file_names[i], NULL);
            g_remove (file);
        }
        if (g_rmdir (path) == 0 || errno == ENOENT)
            return;
    } else if (g_remove (path) == 0 || errno == ENOENT) {
        return;
    }
    log_warning("Could not delete the recording '%s': %s", path, g_strerror (errno));
}

// Deletes the temporary file of the current recording, which failed to start
static void discard_recording (LrdViewer *viewer) {
    if (viewer->recording_path != NULL) {
        log_debug("Deleting the recording file '%s'", viewer->recording_path);
        delete_recording (viewer->recording_path, viewer->record_type);
    }
    g_clear_pointer (&viewer->recording_path, g_free);
    g_clear_pointer (&viewer->recording_name, g_free);
}

// Moves a recording (a video file, or a .gcv folder) to 'to', replacing the file there if 'replace'.
// On another disk, the files are copied: this can take a while.
static gboolean move_recording (const char *from, const char *to, RecordingType recording_type, gboolean replace,
                                GError **error) {
    g_autoptr (GFile) source = g_file_new_for_path (from);
    g_autoptr (GFile) destination = g_file_new_for_path (to);
    GError *move_error = NULL;
    log_debug("Moving the recording '%s' to '%s'", from, to);
    if (g_file_move (source, destination, replace ? G_FILE_COPY_OVERWRITE : G_FILE_COPY_NONE, NULL, NULL, NULL,
                     &move_error))
        return TRUE;
    if (recording_type != RECORDTYPE_GEVCAPTURE || !g_error_matches (move_error, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE)) {
        g_propagate_error (error, move_error);
        return FALSE;
    }
    g_clear_error (&move_error);

    // A folder cannot be moved to another disk at once: its files are copied, then deleted once all are copied
    log_debug("The recording is on another disk: copying its files.");
    if (g_mkdir (to, 0755) != 0) {
        int const saved_errno = errno;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno), "Could not create the folder '%s': %s",
                     to, g_strerror (saved_errno));
        return FALSE;
    }
    for (size_t i = 0; i < G_N_ELEMENTS (gcv_file_names); i++) {
        g_autoptr (GFile) file_from = g_file_get_child (source, gcv_file_names[i]);
        g_autoptr (GFile) file_to = g_file_get_child (destination, gcv_file_names[i]);
        if (!g_file_copy (file_from, file_to, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) {
            delete_recording (to, RECORDTYPE_GEVCAPTURE);
            return FALSE;
        }
    }
    delete_recording (from, RECORDTYPE_GEVCAPTURE);
    return TRUE;
}

// Asks a question with a button for "no" and one for "yes" (destructive: red if 'destructive'). Returns TRUE for yes.
static gboolean ask_question (GtkWindow *parent, const char *question, const char *details,
                              const char *no_label, const char *yes_label, gboolean destructive) {
    GtkWidget *message = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", question);
    if (details != NULL)
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message), "%s", details);
    gtk_dialog_add_buttons (GTK_DIALOG (message), no_label, GTK_RESPONSE_CANCEL, yes_label, GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (message), GTK_RESPONSE_CANCEL);
    if (destructive) {
        GtkWidget *yes_button = gtk_dialog_get_widget_for_response (GTK_DIALOG (message), GTK_RESPONSE_ACCEPT);
        gtk_style_context_add_class (gtk_widget_get_style_context (yes_button), GTK_STYLE_CLASS_DESTRUCTIVE_ACTION);
    }
    gint const response = gtk_dialog_run (GTK_DIALOG (message));
    gtk_widget_destroy (message);
    return response == GTK_RESPONSE_ACCEPT;
}

static void show_error (GtkWindow *parent, const char *error, const char *details) {
    GtkWidget *message = gtk_message_dialog_new (parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error);
    if (details != NULL)
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message), "%s", details);
    gtk_dialog_run (GTK_DIALOG (message));
    gtk_widget_destroy (message);
}

// Whether 'filename' ends with 'extension' (whatever its case)
static gboolean has_extension (const char *filename, const char *extension) {
    size_t const length = strlen (filename), extension_length = strlen (extension);
    return length > extension_length && g_ascii_strcasecmp (filename + length - extension_length, extension) == 0;
}

// Asks the user where to save the recording that just ended (written in 'path'), and moves it there. 'name' is the
// name proposed, with or without a folder. If the user does not want to save it, the recording is deleted, after a
// confirmation.
static void save_recording (LrdViewer *viewer, const char *path, RecordingType recording_type, const char *name) {
    const char *extension = recording_extension (recording_type);

    GtkWidget *dialog = gtk_file_chooser_dialog_new ("Save Video", GTK_WINDOW (viewer->main_window),
                                                     GTK_FILE_CHOOSER_ACTION_SAVE,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Save", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
    gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);

    // A name with a folder (command line option) opens the dialog in that folder, if it exists
    g_autofree char *folder = NULL;
    if (strchr (name, '/') != NULL) {
        g_autofree char *name_folder = g_path_get_dirname (name);
        folder = g_canonicalize_filename (name_folder, NULL);
        if (!g_file_test (folder, G_FILE_TEST_IS_DIR))
            g_clear_pointer (&folder, g_free);
    }
    if (folder == NULL)
        folder = default_videos_folder ();
    g_autofree char *base_name = g_path_get_basename (name);
    g_autofree char *proposed_name = has_extension (base_name, extension) ? g_strdup (base_name)
                                                                          : g_strconcat (base_name, extension, NULL);
    gtk_file_chooser_set_current_folder (chooser, folder);
    gtk_file_chooser_set_current_name (chooser, proposed_name);

    GtkFileFilter *filter = gtk_file_filter_new ();
    g_autofree char *pattern = g_strconcat ("*", extension, NULL);
    switch (recording_type) {
        case RECORDTYPE_GEVCAPTURE: gtk_file_filter_set_name (filter, "GevCapture video (*.gcv)"); break;
        case RECORDTYPE_MKV: gtk_file_filter_set_name (filter, "MKV video (lossless FFV1) (*.mkv)"); break;
        case RECORDTYPE_MP4: gtk_file_filter_set_name (filter, "MP4 video (H.264) (*.mp4)"); break;
        default: gtk_file_filter_set_name (filter, pattern); break;
    }
    gtk_file_filter_add_pattern (filter, pattern);
    gtk_file_chooser_add_filter (chooser, filter);

    while (TRUE) {
        if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_ACCEPT) {
            if (ask_question (GTK_WINDOW (dialog), "Delete the video that was just recorded?",
                              "It has not been saved.", "_Back", "_Delete", TRUE)) {
                delete_recording (path, recording_type);
                log_info("The video was not saved: deleted.");
                break;
            }
            continue;
        }

        g_autofree char *filename = gtk_file_chooser_get_filename (chooser);
        if (filename == NULL)
            continue;
        // The extension of the format is added if missing: the dialog did not check this name
        gboolean const extension_added = !has_extension (filename, extension);
        if (extension_added) {
            char *filename_with_extension = g_strconcat (filename, extension, NULL);
            g_free (filename);
            filename = filename_with_extension;
        }
        g_autofree char *basename = g_path_get_basename (filename);

        gboolean const exists = g_file_test (filename, G_FILE_TEST_EXISTS);
        if (exists && (recording_type == RECORDTYPE_GEVCAPTURE || g_file_test (filename, G_FILE_TEST_IS_DIR))) {
            // Never replace a folder
            g_autofree char *error = g_strdup_printf ("\"%s\" already exists.", basename);
            show_error (GTK_WINDOW (dialog), error, "Choose another name.");
            gtk_file_chooser_set_current_name (chooser, basename);
            continue;
        }
        if (exists && extension_added) {
            g_autofree char *question = g_strdup_printf ("A file named \"%s\" already exists. Do you want to replace it?",
                                                         basename);
            if (!ask_question (GTK_WINDOW (dialog), question, "Replacing it will overwrite its contents.",
                               "_Cancel", "_Replace", TRUE)) {
                gtk_file_chooser_set_current_name (chooser, basename);
                continue;
            }
        }

        g_autoptr (GError) error = NULL;
        if (!move_recording (path, filename, recording_type, exists, &error)) {
            log_warning("Could not save the video as '%s': %s", filename, error->message);
            show_error (GTK_WINDOW (dialog), "Could not save the video", error->message);
            continue;
        }
        settings_set_directory_of (SETTINGS_GROUP_VIDEO, filename);
        log_info("Video saved: '%s'", filename);
        break;
    }

    gtk_widget_destroy (dialog);
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

        // As high as possible: the recording must not lose frames. If -19 is refused (usual without privileges),
        // at least -10, so that the recording stream thread is never less favored than the video one.
        if (arv_make_thread_high_priority (-19)) {
            log_trace ("Made stream thread high priority (nice -19)");
        } else if (arv_make_thread_high_priority (-10)) {
            log_debug ("Stream thread priority: nice -19 refused, nice -10 used.");
            log_info ("The recording stream thread could get a higher priority with higher privileges.");
        } else {
            log_warning ("Failed to make stream thread high priority");
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
        if ((unsigned) n_output_buffers > n_buffers/2) {
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

        // write image data (the ROI only, if one is used)
        write_recorded_region(imageFile, buffer);

        // Push back the buffer (in the right queue)
        if (clock_diff_millis(clock_now_recording, clock_last_snapshot_recording) * display_fps_recording > 1000) {
            clock_last_snapshot_recording = clock_now_recording;
            show_this_frame = TRUE;
        }
        // (if the display holds too many buffers, the frame is not shown and the next one will be)
        if (show_this_frame && can_display_buffer (viewer, stream)) {
            remember_displayed_frame (viewer, buffer);
            GstBuffer *gst_buffer = arv_to_gst_buffer (buffer, stream, viewer);
            // Zebra / peaking: a boolean check when off, a non-blocking hand-off otherwise
            if (g_atomic_int_get (&viewer->analysis_active))
                analysis_offer_frame (viewer, gst_buffer, TRUE);
            gst_app_src_push_buffer(GST_APP_SRC (viewer->appsrc), gst_buffer);
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
    start_display_pipeline (viewer, caps_string, 0, 1);

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

    prepare_recorded_region(viewer, FALSE);
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

// MP4 encoding settings. The default is the most widely readable video format: H.264, 4:2:0, 8 bits, in MP4.
// The fast 'superfast' preset is needed to keep up with high frame rates (x264's default preset cannot follow
// 156 fps at 1920x1200), and a constant quality (CRF) of 18 gives a visually lossless video.
typedef struct {
    const char *raw_format;    // pixel format given to the encoder (I420 = 4:2:0, readable by all players)
    const char *speed_preset;  // x264 speed preset
    guint quality;             // x264 constant quality (CRF): 0 = lossless, 18 = visually lossless, 23 = x264 default
} Mp4EncodingSettings;

static const Mp4EncodingSettings mp4_encoding_settings = {"I420", "superfast", 18};

// MP4 and MKV videos are played at this fixed frame rate, whatever the capture frame rate: the video is slowed down
// (or sped up). The capture frame rate and the slowdown factor are written in the metadata of the file.
static const gint mp4_playback_fps = 30;

// Whether the video file of this type is played at mp4_playback_fps, with the recording settings in its metadata
static gboolean has_fixed_playback_rate (RecordingType record_type) {
    return record_type == RECORDTYPE_MP4 || record_type == RECORDTYPE_MKV;
}

// State of the current GStreamer (MP4/MKV/FLV) recording. Written by forward_buffer_to_gst (stream thread),
// read once the stream is stopped.
static FrameNumbering gst_record_numbering;
static guint64 gst_n_recorded_frames;
static guint32 gst_last_running_number;
static guint64 gst_first_timestamp_ns, gst_last_timestamp_ns;
static double gst_capture_frame_rate;   // actual frame rate of the camera when the recording started

// Description of the recording settings, as "key=value" lines (same names as in the .gcv metainfo.meta file).
// With 'final', also the number of frames recorded / lost and the frame rate measured from the camera timestamps.
static char *recording_metadata_text (LrdViewer *viewer, gboolean final) {
    gint x, y, width, height, binning_x, binning_y;
    arv_camera_get_region (viewer->camera, &x, &y, &width, &height, NULL);
    arv_camera_get_binning (viewer->camera, &binning_x, &binning_y, NULL);

    GString *text = g_string_new (NULL);
    g_string_append_printf (text, "captureCameraVendor=%s\n", arv_camera_get_vendor_name (viewer->camera, NULL));
    g_string_append_printf (text, "captureCameraName=%s\n", arv_camera_get_model_name (viewer->camera, NULL));
    g_string_append_printf (text, "captureCameraSerial=%s\n", arv_camera_get_device_serial_number (viewer->camera, NULL));
    g_string_append_printf (text, "pixelFormat=%s\n", arv_camera_get_pixel_format_as_string (viewer->camera, NULL));
    g_string_append_printf (text, "cameraRegionX=%d\ncameraRegionY=%d\ncameraRegionWidth=%d\ncameraRegionHeight=%d\n",
                            x, y, width, height);
    g_string_append_printf (text, "binningX=%d\nbinningY=%d\n", binning_x, binning_y);
    // The recorded region (prepare_recorded_region), relative to the camera region
    g_string_append_printf (text, "usingROI=%s\n", recorded_region.using_roi ? "true" : "false");
    g_string_append_printf (text, "viewerRegionX=%d\nviewerRegionY=%d\nviewerRegionWidth=%d\nviewerRegionHeight=%d\n",
                            recorded_region.x, recorded_region.y, recorded_region.width, recorded_region.height);
    g_string_append_printf (text, "captureFrequency=%f\n", gst_capture_frame_rate);
    g_string_append_printf (text, "playbackFrequency=%d\n", mp4_playback_fps);
    g_string_append_printf (text, "slowdownFactor=%f\n", gst_capture_frame_rate / mp4_playback_fps);
    g_string_append_printf (text, "autoExposureTime=%u\n", arv_camera_get_exposure_time_auto (viewer->camera, NULL));
    g_string_append_printf (text, "captureExposureTime=%f\n", arv_camera_get_exposure_time (viewer->camera, NULL));
    g_string_append_printf (text, "autoGain=%u\n", arv_camera_get_gain_auto (viewer->camera, NULL));
    g_string_append_printf (text, "captureGain=%f\n", arv_camera_get_gain (viewer->camera, NULL));
    g_string_append_printf (text, "autoBlackLevel=%u\n", arv_camera_get_black_level_auto (viewer->camera, NULL));
    g_string_append_printf (text, "captureBlackLevel=%f\n", arv_camera_get_black_level (viewer->camera, NULL));
    g_string_append_printf (text, "captureProg=Limrendir version %s\n", LIMRENDIR_VERSION);
    if (final) {
        g_string_append_printf (text, "recordedFrames=%" G_GUINT64_FORMAT "\n", gst_n_recorded_frames);
        guint64 n_lost = gst_last_running_number > gst_n_recorded_frames ? gst_last_running_number - gst_n_recorded_frames : 0;
        g_string_append_printf (text, "lostFrames=%" G_GUINT64_FORMAT "\n", n_lost);
        if (gst_last_running_number > 1 && gst_last_timestamp_ns > gst_first_timestamp_ns)
            g_string_append_printf (text, "measuredCaptureFrequency=%f\n", (gst_last_running_number - 1) /
                                    ((double) (gst_last_timestamp_ns - gst_first_timestamp_ns) / 1e9));
    }
    return g_string_free (text, FALSE);
}

// Writes the recording settings in the metadata (tags) of the MP4 / MKV file. Called when the recording starts, and
// again when it stops ('final', with the frame counts): the muxers write the metadata at the end of the file.
static void set_video_file_tags (LrdViewer *viewer, GstElement *muxer, gboolean final) {
    double slowdown = gst_capture_frame_rate / mp4_playback_fps;
    g_autofree char *description = g_strdup_printf ("Recorded at %.2f fps, played at %d fps (%.2fx %s)",
                                                    gst_capture_frame_rate, mp4_playback_fps,
                                                    slowdown >= 1 ? slowdown : 1 / slowdown,
                                                    slowdown >= 1 ? "slower" : "faster");
    // The comment is shown by most players: it starts with the description, then the "key=value" settings
    g_autofree char *settings = recording_metadata_text (viewer, final);
    g_autofree char *comment = g_strdup_printf ("%s\n%s", description, settings);
    GstTagSetter *setter = GST_TAG_SETTER (muxer);
    gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
                             GST_TAG_DESCRIPTION, description,
                             GST_TAG_COMMENT, comment,
                             NULL);
    if (!final) {
        // No title: the name of the video is only chosen at the end of the recording, once the file is finalized
        GstDateTime *now = gst_date_time_new_now_local_time ();
        g_autofree char *application = g_strdup_printf ("Limrendir %s", LIMRENDIR_VERSION);
        gst_tag_setter_add_tags (setter, GST_TAG_MERGE_REPLACE,
                                 GST_TAG_APPLICATION_NAME, application,
                                 GST_TAG_DEVICE_MANUFACTURER, arv_camera_get_vendor_name (viewer->camera, NULL),
                                 GST_TAG_DEVICE_MODEL, arv_camera_get_model_name (viewer->camera, NULL),
                                 GST_TAG_DATE_TIME, now,
                                 NULL);
        gst_date_time_unref (now);
    }
}

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
        guint32 running_number = frame_numbering_next (&gst_record_numbering, arv_buffer_get_frame_id (arv_buffer));
        guint64 timestamp_ns = arv_buffer_get_timestamp (arv_buffer);
        if (gst_n_recorded_frames == 0)
            gst_first_timestamp_ns = timestamp_ns;
        gst_last_timestamp_ns = timestamp_ns;
        gst_last_running_number = running_number;
        gst_n_recorded_frames++;

        remember_displayed_frame (viewer, arv_buffer);

        GstBuffer *gst_buffer = arv_to_gst_buffer (arv_buffer, stream, viewer);
        if (has_fixed_playback_rate (viewer->record_type)) {
            // Fixed playback frame rate: frame n is shown at (n - 1) / playback fps. The running number follows the
            // camera frame counter, so a frame lost during the capture leaves a gap instead of shifting the video.
            GST_BUFFER_PTS (gst_buffer) = gst_util_uint64_scale (running_number - 1, GST_SECOND, mp4_playback_fps);
            GST_BUFFER_DURATION (gst_buffer) = gst_util_uint64_scale (1, GST_SECOND, mp4_playback_fps);
        }
        // Zebra / peaking: a boolean check when off, a non-blocking hand-off otherwise
        if (g_atomic_int_get (&viewer->analysis_active))
            analysis_offer_frame (viewer, gst_buffer, TRUE);
        gst_app_src_push_buffer (GST_APP_SRC (viewer->appsrc), gst_buffer);
    }
}

// Links a pad to the "sink" pad of an element
static GstPadLinkReturn link_pad_to_element (GstPad *src_pad, GstElement *element) {
    GstPad *sink_pad = gst_element_get_static_pad (element, "sink");
    GstPadLinkReturn result = gst_pad_link (src_pad, sink_pad);
    gst_object_unref (sink_pad);
    return result;
}

// Links the tee to the first element of the recording branch, through a videocrop element if only a part of the
// images is recorded (recorded_region). The frames are not rotated / flipped during recordings, so the region is in
// image coordinates.
static gboolean link_tee_to_recording_branch (LrdViewer *viewer, GstPad *tee_src_pad, GstElement *first) {
    const RecordedRegion *r = &recorded_region;
    if (!r->cropped)
        return !GST_PAD_LINK_FAILED (link_pad_to_element (tee_src_pad, first));

    GstElement *videocrop = gst_element_factory_make ("videocrop", NULL);
    if (videocrop == NULL) {
        log_error("The GStreamer element videocrop (gst-plugins-good) is missing: cannot record the ROI only.");
        return FALSE;
    }
    g_object_set (G_OBJECT (videocrop),
                  "left", r->x, "top", r->y,
                  "right", r->fov_width - r->x - r->width, "bottom", r->fov_height - r->y - r->height, NULL);
    gst_bin_add (GST_BIN (viewer->pipeline), videocrop);
    return !GST_PAD_LINK_FAILED (link_pad_to_element (tee_src_pad, videocrop)) && gst_element_link (videocrop, first);
}

// jhfdejhgfuyswfiuse
static gboolean setup_video_display_for_gstreamer_recording(LrdViewer *viewer) {

    const char *filename = viewer->recording_path;

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
    GstPad *tee_display_src_pad = gst_element_request_pad_simple (tee, "src_%u");

    add_display_sink (viewer);

    // Fuck this line.
    // For some reason if we don't add it everything freezes .
    // It took me 3 hours to randomly discover the existence of this parameter.
    // I still have no idea why or how it works.
    // I just tried it out of despair.
    // It works.
    // But fuck it.
    g_object_set(G_OBJECT (viewer->videosink), "async", FALSE, NULL);

    // The display must never slow down the recording: it gets its own thread through a queue that keeps only the
    // latest frame and drops the others when the display cannot follow the camera frame rate.
    GstElement *display_queue = gst_element_factory_make ("queue", NULL);
    gst_util_set_object_arg (G_OBJECT (display_queue), "leaky", "downstream");
    g_object_set (G_OBJECT (display_queue), "max-size-buffers", 1, "max-size-bytes", 0, "max-size-time", (guint64) 0, NULL);
    gst_bin_add (GST_BIN (viewer->pipeline), display_queue);
    link_success = link_pad_to_element (tee_display_src_pad, display_queue);
    if (GST_PAD_LINK_FAILED (link_success) || !gst_element_link (display_queue, viewer->videosink)) {
        log_debug("Link from the tee to the display video sink failed.");
        return FALSE;
    }

    gst_object_unref (GST_OBJECT (tee_display_src_pad));


    // RECORD PIPELINE
    // Todo: try also : multifilesink element to save every buffer in its own file ?
    GstPad *tee_record_src_pad = gst_element_request_pad_simple (tee, "src_%u");
    GstElement *filesink;
    GstElement *videoconvert_rec = NULL;
    GstElement *queue = NULL;
    // codecs
    GstElement *x264enc = NULL;
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
            GstPad *flvmux_sink_pad = gst_element_request_pad_simple (flvmux, "video");
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
        case RECORDTYPE_MP4: {
            /*
             * MP4 RECORDING
             *
             * Record pipeline:
             *  tee -> videoconvert -> capsfilter (I420) -> queue -> x264enc -> mp4mux -> filesink
             */
            videoconvert_rec = gst_element_factory_make ("videoconvert", NULL);
            GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
            GstCaps *raw_caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING, mp4_encoding_settings.raw_format, NULL);
            g_object_set (G_OBJECT (capsfilter), "caps", raw_caps, NULL);
            gst_caps_unref (raw_caps);
            queue = gst_element_factory_make ("queue", NULL);
            x264enc = gst_element_factory_make ("x264enc", NULL);
            gst_util_set_object_arg (G_OBJECT (x264enc), "speed-preset", mp4_encoding_settings.speed_preset);
            gst_util_set_object_arg (G_OBJECT (x264enc), "pass", "qual");
            g_object_set (G_OBJECT (x264enc), "quantizer", mp4_encoding_settings.quality, NULL);
            mp4mux = gst_element_factory_make ("mp4mux", NULL);
            prepare_recorded_region (viewer, TRUE);
            gst_capture_frame_rate = get_actual_frame_rate (viewer);
            set_video_file_tags (viewer, mp4mux, FALSE);
            filesink = gst_element_factory_make ("filesink", NULL);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);
            log_debug("MP4 encoding: H.264 %s, preset %s, constant quality %u", mp4_encoding_settings.raw_format,
                      mp4_encoding_settings.speed_preset, mp4_encoding_settings.quality);

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), videoconvert_rec, capsfilter, queue, x264enc, mp4mux, filesink, NULL);
            // Linking the elements
            if (!link_tee_to_recording_branch (viewer, tee_record_src_pad, videoconvert_rec)) {
                log_debug("Link from the tee to the videoconvert_rec for recording failed.");
                return FALSE;
            }
            if (!gst_element_link_many (videoconvert_rec, capsfilter, queue, x264enc, mp4mux, filesink, NULL)) {
                log_error("Could not build the MP4 recording pipeline.");
                return FALSE;
            }
            break;
        }
        case RECORDTYPE_MKV: {
            /*
             * MKV RECORDING (lossless)
             *
             * Record pipeline:
             *  tee -> capsfilter (BGRx/BGRA) -> queue -> avenc_ffv1 -> matroskamux -> filesink
             *
             * The frames reach the tee in the display format (BGRx or BGRA), which FFV1 encodes losslessly as it is:
             * there is deliberately no videoconvert, so that no lossy conversion can be inserted.
             */
            GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
            GstCaps *raw_caps = gst_caps_from_string ("video/x-raw, format=(string){ BGRx, BGRA }");
            g_object_set (G_OBJECT (capsfilter), "caps", raw_caps, NULL);
            gst_caps_unref (raw_caps);
            queue = gst_element_factory_make ("queue", NULL);
            avenc_ffv1 = gst_element_factory_make ("avenc_ffv1", NULL);
            matroskamux = gst_element_factory_make ("matroskamux", NULL);
            filesink = gst_element_factory_make ("filesink", NULL);
            if (avenc_ffv1 == NULL || matroskamux == NULL) {
                log_error("MKV recording needs the GStreamer elements avenc_ffv1 (gst-libav) and matroskamux "
                          "(gst-plugins-good), which are not installed.");
                arv_viewer_show_notification (viewer, "Cannot record MKV videos",
                                              "The GStreamer FFV1 encoder (gst-libav) or Matroska muxer is missing.");
                g_clear_pointer (&capsfilter, gst_object_unref);
                g_clear_pointer (&queue, gst_object_unref);
                g_clear_pointer (&avenc_ffv1, gst_object_unref);
                g_clear_pointer (&matroskamux, gst_object_unref);
                g_clear_pointer (&filesink, gst_object_unref);
                return FALSE;
            }
            // One encoding thread per core: a single thread cannot follow high frame rates
            g_object_set (G_OBJECT (avenc_ffv1), "threads", 0, NULL);
            prepare_recorded_region (viewer, FALSE);
            gst_capture_frame_rate = get_actual_frame_rate (viewer);
            set_video_file_tags (viewer, matroskamux, FALSE);
            g_object_set (G_OBJECT(filesink), "location", filename, NULL);
            g_object_set(G_OBJECT(filesink), "sync", FALSE, NULL);
            log_debug("MKV encoding: FFV1 (lossless), automatic number of threads");

            // Adding to the bin
            gst_bin_add_many (GST_BIN (viewer->pipeline), capsfilter, queue, avenc_ffv1, matroskamux, filesink, NULL);
            // Linking the elements
            if (!link_tee_to_recording_branch (viewer, tee_record_src_pad, capsfilter)) {
                log_debug("Link from the tee to the capsfilter for recording failed.");
                return FALSE;
            }
            if (!gst_element_link_many (capsfilter, queue, avenc_ffv1, matroskamux, filesink, NULL)) {
                log_error("Could not build the MKV recording pipeline.");
                return FALSE;
            }
            break;
        }
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
            GstPad *avimux_sink_pad = gst_element_request_pad_simple (avimux, "video_%u");
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

    if (has_fixed_playback_rate (viewer->record_type))
        start_display_pipeline (viewer, caps_string, mp4_playback_fps, 1);
    else
        start_display_pipeline (viewer, caps_string, 0, 1);

    return TRUE;
}

static gboolean setup_stream_for_gstreamer_recording(LrdViewer *viewer) {
    log_debug("Preparing to acquisition stream (aravis GigE Vision stream).");

    // Each recording numbers its frames from 1
    frame_numbering_reset (&gst_record_numbering);
    gst_n_recorded_frames = 0;
    gst_last_running_number = 0;

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
        discard_recording (viewer);
        stop_recording (viewer);
        gtk_widget_set_sensitive(viewer->help_button, TRUE);
        gtk_widget_set_sensitive(viewer->record_button, TRUE);
        return FALSE;
    }

    prepare_recorded_region(viewer, FALSE);
    write_meta(viewer);

    // Create the stream
    viewer->stream = create_camera_stream (viewer, stream_record_cb);
    if (viewer->stream == NULL) {
        // Closes the files and puts the interface back in its normal state
        close_files ();
        discard_recording (viewer);
        stop_recording (viewer);
        gtk_widget_set_sensitive(viewer->help_button, TRUE);
        gtk_widget_set_sensitive(viewer->record_button, TRUE);
        return FALSE;
    }

    configure_stream (viewer);

    push_new_buffers (viewer, n_buffers);

    arv_stream_set_emit_signals (viewer->stream, FALSE);

    arv_camera_set_acquisition_mode (viewer->camera, ARV_ACQUISITION_MODE_CONTINUOUS, NULL);

    arv_camera_start_acquisition (viewer->camera, NULL);

    // (a value typed and not validated yet is taken into account)
    burst_harmonize_nframes_and_duration (viewer);
    gint number_of_frames_to_record = viewer->burst_n_frames;

    ArvBuffer *buffer;

    FrameNumbering burst_numbering;
    frame_numbering_reset(&burst_numbering);
    guint32 running_number;

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

        // write image data (the ROI only, if one is used)
        write_recorded_region(imageFile, buffer);
        n_recorded_frames++;

        // Push back the buffer
        arv_stream_push_buffer(viewer->stream, buffer);
    }

    log_info("Burst: %d frames recorded (%d failed frames skipped).", n_recorded_frames, n_failed_frames);

    stop_recording (viewer);
    gtk_widget_set_sensitive(viewer->help_button, TRUE);
    gtk_widget_set_sensitive(viewer->record_button, TRUE);

    return FALSE;
}

// START AND STOP RECORDING
void abort_recording(LrdViewer *viewer) {
    g_signal_handler_block (viewer->record_button, viewer->recording_button_changed);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewer->record_button), FALSE);
    g_signal_handler_unblock (viewer->record_button, viewer->recording_button_changed);
    discard_recording (viewer);
    viewer->record_type = RECORDTYPE_NONE;
}

gboolean start_recording (LrdViewer *viewer) {
    // Check that the recording button is pressed
    if (!is_record_button_active(viewer)) {
        g_signal_handler_block (viewer->record_button, viewer->recording_button_changed);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(viewer->record_button), TRUE);
        g_signal_handler_unblock (viewer->record_button, viewer->recording_button_changed);
    }

    // The recording starts right away, in a temporary file: the user chooses where to save it at its end.
    // The name proposed then is the one given on the command line (for the first video), else the default one.
    viewer->record_type = chosen_recording_type (viewer);
    g_free (viewer->recording_name);
    viewer->recording_name = viewer->dataset_name != NULL ? g_steal_pointer (&viewer->dataset_name)
                                                          : obtain_new_original_filename (viewer);
    g_free (viewer->recording_path);
    viewer->recording_path = new_temporary_recording_path (viewer->recording_name, viewer->record_type);

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
            case RECORDTYPE_GEVCAPTURE:
                stop_video(viewer);
                successful_launch *= setup_video_display_for_gevcapture_style_recording(viewer);
                if (successful_launch) {
                    successful_launch *= setup_stream_for_gevcapture_style_recording(viewer);
                }
                break;
            case RECORDTYPE_FLV:
            case RECORDTYPE_MP4:
            case RECORDTYPE_MKV:
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

            start_video(viewer); // We need to restart the video because if we are here we stopped it
            abort_recording(viewer);
            return FALSE;
        }
        log_info("Recording (in the temporary file '%s')", viewer->recording_path);

        set_video_controls_sensitive(viewer, FALSE);
        gtk_widget_set_sensitive(viewer->help_key_2, FALSE);
        gtk_widget_set_sensitive(viewer->help_description_2, FALSE);
        gtk_widget_set_sensitive(viewer->help_key_3, FALSE);
        gtk_widget_set_sensitive(viewer->help_description_3, FALSE);

        return successful_launch;
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (viewer->recmode_burst_radiobutton))) {
        // burst mode

        switch (viewer->record_type) {
            case RECORDTYPE_GEVCAPTURE:
                stop_video(viewer);

                log_info("Recording a burst (in the temporary file '%s')", viewer->recording_path);

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

    // MP4 / MKV: complete the metadata with the frame counts (the muxer writes them at the end of the file)
    if (has_fixed_playback_rate (viewer->record_type) && GST_IS_BIN (viewer->pipeline)) {
        GstElement *muxer = gst_bin_get_by_interface (GST_BIN (viewer->pipeline), GST_TYPE_TAG_SETTER);
        if (muxer != NULL) {
            set_video_file_tags (viewer, muxer, TRUE);
            gst_object_unref (muxer);
        }
    }

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

    // The video is complete: the user chooses where to save it, while the live video is back
    g_autofree char *recording_path = g_steal_pointer (&viewer->recording_path);
    g_autofree char *recording_name = g_steal_pointer (&viewer->recording_name);
    RecordingType const recording_type = viewer->record_type;
    viewer->record_type = RECORDTYPE_NONE;
    if (recording_path != NULL)
        save_recording (viewer, recording_path, recording_type, recording_name);
}



// MANAGE RECORDING STATE
gboolean is_record_button_active(LrdViewer *viewer) {
    return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(viewer->record_button));
}

void record_button_update_tooltip (GtkToggleButton *toggle, LrdViewer *viewer)
{
    gboolean const recording = gtk_toggle_button_get_active (toggle);

    gtk_widget_set_tooltip_text (GTK_WIDGET (toggle),
                                 recording ? "Stop recording [SPACE]" : "Start recording [SPACE]");
    gtk_image_set_from_icon_name (GTK_IMAGE (gtk_bin_get_child (GTK_BIN (toggle))),
                                  recording ? "media-playback-stop-symbolic" : "media-record-symbolic",
                                  GTK_ICON_SIZE_BUTTON);
}

void record_button_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    if (gtk_toggle_button_get_active (toggle)) {
        start_recording (viewer);
    } else {
        stop_recording (viewer);
    }
}

