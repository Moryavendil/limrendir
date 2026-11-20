// Tests of the image metrics on synthetic images (no camera needed):
//     cmake --build build --target metrics_test && ./build/metrics_test
// Exits with a non-zero status if a check fails.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "limrendir_metrics.h"

typedef enum {
    SCENE_EDGE,          // a vertical step edge through the center
    SCENE_LIGHT_SHEET,   // a vertical bright band (Gaussian profile), on a dark background
    SCENE_GRID,          // a grid of thin dark lines on a bright background
    SCENE_HORIZON,       // a horizontal step edge (e.g. a free surface)
    SCENE_NOISE          // nothing but noise
} Scene;

static const char *scene_name (Scene scene)
{
    switch (scene) {
        case SCENE_EDGE: return "edge";
        case SCENE_LIGHT_SHEET: return "light sheet";
        case SCENE_GRID: return "grid";
        case SCENE_HORIZON: return "horizon";
        default: return "noise";
    }
}

// Brightness of the scene at a point of the scene (0 to 1), the scene being centered on (0, 0)
static double scene_value (Scene scene, double xs, double ys, double blur, double grid_period)
{
    switch (scene) {
        case SCENE_EDGE:
            return blur > 0 ? 0.2 + 0.6 * 0.5 * (1 + erf (xs / (G_SQRT2 * blur))) : (xs >= 0 ? 0.8 : 0.2);
        case SCENE_HORIZON:
            return blur > 0 ? 0.2 + 0.6 * 0.5 * (1 + erf (ys / (G_SQRT2 * blur))) : (ys >= 0 ? 0.8 : 0.2);
        case SCENE_LIGHT_SHEET:
            return 0.1 + 0.7 * exp (-0.5 * (xs - 37) * (xs - 37) / (12.0 * 12.0));
        case SCENE_GRID: {
            double const dx = fabs (xs - grid_period * floor (xs / grid_period + 0.5));
            double const dy = fabs (ys - grid_period * floor (ys / grid_period + 0.5));
            return (dx < 1.0 || dy < 1.0) ? 0.25 : 0.85;
        }
        default:
            return 0.5;
    }
}

static double gaussian_noise (void)
{
    double const u1 = (rand () + 1.0) / (RAND_MAX + 2.0), u2 = (rand () + 1.0) / (RAND_MAX + 2.0);
    return sqrt (-2 * log (u1)) * cos (2 * G_PI * u2);
}

// Renders the scene turned clockwise (on screen, y down) by 'tilt' degrees, area-sampled with ss x ss samples per
// pixel (1: point sampled, aliased), as Mono8 (bits = 8) or Mono12 scaled to 16 bits (bits = 12).
// Returns the rows (stride bytes apart).
static guint8 *render (Scene scene, int width, int height, double tilt, int ss, double blur, double noise,
                       int bits, double grid_period, gsize *stride)
{
    int const bpp = bits == 8 ? 1 : 2;
    *stride = ((gsize) width * bpp + 3) & ~(gsize) 3;
    guint8 *data = g_malloc0 (*stride * height);
    double const c = cos (tilt * G_PI / 180), s = sin (tilt * G_PI / 180);
    int const full = (1 << bits) - 1;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double sum = 0;
            for (int j = 0; j < ss; j++) {
                for (int i = 0; i < ss; i++) {
                    double const px = x + (i + 0.5) / ss - width / 2.0;
                    double const py = y + (j + 0.5) / ss - height / 2.0;
                    // Image to scene: rotation by -tilt
                    sum += scene_value (scene, px * c + py * s, -px * s + py * c, blur, grid_period);
                }
            }
            double v = sum / (ss * ss) * full + noise * full * gaussian_noise ();
            int const value = CLAMP ((int) lround (v), 0, full);
            if (bits == 8)
                data[(gsize) y * *stride + x] = (guint8) value;
            else
                ((guint16 *) (data + (gsize) y * *stride))[x] =
                        GUINT16_TO_LE ((guint16) ((value << (16 - bits)) | (value >> (2 * bits - 16))));
        }
    }
    return data;
}

static int n_failures = 0;
static double rows_ms;    // time spent reading the rows of the last frame analysed (the frame is held meanwhile)
// Frames are independent (a new series each), unless a sequence is being analysed (series 0, sequence_time_us)
static gboolean in_sequence = FALSE;
static gint64 sequence_time_us = 0;
static guint n_series = 0;

static void check (gboolean ok, const char *what)
{
    if (!ok) {
        n_failures++;
        printf ("    FAILED: %s\n", what);
    }
}

