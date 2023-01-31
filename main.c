/* Limrendir
 *
 * This hastily written code was largely inspired, when not copy-pasted, from Aravis Viewer by Emmanuel Pacaud.
 *
 * This software is free; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <limrendir.h>
#include <arvdebugprivate.h>
#include <arvgvstreamprivate.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <arv.h>
#include <stdlib.h>
#include <libintl.h>
#include <logroutines.h>

#if GST_GL_HAVE_WINDOW_X11 && defined (GDK_WINDOWING_X11)
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif

static char *arv_viewer_option_debug_domains = NULL;
static char *arv_option_register_cache = NULL;
static char *arv_option_range_check = NULL;
static gboolean arv_viewer_option_auto_socket_buffer = TRUE; // Auto socket buffer
static gboolean arv_viewer_option_no_packet_resend = FALSE;
static unsigned int arv_viewer_option_initial_packet_timeout = ARV_GV_STREAM_INITIAL_PACKET_TIMEOUT_US_DEFAULT / 1000;
static unsigned int arv_viewer_option_packet_timeout = ARV_GV_STREAM_PACKET_TIMEOUT_US_DEFAULT / 1000;
static unsigned int arv_viewer_option_frame_retention = ARV_GV_STREAM_FRAME_RETENTION_US_DEFAULT / 1000;
static char *arv_option_uv_usb_mode = NULL;

static const GOptionEntry arv_viewer_option_entries[] =
{
	{
		"auto-buffer-size",			'a', 0, G_OPTION_ARG_NONE,
		&arv_viewer_option_auto_socket_buffer,	"Auto socket buffer size", NULL
	},
	{
		"no-packet-resend",			'r', 0, G_OPTION_ARG_NONE,
		&arv_viewer_option_no_packet_resend,	"No packet resend", NULL
	},
	{
		"initial-packet-timeout", 		        'l', 0, G_OPTION_ARG_INT,
		&arv_viewer_option_initial_packet_timeout, 	"Initial packet timeout (ms)", NULL
	},
	{
		"packet-timeout", 			'p', 0, G_OPTION_ARG_INT,
		&arv_viewer_option_packet_timeout, 	"Packet timeout (ms)", NULL
	},
	{
		"frame-retention", 			'm', 0, G_OPTION_ARG_INT,
		&arv_viewer_option_frame_retention, 	"Frame retention (ms)", NULL
	},
	{
		"register-cache",			'\0', 0, G_OPTION_ARG_STRING,
		&arv_option_register_cache, 		"Register cache policy",
		"{disable|enable|debug}"
	},
	{
		"range-check",				'\0', 0, G_OPTION_ARG_STRING,
		&arv_option_range_check,		"Range check policy",
		"{disable|enable}"
	},
	{
		"usb-mode",				's', 0, G_OPTION_ARG_STRING,
		&arv_option_uv_usb_mode,		"USB device I/O mode",
		"{sync|async}"
	},
//	{
//		"debug", 				'd', 0, G_OPTION_ARG_STRING,
//		&arv_viewer_option_debug_domains, 	NULL,
//		"{<category>[:<level>][,...]|help}"
//	},
	{ NULL }
};

const double gc_option_frequency_default = -1;
const double gc_option_exposure_time_us_default = -1;
const double gc_option_gain_default = -1;
const int gc_option_horizontal_binning_default = -1;
const int gc_option_vertical_binning_default = -1;
const int gc_option_width_default = -1;
const int gc_option_height_default = -1;
const int gc_option_x_offset_default = -1;
const int gc_option_y_offset_default = -1;

char *arv_option_camera_name = NULL;
double arv_option_frequency = gc_option_frequency_default;
double arv_option_exposure_time_us = gc_option_exposure_time_us_default;
double arv_option_gain = gc_option_gain_default;
int arv_option_horizontal_binning = gc_option_horizontal_binning_default;
int arv_option_vertical_binning = gc_option_vertical_binning_default;
int arv_option_width = gc_option_width_default;
int arv_option_height = gc_option_height_default;
int arv_option_x_offset = gc_option_x_offset_default;
int arv_option_y_offset = gc_option_y_offset_default;
static const GOptionEntry gevCapture_option_entries[] =
        {
                {
                        "camera-name",              'c',  0, G_OPTION_ARG_STRING,
                        &arv_option_camera_name,              "Camera name",                                       "string"
                },
                {
                        "frequency",                'f',  0, G_OPTION_ARG_DOUBLE,
                        &arv_option_frequency,                "Acquisition frequency (Hz)",                             "double"
                },
                {
                        "exposure",                 'e',  0, G_OPTION_ARG_DOUBLE,
                        &arv_option_exposure_time_us,         "Exposure time (us)",                                "double"
                },
                {
                        "gain",                     'g',  0, G_OPTION_ARG_DOUBLE,
                        &arv_option_gain,                     "Gain (dB)",                                         "double"
                },
                {
                        "x-offset",                 'x',  0, G_OPTION_ARG_INT,
                        &arv_option_x_offset,                 "X-offset",                                          "int"
                },
                {
                        "y-offset",                 'y',  0, G_OPTION_ARG_INT,
                        &arv_option_y_offset,                 "Y-offset",                                          "int"
                },
                {
                        "width",                    'w',  0, G_OPTION_ARG_INT,
                        &arv_option_width,                    "Width",                                             "int"
                },
                {
                        "height",                   'h',  0, G_OPTION_ARG_INT,
                        &arv_option_height,                   "Height",                                            "int"
                },
                {
                        "h-binning",                '\0', 0, G_OPTION_ARG_INT,
                        &arv_option_horizontal_binning,       "Horizontal binning",                                "int"
                },
                {
                        "v-binning",                '\0', 0, G_OPTION_ARG_INT,
                        &arv_option_vertical_binning,         "Vertical binning",                                  "int"
                },
                {       NULL}
        };

static gboolean lrd_option_gevcapture_mode = FALSE;
static char *lrd_option_dataset_name = NULL;
static int lrd_option_log_level = LOG_INFO;
static gboolean lrd_option_create_fake_camera = FALSE;
static const GOptionEntry limrendir_option_entries[] =
        {
                {
                        "gevcapture",			'o', 0, G_OPTION_ARG_NONE,
                        &lrd_option_gevcapture_mode,    "GevCapture legacymode", NULL
                },
                {
                        "dataset-name",                'n',  0, G_OPTION_ARG_STRING,
                        &lrd_option_dataset_name,       "Output dataset name", "s"
                },
                {
                        "log-level",                'd',  0, G_OPTION_ARG_INT,
                        &lrd_option_log_level, "Logging level", "0-4 (error, warn, info, debug, trace)"
                },
                {
                        "fake-camera",                '\0',  0, G_OPTION_ARG_NONE,
                        &lrd_option_create_fake_camera, "Creates a fake camera", NULL
                },
                { NULL }
        };

int main (int argc, char **argv)
{
	LrdViewer *viewer;
	GtkIconTheme *icon_theme;
	int status;
	GOptionContext *context;
	GError *error = NULL;
	ArvRegisterCachePolicy register_cache_policy;
	ArvRangeCheckPolicy range_check_policy;
	ArvUvUsbMode usb_mode;

#if GST_GL_HAVE_WINDOW_X11 && defined(GDK_WINDOWING_X11)
	XInitThreads ();
#endif

    // ??? No idea what this is
//	bindtextdomain (ARAVIS_GETTEXT, ARAVIS_LOCALE_DIR);
//	bind_textdomain_codeset (ARAVIS_GETTEXT, "UTF-8");
//	textdomain (ARAVIS_GETTEXT);

	gtk_init (&argc, &argv);
	gst_init (&argc, &argv);

    context = g_option_context_new (NULL);
    g_option_context_add_main_entries (context, limrendir_option_entries, NULL);
    g_option_context_add_main_entries (context, gevCapture_option_entries, NULL);
    g_option_context_add_main_entries (context, arv_viewer_option_entries, NULL);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    g_option_context_add_group (context, gst_init_get_option_group ());
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_option_context_free (context);
        g_print ("Option parsing failed: %s\n", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_option_context_free (context);

	icon_theme = gtk_icon_theme_get_default ();
	gtk_icon_theme_add_resource_path (icon_theme, "/org/aravis/viewer/icons/gnome/");

    // Log level
    if (lrd_option_log_level < LOG_ERROR) {
        lrd_option_log_level = LOG_ERROR;
    } else if (lrd_option_log_level > LOG_TRACE) {
        lrd_option_log_level = LOG_TRACE;
    }
    log_initialize(lrd_option_log_level);


	if (arv_option_register_cache == NULL)
		register_cache_policy = ARV_REGISTER_CACHE_POLICY_DEFAULT;
	else if (g_strcmp0 (arv_option_register_cache, "disable") == 0)
		register_cache_policy = ARV_REGISTER_CACHE_POLICY_DISABLE;
	else if (g_strcmp0 (arv_option_register_cache, "enable") == 0)
		register_cache_policy = ARV_REGISTER_CACHE_POLICY_ENABLE;
	else if (g_strcmp0 (arv_option_register_cache, "debug") == 0)
		register_cache_policy = ARV_REGISTER_CACHE_POLICY_DEBUG;
	else {
		printf ("Invalid register cache policy\n");
		return EXIT_FAILURE;
	}

	if (arv_option_range_check == NULL)
		range_check_policy = ARV_RANGE_CHECK_POLICY_DEFAULT;
	else if (g_strcmp0 (arv_option_range_check, "disable") == 0)
		range_check_policy = ARV_RANGE_CHECK_POLICY_DISABLE;
	else if (g_strcmp0 (arv_option_range_check, "enable") == 0)
		range_check_policy = ARV_RANGE_CHECK_POLICY_ENABLE;
	else if (g_strcmp0 (arv_option_range_check, "debug") == 0)
		range_check_policy = ARV_RANGE_CHECK_POLICY_DEBUG;
	else {
		printf ("Invalid range check policy\n");
		return EXIT_FAILURE;
	}

	if (arv_option_uv_usb_mode == NULL)
		usb_mode = ARV_UV_USB_MODE_DEFAULT;
	else if (g_strcmp0 (arv_option_uv_usb_mode, "sync") == 0)
		usb_mode = ARV_UV_USB_MODE_SYNC;
	else if (g_strcmp0 (arv_option_uv_usb_mode, "async") == 0)
		usb_mode = ARV_UV_USB_MODE_ASYNC;
	else {
		printf ("Invalid USB device I/O mode\n");
		return EXIT_FAILURE;
	}

	if (!arv_debug_enable (arv_viewer_option_debug_domains)) {
		if (g_strcmp0 (arv_viewer_option_debug_domains, "help") != 0)
			printf ("Invalid debug selection\n");
		else
			arv_debug_print_infos ();
		return EXIT_FAILURE;
	}

	viewer = trn_viewer_new();
	if (!ARV_IS_VIEWER (viewer))
		return EXIT_FAILURE;

    log_debug("Settings arv-viewer options");
    arv_viewer_set_options (viewer,
				arv_viewer_option_auto_socket_buffer,
				!arv_viewer_option_no_packet_resend,
				arv_viewer_option_initial_packet_timeout,
				arv_viewer_option_packet_timeout,
				arv_viewer_option_frame_retention,
				register_cache_policy,
				range_check_policy,
                usb_mode);

    log_debug("Settings gevCapture options");
    gevCapture_set_options (viewer,
                            arv_option_camera_name,
                            arv_option_frequency,
                            arv_option_exposure_time_us,
                            arv_option_gain,
                            arv_option_horizontal_binning,
                            arv_option_vertical_binning ,
                            arv_option_width,
                            arv_option_height,
                            arv_option_x_offset,
                            arv_option_y_offset);

    log_debug("Settings limrendir options");
    limrendir_set_options(viewer,
                          lrd_option_gevcapture_mode,
                          lrd_option_dataset_name,
                          lrd_option_create_fake_camera);

    log_trace("Application run");
    status = g_application_run (G_APPLICATION (viewer), argc, argv);

	g_object_unref (viewer);

    log_terminate();

	return status;
}
