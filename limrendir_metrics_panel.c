#include <math.h>
#include <gtk/gtk.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>
#include "limrendir_analysis.h"

// IMAGE METRICS PANEL (t)
//
// A side panel, on the right of the video, with three sections computed on the ROI (or the whole image):
// - Histogram: 256 bins whatever the bit depth, and statistics in native units,
// - Alignment: angle of the nearly vertical and nearly horizontal straight lines, as seen on the screen,
// - Fourier transform: log power spectrum of the region binned to at most 256 x 256.
// A collapsed section is not computed. The window is widened by the width of the panel when it is shown (and
// narrowed back when it is hidden), so that the video keeps its size, unless the window is maximized or fullscreen.
// The metrics come from the analysis worker (limrendir_analysis.c, limrendir_metrics.c); the panel is redrawn when
// new ones arrive.

#define PANEL_WIDTH 280                 // pixels (the histogram gets about one pixel per bin)
#define PANEL_MARGIN 8
#define HISTOGRAM_HEIGHT 110
#define GAUGE_HEIGHT 74
#define FFT_HEIGHT 256

// Alignment gauge: linear up to GAUGE_LINEAR degrees, then logarithmic (asinh) up to GAUGE_RANGE at the ends
#define GAUGE_RANGE 45.0
#define GAUGE_LINEAR 0.05
// Alignment colors: green up to ALIGN_GOOD degrees (better than the eye by far), orange up to ALIGN_FAIR, red above
#define ALIGN_GOOD 0.05
#define ALIGN_FAIR 0.5
// Quality of the lines (see limrendir_metrics.c): below QUALITY_MIN there are no straight lines, below QUALITY_WEAK
// they are faint (the angle is shown, but less reliable)
#define QUALITY_MIN 0.1
#define QUALITY_WEAK 0.3
// Histogram: the vertical scale changes only if the highest bar becomes HISTOGRAM_RESCALE times higher or lower,
// and at most once every HISTOGRAM_RESCALE_INTERVAL_US
#define HISTOGRAM_RESCALE 2.0
#define HISTOGRAM_RESCALE_INTERVAL_US G_USEC_PER_SEC

#define COLOR_GOOD 0.18, 0.66, 0.27
#define COLOR_FAIR 0.90, 0.55, 0.05
#define COLOR_BAD 0.86, 0.20, 0.18
#define COLOR_VERTICALS 0.25, 0.62, 1.0
#define COLOR_HORIZONTALS 0.95, 0.45, 0.85

struct _LrdMetricsPanel {
    GtkWidget *region_label;
    GtkWidget *histogram_area;
    GtkWidget *log_check;
    GtkWidget *stats_label;
    GtkWidget *gauge_area;
    GtkWidget *alignment_label;
    GtkWidget *fft_area;
    GtkWidget *fft_label;
    // Vertical scale of the histogram: the count at the top (0: none yet), when it was chosen, and for which scale
    double histogram_top;
    gint64 histogram_top_time_us;
    gboolean histogram_top_log;
};

// HELPERS

// The line families as seen on the screen: the rotations of the display keep the sense of the angles but may swap
// the vertical and horizontal lines, the flips reverse the angles
static void displayed_families (LrdViewer *viewer, const LrdMetrics *m, MetricsLineFamily *verticals,
                                MetricsLineFamily *horizontals)
{
    gint const method = get_videoflip_method (viewer);
    gboolean const transposed = method == 1 || method == 3 || method == 6 || method == 7;
    gboolean const mirrored = method >= 4;
    *verticals = transposed ? m->horizontals : m->verticals;
    *horizontals = transposed ? m->verticals : m->horizontals;
    if (mirrored) {
        verticals->tilt = -verticals->tilt;
        horizontals->tilt = -horizontals->tilt;
    }
}

static gboolean family_found (const MetricsLineFamily *family)
{
    return family->computed && family->quality >= QUALITY_MIN;
}

static const char *tilt_color (double tilt)
{
    double const t = fabs (tilt);
    return t < ALIGN_GOOD ? "#2ea844" : t < ALIGN_FAIR ? "#e08c0d" : "#db332e";
}

