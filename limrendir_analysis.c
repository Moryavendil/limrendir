#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>
#include "limrendir_analysis.h"

// ZEBRA STRIPES AND FOCUS PEAKING: worker thread and kernels
//
// The acquisition frame rate must never suffer, while previewing or recording:
// - The stream thread only offers frames (analysis_offer_frame): a time check, a g_mutex_trylock and, if the worker
//   is idle, a gst_buffer_ref. No per-pixel work, no allocation, no waiting: if the lock is taken or the worker is
//   busy, the frame is simply not analysed. Frames are never queued (one frame at most waits for the worker).
// - Only frames already sent to the display are offered, at most ANALYSIS_PREVIEW_MAX_RATE per second
//   (ANALYSIS_RECORDING_MAX_RATE while recording).
// - The analysis runs in a single worker thread, with a low priority (nice ANALYSIS_THREAD_NICE).
//
// Holding the frame: the worker reads the GstBuffer itself (a reference taken by the stream thread) and releases it
// as soon as the masks are computed, in a single pass. The GstBuffer wraps a stream buffer (counted in
// n_display_buffers), which goes back to the acquisition only once released. The alternative, a subsampled copy,
// cannot be made by the stream thread (no per-pixel work there), so the worker would hold the buffer during the copy
// anyway: the copy would save little hold time (it reads the same memory, at the same speed as the single pass that
// computes both masks) for an extra allocation and a second read. The hold is short (a few ms, see the "analysis"
// debug logs), happens at most 20 (5) times a second, and concerns one buffer out of at least 10 (hundreds while
// recording): it cannot starve the acquisition. At worst, in preview, a displayed frame is skipped when the display
// would hold more than LRD_MAX_DISPLAY_BUFFERS buffers (see can_display_buffer).
//
// The masks are in image coordinates (before rotation / flip), at about the screen resolution: a mask pixel covers
// cell x cell image pixels (cell = image pixels per screen pixel, given by the overlay) and is on if any of them is.
// This also thickens 1-pixel edges, which would otherwise disappear when the image is shown smaller.
// They are double-buffered: the worker fills 'back', then swaps it with 'front' (drawn by the main thread) under
// mask_mutex, and asks the main loop to redraw.

// Maximum analysis rates (frames per second)
#define ANALYSIS_PREVIEW_MAX_RATE 20
#define ANALYSIS_RECORDING_MAX_RATE 5
// While recording, only one row out of ANALYSIS_RECORDING_STEP is analysed, if the masks are that coarse anyway
// (cell >= ANALYSIS_RECORDING_STEP), leaving the CPU to the recording. Whole rows are skipped rather than every other
// pixel in x and y: the row kernels stay vectorized, which makes them faster than strided ones doing a quarter of the
// work. A sharp edge still shows, since the Sobel kernel spans 3 rows.
#define ANALYSIS_RECORDING_STEP 2

// Zebra: pixels at or above this value, in native units
#define ANALYSIS_ZEBRA_THRESHOLD_8BIT 253
#define ANALYSIS_ZEBRA_THRESHOLD_12BIT 4063    // = 253 / 255 x 4095

// Focus peaking: pixels where the 3x3 Sobel gradient |gx| + |gy| (native units) is at least this fraction of the
// full scale (255 or 4095). The threshold is fixed, not relative to the frame: a sharp step of amplitude A gives
// |gx| + |gy| = 4 A, so LOW shows the steps of at least 15 % of the full scale, MEDIUM 7.5 %, HIGH 3.75 %.
#define ANALYSIS_PEAKING_THRESHOLD_LOW 0.60
#define ANALYSIS_PEAKING_THRESHOLD_MEDIUM 0.30
#define ANALYSIS_PEAKING_THRESHOLD_HIGH 0.15

// Nice value of the worker thread: the acquisition, the recording and the display go first
#define ANALYSIS_THREAD_NICE 10
// The analysis rate and times are logged (debug level) every...
#define ANALYSIS_STATS_INTERVAL_US (2 * G_USEC_PER_SEC)