static void analyse (MetricsAccumulator *acc, const guint8 *data, gsize stride, int bits, int x, int y,
                     int width, int height, gboolean histogram, gboolean alignment, gboolean fft, LrdMetrics *out)
{
    MetricsRequest request = {0};
    request.histogram = histogram;
    request.alignment = alignment;
    request.fft = fft;
    metrics_layout_from_pixel_format (bits == 8 ? ARV_PIXEL_FORMAT_MONO_8 : ARV_PIXEL_FORMAT_MONO_12,
                                      &request.layout);
    request.x = x;
    request.y = y;
    request.width = width;
    request.height = height;
    request.pixel_aspect = 1;
    request.series = in_sequence ? 0 : ++n_series;
    request.time_us = sequence_time_us;
    int const bpp = request.layout.bytes_per_pixel;
    gint64 const start_us = g_get_monotonic_time ();
    metrics_begin (acc, &request);
    for (int row = 0; row < height; row++)
        metrics_add_row (acc, data + (gsize) (y + row) * stride + (gsize) x * bpp, row);
    rows_ms = (g_get_monotonic_time () - start_us) / 1000.0;
    metrics_finish (acc, out);
}

// Alignment of one family, compared with the tilt of the scene
static double test_alignment (MetricsAccumulator *acc, Scene scene, int width, int height, double tilt, int ss,
                              double blur, double noise, int bits, double grid_period, double tolerance)
{
    gsize stride;
    guint8 *data = render (scene, width, height, tilt, ss, blur, noise, bits, grid_period, &stride);
    LrdMetrics metrics = {0};
    analyse (acc, data, stride, bits, 0, 0, width, height, FALSE, TRUE, FALSE, &metrics);
    gboolean const horizontal = scene == SCENE_HORIZON;
    const MetricsLineFamily *family = horizontal ? &metrics.horizontals : &metrics.verticals;
    const MetricsLineFamily *other = horizontal ? &metrics.verticals : &metrics.horizontals;
    double const error = family->tilt - tilt;
    printf ("  %-11s %4dx%-4d %2d-bit tilt %+8.3f  ss %d blur %.1f noise %.3f: %s %+8.4f (error %+.4f) "
            "quality %.2f | other %+7.3f quality %.2f | rows %.1f ms, finish %.1f ms\n",
            scene_name (scene), width, height, bits, tilt, ss, blur, noise, horizontal ? "horizontals" : "verticals",
            family->tilt, error, family->quality, other->tilt, other->quality, rows_ms, metrics.finish_ms);
    // Beyond 10 degrees, the lines are blurred within each band: lower quality, looser tolerance (the aim is the
    // fine alignment, a rough angle is enough there)
    double const min_quality = fabs (tilt) <= 10 ? 0.3 : 0.03;
    if (fabs (tilt) > 10)
        tolerance = MAX (tolerance, 0.1);
    if (scene == SCENE_NOISE)
        check (family->quality < 0.05 && other->quality < 0.05, "noise must give a low quality");
    else {
        check (family->computed && fabs (error) <= tolerance, "angle error");
        check (family->quality > min_quality, "quality of real lines");
    }
    if (scene == SCENE_GRID)
        check (fabs (other->tilt - tilt) <= tolerance && other->quality > min_quality, "grid: the other family");
    metrics_clear (&metrics);
    g_free (data);
    return error;
}

// (MAX would evaluate the test twice)
static void note_error (double *max_error, double error)
{
    *max_error = MAX (*max_error, fabs (error));
}

static void test_histogram (MetricsAccumulator *acc)
{
    printf ("Histogram\n");
    // Ramp 0..4095 (12 bits), each value 16 times: 256 bins of 256 pixels
    int const width = 256, height = 256;
    gsize const stride = width * 2;
    guint8 *data = g_malloc (stride * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            int const value = (y * width + x) / 16;
            ((guint16 *) (data + y * stride))[x] = GUINT16_TO_LE ((guint16) ((value << 4) | (value >> 8)));
        }
    LrdMetrics m = {0};
    analyse (acc, data, stride, 12, 0, 0, width, height, TRUE, FALSE, FALSE, &m);
    gboolean flat = TRUE;
    for (int i = 0; i < METRICS_HISTOGRAM_BINS; i++)
        flat = flat && m.bins[i] == 256;
    printf ("  12-bit ramp: min %d max %d mean %.2f std %.2f p01 %d p99 %d saturated %" G_GUINT64_FORMAT
            " black %" G_GUINT64_FORMAT " full scale %d\n",
            m.min, m.max, m.mean, m.std, m.p01, m.p99, m.n_saturated, m.n_black, m.full_scale);
    check (flat, "12-bit ramp: flat histogram");
    check (m.min == 0 && m.max == 4095 && m.full_scale == 4095, "12-bit ramp: min / max");
    check (fabs (m.mean - 2047.5) < 1e-6, "12-bit ramp: mean");
    check (m.n_saturated == 16 && m.n_black == 16, "12-bit ramp: saturated / black");

    // RGB: the luma of (255, 255, 255) is 255 (saturated), of (255, 0, 0) is 77
    {
        guint8 rgb[2 * 3] = {255, 255, 255, 255, 0, 0};
        MetricsRequest request = {0};
        request.histogram = TRUE;
        metrics_layout_from_pixel_format (ARV_PIXEL_FORMAT_RGB_8_PACKED, &request.layout);
        request.width = 2;
        request.height = 1;
        LrdMetrics rgb_metrics = {0};
        metrics_begin (acc, &request);
        metrics_add_row (acc, rgb, 0);
        metrics_finish (acc, &rgb_metrics);
        printf ("  RGB luma: min %d max %d saturated %" G_GUINT64_FORMAT "\n", rgb_metrics.min, rgb_metrics.max,
                rgb_metrics.n_saturated);
        check (rgb_metrics.min == 77 && rgb_metrics.max == 255 && rgb_metrics.n_saturated == 1, "RGB luma");
    }

    // ROI: a sub-region of the ramp (rows 64 to 127: values 1024 to 2047)
    analyse (acc, data, stride, 12, 0, 64, width, 64, TRUE, FALSE, FALSE, &m);
    printf ("  12-bit ramp, region of rows 64-127: min %d max %d\n", m.min, m.max);
    check (m.min == 1024 && m.max == 2047 && m.n_pixels == 64 * 256, "region");
    metrics_clear (&m);
    g_free (data);
}