// Position on the gauge, from -1 (left end) to 1 (right end)
static double gauge_position (double tilt)
{
    double const f = asinh (fabs (tilt) / GAUGE_LINEAR) / asinh (GAUGE_RANGE / GAUGE_LINEAR);
    return copysign (MIN (f, 1.0), tilt);
}

static void dark_background (cairo_t *cr, int width, int height)
{
    cairo_set_source_rgb (cr, 0.12, 0.12, 0.13);
    cairo_rectangle (cr, 0, 0, width, height);
    cairo_fill (cr);
}

static void draw_centered_text (GtkWidget *widget, cairo_t *cr, const char *markup, double x, double y,
                                double r, double g, double b)
{
    PangoLayout *layout = gtk_widget_create_pango_layout (widget, NULL);
    pango_layout_set_markup (layout, markup, -1);
    int text_width, text_height;
    pango_layout_get_pixel_size (layout, &text_width, &text_height);
    cairo_move_to (cr, x - text_width / 2.0, y - text_height / 2.0);
    cairo_set_source_rgb (cr, r, g, b);
    pango_cairo_show_layout (cr, layout);
    g_object_unref (layout);
}

// HISTOGRAM

static gboolean histogram_draw_cb (GtkWidget *area, cairo_t *cr, LrdViewer *viewer)
{
    LrdMetricsPanel *panel = viewer->metrics_panel;
    int const width = gtk_widget_get_allocated_width (area);
    int const height = gtk_widget_get_allocated_height (area);
    dark_background (cr, width, height);

    // Quarters of the full scale
    cairo_set_source_rgba (cr, 1, 1, 1, 0.12);
    cairo_set_line_width (cr, 1);
    for (int q = 1; q < 4; q++) {
        double const x = floor (q * width / 4.0) + 0.5;
        cairo_move_to (cr, x, 0);
        cairo_line_to (cr, x, height);
    }
    cairo_stroke (cr);

    const LrdMetrics *m = analysis_lock_metrics (viewer);
    if (m != NULL && m->request.histogram && m->n_pixels > 0) {
        gboolean const log_scale = panel->histogram_top_log;
        // (bars higher than the top of the scale are clipped, see histogram_update_scale)
        double const top = log_scale ? log1p (panel->histogram_top) : panel->histogram_top;
        for (int pass = 0; pass < 3; pass++) {
            // 0: ordinary bins, 1: black, 2: saturated
            if (pass == 0)
                cairo_set_source_rgb (cr, 0.85, 0.85, 0.85);
            else if (pass == 1)
                cairo_set_source_rgb (cr, 0.35, 0.55, 1.0);
            else
                cairo_set_source_rgb (cr, COLOR_BAD);
            for (int i = 0; i < METRICS_HISTOGRAM_BINS; i++) {
                int const kind = i == 0 ? 1 : i == METRICS_HISTOGRAM_BINS - 1 ? 2 : 0;
                guint32 const count = m->bins[i];
                if (kind != pass || count == 0)
                    continue;
                double const f = MIN (1.0, (log_scale ? log1p (count) : count) / top);
                double const x0 = (double) i * width / METRICS_HISTOGRAM_BINS;
                double const x1 = (double) (i + 1) * width / METRICS_HISTOGRAM_BINS;
                // At least one pixel wide and high (the end bins must be seen)
                double const bar_height = MAX (1.0, f * (height - 2));
                cairo_rectangle (cr, x0, height - bar_height, MAX (1.0, x1 - x0), bar_height);
            }
            cairo_fill (cr);
        }
        // Mean
        double const x = m->mean / (m->full_scale + 1) * width;
        cairo_set_source_rgba (cr, 1.0, 0.85, 0.2, 0.9);
        cairo_move_to (cr, floor (x) + 0.5, 0);
        cairo_line_to (cr, floor (x) + 0.5, height);
        cairo_stroke (cr);
    }
    analysis_unlock_metrics (viewer);
    return FALSE;
}

// The count of the highest bar the scale must show. Linear scale: without the end bins (black and saturated pixels,
// often a huge peak, clipped at the top).
static double histogram_highest_bar (const LrdMetrics *m, gboolean log_scale)
{
    guint32 max_inner = 0, max_all = 0;
    for (int i = 0; i < METRICS_HISTOGRAM_BINS; i++) {
        max_all = MAX (max_all, m->bins[i]);
        if (i > 0 && i < METRICS_HISTOGRAM_BINS - 1)
            max_inner = MAX (max_inner, m->bins[i]);
    }
    return MAX (1.0, log_scale || max_inner == 0 ? max_all : max_inner);
}

