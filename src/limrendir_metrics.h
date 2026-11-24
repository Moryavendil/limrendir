#ifndef LIMRENDIR_METRICS_H
#define LIMRENDIR_METRICS_H

#include <glib.h>
#include <arv.h>

// IMAGE METRICS: histogram, alignment of the straight lines, 2-D Fourier transform
//
// Pure computations (no GTK), run by the analysis worker (limrendir_analysis.c) on a region of a frame:
// - metrics_begin prepares the accumulators for a region and the metrics asked,
// - metrics_add_row is called for each row of the region, while the frame is held (cheap work only: about 2 ns per
//   pixel with all the metrics),
// - metrics_finish computes the results once the frame is released (Fourier transforms, angle search).
// The frames are read as the display gets them (see arv_to_gst_buffer): rows padded to a multiple of 4 bytes, and
// the mono formats of more than 8 bits unpacked to 16 bits and scaled to the 16-bit range.

// How the samples of a frame are stored
typedef enum {
    METRICS_SAMPLES_NONE,    // not supported
    METRICS_SAMPLES_U8,      // 1 byte per pixel (Mono8, and the 8-bit Bayer formats read as they are)
    METRICS_SAMPLES_U16,     // 2 bytes per pixel, little endian, scaled to 16 bits from 'bits' bits
    METRICS_SAMPLES_RGB8,    // 3 bytes per pixel, R G B (the luma is analysed)
    METRICS_SAMPLES_BGR8     // 3 bytes per pixel, B G R (the luma is analysed)
} MetricsSamples;

typedef struct {
    MetricsSamples samples;
    int bits;                // significant bits of the native values (8, or 10 to 16 for METRICS_SAMPLES_U16)
    int bytes_per_pixel;
    gboolean bayer;          // raw Bayer mosaic: the values mix the colors
    gboolean luma;           // color pixels, analysed through their luma
} MetricsLayout;

// Layout of the frames of a pixel format, as given to the display. Returns FALSE if the format is not supported.
gboolean metrics_layout_from_pixel_format (ArvPixelFormat pixel_format, MetricsLayout *layout);

#define METRICS_HISTOGRAM_BINS 256
// The 2-D Fourier transform is computed on 2^n x 2^m bins of the region, with 2^n (2^m) the largest power of 2 not
// bigger than the width (height) of the region, and at most METRICS_FFT_MAX_SIZE
#define METRICS_FFT_MAX_SIZE 256

// What to compute, and where
typedef struct {
    gboolean histogram;
    gboolean alignment;
    gboolean fft;
    MetricsLayout layout;
    int x, y, width, height;   // region analysed (the ROI or the whole image), in image pixels
    double pixel_aspect;       // physical width / height of a pixel (horizontal binning / vertical binning)
    // The alignment is averaged over the successive frames of a series (same video, same region) while the scene
    // does not move: series and time of the frame
    guint series;
    gint64 time_us;
} MetricsRequest;

// Straight lines of one family (the nearly vertical ones, or the nearly horizontal ones)
typedef struct {
    gboolean computed;
    double tilt;               // degrees: rotation of the lines from the image axes, clockwise in the image as
                               // displayed without rotation nor flip (y pointing down). Within [-45, 45].
    double quality;            // 0 to 1: how much of the edge energy lies on straight lines at that angle
    double frames;             // number of frames averaged (1 just after the scene moved)
} MetricsLineFamily;

// Results for one frame
typedef struct {
    MetricsRequest request;    // what was computed, and where

    // Histogram of the region: METRICS_HISTOGRAM_BINS bins, whatever the bit depth (bin = value >> (bits - 8))
    guint32 bins[METRICS_HISTOGRAM_BINS];
    guint64 n_pixels;
    int full_scale;            // largest native value (255, 4095...)
    int min, max;              // native values
    double mean, std;          // native units
    int p01, p99;              // 1st and 99th percentiles, native values
    guint64 n_saturated;       // pixels at full scale
    guint64 n_black;           // pixels at 0

    // Alignment
    MetricsLineFamily verticals;
    MetricsLineFamily horizontals;

    // 2-D Fourier transform: log power spectrum, zero frequency at the center (fft_width / 2, fft_height / 2),
    // CAIRO_FORMAT_RGB24 pixels (rows fft_stride bytes apart)
    int fft_width, fft_height, fft_stride;
    guint8 *fft_pixels;        // owned (NULL if not computed)
    gsize fft_allocated;
    double fft_aspect;         // width / height of the spectrum shown, for equal frequency scales on screen

    double finish_ms;          // time spent in metrics_finish
} LrdMetrics;

typedef struct _MetricsAccumulator MetricsAccumulator;

MetricsAccumulator *metrics_accumulator_new (void);
void metrics_accumulator_free (MetricsAccumulator *acc);

// Prepares the accumulation of a region. Returns FALSE if the request cannot be fulfilled (region too small...).
gboolean metrics_begin (MetricsAccumulator *acc, const MetricsRequest *request);
// Adds row y (0 to height - 1, in order: the edge orientations use consecutive rows) of the region: 'row' points
// to its first pixel
void metrics_add_row (MetricsAccumulator *acc, const guint8 *row, int y);
// Computes the results (the frame is not needed anymore)
void metrics_finish (MetricsAccumulator *acc, LrdMetrics *out);

// Frees the storage of the results
void metrics_clear (LrdMetrics *metrics);

#endif //LIMRENDIR_METRICS_H