static void test_fft (MetricsAccumulator *acc)
{
    printf ("Fourier transform\n");
    // A sinusoid along x of period 16 pixels, on 640 x 200: 256 x 128 bins (2.5 x 1.5625 pixels per bin)
    int const width = 640, height = 200;
    gsize const stride = width;
    guint8 *data = g_malloc (stride * height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            data[y * stride + x] = (guint8) lround (128 + 100 * sin (2 * G_PI * x / 16.0));
    LrdMetrics m = {0};
    analyse (acc, data, stride, 8, 0, 0, width, height, FALSE, FALSE, TRUE, &m);
    printf ("  %dx%d bins, aspect %.3f, %.2f ms\n", m.fft_width, m.fft_height, m.fft_aspect, m.finish_ms);
    check (m.fft_width == 256 && m.fft_height == 128, "FFT size");
    // Brightest pixel: frequency 1/16 cycle per pixel = 2.5/16 cycle per bin -> 256 * 2.5 / 16 = 40 from the center
    int best_x = 0, best_y = 0;
    guint32 best = 0;
    for (int j = 0; j < m.fft_height; j++)
        for (int i = 0; i < m.fft_width; i++) {
            guint32 const rgb = ((guint32 *) (m.fft_pixels + j * m.fft_stride))[i];
            guint32 const brightness = ((rgb >> 16) & 0xff) + ((rgb >> 8) & 0xff) + (rgb & 0xff);
            if (brightness > best) {
                best = brightness;
                best_x = i;
                best_y = j;
            }
        }
    guint32 const expected = ((guint32 *) (m.fft_pixels + 64 * m.fft_stride))[128 + 40];
    guint32 const expected_brightness = ((expected >> 16) & 0xff) + ((expected >> 8) & 0xff) + (expected & 0xff);
    printf ("  brightest at (%d, %d), expected (%d or %d, %d), brightness there %u / %u\n", best_x, best_y,
            128 - 40, 128 + 40, 64, expected_brightness, best);
    // (the brightest pixels are all white: the peak may share it with its neighbours or a harmonic)
    check (expected_brightness == best, "FFT peak position");

    // Small region: 2^n x 2^m with the largest powers of 2 not bigger than the region
    analyse (acc, data, stride, 8, 10, 10, 100, 37, FALSE, FALSE, TRUE, &m);
    printf ("  region 100x37: %dx%d bins\n", m.fft_width, m.fft_height);
    check (m.fft_width == 64 && m.fft_height == 32, "FFT size of a small region");
    metrics_clear (&m);
    g_free (data);
}

// A sequence of frames of a static faint scene (fresh noise in each, 250 ms apart): the average over time must
// steady the angle; then the scene turns by 0.2 degree: the angle must follow at once
static void test_sequence (MetricsAccumulator *acc, Scene scene, double contrast, double max_std)
{
    int const width = 300, height = 200;
    double const tilt = 0.1, noise = 0.01, turn = 0.2;
    in_sequence = TRUE;
    double sum = 0, sum_of_squares = 0, after_turn = 0, later = 0;
    int n = 0;
    for (int i = 0; i < 48; i++) {
        double const truth = tilt + (i >= 40 ? turn : 0);
        gsize stride;
        guint8 *data = render (scene, width, height, truth, 4, 1.5, 0, 8, 25.0, &stride);
        for (gsize k = 0; k < stride * height; k++)
            data[k] = (guint8) CLAMP (lround (128 + (data[k] - 128) * contrast + noise * 255 * gaussian_noise ()),
                                      0, 255);
        LrdMetrics m = {0};
        analyse (acc, data, stride, 8, 0, 0, width, height, FALSE, TRUE, FALSE, &m);
        sequence_time_us += G_USEC_PER_SEC / 4;
        if (i >= 16 && i < 40) {
            sum += m.verticals.tilt;
            sum_of_squares += m.verticals.tilt * m.verticals.tilt;
            n++;
        }
        if (i == 41)
            after_turn = m.verticals.tilt;
        if (i == 47)
            later = m.verticals.tilt;
        metrics_clear (&m);
        g_free (data);
    }
    in_sequence = FALSE;
    double const mean = sum / n, std = sqrt (MAX (0.0, sum_of_squares / n - mean * mean));
    printf ("  %-11s %dx%d contrast %.2f noise %.2f: steady mean %+.4f std %.4f; after a turn of %+.1f: %+.4f "
            "(0.5 s), %+.4f (2 s)\n", scene_name (scene), width, height, contrast, noise, mean, std, turn,
            after_turn, later);
    check (fabs (mean - tilt) < 0.015 && std < max_std, "steady angle of a static scene");
    // A turn much larger than the noise of a frame is seen at once, a smaller one within 2 seconds
    if (max_std <= 0.01)
        check (fabs (after_turn - (tilt + turn)) < 0.05, "angle 0.5 s after a turn");
    check (fabs (later - (tilt + turn)) < 0.05, "angle 2 s after a turn");
}

int main (void)
{
    srand (1);
    MetricsAccumulator *acc = metrics_accumulator_new ();

    test_histogram (acc);
    test_fft (acc);

    printf ("Alignment\n");
    double const tolerance = 0.02;    // degrees (the eye sees about 1 degree; the aim is 0.05)
    double const tilts[] = {0, 0.01, -0.03, 0.05, 0.13, -0.37, 1.0, -2.5, 7.3, -20, 33};
    double max_error = 0;
    for (gsize i = 0; i < G_N_ELEMENTS (tilts); i++) {
        double const tilt = tilts[i];
        // Blurred (realistic optics), area sampled
        note_error (&max_error, test_alignment (acc, SCENE_EDGE, 1280, 1024, tilt, 4, 1.0, 0.01, 8, 0, tolerance));
        // Perfectly sharp (no blur at all, area sampled: the worst case of a real camera)
        note_error (&max_error, test_alignment (acc, SCENE_EDGE, 1280, 1024, tilt, 4, 0, 0.01, 8, 0, tolerance));
        note_error (&max_error, test_alignment (acc, SCENE_LIGHT_SHEET, 1280, 1024, tilt, 2, 0, 0.02, 12, 0,
                                                tolerance));
        note_error (&max_error, test_alignment (acc, SCENE_GRID, 1024, 768, tilt, 4, 0, 0.02, 8, 25.0, tolerance));
        note_error (&max_error, test_alignment (acc, SCENE_HORIZON, 640, 480, tilt, 4, 1.5, 0.02, 12, 0, tolerance));
    }
    // Fine grid (period 9 pixels, lines 2 pixels wide), near the alignment
    test_alignment (acc, SCENE_GRID, 1024, 768, 0.04, 8, 0, 0.02, 8, 9.0, tolerance);
    test_alignment (acc, SCENE_GRID, 1024, 768, -0.6, 8, 0, 0.02, 8, 9.0, tolerance);
    // Coarse grid, small and big images
    test_alignment (acc, SCENE_GRID, 2048, 2048, 0.21, 2, 0, 0.02, 8, 64.0, tolerance);
    test_alignment (acc, SCENE_EDGE, 256, 200, -0.4, 4, 1.0, 0.01, 8, 0, 0.05);
    test_alignment (acc, SCENE_EDGE, 4096, 3000, 0.042, 2, 1.0, 0.01, 8, 0, tolerance);
    test_alignment (acc, SCENE_NOISE, 1280, 1024, 0, 1, 0, 0.05, 8, 0, tolerance);
    printf ("Largest alignment error: %.4f degree\n", max_error);

    printf ("Alignment of a sequence of noisy frames\n");
    test_sequence (acc, SCENE_EDGE, 0.1, 0.01);
    test_sequence (acc, SCENE_EDGE, 0.05, 0.02);
    test_sequence (acc, SCENE_LIGHT_SHEET, 0.2, 0.01);
    test_sequence (acc, SCENE_GRID, 0.1, 0.01);

    metrics_accumulator_free (acc);
    printf (n_failures == 0 ? "All tests passed\n" : "%d check(s) FAILED\n", n_failures);
    return n_failures == 0 ? 0 : 1;
}