// Follows the highest bar, but only when it changed by a factor HISTOGRAM_RESCALE, and not more often than every
// HISTOGRAM_RESCALE_INTERVAL_US: the scale does not jump with the noise (or forced: no scale yet, new log / linear)
static void histogram_update_scale (LrdMetricsPanel *panel, const LrdMetrics *m)
{
    gboolean const log_scale = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (panel->log_check));
    double const highest = histogram_highest_bar (m, log_scale);
    gint64 const now_us = g_get_monotonic_time ();
    gboolean const forced = panel->histogram_top <= 0 || log_scale != panel->histogram_top_log;
    gboolean const big_change = highest >= HISTOGRAM_RESCALE * panel->histogram_top ||
                                highest <= panel->histogram_top / HISTOGRAM_RESCALE;
    if (forced || (big_change && now_us - panel->histogram_top_time_us >= HISTOGRAM_RESCALE_INTERVAL_US)) {
        panel->histogram_top = highest;
        panel->histogram_top_time_us = now_us;
        panel->histogram_top_log = log_scale;
    }
}

static char *histogram_text (const LrdMetrics *m)
{
    double const n = MAX (1.0, (double) m->n_pixels);
    double const saturated = 100.0 * m->n_saturated / n, black = 100.0 * m->n_black / n;
    g_autofree char *saturated_text = m->n_saturated > 0
            ? g_strdup_printf ("<span color=\"#db332e\"><b>%.2f %%</b></span>", saturated)
            : g_strdup ("0 %");
    return g_strdup_printf ("<small>Min %d   Max %d   (full scale %d)\n"
                            "Mean %.1f   Std dev %.1f\n"
                            "1 %% below %d   1 %% above %d\n"
                            "Saturated %s   Black %.2f %%</small>",
                            m->min, m->max, m->full_scale, m->mean, m->std, m->p01, m->p99,
                            saturated_text, black);
}

// ALIGNMENT

static void draw_needle (cairo_t *cr, double x, double base, double direction, gboolean faint, double r, double g,
                         double b)
{
    // Triangle pointing at the scale (direction 1: down, -1: up)
    cairo_move_to (cr, x, base);
    cairo_line_to (cr, x - 6, base - direction * 11);
    cairo_line_to (cr, x + 6, base - direction * 11);
    cairo_close_path (cr);
    cairo_set_source_rgb (cr, r, g, b);
    if (faint) {
        cairo_set_line_width (cr, 1.5);
        cairo_stroke (cr);
    } else {
        cairo_fill (cr);
    }
}

