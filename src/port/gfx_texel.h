/*
 * Texel conversions for GL back ends that take RGBA bytes (the AmigaOS GLES2
 * renderer). The CPU copy of a texture holds the PC layout in memory order:
 * 32-bit pixels as the bytes B, G, R, A (a little-endian 0xAARRGGBB word) and
 * bump texels as three bytes, on every target. These work on bytes only, so
 * they give the same result on big-endian and little-endian machines.
 */
#ifndef OPENCMR2_PORT_GFX_TEXEL_H
#define OPENCMR2_PORT_GFX_TEXEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* n pixels of B, G, R, A bytes to R, G, B, A bytes; opaque forces alpha 255
   (GFX_FORMAT_BGRX8). */
static inline void GfxTexel_BgraToRgba(const uint8_t *src, uint8_t *dst, size_t n, int opaque)
{
    size_t i;
    for (i = 0; i < n; i++) {
        const uint8_t b = src[i * 4], g = src[i * 4 + 1], r = src[i * 4 + 2], a = src[i * 4 + 3];
        dst[i * 4] = r;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = b;
        dst[i * 4 + 3] = opaque ? 0xff : a;
    }
}

/* n pixels of R, G, B, A bytes back to B, G, R, A bytes (a render target read
   into its CPU copy). */
static inline void GfxTexel_RgbaToBgra(const uint8_t *src, uint8_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        const uint8_t r = src[i * 4], g = src[i * 4 + 1], b = src[i * 4 + 2], a = src[i * 4 + 3];
        dst[i * 4] = b;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = r;
        dst[i * 4 + 3] = a;
    }
}

/* n bump texels of three bytes to R, G, B, A, as the host executors upload
   them (gfx_gl.cpp): red = byte 2, green = byte 1, blue = byte 0, alpha 255. */
static inline void GfxTexel_BumpToRgba(const uint8_t *src, uint8_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        dst[i * 4] = src[i * 3 + 2];
        dst[i * 4 + 1] = src[i * 3 + 1];
        dst[i * 4 + 2] = src[i * 3];
        dst[i * 4 + 3] = 0xff;
    }
}

/* A 0xAARRGGBB pixel value (what Gfx_ReadBackBuffer returns) from R, G, B, A
   bytes. Built with shifts: native byte order does not matter. */
static inline uint32_t GfxTexel_RgbaBytesToArgb(const uint8_t *rgba)
{
    return ((uint32_t)rgba[3] << 24) | ((uint32_t)rgba[0] << 16) | ((uint32_t)rgba[1] << 8) | (uint32_t)rgba[2];
}

#ifdef __cplusplus
}
#endif

#endif
