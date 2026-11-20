#include <math.h>
#include <string.h>
#include "limrendir_metrics.h"
#include "limrendir_pixelformat.h"

// IMAGE METRICS (see limrendir_metrics.h)
//
// HISTOGRAM: a histogram of the native values (up to 2^16 bins) is filled while the frame is held; the 256 bins and
// the statistics (exact min, max, mean, standard deviation, percentiles, saturated and black pixels) come from it.
//
// ALIGNMENT: the angle of the straight lines (walls, light sheets, grids...) is found to about a hundredth of a
// degree, by projecting the image along candidate directions (a Radon transform restricted to near-vertical,
// or near-horizontal, directions):
// - While the frame is held, the region is cut into ~32 bands of rows, and the rows of each band are summed into a
//   full resolution profile (one value per column). A nearly vertical line gives a narrow feature in each profile,
//   at a position that moves from band to band by (band spacing) x tan(angle). The same is done with bands of
//   columns for the nearly horizontal lines.
// - Then, for a candidate angle, each band profile is shifted by (band center) x tan(angle) and the profiles are
//   summed: the lines at that angle add up coherently, so the energy of the derivative of the sum is largest at the
//   angle of the lines. The shifts are made in Fourier space (multiplication by a phase): they are exact for any
//   fraction of a pixel, so the estimate is not pulled towards the pixel grid (0 degree), unlike an interpolation
//   or the local gradients (an almost horizontal sharp edge is a staircase, whose gradients mostly point at 0).
// - Starting points: the angle found in the previous frame, the main orientations of the edges, modulo 90 degrees
//   (histogram of the orientations of 32 x 32 tiles, from Scharr gradients on one row out of 4: |g|^2 e^(4 i theta)
//   adds up the horizontal and vertical edges of a grid instead of cancelling them), and the best angle of a coarse
//   sweep of +/- 45 degrees on smoothed profiles (a Gaussian weight on the frequencies), which finds isolated lines
//   among other edges. A fine pattern (a grid finer
//   than the coarse resolution) is invisible to the sweep, hence the orientations.
// - Around each starting point, the energy is computed at a resolution of 4 pixels within +/- 2.5 degrees, then
//   the resolution (down to 2 pixels) and the angular step are refined around the best angle. The starting point
//   giving the best quality wins.
// - Final angle: the weights of the frequencies are matched to their signal and noise, measured in the profiles
//   themselves (see align_set_matched_weights): the noise of the frequencies without signal is ignored. They are
//   limited to a resolution of 4 pixels: a perfectly sharp edge, sampled by the pixels, is not band-limited, and
//   its finest details would pull the angle towards the pixel grid. A parabola through the energies at the best
//   angle and its neighbours gives the angle.
// - The spectra of the band profiles are averaged over the frames while the scene does not move (see AlignHistory):
//   the noise of the camera decreases as 1 / sqrt (frames).
// - The bands overlap (triangular weights, see BandLayout) and their spacings are irregular: with regular bands, a
//   grid of lines finer than the bands would also add up coherently at other angles (each band shifted by a whole
//   period).
// - The quality is the fraction of the (band-pass filtered) edge energy that adds up coherently at that angle:
//   about 0 for noise or a scene without straight lines, 1 for perfect lines crossing all the bands. It decreases
//   for large angles (the lines are blurred within each band: by 0.4 pixel per degree for 32 bands of 768 rows).
// On synthetic images (tests/metrics_test.c), the error is below 0.01 degree up to 10 degrees (below 0.1 degree up
// to 45) for blurred or sharp edges, light sheets, grids and horizons, usually below 0.005 degree near the
// alignment. With faint lines in a small noisy region, a single frame fluctuates by a few hundredths of a degree,
// the average over time by a few thousandths.
//
// FOURIER TRANSFORM: the region is binned (area average, bins of equal fractional sizes) to 2^n x 2^m bins while the
// frame is held; then the mean is
// subtracted, a Hann window is applied (else the borders of the region would add a strong cross to the spectrum),
// and the log power spectrum is computed and colored.

// Alignment
#define ALIGN_N_BANDS 32                // bands of rows (columns) for the vertical (horizontal) lines
#define ALIGN_MIN_BAND_SIZE 4           // pixels: smaller regions get fewer bands
#define ALIGN_MIN_BANDS 6               // below, the lines are not searched
#define ALIGN_MIN_LENGTH 16             // pixels: shortest profile
#define ALIGN_MAX_ANGLE (G_PI / 4)      // searched angles: +/- 45 degrees
#define ALIGN_FINE_RESOLUTION 2.0       // pixels: finest smoothing of the profiles (removes the Nyquist frequency,
                                        // e.g. the pattern of a Bayer mosaic)
#define ALIGN_COARSE_SAMPLES 24.0       // the coarse resolution is the span of the bands / this
#define ALIGN_QUALITY_LARGEST 32.0      // pixels: larger structures (illumination...) are ignored by the quality
#define ALIGN_BAND_JITTER 0.7           // relative irregularity of the band widths
#define ALIGN_TILE 32                   // pixels: tiles of the edge orientations
#define ALIGN_ORIENTATION_STEP 4        // the edge orientations are measured on one row out of this
#define ALIGN_MAX_SEEDS 2               // edge orientations tried as starting points of the angle search
#define ALIGN_SEED_WINDOW (2.5 * G_PI / 180)    // the lines are searched within +/- this angle of each seed
#define ALIGN_SEED_RESOLUTION 4.0       // pixels: resolution of the search around a seed
#define ALIGN_MATCHED_RESOLUTION 4.0    // pixels: finest resolution of the final angle (see align_set_matched_weights)
// Averaging over time (see AlignHistory)
#define ALIGN_AVERAGE_TIME_US (4 * G_USEC_PER_SEC)    // time constant of the average
#define ALIGN_HISTORY_GAP_US (5 * G_USEC_PER_SEC)     // a longer gap between two frames starts a new average
#define ALIGN_MOTION_SIGMAS 5.0         // a frame further than this from the average (in standard deviations of
                                        // the single frames) means that the scene moved: new average
#define ALIGN_MOTION_FLOOR (0.003 * G_PI / 180)       // (radians) ... and further than this
#define ALIGN_VARIANCE_SMOOTHING 0.2    // weight of a new frame in the variance of the single frames
#define ALIGN_CUSUM_SLACK 0.5           // CUSUM of the offsets of the single frames (in standard deviations):
#define ALIGN_CUSUM_LIMIT 5.0           // a small turn, hidden in the noise of each frame, is detected in a few frames

// Averaging over time. The noise of the camera makes the angle found in one frame fluctuate, all the more as the
// lines are faint, the region small and the noise high. As long as the scene does not move, the spectra of the band
// profiles (linear in the image) are averaged over the frames, with a time constant of ALIGN_AVERAGE_TIME_US: it is
// the same as averaging the frames, the noise decreases as 1 / sqrt (frames). To follow the camera when it is
// turned, each new frame is compared to the average: the angle of the lines in the frame alone, near the angle of
// the average, is found from 3 energies (vertex of a parabola). If it is further from the angle of the average than
// ALIGN_MOTION_SIGMAS times the usual spread of the single frames (learnt while the scene does not move), the scene
// moved: the average starts again from this frame. A smaller turn, hidden in the noise of each frame, makes offsets
// of the same sign frame after frame: they are added up (CUSUM, one sum for each sign, minus ALIGN_CUSUM_SLACK
// standard deviations per frame), and the average starts again when a sum reaches ALIGN_CUSUM_LIMIT.
typedef struct {
    gboolean valid;
    int n, n_bands;                 // size of the transforms and number of bands of the average
    float *re, *im;                 // averaged spectra [frequency][band]
    gsize allocated;
    double frames;                  // frames in the average (1 after a reset; 1 / weight of a new frame)
    double angle;                   // last angle found (radians, as in the search)
    double variance;                // variance of the angle of a single frame around the average (radians^2),
                                    // < 0 if unknown
    double cusum_up, cusum_down;    // CUSUM of the offsets of the single frames (standard deviations)
    gint64 time_us;                 // time of the last frame
} AlignHistory;

// A radix-2 complex FFT
typedef struct {
    int n;
    float *cos_table, *sin_table;    // e^(-2 i pi k / n), k < n / 2
} FftPlan;

