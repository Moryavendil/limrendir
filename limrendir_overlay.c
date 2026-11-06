#include <gtk/gtk.h>
#include <gst/video/video.h>
#include <arv.h>
#include <limrendir.h>
#include <logroutines.h>

// OVERLAY
//
// The grid and the Region Of Interest are drawn on top of the video widget (after gtksink has drawn the frame),
// never in the images themselves: snapshots and recordings stay untouched, whatever the pixel format.
// Everything is computed in image coordinates (pixels of the camera region, before rotation / flip), and mapped
// to the widget by the same rotation / flip as the videoflip element and the same letterboxing as gtksink.

// Look of the overlay (line widths are in screen pixels, whatever the zoom)
#define OVERLAY_GRID_SPACING 100        // image pixels between two grid lines, from the center
#define OVERLAY_GRID_LINE_WIDTH 2.0
#define OVERLAY_ROI_LINE_WIDTH 3.0
#define OVERLAY_SHADE_ALPHA (2.0 / 3.0) // the region outside the ROI is shown at 1/3 of its brightness
#define OVERLAY_ROI_DRAG_THRESHOLD 3.0  // a press and release closer than this (screen pixels) is a click, not a drag

// Matrix mapping image coordinates to the rotated / flipped image, for each videoflip method
static void videoflip_matrix (gint method, double width, double height, cairo_matrix_t *matrix)
{
    // cairo_matrix_init (m, xx, yx, xy, yy, x0, y0): x' = xx x + xy y + x0 ; y' = yx x + yy y + y0
    switch (method) {
        case 1: cairo_matrix_init (matrix, 0, 1, -1, 0, height, 0); break;          // clockwise
        case 2: cairo_matrix_init (matrix, -1, 0, 0, -1, width, height); break;     // rotate 180
        case 3: cairo_matrix_init (matrix, 0, -1, 1, 0, 0, width); break;           // counterclockwise
        case 4: cairo_matrix_init (matrix, -1, 0, 0, 1, width, 0); break;           // horizontal flip
        case 5: cairo_matrix_init (matrix, 1, 0, 0, -1, 0, height); break;          // vertical flip
        case 6: cairo_matrix_init (matrix, 0, 1, 1, 0, 0, 0); break;                // upper left diagonal
        case 7: cairo_matrix_init (matrix, 0, -1, -1, 0, height, width); break;     // upper right diagonal
        default: cairo_matrix_init_identity (matrix); break;                        // identity
    }
}

// Matrix mapping image coordinates to widget coordinates. Returns FALSE if there is no image to map.
static gboolean overlay_image_to_widget_matrix (LrdViewer *viewer, GtkWidget *widget, cairo_matrix_t *matrix)
{
    int const width = viewer->display_image_width;
    int const height = viewer->display_image_height;
    if (width <= 0 || height <= 0)
        return FALSE;

    gint const method = get_videoflip_method (viewer);
    gboolean const transposed = method == 1 || method == 3 || method == 6 || method == 7;
    int const shown_width = transposed ? height : width;
    int const shown_height = transposed ? width : height;

    // Where gtksink draws the frame: centered, scaled to fit, keeping the aspect ratio
    GstVideoRectangle source = {0, 0, shown_width, shown_height};
    GstVideoRectangle destination = {0, 0, gtk_widget_get_allocated_width (widget),
                                     gtk_widget_get_allocated_height (widget)};
    GstVideoRectangle result;
    gst_video_center_rect (&source, &destination, &result, TRUE);
    if (result.w <= 0 || result.h <= 0)
        return FALSE;

    cairo_matrix_t flip, placement;
    videoflip_matrix (method, width, height, &flip);
    cairo_matrix_init_translate (&placement, result.x, result.y);
    cairo_matrix_scale (&placement, (double) result.w / shown_width, (double) result.h / shown_height);
    cairo_matrix_multiply (matrix, &flip, &placement); // first the flip, then the placement
    return TRUE;
}

// Converts a point of the widget to image coordinates. Returns FALSE if there is no image.
static gboolean overlay_widget_to_image (LrdViewer *viewer, GtkWidget *widget, double *x, double *y)
{
    cairo_matrix_t matrix;
    if (!overlay_image_to_widget_matrix (viewer, widget, &matrix))
        return FALSE;
    if (cairo_matrix_invert (&matrix) != CAIRO_STATUS_SUCCESS)
        return FALSE;
    cairo_matrix_transform_point (&matrix, x, y);
    return TRUE;
}