// What to do with a frame
typedef struct {
    int depth;                 // 8 (8-bit data), 12 (16-bit data scaled from 12 bits, see arv_to_gst_buffer), 0: none
    int width, height;
    gboolean zebra;
    int zebra_threshold;       // native units
    int peaking_threshold;     // native units, 0 if peaking is off
    guint generation;          // see LrdAnalysis
    gboolean recording;
} AnalysisJob;

// One set of masks and its storage
typedef struct {
    LrdAnalysisMasks masks;    // zebra / peaking point to the storage below, or are NULL
    guint8 *zebra_data;
    gsize zebra_size;          // allocated bytes
    guint8 *peaking_data;
    gsize peaking_size;
} AnalysisMaskBuffers;

struct _LrdAnalysis {
    LrdViewer *viewer;

    // Hand-off of the frames, between the stream thread (g_mutex_trylock only) and the worker
    GMutex mutex;
    GCond cond;
    GThread *thread;           // the worker, started when first needed (main thread)
    gboolean quit;
    gboolean running;          // between analysis_start and analysis_stop
    gboolean accepting;        // running, a feature is on and the pixel format is supported
    gboolean busy;             // the worker analyses a frame
    GstBuffer *pending;        // frame waiting for the worker (a reference), or NULL
    AnalysisJob pending_job;
    AnalysisJob settings;      // for the next frames (written by the main thread). settings.generation changes when
                               // the video starts or stops: masks of an older generation are discarded.
    gint64 last_offer_us;      // stream thread only

    // Masks
    GMutex mask_mutex;
    AnalysisMaskBuffers front; // drawn by the main thread
    AnalysisMaskBuffers back;  // filled by the worker
    gboolean front_valid;
    guint front_generation;
    guint redraw_source;       // idle redraw requested by the worker, 0 if none

    // Worker only
    guint8 *hits;              // per-pixel result of one row
    int hits_size;
    gint64 stats_first_us, stats_last_us;
    guint stats_n_frames;
    gint64 stats_total_us, stats_max_us;
    guint stats_generation;
};

// KERNELS
// Each fills hits[x] (255 or 0) for the pixels x of one row. They are written to be vectorized by the compiler.

// Native value of pixel x of a row
#define LOAD_8BIT(row, x) ((int) (row)[x])
#define LOAD_12BIT(row, x) ((int) (GUINT16_FROM_LE ((row)[x]) >> 4))    // undoes (v << 4) | (v >> 8)

#define DEFINE_ROW_KERNELS(SUFFIX, TYPE, LOAD)                                                                       \
static void zebra_row_##SUFFIX (const guint8 *row_bytes, int width, int threshold, guint8 *hits)                    \
{                                                                                                                    \
    const TYPE *row = (const TYPE *) row_bytes;                                                                      \
    for (int x = 0; x < width; x++)                                                                                  \
        hits[x] = LOAD (row, x) >= threshold ? 0xff : 0;                                                             \
}                                                                                                                    \
                                                                                                                     \
/* 3x3 Sobel, |gx| + |gy|. The first and last pixels of the row (no neighbour) are never edges. */                  \
static void peaking_row_##SUFFIX (const guint8 *above_bytes, const guint8 *row_bytes, const guint8 *below_bytes,     \
                                  int width, int threshold, guint8 *hits)                                            \
{                                                                                                                    \
    const TYPE *r0 = (const TYPE *) above_bytes;                                                                     \
    const TYPE *r1 = (const TYPE *) row_bytes;                                                                       \
    const TYPE *r2 = (const TYPE *) below_bytes;                                                                     \
    hits[0] = 0;                                                                                                     \
    hits[width - 1] = 0;                                                                                             \
    for (int x = 1; x < width - 1; x++) {                                                                            \
        int const gx = (LOAD (r0, x + 1) + 2 * LOAD (r1, x + 1) + LOAD (r2, x + 1))                                  \
                     - (LOAD (r0, x - 1) + 2 * LOAD (r1, x - 1) + LOAD (r2, x - 1));                                 \
        int const gy = (LOAD (r2, x - 1) + 2 * LOAD (r2, x) + LOAD (r2, x + 1))                                      \
                     - (LOAD (r0, x - 1) + 2 * LOAD (r0, x) + LOAD (r0, x + 1));                                     \
        hits[x] = abs (gx) + abs (gy) >= threshold ? 0xff : 0;                                                       \
    }                                                                                                                \
}