static gboolean gauge_draw_cb (GtkWidget *area, cairo_t *cr, LrdViewer *viewer)
{
    int const width = gtk_widget_get_allocated_width (area);
    int const height = gtk_widget_get_allocated_height (area);
    dark_background (cr, width, height);

    double const margin = 14;
    double const half = width / 2.0 - margin;
    double const center = width / 2.0;
    double const bar_top = height / 2.0 - 5, bar_height = 10;
#define GAUGE_X(tilt) (center + gauge_position (tilt) * half)

    // Zones
    double const zones[] = {GAUGE_RANGE, ALIGN_FAIR, ALIGN_GOOD};
    for (int z = 0; z < 3; z++) {
        if (z == 0)
            cairo_set_source_rgb (cr, COLOR_BAD);
        else if (z == 1)
            cairo_set_source_rgb (cr, COLOR_FAIR);
        else
            cairo_set_source_rgb (cr, COLOR_GOOD);
        double const x0 = GAUGE_X (-zones[z]), x1 = GAUGE_X (zones[z]);
        cairo_rectangle (cr, x0, bar_top, x1 - x0, bar_height);
        cairo_fill (cr);
    }

    // Ticks, labelled in degrees
    const struct { double tilt; const char *label; } ticks[] = {
        {0, "0"}, {0.1, "0.1"}, {1, "1"}, {10, "10"}, {45, "45°"}
    };
    cairo_set_source_rgb (cr, 0.9, 0.9, 0.9);
    cairo_set_line_width (cr, 1);
    for (gsize i = 0; i < G_N_ELEMENTS (ticks); i++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (ticks[i].tilt == 0 && sign > 0)
                continue;
            double const x = floor (GAUGE_X (sign * ticks[i].tilt)) + 0.5;
            cairo_move_to (cr, x, bar_top - 2);
            cairo_line_to (cr, x, bar_top + bar_height + 2);
        }
    }
    cairo_stroke (cr);
    for (gsize i = 0; i < G_N_ELEMENTS (ticks); i++)
        for (int sign = -1; sign <= 1; sign += 2) {
            if (ticks[i].tilt == 0 && sign > 0)
                continue;
            g_autofree char *markup = g_strdup_printf ("<span size=\"x-small\">%s</span>", ticks[i].label);
            draw_centered_text (area, cr, markup, GAUGE_X (sign * ticks[i].tilt), height - 7, 0.7, 0.7, 0.7);
        }
    draw_centered_text (area, cr, "<span size=\"x-small\">↺</span>", 7, height / 2.0, 0.8, 0.8, 0.8);
    draw_centered_text (area, cr, "<span size=\"x-small\">↻</span>", width - 7, height / 2.0, 0.8, 0.8, 0.8);

    // Needles: verticals above the scale, horizontals below
    const LrdMetrics *m = analysis_lock_metrics (viewer);
    if (m != NULL && m->request.alignment) {
        MetricsLineFamily verticals, horizontals;
        displayed_families (viewer, m, &verticals, &horizontals);
        if (family_found (&verticals))
            draw_needle (cr, GAUGE_X (verticals.tilt), bar_top - 1, 1, verticals.quality < QUALITY_WEAK,
                         COLOR_VERTICALS);
        if (family_found (&horizontals))
            draw_needle (cr, GAUGE_X (horizontals.tilt), bar_top + bar_height + 1, -1,
                         horizontals.quality < QUALITY_WEAK, COLOR_HORIZONTALS);
    }
    analysis_unlock_metrics (viewer);
#undef GAUGE_X
    return FALSE;
}

static void append_family_text (GString *text, const char *name, const char *color, const MetricsLineFamily *family)
{
    g_string_append_printf (text, "<span color=\"%s\">■</span> %s: ", color, name);
    if (!family->computed) {
        g_string_append (text, "region too small\n");
    } else if (!family_found (family)) {
        g_string_append (text, "<i>no straight lines</i>\n");
    } else {
        double const t = family->tilt;
        g_string_append_printf (text, "<span color=\"%s\"><b>%+.3f°</b></span> %s",
                                tilt_color (t), t, t > 0 ? "↻" : t < 0 ? "↺" : "");
        g_string_append_printf (text, "\n<small>    lines %.0f %%%s, average of %.0f frame%s</small>\n",
                                100 * family->quality, family->quality < QUALITY_WEAK ? " (faint)" : "",
                                family->frames, family->frames >= 1.5 ? "s" : "");
    }
}

static char *alignment_text (LrdViewer *viewer, const LrdMetrics *m)
{
    MetricsLineFamily verticals, horizontals;
    displayed_families (viewer, m, &verticals, &horizontals);
    GString *text = g_string_new (NULL);
    append_family_text (text, "Verticals", "#40a0ff", &verticals);
    append_family_text (text, "Horizontals", "#f273d9", &horizontals);
    if (text->len > 0 && text->str[text->len - 1] == '\n')
        g_string_truncate (text, text->len - 1);
    return g_string_free (text, FALSE);
}

// FOURIER TRANSFORM

