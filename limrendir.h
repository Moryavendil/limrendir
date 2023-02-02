#include <arv.h>
#include <math.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef struct	_LrdViewer	LrdViewer;

#define ARV_TYPE_VIEWER             (arv_viewer_get_type ())
#define ARV_IS_VIEWER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ARV_TYPE_VIEWER))

GType 			    arv_viewer_get_type 		(void);

LrdViewer * 		trn_viewer_new 			(void);

void                arv_viewer_set_options		(LrdViewer *viewer,
                                                   gboolean auto_socket_buffer,
                                                   gboolean packet_resend,
                                                   guint initial_packet_timeout,
                                                   guint packet_timeout,
                                                   guint frame_retention,
                                                   ArvRegisterCachePolicy register_cache_policy,
                                                   ArvRangeCheckPolicy range_check_policy,
                                                   ArvUvUsbMode usb_mode);

void                gevCapture_set_options (LrdViewer *viewer,
                                            char *gc_option_camera_name,
                                            double gc_option_frame_rate,
                                            double gc_option_exposure_time_us,
                                            double gc_option_gain,
                                            int gc_option_horizontal_binning,
                                            int gc_option_vertical_binning ,
                                            int gc_option_width,
                                            int gc_option_height,
                                            int gc_option_x_offset,
                                            int gc_option_y_offset);


extern const double gc_option_frequency_default;
extern const double gc_option_exposure_time_us_default;
extern const double gc_option_gain_default;
extern const int gc_option_horizontal_binning_default;
extern const int gc_option_vertical_binning_default;
extern const int gc_option_width_default;
extern const int gc_option_height_default;
extern const int gc_option_x_offset_default;
extern const int gc_option_y_offset_default;

void                limrendir_set_options       (LrdViewer *viewer,
                                                 gboolean lrd_option_gevcapture_mode,
                                                 char *lrd_option_dataset_name,
                                                 gboolean lrd_option_create_fake_camera);

// GRID DRAWING
typedef enum {
    GRID_NOGRID,
    GRID_HORIZONTAL,
    GRID_VERTICAL,
    GRID_BOTH
} GridType;

// TYPE OF RECORD THAT IS WISHED
typedef enum {
    RECORDTYPE_NONE,
    RECORDTYPE_GEVCAPTURE,
    RECORDTYPE_AVI,
    RECORDTYPE_FLV,
    RECORDTYPE_MKV,
    RECORDTYPE_MP4
} RecordingType;


struct  _LrdViewer {
    GtkApplication parent_instance;

    ArvCamera *camera;
    char *camera_name;
    ArvStream *stream;
    ArvBuffer *last_buffer;

    GstElement *pipeline;
    GstElement *appsrc;
    GstElement *transform;
    GstElement *videosink;

    guint rotation;
    gboolean flip_vertical;
    gboolean flip_horizontal;

    double exposure_min, exposure_max;

    GtkWidget *main_window;
    GtkWidget *main_stack;
    GtkWidget *main_headerbar;
    // CAMERA SELECTION
    GtkWidget *camera_box;
    GtkWidget *refresh_button;
    GtkWidget *video_mode_button;
    GtkWidget *camera_tree;
    GtkWidget *back_button;
    GtkWidget *snapshot_button;
    GtkWidget *rotate_cw_button;
    GtkWidget *flip_vertical_toggle;
    GtkWidget *flip_horizontal_toggle;
    GtkWidget *camera_parameters;
    GtkWidget *pixel_format_combo;
    GtkWidget *camera_x;
    GtkWidget *camera_y;
    GtkWidget *camera_binning_x;
    GtkWidget *camera_binning_y;
    GtkWidget *camera_width;
    GtkWidget *camera_height;
    // VIDEO
    GtkWidget *video_box;
    GtkWidget *video_frame;
    // bottom bar
    GtkWidget *fps_label;
    GtkWidget *roi_label;
    GtkWidget *image_label;
    // Camera control popover
    GtkWidget *acquisition_button;
    GtkWidget *trigger_combo_box;
    GtkWidget *frame_rate_entry;
    GtkWidget *exposure_spin_button;
    GtkWidget *exposure_hscale;
    GtkWidget *auto_exposure_toggle;
    GtkWidget *gain_spin_button;
    GtkWidget *gain_hscale;
    GtkWidget *auto_gain_toggle;
    GtkWidget *black_level_spin_button;
    GtkWidget *black_level_hscale;
    GtkWidget *auto_black_level_toggle;
    // Record mode control
    GtkWidget *recmode_usercontrolled_radiobutton;
    GtkWidget *recmode_burst_radiobutton;
    GtkWidget *burst_nframes_radiobutton;
    GtkWidget *burst_duration_radiobutton;
    GtkWidget *burst_nframes_spinBox;
    GtkWidget *burst_duration_spinBox;