// Overlapping bands (of rows, or of columns) with irregular centers: a row (column) between the centers of two
// bands belongs to both, with weights linear in its distance to them (triangular bands). With hard boundaries, a
// line of the other family (e.g. an almost horizontal line of a grid, for the vertical lines) that crosses a
// boundary would make a step in the profiles of the two bands, which biases the angle.
typedef struct {
    int n;                          // bands (0: none)
    int *band;                      // per position: first band (at most n - 2)...
    float *weight;                  // ... and its weight, the rest going to the next band
    gsize allocated;
    int starts[ALIGN_N_BANDS];      // first position of each first band (band[] is non decreasing), then the size
    double centers[ALIGN_N_BANDS];  // weighted mean position of each band, from the middle of the span
    double weight_sums[ALIGN_N_BANDS];
} BandLayout;

struct _MetricsAccumulator {
    MetricsRequest request;
    gboolean active;
    gboolean need_values;           // the float values of the rows are needed (alignment, FFT)

    float *values;                  // one row, as floats
    gsize values_allocated;

    // Histogram
    guint32 *native_histogram;      // 2^bits bins
    gsize native_histogram_allocated;

    // Alignment: vertical lines (bands of rows, profiles along x) and horizontal lines (bands of columns, along y)
    int n_vertical_bands, n_horizontal_bands;    // 0 if the family is not searched
    BandLayout vertical_bands;      // bands of rows
    BandLayout horizontal_bands;    // bands of columns
    float *band_sums;               // one value per band of columns (one row)
    // Averages over time of the alignment (vertical, horizontal lines), and what they were computed for
    AlignHistory vertical_history, horizontal_history;
    guint history_series;
    int history_region[4];
    MetricsLayout history_layout;

    // Orientation of the edges (seeds of the angle search), in tiles of ALIGN_TILE x ALIGN_TILE pixels: sum of
    // |g|^2 e^(4 i theta) (re, im) and of |g|^2, for the gradients g of angle theta. The angle is multiplied by 4 so
    // that the horizontal and vertical edges of a grid add up instead of cancelling (theta modulo 90 degrees).
    int n_tiles_x, n_tiles_y;      // 0 if not computed
    float *tiles;                   // [tile][3]
    gsize tiles_allocated;
    float *previous_values;         // the 2 previous rows (3 x 3 gradients), values_allocated floats each
    float *previous2_values;
    float *gradient_x, *gradient_y; // gradients between the previous row and this one, values_allocated floats
    int previous_y;                 // its row, -1 if none
    float *vertical_profiles;       // [band][x]
    gsize vertical_profiles_allocated;
    float *horizontal_profiles;     // [band][y]
    gsize horizontal_profiles_allocated;

    // Fourier transform
    int fft_width, fft_height;      // 0 if not computed
    // Bins of equal size (a fractional number of pixels): a pixel covers at most 2 bins in each direction, the
    // first one (fft_x_bin[x], fft_y_bin[y]) for the fraction fft_x_weight[x] (fft_y_weight[y]) of its size
    int *fft_x_bin, *fft_y_bin;
    float *fft_x_weight, *fft_y_weight;
    int fft_x_starts[METRICS_FFT_MAX_SIZE + 1];    // first column of each bin (fft_x_bin is non decreasing)
    gsize fft_x_allocated, fft_y_allocated;
    float *fft_row;                 // one binned row (fft_width + 1 values)
    float *fft_sums;                // [bin row][bin column]
    gsize fft_sums_allocated;

    // Work space of metrics_finish
    FftPlan plan_align, plan_rows, plan_columns;
    float *work_re, *work_im;
    gsize work_allocated;
    float *spectra_re, *spectra_im; // [frequency][band]
    gsize spectra_allocated;
    double *weights;                // per frequency
    gsize weights_allocated;
    double *matched_signal, *matched_noise;    // per frequency (align_set_matched_weights)
    float *fft_re, *fft_im;
    gsize fft_allocated;
};

// STORAGE

static void *ensure_size (void *data, gsize *allocated, gsize n, gsize element_size)
{
    if (*allocated < n) {
        g_free (data);
        data = g_malloc (n * element_size);
        *allocated = n;
    }
    return data;
}
#define ENSURE(pointer, allocated, n) ((pointer) = ensure_size ((pointer), &(allocated), (n), sizeof (*(pointer))))

// Work arrays of at least n values
static void ensure_work (MetricsAccumulator *acc, int n)
{
    if (acc->work_allocated < (gsize) n) {
        g_free (acc->work_re);
        g_free (acc->work_im);
        acc->work_re = g_new (float, n);
        acc->work_im = g_new (float, n);
        acc->work_allocated = n;
    }
}

MetricsAccumulator *metrics_accumulator_new (void)
{
    return g_new0 (MetricsAccumulator, 1);
}

static void fft_plan_clear (FftPlan *plan)
{
    g_clear_pointer (&plan->cos_table, g_free);
    g_clear_pointer (&plan->sin_table, g_free);
    plan->n = 0;
}

void metrics_accumulator_free (MetricsAccumulator *acc)
{
    if (acc == NULL)
        return;
    g_free (acc->values);
    g_free (acc->native_histogram);
    g_free (acc->vertical_bands.band);
    g_free (acc->vertical_bands.weight);
    g_free (acc->horizontal_bands.band);
    g_free (acc->horizontal_bands.weight);
    g_free (acc->band_sums);
    g_free (acc->vertical_profiles);
    g_free (acc->horizontal_profiles);
    g_free (acc->fft_x_bin);
    g_free (acc->fft_y_bin);
    g_free (acc->fft_x_weight);
    g_free (acc->fft_y_weight);
    g_free (acc->fft_row);
    g_free (acc->tiles);
    g_free (acc->previous_values);
    g_free (acc->previous2_values);
    g_free (acc->gradient_x);
    g_free (acc->gradient_y);
    g_free (acc->fft_sums);
    fft_plan_clear (&acc->plan_align);
    fft_plan_clear (&acc->plan_rows);
    fft_plan_clear (&acc->plan_columns);
    g_free (acc->work_re);
    g_free (acc->work_im);
    g_free (acc->spectra_re);
    g_free (acc->spectra_im);
    g_free (acc->weights);
    g_free (acc->matched_signal);
    g_free (acc->matched_noise);
    g_free (acc->vertical_history.re);
    g_free (acc->vertical_history.im);
    g_free (acc->horizontal_history.re);
    g_free (acc->horizontal_history.im);
    g_free (acc->fft_re);
    g_free (acc->fft_im);
    g_free (acc);
}

void metrics_clear (LrdMetrics *metrics)
{
    g_clear_pointer (&metrics->fft_pixels, g_free);
    metrics->fft_allocated = 0;
}

// PIXEL FORMATS

gboolean metrics_layout_from_pixel_format (ArvPixelFormat pixel_format, MetricsLayout *layout)
{
    memset (layout, 0, sizeof (*layout));
    switch (pixel_format) {
        case ARV_PIXEL_FORMAT_MONO_8:
            layout->samples = METRICS_SAMPLES_U8;
            layout->bits = 8;
            layout->bytes_per_pixel = 1;
            return TRUE;
        case ARV_PIXEL_FORMAT_BAYER_GR_8:
        case ARV_PIXEL_FORMAT_BAYER_RG_8:
        case ARV_PIXEL_FORMAT_BAYER_GB_8:
        case ARV_PIXEL_FORMAT_BAYER_BG_8:
            layout->samples = METRICS_SAMPLES_U8;
            layout->bits = 8;
            layout->bytes_per_pixel = 1;
            layout->bayer = TRUE;
            return TRUE;
        case ARV_PIXEL_FORMAT_RGB_8_PACKED:
        case ARV_PIXEL_FORMAT_BGR_8_PACKED:
            layout->samples = pixel_format == ARV_PIXEL_FORMAT_RGB_8_PACKED ? METRICS_SAMPLES_RGB8
                                                                           : METRICS_SAMPLES_BGR8;
            layout->bits = 8;
            layout->bytes_per_pixel = 3;
            layout->luma = TRUE;
            return TRUE;
        default:
            // Mono formats of more than 8 bits: unpacked to 16 bits (and scaled) for the display
            layout->bits = (int) mono_high_bit_depth (pixel_format);
            if (layout->bits == 0) {
                layout->samples = METRICS_SAMPLES_NONE;
                return FALSE;
            }
            layout->samples = METRICS_SAMPLES_U16;
            layout->bytes_per_pixel = 2;
            return TRUE;
    }
}