static gboolean fft_draw_cb (GtkWidget *area, cairo_t *cr, LrdViewer *viewer)
{
    int const width = gtk_widget_get_allocated_width (area);
    int const height = gtk_widget_get_allocated_height (area);
    dark_background (cr, width, height);

    const LrdMetrics *m = analysis_lock_metrics (viewer);
    if (m != NULL && m->request.fft && m->fft_pixels != NULL) {
        int const fw = m->fft_width, fh = m->fft_height;
        // The spectrum, with equal frequency scales (fft_aspect), rotated / flipped like the video
        double const sw = m->fft_aspect, sh = 1.0;
        gint const method = get_videoflip_method (viewer);
        gboolean const transposed = method == 1 || method == 3 || method == 6 || method == 7;
        double const shown_w = transposed ? sh : sw, shown_h = transposed ? sw : sh;
        double const scale = MIN (width / shown_w, height / shown_h);

        cairo_surface_t *surface = cairo_image_surface_create_for_data (m->fft_pixels, CAIRO_FORMAT_RGB24,
                                                                        fw, fh, m->fft_stride);
        cairo_save (cr);
        cairo_translate (cr, (width - shown_w * scale) / 2, (height - shown_h * scale) / 2);
        cairo_scale (cr, scale, scale);
        cairo_matrix_t flip;
        overlay_videoflip_matrix (method, sw, sh, &flip);
        cairo_transform (cr, &flip);
        cairo_scale (cr, sw / fw, sh / fh);
        cairo_set_source_surface (cr, surface, 0, 0);
        cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_NEAREST);
        cairo_rectangle (cr, 0, 0, fw, fh);
        cairo_fill (cr);
        cairo_restore (cr);
        cairo_surface_destroy (surface);
    }
    analysis_unlock_metrics (viewer);
    return FALSE;
}

// UPDATES

void metrics_panel_update (LrdViewer *viewer)
{
    LrdMetricsPanel *panel = viewer->metrics_panel;
    if (panel == NULL || !viewer->metrics_visible)
        return;
    analysis_update_metrics_region (viewer);

    const LrdMetrics *m = analysis_lock_metrics (viewer);
    g_autofree char *region = NULL;
    if (viewer->video_widget == NULL)
        region = g_strdup ("<i>No video</i>");
    else if (!analysis_metrics_available (viewer))
        region = g_strdup ("<i>Not available for this pixel format</i>");
    else if (m == NULL)
        region = g_strdup ("<i>Waiting for images…</i>");
    else {
        const MetricsRequest *r = &m->request;
        gboolean const whole = r->x == 0 && r->y == 0 && r->width == viewer->display_image_width &&
                               r->height == viewer->display_image_height;
        region = whole ? g_strdup_printf ("Whole image, %d × %d", r->width, r->height)
                       : g_strdup_printf ("ROI, %d × %d at (%d, %d)", r->width, r->height, r->x, r->y);
        if (r->layout.bayer) {
            char *with_note = g_strconcat (region, "\n<small>Raw Bayer values (all colors mixed)</small>", NULL);
            g_free (region);
            region = with_note;
        } else if (r->layout.luma) {
            char *with_note = g_strconcat (region, "\n<small>Luma of the colors</small>", NULL);
            g_free (region);
            region = with_note;
        }
    }
    gtk_label_set_markup (GTK_LABEL (panel->region_label), region);

    if (m != NULL && m->request.histogram && m->n_pixels > 0) {
        histogram_update_scale (panel, m);
        g_autofree char *text = histogram_text (m);
        gtk_label_set_markup (GTK_LABEL (panel->stats_label), text);
    } else {
        gtk_label_set_text (GTK_LABEL (panel->stats_label), "");
    }
    if (m != NULL && m->request.alignment) {
        g_autofree char *text = alignment_text (viewer, m);
        gtk_label_set_markup (GTK_LABEL (panel->alignment_label), text);
    } else {
        gtk_label_set_text (GTK_LABEL (panel->alignment_label), "");
    }
    if (m != NULL && m->request.fft && m->fft_pixels != NULL) {
        g_autofree char *text = g_strdup_printf ("<small>%d × %d bins of %.2f × %.2f pixels, log power</small>",
                                                 m->fft_width, m->fft_height,
                                                 (double) m->request.width / m->fft_width,
                                                 (double) m->request.height / m->fft_height);
        gtk_label_set_markup (GTK_LABEL (panel->fft_label), text);
    } else {
        gtk_label_set_text (GTK_LABEL (panel->fft_label), "");
    }
    analysis_unlock_metrics (viewer);

    gtk_widget_queue_draw (panel->histogram_area);
    gtk_widget_queue_draw (panel->gauge_area);
    gtk_widget_queue_draw (panel->fft_area);
}