// Adds the grid lines (in image coordinates) to the current path
static void overlay_grid_path (cairo_t *cr, GridType grid_type, int width, int height)
{
    // Lines go through the middle of the pixel rows / columns where the grid was drawn before
    if (grid_type == GRID_HORIZONTAL || grid_type == GRID_BOTH) {
        for (int line = height / 2 % OVERLAY_GRID_SPACING; line < height; line += OVERLAY_GRID_SPACING) {
            cairo_move_to (cr, 0, line + 0.5);
            cairo_line_to (cr, width, line + 0.5);
        }
    }
    if (grid_type == GRID_VERTICAL || grid_type == GRID_BOTH) {
        for (int line = width / 2 % OVERLAY_GRID_SPACING; line < width; line += OVERLAY_GRID_SPACING) {
            cairo_move_to (cr, line + 0.5, 0);
            cairo_line_to (cr, line + 0.5, height);
        }
    }
}

// Draws the overlay on top of the frame drawn by gtksink
static gboolean overlay_draw_cb (GtkWidget *widget, cairo_t *cr, LrdViewer *viewer)
{
    int const width = viewer->display_image_width;
    int const height = viewer->display_image_height;
    gboolean const draw_grid = viewer->grid_type != GRID_NOGRID;
    if (!viewer->show_roi && !draw_grid)
        return FALSE;

    cairo_matrix_t matrix;
    if (!overlay_image_to_widget_matrix (viewer, widget, &matrix))
        return FALSE;

    cairo_save (cr);
    // gtksink leaves the rectangle of the frame in the current path: start from an empty one
    cairo_new_path (cr);

    // Paths are built in image coordinates, but stroked in widget coordinates (constant line widths)
    if (viewer->show_roi) {
        constrain_roi_to_field_of_view (viewer, width, height);

        // Shade the region outside the ROI
        cairo_save (cr);
        cairo_transform (cr, &matrix);
        cairo_rectangle (cr, 0, 0, width, height);
        cairo_rectangle (cr, viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);
        cairo_restore (cr);
        cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_set_source_rgba (cr, 0, 0, 0, OVERLAY_SHADE_ALPHA);
        cairo_fill (cr);
    }

    if (draw_grid) {
        cairo_save (cr);
        cairo_transform (cr, &matrix);
        overlay_grid_path (cr, viewer->grid_type, width, height);
        cairo_restore (cr);
        cairo_set_line_width (cr, OVERLAY_GRID_LINE_WIDTH);
        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_stroke (cr);
    }

    if (viewer->show_roi) {
        // ROI border (blue-cyan), on top of the grid
        cairo_save (cr);
        cairo_transform (cr, &matrix);
        cairo_rectangle (cr, viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);
        cairo_restore (cr);
        cairo_set_line_width (cr, OVERLAY_ROI_LINE_WIDTH);
        cairo_set_line_join (cr, CAIRO_LINE_JOIN_MITER);
        cairo_set_source_rgb (cr, 0.0, 0.75, 1.0);
        cairo_stroke (cr);
    }

    cairo_restore (cr);
    return FALSE;
}

// MOUSE: when the ROI mode is active, click, drag and release to draw the new ROI

static gboolean overlay_can_edit_roi (LrdViewer *viewer)
{
    return viewer->show_roi && !is_record_button_active (viewer);
}

// Sets the ROI to the rectangle between the drag start and (x, y), in image coordinates
static void overlay_set_roi_from_drag (LrdViewer *viewer, double x, double y)
{
    int const width = viewer->display_image_width;
    int const height = viewer->display_image_height;
    double const x_min = CLAMP (MIN (viewer->roi_drag_x, x), 0, width);
    double const x_max = CLAMP (MAX (viewer->roi_drag_x, x), 0, width);
    double const y_min = CLAMP (MIN (viewer->roi_drag_y, y), 0, height);
    double const y_max = CLAMP (MAX (viewer->roi_drag_y, y), 0, height);

    // All the pixels touched by the rectangle
    viewer->roi_x = (int) floor (x_min);
    viewer->roi_y = (int) floor (y_min);
    viewer->roi_w = MAX ((int) ceil (x_max) - viewer->roi_x, 1);
    viewer->roi_h = MAX ((int) ceil (y_max) - viewer->roi_y, 1);
    constrain_roi_to_field_of_view (viewer, width, height);
}

static void overlay_update_cursor (LrdViewer *viewer, GtkWidget *widget)
{
    GdkWindow *window = gtk_widget_get_window (widget);
    gboolean const crosshair = overlay_can_edit_roi (viewer);
    if (window == NULL || crosshair == viewer->roi_crosshair_cursor)
        return;
    GdkCursor *cursor = crosshair ? gdk_cursor_new_from_name (gtk_widget_get_display (widget), "crosshair") : NULL;
    gdk_window_set_cursor (window, cursor);
    g_clear_object (&cursor);
    viewer->roi_crosshair_cursor = crosshair;
}

