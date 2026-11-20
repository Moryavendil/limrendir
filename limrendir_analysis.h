#ifndef LIMRENDIR_ANALYSIS_H
#define LIMRENDIR_ANALYSIS_H

// (include limrendir.h first: it has no include guard)
#include <gst/gst.h>
#include <arv.h>
#include "limrendir_metrics.h"

// ZEBRA STRIPES, FOCUS PEAKING AND IMAGE METRICS
//
// Some of the frames sent to the display are analysed by a background worker thread, which computes two masks:
// the saturated pixels (zebra) and the sharp edges (focus peaking). The overlay draws them over the video: the
// images themselves, the snapshots and the recordings are untouched.
// Only Mono8 and the 12-bit mono formats (Mono12, Mono12p, Mono12Packed) are supported.
// The same worker computes the image metrics of the side panel (histogram, alignment, Fourier transform, see
// limrendir_metrics.h) of the ROI, or of the whole image, for all the mono formats, 8-bit Bayer and RGB / BGR.

// Masks computed from one frame, in image coordinates: mask pixel (i, j) covers the image pixels
// [i * cell, (i + 1) * cell) x [j * cell, (j + 1) * cell), and is 255 if one of them is on, 0 otherwise.
// The masks are CAIRO_FORMAT_A8 data (rows 'stride' bytes apart).
typedef struct {
    guint8 *zebra;             // NULL if zebra was off for this frame
    guint8 *peaking;           // NULL if peaking was off for this frame
    gboolean zebra_any;        // the zebra mask has at least one pixel on
    int width, height, stride; // size of the masks
    int cell;                  // image pixels per mask pixel, in x and y
    int image_width, image_height;
} LrdAnalysisMasks;

// Lifetime of the viewer (the worker thread is started when first needed, and stopped here)
void analysis_init (LrdViewer *viewer);
void analysis_finalize (LrdViewer *viewer);

// Called when the display starts (with the pixel format and size of the frames) and stops. While stopped, no frame
// is accepted and no mask is given. A pixel format that is not supported turns the features off.
void analysis_start (LrdViewer *viewer, ArvPixelFormat pixel_format, int width, int height);
void analysis_stop (LrdViewer *viewer);

// Whether the pixel format of the video can be analysed
gboolean analysis_is_available (LrdViewer *viewer);

// To be called when viewer->zebra, viewer->peaking or the metrics wanted (viewer->metrics_*) changed
void analysis_settings_changed (LrdViewer *viewer);

// Whether the pixel format of the video can be measured by the metrics
gboolean analysis_metrics_available (LrdViewer *viewer);
// Follows the ROI (region of the metrics): cheap, to be called whenever it may have changed (main thread)
void analysis_update_metrics_region (LrdViewer *viewer);

// Offers a frame about to be sent to the display, from the stream thread. Cheap and never waits: the frame is
// taken (a reference) only if the worker is idle and the last frame taken is old enough (the analysis rate is limited,
// more when 'recording'). The caller first checks viewer->analysis_active (atomic), so that nothing at all is done
// when the features are off.
void analysis_offer_frame (LrdViewer *viewer, GstBuffer *buffer, gboolean recording);

// The latest masks of the current video, or NULL if there are none. analysis_unlock_masks must always be called
// after (the masks stay valid until then). Main thread.
const LrdAnalysisMasks *analysis_lock_masks (LrdViewer *viewer);
void analysis_unlock_masks (LrdViewer *viewer);

// The latest metrics of the current video, or NULL if there are none. analysis_unlock_metrics must always be called
// after (the metrics stay valid until then). Main thread.
const LrdMetrics *analysis_lock_metrics (LrdViewer *viewer);
void analysis_unlock_metrics (LrdViewer *viewer);

#endif //LIMRENDIR_ANALYSIS_H