// Shows or hides the panel, widening or narrowing the window by its width so that the video keeps its size
static void set_panel_visible (LrdViewer *viewer, gboolean visible)
{
    if (visible == viewer->metrics_visible)
        return;
    GtkWindow *window = GTK_WINDOW (viewer->main_window);
    gboolean const free_size = !viewer->is_fullscreen && !gtk_window_is_maximized (window);
    int width, height;
    gtk_window_get_size (window, &width, &height);

    viewer->metrics_visible = visible;
    gtk_revealer_set_reveal_child (GTK_REVEALER (viewer->metrics_revealer), visible);
    if (visible) {
        int minimum = 0, natural = 0;
        GtkWidget *child = gtk_bin_get_child (GTK_BIN (viewer->metrics_revealer));
        gtk_widget_get_preferred_width (child, &minimum, &natural);
        viewer->metrics_window_growth = free_size ? MAX (minimum, natural) : 0;
        if (viewer->metrics_window_growth > 0)
            gtk_window_resize (window, width + viewer->metrics_window_growth, height);
    } else {
        if (viewer->metrics_window_growth > 0 && free_size)
            gtk_window_resize (window, MAX (1, width - viewer->metrics_window_growth), height);
        viewer->metrics_window_growth = 0;
    }
    log_debug ("Image metrics panel %s.", visible ? "shown" : "hidden");

    analysis_settings_changed (viewer);
    metrics_panel_update (viewer);
}

static void metrics_toggle_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    set_panel_visible (viewer, gtk_toggle_button_get_active (toggle));
}

void toggle_metrics_panel (LrdViewer *viewer)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (viewer->metrics_toggle), !viewer->metrics_visible);
}

static void section_expanded_cb (GtkExpander *expander, GParamSpec *pspec, LrdViewer *viewer)
{
    gboolean *flag = g_object_get_data (G_OBJECT (expander), "metrics-flag");
    *flag = gtk_expander_get_expanded (expander);
    analysis_settings_changed (viewer);
    metrics_panel_update (viewer);
}

static void log_scale_toggled_cb (GtkToggleButton *toggle, LrdViewer *viewer)
{
    // (new scale at once)
    const LrdMetrics *m = analysis_lock_metrics (viewer);
    if (m != NULL && m->request.histogram && m->n_pixels > 0)
        histogram_update_scale (viewer->metrics_panel, m);
    analysis_unlock_metrics (viewer);
    gtk_widget_queue_draw (viewer->metrics_panel->histogram_area);
}

// CONSTRUCTION

static GtkWidget *wrapped_label (void)
{
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 1);    // (wraps at the width of the panel)
    return label;
}

// A section: an expander whose state is kept in *flag (the metric is computed when it is expanded)
static GtkWidget *add_section (LrdViewer *viewer, GtkWidget *box, const char *title, const char *tooltip,
                               gboolean *flag, GtkWidget *content)
{
    g_autofree char *markup = g_strdup_printf ("<b>%s</b>", title);
    GtkWidget *expander = gtk_expander_new (markup);
    gtk_expander_set_use_markup (GTK_EXPANDER (expander), TRUE);
    gtk_expander_set_expanded (GTK_EXPANDER (expander), *flag);
    gtk_widget_set_can_focus (expander, FALSE);
    gtk_widget_set_tooltip_text (gtk_expander_get_label_widget (GTK_EXPANDER (expander)), tooltip);
    gtk_widget_set_margin_top (content, 4);
    gtk_container_add (GTK_CONTAINER (expander), content);
    g_object_set_data (G_OBJECT (expander), "metrics-flag", flag);
    g_signal_connect (expander, "notify::expanded", G_CALLBACK (section_expanded_cb), viewer);
    gtk_box_pack_start (GTK_BOX (box), expander, FALSE, FALSE, 0);
    return expander;
}

static GtkWidget *drawing_area (LrdViewer *viewer, int height, GCallback draw_cb)
{
    GtkWidget *area = gtk_drawing_area_new ();
    gtk_widget_set_size_request (area, -1, height);
    g_signal_connect (area, "draw", draw_cb, viewer);
    return area;
}