    // Help popover
    GtkWidget *help_button;
    GtkWidget *help_key_1;
    GtkWidget *help_description_1;
    GtkWidget *help_key_2;
    GtkWidget *help_description_2;
    GtkWidget *help_key_3;
    GtkWidget *help_description_3;
    GtkWidget *help_key_4;
    GtkWidget *help_description_4;

    // Custom G2L
    GtkWidget *record_button;
    GtkWidget *max_frame_rate_button;

    // Notification
    GtkWidget *notification_revealer;
    GtkWidget *notification_label;
    GtkWidget *notification_details;
    GtkWidget *notification_dismiss;
    guint notification_timeout;

    // Signals
    gulong camera_selected;
    gulong exposure_spin_changed;
    gulong exposure_hscale_changed;
    gulong auto_exposure_clicked;
    gulong gain_spin_changed;
    gulong gain_hscale_changed;
    gulong auto_gain_clicked;
    gulong black_level_spin_changed;
    gulong black_level_hscale_changed;
    gulong auto_black_level_clicked;
    gulong camera_x_changed;
    gulong camera_y_changed;
    gulong camera_binning_x_changed;
    gulong camera_binning_y_changed;
    gulong camera_width_changed;
    gulong camera_height_changed;
    gulong pixel_format_changed;
    gulong new_buffer_available_video;
    gulong recording_button_changed;
    gulong burst_control_changed;
    gulong burst_nframes_changed;
    gulong burst_duration_changed;

    guint gain_update_event;
    guint black_level_update_event;
    guint exposure_update_event;

    // Status bar
    guint status_bar_update_event;
    gint64 last_status_bar_update_time_ms;
    guint64 last_n_images;
    guint64 last_n_bytes;

    guint recording_snapshots_update_event;

    gboolean auto_socket_buffer;
    gboolean packet_resend;
    guint initial_packet_timeout;
    guint packet_timeout;
    guint frame_retention;
    ArvRegisterCachePolicy register_cache_policy;
    ArvRangeCheckPolicy range_check_policy;
    ArvUvUsbMode usb_mode;

    // Arecorder settings
    gboolean gevcapture_mode;
    char *dataset_name;

    // GevCapture settings
    int gc_option_x_offset;
    int gc_option_y_offset;
    int gc_option_width;
    int gc_option_height;
    int gc_option_horizontal_binning;
    int gc_option_vertical_binning;
    double gc_option_gain;
    double gc_option_frame_rate;
    double gc_option_exposure_time_us;
    char *gc_option_camera_name;


    // Grid
    GridType grid_type;

    // Roi
    gboolean show_roi;
    int roi_x;
    int roi_y;
    int roi_w;
    int roi_h;

    // download
    RecordingType record_type;

    // Keypress commands
    gulong keypress_handler_id;

    gboolean is_fullscreen; //Retrieving the fullscreen state directly by asking is_fullscreen for a window is only possible in GTK4 so we have to track fullscreen state ourselves in GTK3.


    gulong video_window_xid;
};

extern gboolean has_autovideo_sink;
extern gboolean has_gtksink;
extern gboolean has_gtkglsink;
extern gboolean has_bayer2rgb;

// Notifications

void notification_dismiss_clicked_cb (GtkButton *dismiss, LrdViewer *viewer);

gboolean hide_notification (gpointer user_data);

void arv_viewer_show_notification (LrdViewer *viewer, const char *message, const char *details);


// MODE CHANGE

// Modes
typedef enum {
    TRN_VIEWER_MODE_CAMERA_LIST,
    TRN_VIEWER_MODE_VIDEO
} TrnViewerMode;

// Select mode
void select_mode (LrdViewer *viewer, TrnViewerMode mode);

// VIDEO MODE
void switch_to_video_mode_cb (GtkToolButton *button, LrdViewer *viewer);

// Image transforms
void update_transform (LrdViewer *viewer);

void rotate_cw_cb (GtkButton *button, LrdViewer *viewer);

void flip_horizontal_cb (GtkToggleButton *toggle, LrdViewer *viewer);

void flip_vertical_cb (GtkToggleButton *toggle, LrdViewer *viewer);

// Snapshot saving
gboolean _save_gst_sample_to_file (GstSample *sample, const char *path, const char *mime_type, GError **error);

void snapshot_cb (GtkButton *button, LrdViewer *viewer);


// CAMERA LIST MODE
gboolean select_camera_list_mode (gpointer user_data);

// Go back to list mode if camera control is lost
void control_lost_cb (ArvCamera *camera, LrdViewer *viewer);
void switch_to_camera_list_cb (GtkToolButton *button, LrdViewer *viewer);

// update the camera list
void update_device_list_cb (GtkToolButton *button, LrdViewer *viewer);

// Manage the camera selection change: start and stop the right camera
void camera_selection_changed_cb (GtkTreeSelection *selection, LrdViewer *viewer);

gboolean start_camera (LrdViewer *viewer, const char *camera_id);

void stop_camera (LrdViewer *viewer);


// FOV SETTINGS

// Pixel format
void pixel_format_combo_cb (GtkComboBoxText *combo, LrdViewer *viewer);

