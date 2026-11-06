#include "limrendir_pixelformat.h"

// How the pixels are stored
typedef enum {
    PACKING_NONE,        // 2 bytes per pixel, little endian (Mono10, Mono12, Mono14, Mono16)
    PACKING_LSB,         // Continuous little endian bit stream (PFNC Mono10p: 4 pixels in 5 bytes, Mono12p: 2 in 3)
    PACKING_GIGE_LEGACY  // 2 pixels in 3 bytes: high bits in bytes 0 and 2, low bits in byte 1 (Mono10Packed, Mono12Packed)
} Packing;

static gboolean get_layout (ArvPixelFormat format, guint *bits, Packing *packing)
{
    switch (format) {
        case ARV_PIXEL_FORMAT_MONO_10:         *bits = 10; *packing = PACKING_NONE; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_12:         *bits = 12; *packing = PACKING_NONE; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_14:         *bits = 14; *packing = PACKING_NONE; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_16:         *bits = 16; *packing = PACKING_NONE; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_10P:        *bits = 10; *packing = PACKING_LSB; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_12P:        *bits = 12; *packing = PACKING_LSB; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_10_PACKED:  *bits = 10; *packing = PACKING_GIGE_LEGACY; return TRUE;
        case ARV_PIXEL_FORMAT_MONO_12_PACKED:  *bits = 12; *packing = PACKING_GIGE_LEGACY; return TRUE;
        default: return FALSE;
    }
}

guint mono_high_bit_depth (ArvPixelFormat format)
{
    guint bits;
    Packing packing;
    return get_layout (format, &bits, &packing) ? bits : 0;
}

// Value of pixel number n (counted from the start of the image, rows are not padded)
static inline guint pixel_value (const uint8_t *src, size_t n, guint bits, Packing packing)
{
    guint const mask = (1u << bits) - 1;
    switch (packing) {
        case PACKING_NONE:
            return (src[2 * n] | ((guint) src[2 * n + 1] << 8)) & mask;
        case PACKING_LSB: {
            size_t const bit = n * bits;
            size_t const byte = bit >> 3;
            guint const shift = bit & 7;
            // Only the bytes holding bits of this pixel are read (bits + shift > 8 since bits >= 10)
            guint32 word = src[byte] | ((guint32) src[byte + 1] << 8);
            if (shift + bits > 16)
                word |= (guint32) src[byte + 2] << 16;
            return (word >> shift) & mask;
        }
        case PACKING_GIGE_LEGACY: {
            const uint8_t *group = src + 3 * (n / 2);
            guint const low_mask = (1u << (bits - 8)) - 1;
            if (n % 2 == 0)
                return ((guint) group[0] << (bits - 8)) | (group[1] & low_mask);
            return ((guint) group[2] << (bits - 8)) | ((group[1] >> 4) & low_mask);
        }
    }
    return 0;
}

gboolean mono_unpack_to_16 (ArvPixelFormat format, const uint8_t *src, size_t src_size, int width, int height,
                            uint16_t *dst, size_t dst_stride, gboolean scale)
{
    guint bits;
    Packing packing;
    if (!get_layout (format, &bits, &packing) || src == NULL || dst == NULL || width <= 0 || height <= 0)
        return FALSE;

    size_t const n_pixels = (size_t) width * height;
    size_t const needed = packing == PACKING_NONE ? 2 * n_pixels
                        : packing == PACKING_LSB ? (n_pixels * bits + 7) / 8
                        : 3 * ((n_pixels + 1) / 2);
    if (src_size < needed)
        return FALSE;

    // Scaling copies the high bits into the low ones, so that the maximum value becomes 65535
    guint const left = scale ? 16 - bits : 0;
    guint const right = bits - left;
#define OUT(v) ((uint16_t) GUINT16_TO_LE ((left == 0) ? (v) : (((v) << left) | ((v) >> right))))

    for (int row = 0; row < height; row++) {
        uint16_t *out = (uint16_t *) ((uint8_t *) dst + row * dst_stride);
        size_t n = (size_t) row * width;
        int i = 0;

        // Fast paths for whole groups of pixels starting on a byte boundary
        if (packing == PACKING_NONE) {
            guint const mask = (1u << bits) - 1;
            const uint8_t *p = src + 2 * n;
            for (; i < width; i++, p += 2) {
                guint const v = (p[0] | ((guint) p[1] << 8)) & mask;
                out[i] = OUT (v);
            }
        } else if (packing == PACKING_LSB && bits == 12) {
            for (; i < width && n % 2 != 0; i++, n++)
                out[i] = OUT (pixel_value (src, n, bits, packing));
            const uint8_t *p = src + n / 2 * 3;
            for (; i + 2 <= width; i += 2, n += 2, p += 3) {
                guint const v0 = p[0] | ((guint) (p[1] & 0x0f) << 8);
                guint const v1 = (p[1] >> 4) | ((guint) p[2] << 4);
                out[i] = OUT (v0);
                out[i + 1] = OUT (v1);
            }
        } else if (packing == PACKING_LSB && bits == 10) {
            for (; i < width && n % 4 != 0; i++, n++)
                out[i] = OUT (pixel_value (src, n, bits, packing));
            const uint8_t *p = src + n / 4 * 5;
            for (; i + 4 <= width; i += 4, n += 4, p += 5) {
                guint const v0 = p[0] | ((guint) (p[1] & 0x03) << 8);
                guint const v1 = (p[1] >> 2) | ((guint) (p[2] & 0x0f) << 6);
                guint const v2 = (p[2] >> 4) | ((guint) (p[3] & 0x3f) << 4);
                guint const v3 = (p[3] >> 6) | ((guint) p[4] << 2);
                out[i] = OUT (v0);
                out[i + 1] = OUT (v1);
                out[i + 2] = OUT (v2);
                out[i + 3] = OUT (v3);
            }
        }
        // Remaining pixels (and the GigE legacy formats, which are rare)
        for (; i < width; i++, n++)
            out[i] = OUT (pixel_value (src, n, bits, packing));
    }
#undef OUT
    return TRUE;
}