DEFINE_ROW_KERNELS (8bit, guint8, LOAD_8BIT)
DEFINE_ROW_KERNELS (12bit, guint16, LOAD_12BIT)

// ORs the hits of each group of 'cell' pixels into the mask row (inlined with a constant cell, it is vectorized)
static inline guint8 reduce_cells (const guint8 *hits, int width, int cell, guint8 *mask_row)
{
    guint8 any = 0;
    int const n_full = width / cell;
    for (int i = 0; i < n_full; i++) {
        guint8 hit = 0;
        for (int k = 0; k < cell; k++)
            hit |= hits[i * cell + k];
        mask_row[i] |= hit;
        any |= hit;
    }
    // Last, partial cell
    if (n_full * cell < width) {
        guint8 hit = 0;
        for (int x = n_full * cell; x < width; x++)
            hit |= hits[x];
        mask_row[n_full] |= hit;
        any |= hit;
    }
    return any;
}

// Turns on the mask pixels of a mask row that cover a hit of the image row. Returns whether there was a hit.
static gboolean reduce_row (const guint8 *hits, int width, int cell, guint8 *mask_row)
{
    switch (cell) {
        // The usual cells, with a constant cell
        case 1: return reduce_cells (hits, width, 1, mask_row) != 0;
        case 2: return reduce_cells (hits, width, 2, mask_row) != 0;
        case 3: return reduce_cells (hits, width, 3, mask_row) != 0;
        case 4: return reduce_cells (hits, width, 4, mask_row) != 0;
        default: return reduce_cells (hits, width, cell, mask_row) != 0;
    }
}

// A cleared mask of 'size' bytes (reallocated only when the size changes)
static guint8 *cleared_mask (guint8 **data, gsize *allocated, gsize size)
{
    if (*allocated != size) {
        g_free (*data);
        *data = g_malloc (size);
        *allocated = size;
    }
    memset (*data, 0, size);
    return *data;
}

// Computes the masks of a frame. Returns FALSE if the frame cannot be read.
static gboolean analysis_compute (LrdAnalysis *a, GstBuffer *buffer, const AnalysisJob *job, int cell,
                                  AnalysisMaskBuffers *out)
{
    int const width = job->width, height = job->height;
    int const bytes_per_pixel = job->depth == 8 ? 1 : 2;
    // Rows are padded to a multiple of 4 bytes (see arv_to_gst_buffer)
    gsize const stride = ((gsize) width * bytes_per_pixel + 3) & ~(gsize) 3;
    if (width < 3 || height < 3 || job->depth == 0)
        return FALSE;

    GstMapInfo map;
    if (!gst_buffer_map (buffer, &map, GST_MAP_READ))
        return FALSE;
    if (map.size < stride * (height - 1) + (gsize) width * bytes_per_pixel) {
        log_debug ("Analysis: frame of %" G_GSIZE_FORMAT " bytes, too small for %dx%d.", map.size, width, height);
        gst_buffer_unmap (buffer, &map);
        return FALSE;
    }

    cell = CLAMP (cell, 1, MAX (width, height));
    int const step = job->recording ? MIN (ANALYSIS_RECORDING_STEP, cell) : 1;    // between analysed rows
    LrdAnalysisMasks *m = &out->masks;
    m->cell = cell;
    m->image_width = width;
    m->image_height = height;
    m->width = (width + cell - 1) / cell;
    m->height = (height + cell - 1) / cell;
    m->stride = cairo_format_stride_for_width (CAIRO_FORMAT_A8, m->width);
    gsize const size = (gsize) m->stride * m->height;
    m->zebra = job->zebra ? cleared_mask (&out->zebra_data, &out->zebra_size, size) : NULL;
    m->peaking = job->peaking_threshold > 0 ? cleared_mask (&out->peaking_data, &out->peaking_size, size) : NULL;
    m->zebra_any = FALSE;

    if (a->hits_size < width) {
        g_free (a->hits);
        a->hits = g_malloc (width);
        a->hits_size = width;
    }
    guint8 *const hits = a->hits;

    const guint8 *const data = map.data;
    for (int y = 0; y < height; y += step) {
        const guint8 *row = data + (gsize) y * stride;
        gsize const mask_offset = (gsize) (y / cell) * m->stride;
        if (m->zebra != NULL) {
            if (job->depth == 8)
                zebra_row_8bit (row, width, job->zebra_threshold, hits);
            else
                zebra_row_12bit (row, width, job->zebra_threshold, hits);
            if (reduce_row (hits, width, cell, m->zebra + mask_offset))
                m->zebra_any = TRUE;
        }
        if (m->peaking != NULL && y >= 1 && y < height - 1) {
            if (job->depth == 8)
                peaking_row_8bit (row - stride, row, row + stride, width, job->peaking_threshold, hits);
            else
                peaking_row_12bit (row - stride, row, row + stride, width, job->peaking_threshold, hits);
            reduce_row (hits, width, cell, m->peaking + mask_offset);
        }
    }

    gst_buffer_unmap (buffer, &map);
    return TRUE;
}

