#ifndef LIMRENDIR_PIXELFORMAT_H
#define LIMRENDIR_PIXELFORMAT_H

#include <arv.h>
#include <stdint.h>

// Mono pixel formats with more than 8 bits per pixel.
// Aravis gives them to GStreamer as 16-bit grey (GRAY16_LE), where 65535 is white. Their values are smaller
// (at most 4095 for Mono12) and some are packed (Mono10p, Mono12p, Mono10Packed, Mono12Packed), so they are
// unpacked and scaled to the 16-bit range for the display.

// Number of significant bits (10, 12, 14 or 16) of a mono format stored on more than 8 bits, or 0 for other formats
guint mono_high_bit_depth (ArvPixelFormat format);

// Writes the width x height pixel values of the mono image 'src' (src_size bytes, of pixel format 'format')
// as 16-bit values in 'dst', whose rows are 'dst_stride' bytes apart.
// If 'scale' is TRUE, the values are scaled to the full 16-bit range (the maximum value becomes 65535).
// Returns FALSE (and writes nothing) if the format is not a high bit depth mono format, or if src is too small.
gboolean mono_unpack_to_16 (ArvPixelFormat format, const uint8_t *src, size_t src_size, int width, int height,
                            uint16_t *dst, size_t dst_stride, gboolean scale);

#endif //LIMRENDIR_PIXELFORMAT_H