static gboolean overlay_button_press_cb (GtkWidget *widget, GdkEventButton *event, LrdViewer *viewer)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_PRIMARY || !overlay_can_edit_roi (viewer))
        return FALSE;

    double x = event->x, y = event->y;
    if (!overlay_widget_to_image (viewer, widget, &x, &y))
        return FALSE;
    // The drag must start on the image
    if (x < 0 || y < 0 || x > viewer->display_image_width || y > viewer->display_image_height)
        return FALSE;

    viewer->roi_dragging = TRUE;
    viewer->roi_drag_moved = FALSE;
    viewer->roi_drag_x = x;
    viewer->roi_drag_y = y;
    viewer->roi_drag_widget_x = event->x;
    viewer->roi_drag_widget_y = event->y;
    viewer->roi_before_drag[0] = viewer->roi_x;
    viewer->roi_before_drag[1] = viewer->roi_y;
    viewer->roi_before_drag[2] = viewer->roi_w;
    viewer->roi_before_drag[3] = viewer->roi_h;
    return TRUE;
}

static gboolean overlay_motion_cb (GtkWidget *widget, GdkEventMotion *event, LrdViewer *viewer)
{
    overlay_update_cursor (viewer, widget);
    if (!viewer->roi_dragging)
        return FALSE;

    if (!viewer->roi_drag_moved &&
        hypot (event->x - viewer->roi_drag_widget_x, event->y - viewer->roi_drag_widget_y) < OVERLAY_ROI_DRAG_THRESHOLD)
        return TRUE;
    viewer->roi_drag_moved = TRUE;

    double x = event->x, y = event->y;
    if (overlay_widget_to_image (viewer, widget, &x, &y)) {
        overlay_set_roi_from_drag (viewer, x, y);
        gtk_widget_queue_draw (widget);
    }
    return TRUE;
}

static gboolean overlay_button_release_cb (GtkWidget *widget, GdkEventButton *event, LrdViewer *viewer)
{
    if (event->button != GDK_BUTTON_PRIMARY || !viewer->roi_dragging)
        return FALSE;
    viewer->roi_dragging = FALSE;

    double x = event->x, y = event->y;
    if (!viewer->roi_drag_moved || !overlay_widget_to_image (viewer, widget, &x, &y)) {
        // A simple click (or no image anymore): keep the previous ROI
        viewer->roi_x = viewer->roi_before_drag[0];
        viewer->roi_y = viewer->roi_before_drag[1];
        viewer->roi_w = viewer->roi_before_drag[2];
        viewer->roi_h = viewer->roi_before_drag[3];
    } else {
        overlay_set_roi_from_drag (viewer, x, y);
        log_debug ("New ROI drawn with the mouse: x: %d ; y: %d ; w: %d ; h: %d",
                   viewer->roi_x, viewer->roi_y, viewer->roi_w, viewer->roi_h);
    }
    gtk_widget_queue_draw (widget);
    update_status_bar_cb (viewer);
    return TRUE;
}

// Draws the overlay on this video widget (the gtksink widget) and listens to its mouse events
void overlay_attach (LrdViewer *viewer, GtkWidget *video_widget)
{
    viewer->video_widget = video_widget;
    viewer->roi_dragging = FALSE;
    viewer->roi_crosshair_cursor = FALSE;
    gtk_widget_add_events (video_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    g_signal_connect_after (video_widget, "draw", G_CALLBACK (overlay_draw_cb), viewer);
    g_signal_connect (video_widget, "button-press-event", G_CALLBACK (overlay_button_press_cb), viewer);
    g_signal_connect (video_widget, "motion-notify-event", G_CALLBACK (overlay_motion_cb), viewer);
    g_signal_connect (video_widget, "button-release-event", G_CALLBACK (overlay_button_release_cb), viewer);
}

// Forgets the video widget (it is about to be removed)
void overlay_detach (LrdViewer *viewer)
{
    if (viewer->video_widget != NULL)
        g_signal_handlers_disconnect_by_data (viewer->video_widget, viewer);
    viewer->video_widget = NULL;
    viewer->roi_dragging = FALSE;
}

// Redraws the overlay (needed when the grid or the ROI change while no new frame arrives)
void overlay_queue_redraw (LrdViewer *viewer)
{
    if (viewer->video_widget == NULL)
        return;
    gtk_widget_queue_draw (viewer->video_widget);
    overlay_update_cursor (viewer, viewer->video_widget);
}