// WORKER THREAD

static void analysis_lower_thread_priority (void)
{
#ifdef __linux__
    // On Linux, the nice value of a single thread is set with its thread id
    if (setpriority (PRIO_PROCESS, (id_t) syscall (SYS_gettid), ANALYSIS_THREAD_NICE) == 0)
        log_trace ("Analysis thread priority lowered (nice %d).", ANALYSIS_THREAD_NICE);
    else
        log_debug ("Could not lower the priority of the analysis thread.");
#endif
}

static gboolean analysis_redraw_idle (gpointer data)
{
    LrdViewer *viewer = data;
    LrdAnalysis *a = viewer->analysis;
    g_mutex_lock (&a->mask_mutex);
    a->redraw_source = 0;
    g_mutex_unlock (&a->mask_mutex);
    overlay_queue_draw (viewer);
    return G_SOURCE_REMOVE;
}

// Logs (debug level) the effective analysis rate and the time per frame, every ANALYSIS_STATS_INTERVAL_US
static void analysis_update_stats (LrdAnalysis *a, const AnalysisJob *job, gint64 start_us, gint64 duration_us)
{
    const LrdAnalysisMasks *m = &a->front.masks;
    log_trace ("Analysis of a frame: %.2f ms", duration_us / 1000.0);

    // A new video, or the features were off for a while: new measurement
    if (a->stats_n_frames == 0 || job->generation != a->stats_generation ||
        start_us - a->stats_last_us > 2 * G_USEC_PER_SEC / ANALYSIS_RECORDING_MAX_RATE) {
        a->stats_first_us = start_us;
        a->stats_n_frames = 0;
        a->stats_total_us = 0;
        a->stats_max_us = 0;
        a->stats_generation = job->generation;
    }
    a->stats_n_frames++;
    a->stats_last_us = start_us;
    a->stats_total_us += duration_us;
    a->stats_max_us = MAX (a->stats_max_us, duration_us);

    if (a->stats_n_frames > 1 && start_us - a->stats_first_us >= ANALYSIS_STATS_INTERVAL_US) {
        log_debug ("Zebra / peaking analysis: %.1f frames/s, %.2f ms per frame (max %.2f ms), %dx%d %d-bit, "
                   "mask cell %d, 1 row out of %d%s",
                   (a->stats_n_frames - 1) * (double) G_USEC_PER_SEC / (start_us - a->stats_first_us),
                   a->stats_total_us / 1000.0 / a->stats_n_frames, a->stats_max_us / 1000.0,
                   job->width, job->height, job->depth, m->cell,
                   job->recording ? MIN (ANALYSIS_RECORDING_STEP, m->cell) : 1,
                   job->recording ? " (recording)" : "");
        a->stats_n_frames = 0;
    }
}