// FFT

static void fft_plan_init (FftPlan *plan, int n)
{
    if (plan->n == n)
        return;
    fft_plan_clear (plan);
    plan->n = n;
    plan->cos_table = g_new (float, MAX (n / 2, 1));
    plan->sin_table = g_new (float, MAX (n / 2, 1));
    for (int k = 0; k < n / 2; k++) {
        double const angle = -2.0 * G_PI * k / n;
        plan->cos_table[k] = (float) cos (angle);
        plan->sin_table[k] = (float) sin (angle);
    }
}

// In place forward transform: X[k] = sum x[j] e^(-2 i pi j k / n), n a power of 2
static void fft_forward (const FftPlan *plan, float *re, float *im)
{
    int const n = plan->n;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int length = 2; length <= n; length <<= 1) {
        int const half = length >> 1;
        int const step = n / length;
        for (int start = 0; start < n; start += length) {
            for (int k = 0; k < half; k++) {
                float const wr = plan->cos_table[k * step];
                float const wi = plan->sin_table[k * step];
                int const a = start + k, b = a + half;
                float const tr = re[b] * wr - im[b] * wi;
                float const ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

// BEGIN

// A uniform pseudo-random number in [0, 1), always the same for a given i
static double hash_unit (guint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x / 4294967296.0;
}

// Overlapping bands over [0, size), whose centers are about size / n apart, with irregular spacings (+/- 35 %)
static void band_layout_init (BandLayout *layout, int size, int n)
{
    layout->n = n;
    if (layout->allocated < (gsize) size) {
        g_free (layout->band);
        g_free (layout->weight);
        layout->band = g_new (int, size);
        layout->weight = g_new (float, size);
        layout->allocated = size;
    }
    double peaks[ALIGN_N_BANDS];
    for (int i = 0; i < n; i++)
        peaks[i] = (i + 0.5 + ALIGN_BAND_JITTER * (hash_unit ((guint32) i + 1) - 0.5)) * size / n;
    double position_sums[ALIGN_N_BANDS] = {0};
    for (int b = 0; b < n; b++)
        layout->weight_sums[b] = 0;
    int b = 0;
    for (int i = 0; i < size; i++) {
        double const position = i + 0.5;
        while (b < n - 2 && position >= peaks[b + 1])
            b++;
        double const w = CLAMP ((peaks[b + 1] - position) / (peaks[b + 1] - peaks[b]), 0.0, 1.0);
        layout->band[i] = b;
        layout->weight[i] = (float) w;
        layout->weight_sums[b] += w;
        layout->weight_sums[b + 1] += 1 - w;
        position_sums[b] += w * position;
        position_sums[b + 1] += (1 - w) * position;
    }
    for (int i = 0; i < n; i++)
        layout->centers[i] = (layout->weight_sums[i] > 0 ? position_sums[i] / layout->weight_sums[i] : peaks[i])
                             - size / 2.0;
    for (int band = 0, i = 0; band < n - 1; band++) {
        while (i < size && layout->band[i] < band)
            i++;
        layout->starts[band] = i;
    }
    layout->starts[n - 1] = size;
}

// Sum of values[i] and of weights[i] x values[i]. (Float sums are only vectorized by the compiler if they are made
// of independent partial sums: 8 here.)
static inline void sum_and_weighted_sum (const float *values, const float *weights, int n, float *sum,
                                         float *weighted_sum)
{
    float s[8] = {0}, t[8] = {0};
    int i = 0;
    for (; i + 8 <= n; i += 8)
        for (int j = 0; j < 8; j++) {
            s[j] += values[i + j];
            t[j] += weights[i + j] * values[i + j];
        }
    float a = 0, b = 0;
    for (int j = 0; j < 8; j++) {
        a += s[j];
        b += t[j];
    }
    for (; i < n; i++) {
        a += values[i];
        b += weights[i] * values[i];
    }
    *sum = a;
    *weighted_sum = b;
}

// Bins of size / n_bins pixels: pixel i covers bin[i] for the fraction weight[i] of its size, and the next bin for
// the rest
static void fractional_bins (int size, int n_bins, int *bin, float *weight)
{
    for (int i = 0; i < size; i++) {
        double const start = (double) i * n_bins / size, end = (double) (i + 1) * n_bins / size;
        int const first = MIN ((int) floor (start), n_bins - 1);
        bin[i] = first;
        weight[i] = end <= first + 1 ? 1.0f : (float) ((first + 1 - start) / (end - start));
    }
}

static int floor_log2 (int n)
{
    int log = 0;
    while ((2 << log) <= n)
        log++;
    return log;
}

gboolean metrics_begin (MetricsAccumulator *acc, const MetricsRequest *request)
{
    acc->request = *request;
    acc->active = FALSE;
    acc->n_vertical_bands = 0;
    acc->n_horizontal_bands = 0;
    acc->fft_width = 0;
    acc->fft_height = 0;
    acc->n_tiles_x = 0;
    acc->n_tiles_y = 0;

    int const width = request->width, height = request->height;
    const MetricsLayout *layout = &request->layout;
    if (layout->samples == METRICS_SAMPLES_NONE || width < 1 || height < 1 || layout->bits < 8 || layout->bits > 16)
        return FALSE;

    if (request->histogram) {
        gsize const n_bins = (gsize) 1 << layout->bits;
        ENSURE (acc->native_histogram, acc->native_histogram_allocated, n_bins);
        memset (acc->native_histogram, 0, n_bins * sizeof (guint32));
    }

    if (request->alignment) {
        // Vertical lines: bands of rows
        int n = MIN (ALIGN_N_BANDS, height / ALIGN_MIN_BAND_SIZE);
        if (n >= ALIGN_MIN_BANDS && width >= ALIGN_MIN_LENGTH) {
            acc->n_vertical_bands = n;
            band_layout_init (&acc->vertical_bands, height, n);
            ENSURE (acc->vertical_profiles, acc->vertical_profiles_allocated, (gsize) n * width);
            memset (acc->vertical_profiles, 0, (gsize) n * width * sizeof (float));
        }
        // Horizontal lines: bands of columns
        n = MIN (ALIGN_N_BANDS, width / ALIGN_MIN_BAND_SIZE);
        if (n >= ALIGN_MIN_BANDS && height >= ALIGN_MIN_LENGTH) {
            acc->n_horizontal_bands = n;
            band_layout_init (&acc->horizontal_bands, width, n);
            g_free (acc->band_sums);
            acc->band_sums = g_new (float, n);
            ENSURE (acc->horizontal_profiles, acc->horizontal_profiles_allocated, (gsize) n * height);
            memset (acc->horizontal_profiles, 0, (gsize) n * height * sizeof (float));
        }
        // Orientation tiles
        if (acc->n_vertical_bands > 0 || acc->n_horizontal_bands > 0) {
            acc->n_tiles_x = (width - 1 + ALIGN_TILE - 1) / ALIGN_TILE;
            acc->n_tiles_y = (height - 1 + ALIGN_TILE - 1) / ALIGN_TILE;
            gsize const n_values = (gsize) 3 * acc->n_tiles_x * acc->n_tiles_y;
            ENSURE (acc->tiles, acc->tiles_allocated, n_values);
            memset (acc->tiles, 0, n_values * sizeof (float));
            acc->previous_y = -1;
        }
    }

    if (request->fft && width >= 8 && height >= 8) {
        int const max_log = floor_log2 (METRICS_FFT_MAX_SIZE);
        acc->fft_width = 1 << MIN (floor_log2 (width), max_log);
        acc->fft_height = 1 << MIN (floor_log2 (height), max_log);
        if (acc->fft_x_allocated < (gsize) width) {
            g_free (acc->fft_x_bin);
            g_free (acc->fft_x_weight);
            acc->fft_x_bin = g_new (int, width);
            acc->fft_x_weight = g_new (float, width);
            acc->fft_x_allocated = width;
        }
        if (acc->fft_y_allocated < (gsize) height) {
            g_free (acc->fft_y_bin);
            g_free (acc->fft_y_weight);
            acc->fft_y_bin = g_new (int, height);
            acc->fft_y_weight = g_new (float, height);
            acc->fft_y_allocated = height;
        }
        fractional_bins (width, acc->fft_width, acc->fft_x_bin, acc->fft_x_weight);
        for (int i = 0, x = 0; i <= acc->fft_width; i++) {
            while (x < width && acc->fft_x_bin[x] < i)
                x++;
            acc->fft_x_starts[i] = x;
        }
        fractional_bins (height, acc->fft_height, acc->fft_y_bin, acc->fft_y_weight);
        g_free (acc->fft_row);
        acc->fft_row = g_new (float, acc->fft_width + 1);
        gsize const n_bins = (gsize) acc->fft_width * acc->fft_height;
        ENSURE (acc->fft_sums, acc->fft_sums_allocated, n_bins);
        memset (acc->fft_sums, 0, n_bins * sizeof (float));
    }

    acc->need_values = acc->n_vertical_bands > 0 || acc->n_horizontal_bands > 0 || acc->fft_width > 0;
    if (acc->need_values && acc->values_allocated < (gsize) width) {
        // (values and the previous rows are swapped after each row: same size)
        g_free (acc->values);
        g_free (acc->previous_values);
        g_free (acc->previous2_values);
        g_free (acc->gradient_x);
        g_free (acc->gradient_y);
        acc->values = g_new (float, width);
        acc->previous_values = g_new (float, width);
        acc->previous2_values = g_new (float, width);
        acc->gradient_x = g_new (float, width);
        acc->gradient_y = g_new (float, width);
        acc->values_allocated = width;
    }
    acc->active = TRUE;
    return TRUE;
}

// ROWS

// Luma (Rec. 601) of 8-bit RGB, 0 to 255
#define LUMA(r, g, b) ((77 * (guint) (r) + 150 * (guint) (g) + 29 * (guint) (b) + 128) >> 8)

// Orientation of the edges: gradients at row y - 1 (Scharr 3 x 3 kernel, nearly isotropic: the orientations of
// sharp edges are much less biased than with smaller kernels), from the rows y - 2 ('above'), y - 1 ('middle') and
// y ('below'), whose |g|^2 e^(4 i theta) and |g|^2 are added to the orientation tiles
static void accumulate_orientations (MetricsAccumulator *acc, const float *above, const float *middle,
                                     const float *below, int y)
{
    int const n = acc->request.width - 1;    // gradients at x = 1 to n - 1
    float *const gx = acc->gradient_x, *const gy = acc->gradient_y;
    for (int x = 1; x < n; x++) {
        gx[x] = 3 * (above[x + 1] - above[x - 1]) + 10 * (middle[x + 1] - middle[x - 1])
                + 3 * (below[x + 1] - below[x - 1]);
        gy[x] = 3 * (below[x - 1] - above[x - 1]) + 10 * (below[x] - above[x]) + 3 * (below[x + 1] - above[x + 1]);
    }
    float *tiles = acc->tiles + (gsize) 3 * ((y - 1) / ALIGN_TILE) * acc->n_tiles_x;
    for (int t = 0; t < acc->n_tiles_x; t++) {
        int const start = MAX (t * ALIGN_TILE, 1), end = MIN ((t + 1) * ALIGN_TILE, n);
        // (8 independent partial sums: vectorized)
        float sum_re[8] = {0}, sum_im[8] = {0}, sum_energy[8] = {0};
        for (int x0 = start; x0 < end; x0 += 8) {
            for (int j = 0; j < 8; j++) {
                int const x = MIN (x0 + j, end - 1);
                float const in = x0 + j < end ? 1.0f : 0.0f;
                float const gx2 = gx[x] * gx[x], gy2 = gy[x] * gy[x];
                // (gx + i gy)^4 / |g|^2 = |g|^2 e^(4 i theta)
                float const u = gx2 - gy2, v = 2 * gx[x] * gy[x], energy = gx2 + gy2;
                float const inverse = in / (energy + 1e-12f);
                sum_re[j] += (u * u - v * v) * inverse;
                sum_im[j] += 2 * u * v * inverse;
                sum_energy[j] += in * energy;
            }
        }
        for (int j = 0; j < 8; j++) {
            tiles[3 * t] += sum_re[j];
            tiles[3 * t + 1] += sum_im[j];
            tiles[3 * t + 2] += sum_energy[j];
        }
    }
}

void metrics_add_row (MetricsAccumulator *acc, const guint8 *row, int y)
{
    if (!acc->active || y < 0 || y >= acc->request.height)
        return;
    int const width = acc->request.width;
    const MetricsLayout *layout = &acc->request.layout;
    guint32 *const histogram = acc->request.histogram ? acc->native_histogram : NULL;
    float *const values = acc->values;

    // Values (as floats) and histogram, in separate loops: the first one is vectorized
    switch (layout->samples) {
        case METRICS_SAMPLES_U8:
            if (acc->need_values)
                for (int x = 0; x < width; x++)
                    values[x] = row[x];
            if (histogram != NULL)
                for (int x = 0; x < width; x++)
                    histogram[row[x]]++;
            break;
        case METRICS_SAMPLES_U16: {
            const guint16 *row16 = (const guint16 *) row;
            int const shift = 16 - layout->bits;
            if (acc->need_values)
                for (int x = 0; x < width; x++)
                    values[x] = GUINT16_FROM_LE (row16[x]) >> shift;
            if (histogram != NULL)
                for (int x = 0; x < width; x++)
                    histogram[GUINT16_FROM_LE (row16[x]) >> shift]++;
            break;
        }
        case METRICS_SAMPLES_RGB8:
        case METRICS_SAMPLES_BGR8: {
            int const r = layout->samples == METRICS_SAMPLES_RGB8 ? 0 : 2;
            int const b = 2 - r;
            for (int x = 0; x < width; x++) {
                guint const luma = LUMA (row[3 * x + r], row[3 * x + 1], row[3 * x + b]);
                if (acc->need_values)
                    values[x] = luma;
                if (histogram != NULL)
                    histogram[luma]++;
            }
            break;
        }
        default:
            return;
    }

    // Alignment: the row is added to the profile of its band (vertical lines), and the sum of each band of columns
    // is the value of the profile of that band at this row (horizontal lines)
    if (acc->n_vertical_bands > 0) {
        int const band = acc->vertical_bands.band[y];
        float const w = acc->vertical_bands.weight[y];
        float *profile = acc->vertical_profiles + (gsize) band * width;
        if (w > 0)
            for (int x = 0; x < width; x++)
                profile[x] += w * values[x];
        profile += width;
        if (w < 1)
            for (int x = 0; x < width; x++)
                profile[x] += (1 - w) * values[x];
    }
    if (acc->n_horizontal_bands > 0) {
        int const height = acc->request.height, n = acc->n_horizontal_bands;
        const BandLayout *bands = &acc->horizontal_bands;
        float *sums = acc->band_sums;
        memset (sums, 0, n * sizeof (float));
        // Columns from starts[b] to starts[b + 1]: band b (weight w), and band b + 1 (1 - w)
        for (int b = 0; b < n - 1; b++) {
            int const start = bands->starts[b], end = bands->starts[b + 1];
            float sum, weighted;
            sum_and_weighted_sum (values + start, bands->weight + start, end - start, &sum, &weighted);
            sums[b] += weighted;
            sums[b + 1] += sum - weighted;
        }
        for (int b = 0; b < n; b++)
            acc->horizontal_profiles[(gsize) b * height + y] = sums[b];
    }

    // Fourier transform: binning
    if (acc->fft_width > 0) {
        int const n_bins = acc->fft_width;
        float *binned = acc->fft_row;
        memset (binned, 0, (n_bins + 1) * sizeof (float));
        // Columns of bin i: their fraction 'weight' in bin i, the rest in bin i + 1
        for (int i = 0; i < n_bins; i++) {
            int const start = acc->fft_x_starts[i], end = acc->fft_x_starts[i + 1];
            float sum, weighted;
            sum_and_weighted_sum (values + start, acc->fft_x_weight + start, end - start, &sum, &weighted);
            binned[i] += weighted;
            binned[i + 1] += sum - weighted;
        }
        int const bin_row = acc->fft_y_bin[y];
        float const weight = acc->fft_y_weight[y];
        float *sums = acc->fft_sums + (gsize) bin_row * n_bins;
        for (int i = 0; i < n_bins; i++)
            sums[i] += weight * binned[i];
        if (weight < 1 && bin_row + 1 < acc->fft_height) {
            sums += n_bins;
            for (int i = 0; i < n_bins; i++)
                sums[i] += (1 - weight) * binned[i];
        }
    }

    // Orientation of the edges: gradients of the previous row (if the rows come in order), one row out of
    // ALIGN_ORIENTATION_STEP (they are only the starting points of the search)
    if (acc->n_tiles_x > 0) {
        if (acc->previous_y == y - 1 && y >= 2 && (y - 1) % ALIGN_ORIENTATION_STEP == 0)
            accumulate_orientations (acc, acc->previous2_values, acc->previous_values, values, y);
        float *const oldest = acc->previous2_values;
        acc->previous2_values = acc->previous_values;
        acc->previous_values = values;
        acc->values = oldest;
        acc->previous_y = y;
    }
}

// HISTOGRAM

static void finish_histogram (MetricsAccumulator *acc, LrdMetrics *out)
{
    int const bits = acc->request.layout.bits;
    int const n_native = 1 << bits;
    int const shift = bits - 8;
    const guint32 *histogram = acc->native_histogram;

    memset (out->bins, 0, sizeof (out->bins));
    guint64 n = 0;
    double sum = 0, sum_of_squares = 0;
    int min = -1, max = 0;
    for (int v = 0; v < n_native; v++) {
        guint32 const count = histogram[v];
        if (count == 0)
            continue;
        out->bins[v >> shift] += count;
        n += count;
        sum += (double) count * v;
        sum_of_squares += (double) count * v * v;
        if (min < 0)
            min = v;
        max = v;
    }
    out->n_pixels = n;
    out->full_scale = n_native - 1;
    out->min = MAX (min, 0);
    out->max = max;
    out->mean = n > 0 ? sum / n : 0;
    out->std = n > 0 ? sqrt (MAX (0.0, sum_of_squares / n - out->mean * out->mean)) : 0;
    out->n_saturated = histogram[n_native - 1];
    out->n_black = histogram[0];

    // Percentiles
    out->p01 = out->min;
    out->p99 = out->max;
    guint64 cumulated = 0;
    gboolean p01_found = FALSE;
    for (int v = 0; v < n_native && n > 0; v++) {
        cumulated += histogram[v];
        if (!p01_found && cumulated >= 0.01 * n) {
            out->p01 = v;
            p01_found = TRUE;
        }
        if (cumulated >= 0.99 * n) {
            out->p99 = v;
            break;
        }
    }
}

// ALIGNMENT

typedef struct {
    int n_bands;
    int n;                       // size of the transforms (zero padded profiles)
    double centers[ALIGN_N_BANDS];
    const float *spectra_re, *spectra_im;    // [frequency][band], frequencies 0 to n / 2
    const double *weights;
    int n_frequencies;           // frequencies 1 to n_frequencies - 1 are used
} AlignProblem;

// Gaussian low-pass weights of resolution 'resolution' pixels (minus a Gaussian of resolution 'largest' pixels if
// largest > 0: band-pass)
static void align_set_weights (MetricsAccumulator *acc, AlignProblem *p, double resolution, double largest)
{
    double const kc = p->n / (2.0 * resolution);
    double const kl = largest > 0 ? p->n / (2.0 * largest) : 0;
    p->n_frequencies = MIN (p->n / 2, (int) ceil (3 * kc)) + 1;
    for (int k = 0; k < p->n_frequencies; k++) {
        double w = exp (-(k / kc) * (k / kc));
        if (kl > 0)
            w -= exp (-(k / kl) * (k / kl));
        acc->weights[k] = w;
    }
    p->weights = acc->weights;
}

// Energy of the derivative of the sum of the band profiles, each shifted by center x slope.
// If 'incoherent' is not NULL, it receives the sum of the energies of the band profiles (the energy of the sum
// when they do not add up coherently).
static double align_energy (const AlignProblem *p, double slope, double *incoherent)
{
    int const n_bands = p->n_bands;
    double zr[ALIGN_N_BANDS], zi[ALIGN_N_BANDS], pr[ALIGN_N_BANDS], pi[ALIGN_N_BANDS];
    for (int b = 0; b < n_bands; b++) {
        // Shifting a profile by s (f(u + s)) multiplies frequency k by e^(2 i pi k s / n)
        double const phase = 2.0 * G_PI * p->centers[b] * slope / p->n;
        zr[b] = pr[b] = cos (phase);
        zi[b] = pi[b] = sin (phase);
    }
    double energy = 0, incoherent_energy = 0;
    for (int k = 1; k < p->n_frequencies; k++) {
        const float *sr = p->spectra_re + (gsize) k * n_bands;
        const float *si = p->spectra_im + (gsize) k * n_bands;
        double ar = 0, ai = 0;
        for (int b = 0; b < n_bands; b++) {
            ar += sr[b] * pr[b] - si[b] * pi[b];
            ai += sr[b] * pi[b] + si[b] * pr[b];
            double const next_pr = pr[b] * zr[b] - pi[b] * zi[b];
            pi[b] = pr[b] * zi[b] + pi[b] * zr[b];
            pr[b] = next_pr;
        }
        energy += p->weights[k] * (ar * ar + ai * ai);
        if (incoherent != NULL) {
            double band_energy = 0;
            for (int b = 0; b < n_bands; b++)
                band_energy += (double) sr[b] * sr[b] + (double) si[b] * si[b];
            incoherent_energy += p->weights[k] * band_energy;
        }
    }
    if (incoherent != NULL)
        *incoherent = incoherent_energy;
    return energy;
}

// Main orientations of the edges, modulo 90 degrees (tilts in degrees, clockwise on screen, in image pixels), from
// the orientation tiles: histogram of the tile orientations weighted by their coherent energy. Returns the number
// of seeds (the strongest first).
static int orientation_seeds (MetricsAccumulator *acc, double *seeds)
{
    enum { N_BINS = 180 };    // 0.5 degree
    double histogram[N_BINS] = {0};
    int const n_tiles = acc->n_tiles_x * acc->n_tiles_y;
    for (int t = 0; t < n_tiles; t++) {
        double const re = acc->tiles[3 * t], im = acc->tiles[3 * t + 1];
        double const magnitude = hypot (re, im);
        if (magnitude <= 0)
            continue;
        // A gradient at theta comes from an edge turned by theta: tilt = theta, modulo 90 degrees
        double const tilt = atan2 (im, re) / 4 * 180 / G_PI;    // -45 to 45
        int const bin = ((int) floor ((tilt + 45) / 90 * N_BINS)) % N_BINS;
        histogram[bin] += magnitude;
    }
    double smoothed[N_BINS];
    for (int i = 0; i < N_BINS; i++)
        smoothed[i] = 0.25 * histogram[(i + N_BINS - 1) % N_BINS] + 0.5 * histogram[i]
                      + 0.25 * histogram[(i + 1) % N_BINS];

    // Local maxima, the strongest first
    int n_seeds = 0;
    double seed_values[ALIGN_MAX_SEEDS];
    for (int i = 0; i < N_BINS; i++) {
        double const left = smoothed[(i + N_BINS - 1) % N_BINS], right = smoothed[(i + 1) % N_BINS];
        double const value = smoothed[i];
        if (value <= 0 || value < left || value <= right)
            continue;
        double offset = 0;
        double const curvature = left - 2 * value + right;
        if (curvature < 0)
            offset = CLAMP (0.5 * (left - right) / curvature, -0.5, 0.5);
        double tilt = (i + 0.5 + offset) * 90.0 / N_BINS - 45;
        // Insert, sorted by value
        int position = n_seeds;
        while (position > 0 && seed_values[position - 1] < value)
            position--;
        if (position >= ALIGN_MAX_SEEDS)
            continue;
        for (int k = MIN (n_seeds, ALIGN_MAX_SEEDS - 1); k > position; k--) {
            seeds[k] = seeds[k - 1];
            seed_values[k] = seed_values[k - 1];
        }
        seeds[position] = tilt;
        seed_values[position] = value;
        n_seeds = MIN (n_seeds + 1, ALIGN_MAX_SEEDS);
    }
    // Only the significant ones
    while (n_seeds > 1 && seed_values[n_seeds - 1] < 0.25 * seed_values[0])
        n_seeds--;
    return n_seeds;
}

// Quality of the lines at an angle: fraction of the band-pass filtered edge energy that adds up coherently
static double align_quality (MetricsAccumulator *acc, AlignProblem *p, double angle)
{
    align_set_weights (acc, p, ALIGN_FINE_RESOLUTION, ALIGN_QUALITY_LARGEST);
    double incoherent;
    double const energy = align_energy (p, tan (angle), &incoherent);
    double const ratio = incoherent > 0 ? energy / incoherent : 0;    // 1 (incoherent) to n_bands (coherent)
    return CLAMP ((ratio - 1) / (p->n_bands - 1), 0.0, 1.0);
}

// Weights matched to the signal and the noise of each frequency, at an angle close to the one of the lines: with
// n bands each made of the same signal (power S) plus independent noise (power P), the energy of their sum is
// C = n^2 S + n P and the sum of their energies is I = n S + n P, so that S = (C - I) / (n (n - 1)) and
// P = (n I - C) / (n (n - 1)). The variance of the angle found by maximizing the weighted energy is smallest with
// weights S / (P (n S + P)): the frequencies without signal (high frequencies of blurred edges, made larger by the
// derivative) are ignored, those where the signal is above the noise count the most. S and P are smoothed over a
// few frequencies (their estimates from one frequency are noisy).
static void align_set_matched_weights (MetricsAccumulator *acc, AlignProblem *p, double angle)
{
    int const n_bands = p->n_bands, half = p->n / 2;
    double const slope = tan (angle);
    double *signal = acc->matched_signal, *noise = acc->matched_noise;
    double zr[ALIGN_N_BANDS], zi[ALIGN_N_BANDS], pr[ALIGN_N_BANDS], pi[ALIGN_N_BANDS];
    for (int b = 0; b < n_bands; b++) {
        double const phase = 2.0 * G_PI * p->centers[b] * slope / p->n;
        zr[b] = pr[b] = cos (phase);
        zi[b] = pi[b] = sin (phase);
    }
    signal[0] = noise[0] = 0;
    for (int k = 1; k <= half; k++) {
        const float *sr = p->spectra_re + (gsize) k * n_bands;
        const float *si = p->spectra_im + (gsize) k * n_bands;
        double ar = 0, ai = 0, incoherent = 0;
        for (int b = 0; b < n_bands; b++) {
            ar += sr[b] * pr[b] - si[b] * pi[b];
            ai += sr[b] * pi[b] + si[b] * pr[b];
            incoherent += (double) sr[b] * sr[b] + (double) si[b] * si[b];
            double const next_pr = pr[b] * zr[b] - pi[b] * zi[b];
            pi[b] = pr[b] * zi[b] + pi[b] * zr[b];
            pr[b] = next_pr;
        }
        double const coherent = ar * ar + ai * ai;
        signal[k] = (coherent - incoherent) / (n_bands * (n_bands - 1.0));
        noise[k] = (n_bands * incoherent - coherent) / (n_bands * (n_bands - 1.0));
    }
    // Smoothing (running sums over +/- half_width frequencies), then the weights, limited to a resolution of
    // ALIGN_MATCHED_RESOLUTION pixels
    double const kc = p->n / (2.0 * ALIGN_MATCHED_RESOLUTION);
    int const half_width = MAX (2, p->n / 256);
    double sum_signal = 0, sum_noise = 0;
    int first = 1, last = 0;    // frequencies in the running sums
    double max_weight = 0;
    for (int k = 1; k <= half; k++) {
        while (last < MIN (half, k + half_width)) {
            last++;
            sum_signal += signal[last];
            sum_noise += noise[last];
        }
        while (first < k - half_width) {
            sum_signal -= signal[first];
            sum_noise -= noise[first];
            first++;
        }
        double const s = MAX (0.0, sum_signal), n = MAX (sum_noise, 1e-12 * (fabs (sum_signal) + 1e-30));
        double const w = s / (n * (n_bands * s + n)) * exp (-(k / kc) * (k / kc));
        acc->weights[k] = w;
        max_weight = MAX (max_weight, w);
    }
    for (int k = 1; k <= half; k++)
        acc->weights[k] = max_weight > 0 ? acc->weights[k] / max_weight : 1.0;
    acc->weights[0] = 0;
    p->weights = acc->weights;
    p->n_frequencies = half + 1;
}

// Refines the angle with the matched weights: 9 samples 1 / span apart, then 9 samples 4 times closer, and the
// vertex of the parabola through the best sample and its neighbours
static double align_refine_matched (MetricsAccumulator *acc, AlignProblem *p, int span, double angle)
{
    align_set_matched_weights (acc, p, angle);
    double step = 1.0 / span;
    for (int level = 0; level < 2; level++, step /= 4) {
        double energies[9];
        int best = 0;
        for (int j = 0; j < 9; j++) {
            double const a = CLAMP (angle + (j - 4) * step, -ALIGN_MAX_ANGLE, ALIGN_MAX_ANGLE);
            energies[j] = align_energy (p, tan (a), NULL);
            if (energies[j] > energies[best])
                best = j;
        }
        angle = CLAMP (angle + (best - 4) * step, -ALIGN_MAX_ANGLE, ALIGN_MAX_ANGLE);
        if (level == 1 && best > 0 && best < 8) {
            double const e0 = energies[best - 1], e1 = energies[best], e2 = energies[best + 1];
            double const curvature = e0 - 2 * e1 + e2;
            if (curvature < 0)
                angle += CLAMP (0.5 * (e0 - e2) / curvature, -0.5, 0.5) * step;
        }
    }
    return angle;
}

// Searches the angle of the lines from 'resolution' pixels and 'step' radians, within +/- 'window' radians of
// 'angle', then refines it (4 times finer resolution, at most ALIGN_FINE_RESOLUTION, and step each time), until
// the step is a quarter of the width of the finest peak. Returns the angle (radians).
static double align_refine (MetricsAccumulator *acc, AlignProblem *p, int span, double angle, double window,
                            double resolution)
{
    double step = resolution / (2.0 * span);
    align_set_weights (acc, p, resolution, 0);
    double best_angle = angle, best_energy = -1;
    int const n_steps = MAX (1, (int) ceil (window / step));
    for (int i = -n_steps; i <= n_steps; i++) {
        double const a = CLAMP (angle + i * step, -ALIGN_MAX_ANGLE, ALIGN_MAX_ANGLE);
        double const energy = align_energy (p, tan (a), NULL);
        if (energy > best_energy) {
            best_energy = energy;
            best_angle = a;
        }
    }

    for (;;) {
        resolution = MAX (ALIGN_FINE_RESOLUTION, resolution / 4);
        step /= 4;
        align_set_weights (acc, p, resolution, 0);
        double energies[9];
        int best = 0;
        for (int j = 0; j < 9; j++) {
            double const a = CLAMP (best_angle + (j - 4) * step, -ALIGN_MAX_ANGLE, ALIGN_MAX_ANGLE);
            energies[j] = align_energy (p, tan (a), NULL);
            if (energies[j] > energies[best])
                best = j;
        }
        best_angle = CLAMP (best_angle + (best - 4) * step, -ALIGN_MAX_ANGLE, ALIGN_MAX_ANGLE);
        if (resolution <= ALIGN_FINE_RESOLUTION && step <= ALIGN_FINE_RESOLUTION / (4.0 * span)) {
            // Vertex of the parabola through the best sample and its neighbours
            if (best > 0 && best < 8) {
                double const e0 = energies[best - 1], e1 = energies[best], e2 = energies[best + 1];
                double const curvature = e0 - 2 * e1 + e2;
                if (curvature < 0)
                    best_angle += CLAMP (0.5 * (e0 - e2) / curvature, -0.5, 0.5) * step;
            }
            return best_angle;
        }
    }
}

// Adds the spectra of the new frame (in p) to the average of the history, or starts a new average from them (first
// frame, new size, long gap, or the scene moved)
static void align_average (MetricsAccumulator *acc, AlignHistory *history, AlignProblem *p, int span)
{
    gint64 const now_us = acc->request.time_us;
    gsize const n_values = (gsize) (p->n / 2 + 1) * p->n_bands;
    gboolean restart = !history->valid || history->n != p->n || history->n_bands != p->n_bands ||
                       now_us - history->time_us > ALIGN_HISTORY_GAP_US || now_us < history->time_us;

    if (!restart) {
        // The angle of the new frame alone, near the angle of the average (weights matched to the average)
        AlignProblem average = *p;
        average.spectra_re = history->re;
        average.spectra_im = history->im;
        align_set_matched_weights (acc, &average, history->angle);
        p->weights = average.weights;
        p->n_frequencies = average.n_frequencies;
        double const step = 1.0 / span;
        double const e0 = align_energy (p, tan (history->angle - step), NULL);
        double const e1 = align_energy (p, tan (history->angle), NULL);
        double const e2 = align_energy (p, tan (history->angle + step), NULL);
        double const curvature = e0 - 2 * e1 + e2;
        double const offset = curvature < 0 ? 0.5 * (e0 - e2) / curvature * step : G_MAXDOUBLE;
        double const limit = ALIGN_MOTION_SIGMAS * sqrt (MAX (history->variance, 0.0)) + ALIGN_MOTION_FLOOR;
        gboolean moved = fabs (offset) > 1.5 * step || (history->variance >= 0 && fabs (offset) > limit);
        if (!moved && history->variance > 0) {
            double const z = offset / sqrt (history->variance);
            history->cusum_up = MAX (0.0, history->cusum_up + z - ALIGN_CUSUM_SLACK);
            history->cusum_down = MAX (0.0, history->cusum_down - z - ALIGN_CUSUM_SLACK);
            moved = history->cusum_up > ALIGN_CUSUM_LIMIT || history->cusum_down > ALIGN_CUSUM_LIMIT;
        }
        if (moved) {
            restart = TRUE;
        } else {
            // (the offsets are limited to 3 standard deviations: a turn being detected must not inflate the spread)
            double const square = history->variance > 0 ? MIN (offset * offset, 9 * history->variance)
                                                        : offset * offset;
            history->variance = history->variance < 0 ? square
                                : history->variance + ALIGN_VARIANCE_SMOOTHING * (square - history->variance);
        }
    }

    if (restart) {
        if (history->allocated < n_values) {
            g_free (history->re);
            g_free (history->im);
            history->re = g_new (float, n_values);
            history->im = g_new (float, n_values);
            history->allocated = n_values;
        }
        memcpy (history->re, p->spectra_re, n_values * sizeof (float));
        memcpy (history->im, p->spectra_im, n_values * sizeof (float));
        if (!history->valid || history->n != p->n || history->n_bands != p->n_bands)
            history->variance = -1;    // (else the spread of the single frames is still the same)
        history->valid = TRUE;
        history->n = p->n;
        history->n_bands = p->n_bands;
        history->frames = 1;
        history->cusum_up = history->cusum_down = 0;
    } else {
        double const weight = MAX (1.0 / (history->frames + 1),
                                   (double) (now_us - history->time_us) / ALIGN_AVERAGE_TIME_US);
        float const w = (float) MIN (weight, 1.0);
        for (gsize i = 0; i < n_values; i++) {
            history->re[i] += w * (p->spectra_re[i] - history->re[i]);
            history->im[i] += w * (p->spectra_im[i] - history->im[i]);
        }
        history->frames = 1.0 / w;
    }
    history->time_us = now_us;
}

// Finds the angle of the lines that cross the bands. profiles: n_bands x length values; bands: along the other
// axis, of size 'span'; seed_angles: starting points (radians, same convention as
// the result). Gives the slope (shift along the profiles per pixel across them) and the quality.
static void align_family (MetricsAccumulator *acc, AlignHistory *history, const float *profiles,
                          const BandLayout *bands, int length, int span, const double *seed_angles, int n_seeds,
                          double *slope, double *quality)
{
    int const n_bands = bands->n;
    AlignProblem p = {0};
    p.n_bands = n_bands;
    // Zero padding: no wrap around for shifts up to span / 2 (45 degrees)
    p.n = 64;
    while (p.n < length + span)
        p.n <<= 1;
    int const half = p.n / 2;

    fft_plan_init (&acc->plan_align, p.n);
    ensure_work (acc, p.n);
    gsize const n_spectra = (gsize) (half + 1) * n_bands;
    if (acc->spectra_allocated < n_spectra) {
        g_free (acc->spectra_re);
        g_free (acc->spectra_im);
        acc->spectra_re = g_new (float, n_spectra);
        acc->spectra_im = g_new (float, n_spectra);
        acc->spectra_allocated = n_spectra;
    }
    if (acc->weights_allocated < (gsize) half + 1) {
        g_free (acc->weights);
        g_free (acc->matched_signal);
        g_free (acc->matched_noise);
        acc->weights = g_new (double, half + 1);
        acc->matched_signal = g_new (double, half + 1);
        acc->matched_noise = g_new (double, half + 1);
        acc->weights_allocated = half + 1;
    }

    // Spectra of the derivatives of the mean profiles (all the bands have the same weight)
    for (int b = 0; b < n_bands; b++) {
        const float *profile = profiles + (gsize) b * length;
        float const scale = (float) (1.0 / bands->weight_sums[b]);
        for (int u = 0; u < length - 1; u++)
            acc->work_re[u] = (profile[u + 1] - profile[u]) * scale;
        memset (acc->work_re + length - 1, 0, (gsize) (p.n - length + 1) * sizeof (float));
        memset (acc->work_im, 0, (gsize) p.n * sizeof (float));
        fft_forward (&acc->plan_align, acc->work_re, acc->work_im);
        for (int k = 0; k <= half; k++) {
            acc->spectra_re[(gsize) k * n_bands + b] = acc->work_re[k];
            acc->spectra_im[(gsize) k * n_bands + b] = acc->work_im[k];
        }
        p.centers[b] = bands->centers[b];
    }
    p.spectra_re = acc->spectra_re;
    p.spectra_im = acc->spectra_im;

    // Average over time (the search is made on the average)
    align_average (acc, history, &p, span);
    p.spectra_re = history->re;
    p.spectra_im = history->im;

    // Starting points: the angle found in the previous frame (0 at first), the edge orientations, and the best
    // angle of a coarse sweep of +/- 45 degrees (smoothed profiles: the peak is about resolution / span radians
    // wide). The sweep finds isolated lines even among other edges, the orientations find fine patterns (a grid
    // finer than the coarse resolution is invisible).
    double starts[ALIGN_MAX_SEEDS + 2];
    int n_starts = 0;
    starts[n_starts++] = history->angle;
    for (int i = 0; i < n_seeds; i++)
        starts[n_starts++] = seed_angles[i];
    double const coarse_resolution = MAX (ALIGN_SEED_RESOLUTION, span / ALIGN_COARSE_SAMPLES);
    double const coarse_step = coarse_resolution / (2.0 * span);
    align_set_weights (acc, &p, coarse_resolution, 0);
    double coarse_angle = 0, coarse_energy = -1;
    int const n_coarse = (int) ceil (ALIGN_MAX_ANGLE / coarse_step);
    for (int i = -n_coarse; i <= n_coarse; i++) {
        double const angle = CLAMP (i * coarse_step, -ALIGN_MAX_ANGLE, ALIGN_MAX_ANGLE);
        double const energy = align_energy (&p, tan (angle), NULL);
        if (energy > coarse_energy) {
            coarse_energy = energy;
            coarse_angle = angle;
        }
    }
    gboolean coarse_is_new = TRUE;
    for (int i = 0; i < n_starts; i++)
        coarse_is_new = coarse_is_new && fabs (starts[i] - coarse_angle) > ALIGN_SEED_WINDOW / 2;
    if (coarse_is_new)
        starts[n_starts++] = coarse_angle;

    // The best line from any starting point
    double best_angle = 0, best_quality = -1;
    for (int i = 0; i < n_starts; i++) {
        double const window = MAX (ALIGN_SEED_WINDOW, i == n_starts - 1 && coarse_is_new ? coarse_step : 0);
        double const angle = align_refine (acc, &p, span, starts[i], window, ALIGN_SEED_RESOLUTION);
        double const q = align_quality (acc, &p, angle);
        if (q > best_quality) {
            best_quality = q;
            best_angle = angle;
        }
    }
    // Final angle, with the weights matched to the signal and the noise
    best_angle = align_refine_matched (acc, &p, span, best_angle);
    history->angle = best_angle;
    *quality = MAX (best_quality, 0.0);
    *slope = tan (best_angle);
}

static void finish_alignment (MetricsAccumulator *acc, LrdMetrics *out)
{
    double const aspect = acc->request.pixel_aspect > 0 ? acc->request.pixel_aspect : 1.0;
    int const width = acc->request.width, height = acc->request.height;
    memset (&out->verticals, 0, sizeof (out->verticals));
    memset (&out->horizontals, 0, sizeof (out->horizontals));

    double seeds[ALIGN_MAX_SEEDS], seed_angles[ALIGN_MAX_SEEDS];
    int const n_seeds = acc->n_tiles_x > 0 ? orientation_seeds (acc, seeds) : 0;

    // A new series, region or format: new averages
    const MetricsRequest *r = &acc->request;
    int const region[4] = {r->x, r->y, r->width, r->height};
    if (r->series != acc->history_series || memcmp (region, acc->history_region, sizeof (region)) != 0 ||
        memcmp (&r->layout, &acc->history_layout, sizeof (r->layout)) != 0) {
        acc->vertical_history.valid = FALSE;
        acc->horizontal_history.valid = FALSE;
        acc->history_series = r->series;
        memcpy (acc->history_region, region, sizeof (region));
        acc->history_layout = r->layout;
    }

    if (acc->n_vertical_bands > 0) {
        // Lines x = x0 + slope y (image pixels). A positive slope is a line whose bottom is on the right: turned
        // counterclockwise on the screen (y down). Physical slope: x and y in physical units (pixel_aspect).
        for (int i = 0; i < n_seeds; i++)
            seed_angles[i] = -seeds[i] * G_PI / 180;
        double slope, quality;
        align_family (acc, &acc->vertical_history, acc->vertical_profiles, &acc->vertical_bands, width, height,
                      seed_angles, n_seeds, &slope, &quality);
        out->verticals.computed = TRUE;
        out->verticals.frames = acc->vertical_history.frames;
        out->verticals.tilt = -atan (slope * aspect) * 180.0 / G_PI;
        out->verticals.quality = quality;
    }
    if (acc->n_horizontal_bands > 0) {
        // Lines y = y0 + slope x. A positive slope is a line whose right end is lower: turned clockwise.
        for (int i = 0; i < n_seeds; i++)
            seed_angles[i] = seeds[i] * G_PI / 180;
        double slope, quality;
        align_family (acc, &acc->horizontal_history, acc->horizontal_profiles, &acc->horizontal_bands, height, width,
                      seed_angles, n_seeds, &slope, &quality);
        out->horizontals.computed = TRUE;
        out->horizontals.frames = acc->horizontal_history.frames;
        out->horizontals.tilt = atan (slope / aspect) * 180.0 / G_PI;
        out->horizontals.quality = quality;
    }
}

// FOURIER TRANSFORM

// Inferno color map (matplotlib), 0 to 1
static guint32 inferno (double t)
{
    static const guint8 stops[][3] = {
        {0, 0, 4}, {22, 11, 57}, {66, 10, 104}, {106, 23, 110}, {147, 38, 103}, {188, 55, 84},
        {221, 81, 58}, {243, 120, 25}, {252, 165, 10}, {246, 215, 70}, {252, 255, 164}
    };
    int const n_stops = G_N_ELEMENTS (stops);
    double const position = CLAMP (t, 0.0, 1.0) * (n_stops - 1);
    int const i = MIN ((int) position, n_stops - 2);
    double const f = position - i;
    guint32 rgb = 0;
    for (int c = 0; c < 3; c++)
        rgb = (rgb << 8) | (guint32) lround (stops[i][c] + f * (stops[i + 1][c] - stops[i][c]));
    return rgb;
}

static void finish_fft (MetricsAccumulator *acc, LrdMetrics *out)
{
    int const w = acc->fft_width, h = acc->fft_height;
    gsize const n_bins = (gsize) w * h;
    if (acc->fft_allocated < n_bins) {
        g_free (acc->fft_re);
        g_free (acc->fft_im);
        acc->fft_re = g_new (float, n_bins);
        acc->fft_im = g_new (float, n_bins);
        acc->fft_allocated = n_bins;
    }
    float *re = acc->fft_re, *im = acc->fft_im;

    // Mean of each bin, minus the mean of the region, times a Hann window
    double sum = 0;
    float const scale = (float) ((double) n_bins / ((double) acc->request.width * acc->request.height));
    for (gsize k = 0; k < n_bins; k++) {
        re[k] = acc->fft_sums[k] * scale;
        sum += re[k];
    }
    float const mean = (float) (sum / n_bins);
    for (int j = 0; j < h; j++) {
        double const wy = 0.5 - 0.5 * cos (2 * G_PI * j / h);
        for (int i = 0; i < w; i++) {
            double const wx = 0.5 - 0.5 * cos (2 * G_PI * i / w);
            re[(gsize) j * w + i] = (float) ((re[(gsize) j * w + i] - mean) * wx * wy);
            im[(gsize) j * w + i] = 0;
        }
    }

    // Rows, then columns
    fft_plan_init (&acc->plan_rows, w);
    for (int j = 0; j < h; j++)
        fft_forward (&acc->plan_rows, re + (gsize) j * w, im + (gsize) j * w);
    fft_plan_init (&acc->plan_columns, h);
    ensure_work (acc, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            acc->work_re[j] = re[(gsize) j * w + i];
            acc->work_im[j] = im[(gsize) j * w + i];
        }
        fft_forward (&acc->plan_columns, acc->work_re, acc->work_im);
        for (int j = 0; j < h; j++) {
            re[(gsize) j * w + i] = acc->work_re[j];
            im[(gsize) j * w + i] = acc->work_im[j];
        }
    }

    // Log power (in re), and its range without the zero frequency
    float low = G_MAXFLOAT, high = -G_MAXFLOAT;
    for (gsize k = 0; k < n_bins; k++) {
        re[k] = log10f (re[k] * re[k] + im[k] * im[k] + 1e-12f);
        if (k != 0) {
            low = MIN (low, re[k]);
            high = MAX (high, re[k]);
        }
    }
    // Contrast: from the median (most of the spectrum is noise, shown dark) to the 99.9th percentile
    double black = low, white = high;
    if (high > low) {
        enum { N_LEVELS = 1024 };
        guint32 levels[N_LEVELS] = {0};
        double const scale = (N_LEVELS - 1) / (high - low);
        for (gsize k = 1; k < n_bins; k++)
            levels[(int) ((re[k] - low) * scale)]++;
        guint64 cumulated = 0;
        gboolean black_found = FALSE;
        for (int l = 0; l < N_LEVELS; l++) {
            cumulated += levels[l];
            if (!black_found && cumulated >= 0.5 * (n_bins - 1)) {
                black = low + l / scale;
                black_found = TRUE;
            }
            if (cumulated >= 0.999 * (n_bins - 1)) {
                white = low + (l + 1) / scale;
                break;
            }
        }
    }
    if (white <= black)
        white = black + 1;

    // Colors, with the zero frequency at the center
    out->fft_width = w;
    out->fft_height = h;
    out->fft_stride = 4 * w;
    gsize const size = (gsize) out->fft_stride * h;
    if (out->fft_allocated != size) {
        g_free (out->fft_pixels);
        out->fft_pixels = g_malloc (size);
        out->fft_allocated = size;
    }
    for (int j = 0; j < h; j++) {
        guint32 *pixels = (guint32 *) (out->fft_pixels + (gsize) ((j + h / 2) % h) * out->fft_stride);
        for (int i = 0; i < w; i++)
            pixels[(i + w / 2) % w] = inferno ((re[(gsize) j * w + i] - black) / (white - black));
    }
    // Equal frequency scales on screen (the image is shown with square pixels): the spectrum spans +/- 1/2 cycle
    // per bin, w / width bins per image pixel
    out->fft_aspect = ((double) w / acc->request.width) / ((double) h / acc->request.height);
}

// FINISH

void metrics_finish (MetricsAccumulator *acc, LrdMetrics *out)
{
    gint64 const start_us = g_get_monotonic_time ();
    out->request = acc->request;
    if (!acc->active) {
        out->request.histogram = out->request.alignment = out->request.fft = FALSE;
        return;
    }
    if (acc->request.histogram)
        finish_histogram (acc, out);
    if (acc->request.alignment)
        finish_alignment (acc, out);
    if (acc->fft_width > 0)
        finish_fft (acc, out);
    else
        out->request.fft = FALSE;
    acc->active = FALSE;
    out->finish_ms = (g_get_monotonic_time () - start_us) / 1000.0;
}