void metrics_panel_init (LrdViewer *viewer)
{
    LrdMetricsPanel *panel = g_new0 (LrdMetricsPanel, 1);
    viewer->metrics_panel = panel;
    viewer->metrics_visible = FALSE;
    viewer->metrics_histogram = TRUE;
    viewer->metrics_alignment = FALSE;    // expanded when needed
    viewer->metrics_fft = FALSE;          // the costliest, and the least used
    viewer->metrics_window_growth = 0;

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    g_object_set (box, "margin", PANEL_MARGIN, NULL);

    panel->region_label = wrapped_label ();
    gtk_box_pack_start (GTK_BOX (box), panel->region_label, FALSE, FALSE, 0);

    // Histogram
    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    panel->histogram_area = drawing_area (viewer, HISTOGRAM_HEIGHT, G_CALLBACK (histogram_draw_cb));
    gtk_widget_set_tooltip_text (panel->histogram_area, "256 bins from black to full scale, whatever the bit "
                                                        "depth. Blue: black pixels, red: saturated pixels, "
                                                        "yellow: mean.");
    gtk_box_pack_start (GTK_BOX (content), panel->histogram_area, FALSE, FALSE, 0);
    panel->log_check = gtk_check_button_new_with_label ("Logarithmic scale");
    // (no focus: the keys, space first, must keep their meaning for the video)
    gtk_widget_set_can_focus (panel->log_check, FALSE);
    g_signal_connect (panel->log_check, "toggled", G_CALLBACK (log_scale_toggled_cb), viewer);
    gtk_box_pack_start (GTK_BOX (content), panel->log_check, FALSE, FALSE, 0);
    panel->stats_label = wrapped_label ();
    gtk_box_pack_start (GTK_BOX (content), panel->stats_label, FALSE, FALSE, 0);
    add_section (viewer, box, "Histogram", "Histogram and statistics of the ROI (or of the whole image), in the "
                 "units of the camera", &viewer->metrics_histogram, content);

    // Alignment
    content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    panel->gauge_area = drawing_area (viewer, GAUGE_HEIGHT, G_CALLBACK (gauge_draw_cb));
    gtk_widget_set_tooltip_text (panel->gauge_area, "Rotation of the straight lines on the screen (↻ clockwise): "
                                                    "vertical lines above the scale, horizontal lines below. Green: "
                                                    "better than 0.05°, orange: better than 0.5°. The angle is "
                                                    "averaged over the frames while the image does not move.");
    gtk_box_pack_start (GTK_BOX (content), panel->gauge_area, FALSE, FALSE, 0);
    panel->alignment_label = wrapped_label ();
    gtk_box_pack_start (GTK_BOX (content), panel->alignment_label, FALSE, FALSE, 0);
    add_section (viewer, box, "Alignment", "Angle of the nearly vertical and nearly horizontal straight lines (walls, "
                 "light sheets, grids...) of the ROI (or of the whole image), to a few hundredths of a degree",
                 &viewer->metrics_alignment, content);

    // Fourier transform
    content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    panel->fft_area = drawing_area (viewer, FFT_HEIGHT, G_CALLBACK (fft_draw_cb));
    gtk_box_pack_start (GTK_BOX (content), panel->fft_area, FALSE, FALSE, 0);
    panel->fft_label = wrapped_label ();
    gtk_box_pack_start (GTK_BOX (content), panel->fft_label, FALSE, FALSE, 0);
    add_section (viewer, box, "Fourier transform", "Power spectrum (logarithmic) of the ROI (or of the whole image), "
                 "binned to at most 256 × 256, with a Hann window", &viewer->metrics_fft, content);

    GtkWidget *scrolled = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (scrolled, PANEL_WIDTH, -1);
    gtk_container_add (GTK_CONTAINER (scrolled), box);

    GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start (GTK_BOX (outer), gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (outer), scrolled, TRUE, TRUE, 0);
    gtk_widget_show_all (outer);

    // (no transition: the window is resized at once, see set_panel_visible)
    gtk_revealer_set_transition_type (GTK_REVEALER (viewer->metrics_revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
    gtk_revealer_set_reveal_child (GTK_REVEALER (viewer->metrics_revealer), FALSE);
    gtk_container_add (GTK_CONTAINER (viewer->metrics_revealer), outer);

    g_signal_connect (viewer->metrics_toggle, "toggled", G_CALLBACK (metrics_toggle_cb), viewer);
}

void metrics_panel_finalize (LrdViewer *viewer)
{
    g_clear_pointer (&viewer->metrics_panel, g_free);
}