static gpointer analysis_thread (gpointer data)
{
    LrdAnalysis *a = data;
    LrdViewer *viewer = a->viewer;

    analysis_lower_thread_priority ();

    for (;;) {
        g_mutex_lock (&a->mutex);
        while (!a->quit && a->pending == NULL)
            g_cond_wait (&a->cond, &a->mutex);
        if (a->quit) {
            g_mutex_unlock (&a->mutex);
            break;
        }
        GstBuffer *buffer = g_steal_pointer (&a->pending);
        AnalysisJob const job = a->pending_job;
        a->busy = TRUE;
        g_mutex_unlock (&a->mutex);

        gint64 const start_us = g_get_monotonic_time ();
        gboolean const computed = analysis_compute (a, buffer, &job, g_atomic_int_get (&viewer->analysis_cell_size),
                                                    &a->back);
        // The stream buffer goes back to the acquisition at once
        gst_buffer_unref (buffer);
        gint64 const duration_us = g_get_monotonic_time () - start_us;

        g_mutex_lock (&a->mutex);
        a->busy = FALSE;
        gboolean const current = job.generation == a->settings.generation;
        g_mutex_unlock (&a->mutex);

        if (!computed || !current)
            continue;

        g_mutex_lock (&a->mask_mutex);
        AnalysisMaskBuffers const swap = a->front;
        a->front = a->back;
        a->back = swap;
        a->front_valid = TRUE;
        a->front_generation = job.generation;
        if (a->redraw_source == 0)
            a->redraw_source = g_idle_add (analysis_redraw_idle, viewer);
        g_mutex_unlock (&a->mask_mutex);

        analysis_update_stats (a, &job, start_us, duration_us);
    }
    return NULL;
}

// PUBLIC FUNCTIONS

// Bit depth of the supported pixel formats, 0 for the others
static int analysis_depth (ArvPixelFormat pixel_format)
{
    switch (pixel_format) {
        case ARV_PIXEL_FORMAT_MONO_8:
            return 8;
        case ARV_PIXEL_FORMAT_MONO_12:
        case ARV_PIXEL_FORMAT_MONO_12_PACKED:
#ifdef ARV_PIXEL_FORMAT_MONO_12P    // (not defined in Aravis 0.8)
        case ARV_PIXEL_FORMAT_MONO_12P:
#endif
            return 12;
        default:
            return 0;
    }
}

void analysis_init (LrdViewer *viewer)
{
    LrdAnalysis *a = g_new0 (LrdAnalysis, 1);
    a->viewer = viewer;
    g_mutex_init (&a->mutex);
    g_cond_init (&a->cond);
    g_mutex_init (&a->mask_mutex);
    viewer->analysis = a;
    viewer->analysis_active = FALSE;
    viewer->analysis_cell_size = 1;
}

void analysis_finalize (LrdViewer *viewer)
{
    LrdAnalysis *a = viewer->analysis;
    if (a == NULL)
        return;
    g_atomic_int_set (&viewer->analysis_active, FALSE);

    g_mutex_lock (&a->mutex);
    a->quit = TRUE;
    a->accepting = FALSE;
    g_cond_signal (&a->cond);
    g_mutex_unlock (&a->mutex);
    if (a->thread != NULL)
        g_thread_join (a->thread);

    g_clear_pointer (&a->pending, gst_buffer_unref);
    if (a->redraw_source != 0)
        g_source_remove (a->redraw_source);
    g_free (a->front.zebra_data);
    g_free (a->front.peaking_data);
    g_free (a->back.zebra_data);
    g_free (a->back.peaking_data);
    g_free (a->hits);
    g_mutex_clear (&a->mutex);
    g_cond_clear (&a->cond);
    g_mutex_clear (&a->mask_mutex);
    g_free (a);
    viewer->analysis = NULL;
}

void analysis_settings_changed (LrdViewer *viewer)
{
    LrdAnalysis *a = viewer->analysis;
    gboolean const on = viewer->zebra || viewer->peaking != PEAKING_OFF;

    double fraction = 0;
    switch (viewer->peaking) {
        case PEAKING_LOW: fraction = ANALYSIS_PEAKING_THRESHOLD_LOW; break;
        case PEAKING_MEDIUM: fraction = ANALYSIS_PEAKING_THRESHOLD_MEDIUM; break;
        case PEAKING_HIGH: fraction = ANALYSIS_PEAKING_THRESHOLD_HIGH; break;
        default: break;
    }

    g_mutex_lock (&a->mutex);
    AnalysisJob *s = &a->settings;
    int const full_scale = s->depth == 12 ? 4095 : 255;
    s->zebra = viewer->zebra;
    s->zebra_threshold = s->depth == 12 ? ANALYSIS_ZEBRA_THRESHOLD_12BIT : ANALYSIS_ZEBRA_THRESHOLD_8BIT;
    s->peaking_threshold = fraction > 0 ? MAX (1, (int) (fraction * full_scale + 0.5)) : 0;
    a->accepting = a->running && on && s->depth != 0;
    gboolean const accepting = a->accepting;
    g_mutex_unlock (&a->mutex);

    if (accepting && a->thread == NULL) {
        GError *error = NULL;
        a->thread = g_thread_try_new ("limrendir-analysis", analysis_thread, a, &error);
        if (a->thread == NULL) {
            log_warning ("Could not start the zebra / peaking thread: %s", error->message);
            g_clear_error (&error);
        }
    }
    g_atomic_int_set (&viewer->analysis_active, accepting && a->thread != NULL);
}