void set_sensitive (GtkCellLayout *cell_layout,
                    GtkCellRenderer *cell,
                    GtkTreeModel *tree_model,
                    GtkTreeIter *iter,
                    gpointer data);

// Region
void camera_region_cb (GtkSpinButton *spin_button, LrdViewer *viewer);

// Binning
void camera_binning_cb (GtkSpinButton *spin_button, LrdViewer *viewer);

// Updates the region and binning spin boxes
void update_camera_region (LrdViewer *viewer);


// ACQUISITION SETTINGS

// Set all the widgets
void set_camera_control_widgets(LrdViewer *viewer);


// Frame rate routines
void apply_frame_rate (GtkEntry *entry, LrdViewer *viewer, gboolean grab_focus);

void frame_rate_entry_cb (GtkEntry *entry, LrdViewer *viewer);

gboolean frame_rate_entry_focus_cb (GtkEntry *entry, GdkEventFocus *event, LrdViewer *viewer);

void apply_max_frame_rate_if_wanted (GtkButton *button, LrdViewer *viewer);


// Mathematics routines for the sliders
double arv_viewer_value_to_log (double value, double min, double max);

double arv_viewer_value_from_log (double value, double min, double max);


// Exposure routines
void exposure_spin_cb (GtkSpinButton *spin_button, LrdViewer *viewer);

void exposure_scale_cb (GtkRange *range, LrdViewer *viewer);

gboolean update_exposure_cb (void *data);

void update_exposure_ui (LrdViewer *viewer, gboolean is_auto);

void auto_exposure_cb (GtkToggleButton *toggle, LrdViewer *viewer);


// Gain routines
void gain_spin_cb (GtkSpinButton *spin_button, LrdViewer *viewer);

void gain_scale_cb (GtkRange *range, LrdViewer *viewer);

gboolean update_gain_cb (void *data);

void update_gain_ui (LrdViewer *viewer, gboolean is_auto);

void auto_gain_cb (GtkToggleButton *toggle, LrdViewer *viewer);


// Black level routines
void black_level_spin_cb (GtkSpinButton *spin_button, LrdViewer *viewer);

void black_level_scale_cb (GtkRange *range, LrdViewer *viewer);

gboolean update_black_level_cb (void *data);

void update_black_level_ui (LrdViewer *viewer, gboolean is_auto);

void auto_black_level_cb (GtkToggleButton *toggle, LrdViewer *viewer);


// RECORD MODE
void record_mode_toggled (GtkToggleButton *button, LrdViewer *viewer);

void burst_control_toggled (GtkToggleButton *button, LrdViewer *viewer);

void burst_harmonize_nframes_and_duration (GtkSpinButton *spin_button, LrdViewer *viewer);

// Best exposure selection
void initialize_best_exposure_search (LrdViewer *viewer);

void finalize_best_exposure_search (LrdViewer *viewer);

uint8_t get_max_px_value(LrdViewer *viewer, int n_frames, int n_frames_to_skip);

// The blocking version (simple but annoying)
void best_exposure_search_blocking (LrdViewer *viewer);

// The non blocking version (multi-threaded so not blocking)
gboolean best_exposure_search_next_step(void *data);

void best_exposure_search_nonblocking (LrdViewer *viewer);

// KEYBOARD ROUTINES
// Help popover
void setup_help_popover (LrdViewer *viewer);

// Key listener
void start_key_listener(LrdViewer *viewer);
void stop_key_listener(LrdViewer *viewer);
gint key_press_cb(GtkWidget* widget, GdkEventKey* event, gpointer data);

// roi
void constrain_roi(LrdViewer *viewer);
void constrain_roi_to_field_of_view(LrdViewer *viewer, int width, int height);
void crop_to_roi(LrdViewer *viewer);

// Buttons on camera mode

void video_frame_realize_cb (GtkWidget * widget, LrdViewer *viewer);

// Video streaming

/* return time from timespec-structure in milliseconds */
long clock_millis(struct timespec t);

/* time difference in milliseconds */
long clock_diff_millis(struct timespec t1, struct timespec t2);

gboolean start_video (LrdViewer *viewer);
void stop_video (LrdViewer *viewer);

gboolean update_status_bar_cb (void *data);

GstBusSyncReply bus_sync_handler (GstBus *bus, GstMessage *message, gpointer user_data);

GstBuffer * arv_to_gst_buffer (ArvBuffer *arv_buffer, ArvStream *stream, LrdViewer *viewer);
void stream_video_cb (void *user_data, ArvStreamCallbackType type, ArvBuffer *buffer);

// Video recording
gboolean is_buffer_successful(ArvBuffer *buffer);

gboolean is_record_button_active(LrdViewer *viewer);
void record_button_cb (GtkToggleButton *toggle, LrdViewer *viewer);
gboolean start_recording (LrdViewer *viewer);
gboolean stop_recording (LrdViewer *viewer);


G_END_DECLS