void analysis_start (LrdViewer *viewer, ArvPixelFormat pixel_format, int width, int height)
{
    LrdAnalysis *a = viewer->analysis;
    int const depth = analysis_depth (pixel_format);
    if (depth == 0 && (viewer->zebra || viewer->peaking != PEAKING_OFF)) {
        log_info ("Zebra and focus peaking are not available for this pixel format (0x%08x): turned off.",
                  (unsigned) pixel_format);
        viewer->zebra = FALSE;
        viewer->peaking = PEAKING_OFF;
    }

    g_mutex_lock (&a->mutex);
    a->settings.depth = depth;
    a->settings.width = width;
    a->settings.height = height;
    a->settings.generation++;
    a->running = TRUE;
    g_mutex_unlock (&a->mutex);

    analysis_settings_changed (viewer);
}

void analysis_stop (LrdViewer *viewer)
{
    LrdAnalysis *a = viewer->analysis;
    g_atomic_int_set (&viewer->analysis_active, FALSE);

    g_mutex_lock (&a->mutex);
    a->running = FALSE;
    a->accepting = FALSE;
    a->settings.generation++;    // the frame being analysed, if any, will be discarded
    GstBuffer *pending = g_steal_pointer (&a->pending);
    g_mutex_unlock (&a->mutex);
    // (outside the lock: releasing the frame gives its buffer back to the stream)
    g_clear_pointer (&pending, gst_buffer_unref);

    g_mutex_lock (&a->mask_mutex);
    a->front_valid = FALSE;
    g_mutex_unlock (&a->mask_mutex);
}

gboolean analysis_is_available (LrdViewer *viewer)
{
    // (settings.depth is only written by the main thread)
    return viewer->analysis->settings.depth != 0;
}

void analysis_offer_frame (LrdViewer *viewer, GstBuffer *buffer, gboolean recording)
{
    LrdAnalysis *a = viewer->analysis;
    gint64 const now_us = g_get_monotonic_time ();
    gint64 const interval_us = G_USEC_PER_SEC / (recording ? ANALYSIS_RECORDING_MAX_RATE : ANALYSIS_PREVIEW_MAX_RATE);
    if (now_us - a->last_offer_us < interval_us)
        return;
    // Never wait: if the main thread or the worker holds the lock, this frame is not analysed
    if (!g_mutex_trylock (&a->mutex))
        return;
    if (a->accepting && !a->busy && a->pending == NULL) {
        a->pending = gst_buffer_ref (buffer);
        a->pending_job = a->settings;
        a->pending_job.recording = recording;
        a->last_offer_us = now_us;
        g_cond_signal (&a->cond);
    }
    g_mutex_unlock (&a->mutex);
}

const LrdAnalysisMasks *analysis_lock_masks (LrdViewer *viewer)
{
    LrdAnalysis *a = viewer->analysis;
    g_mutex_lock (&a->mask_mutex);
    const LrdAnalysisMasks *m = &a->front.masks;
    // (settings.generation is only written by the main thread)
    if (!a->front_valid || a->front_generation != a->settings.generation ||
        m->image_width != viewer->display_image_width || m->image_height != viewer->display_image_height)
        return NULL;
    return m;
}

void analysis_unlock_masks (LrdViewer *viewer)
{
    g_mutex_unlock (&viewer->analysis->mask_mutex);
}
