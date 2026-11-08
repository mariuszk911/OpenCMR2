/*
 * The luminance of a BUMPENVMAPLUMINANCE stage, for renderers without bump
 * mapping (MiniGL on the Amiga).
 *
 * The host shader (render/shaders/fixed.frag) leaves the colour of such a
 * stage unchanged and scales the next stage's texel by
 * clamp(blue * lscale + loffset, 0, 1), blue being the bump texel's blue
 * channel as the host executors upload it: byte 0 of a BGRX8/BGRA8 texel and
 * byte 2 (the luminance) of a 24-bit dU,dV,L texel. MiniGL instead multiplied
 * the whole bump texel into the colour: the car's Bump environment pass, whose
 * BGRX8 bump maps (CGraphics::GenerateBumpMap) hold dU, dV, L in bytes 0-2,
 * came out tinted red. A grey texture of these values under MODULATE gives the
 * host's colour; only the dU/dV perturbation of the next stage is lost.
 */
#ifndef OPENCMR2_PORT_BUMP_LUMINANCE_H
#define OPENCMR2_PORT_BUMP_LUMINANCE_H

#include <stddef.h>

#include "port/gfx.h"

/* Bytes per texel of the formats this reads, 0 for the others. */
static inline int BumpLuminance_TexelBytes(int format)
{
    switch (format) {
    case GFX_FORMAT_BGRX8:
    case GFX_FORMAT_BGRA8:
        return 4;
    case GFX_FORMAT_BUMP_DUDVL:
        return 3;
    default:
        return 0;
    }
}

/* The blue channel the host shader reads from one texel. */
static inline unsigned char BumpLuminance_Blue(int format, const unsigned char *texel)
{
    return format == GFX_FORMAT_BUMP_DUDVL ? texel[2] : texel[0];
}

/* clamp(blue / 255 * scale + offset, 0, 1) as a byte, rounded. */
static inline unsigned char BumpLuminance_Scale(unsigned char blue, float scale, float offset)
{
    float l = (float)blue / 255.0f * scale + offset;
    if (!(l > 0.0f))
        l = 0.0f;
    if (l > 1.0f)
        l = 1.0f;
    return (unsigned char)(l * 255.0f + 0.5f);
}

/*
 * Fills width * height R,G,B,A texels (grey luminance, opaque) from a texture
 * of the given format and row pitch. Returns 0, writing nothing, for a format
 * it cannot read (compressed ones).
 */
static inline int BumpLuminance_Build(int format, const unsigned char *src, long pitch, int width,
                                      int height, float scale, float offset, unsigned char *rgba)
{
    const int bytes = BumpLuminance_TexelBytes(format);
    int x, y;
    if (bytes == 0)
        return 0;
    for (y = 0; y < height; ++y) {
        const unsigned char *row = src + (size_t)y * (size_t)pitch;
        for (x = 0; x < width; ++x) {
            const unsigned char l = BumpLuminance_Scale(BumpLuminance_Blue(format, row + x * bytes), scale, offset);
            unsigned char *d = rgba + ((size_t)y * (size_t)width + (size_t)x) * 4;
            d[0] = l;
            d[1] = l;
            d[2] = l;
            d[3] = 0xff;
        }
    }
    return 1;
}

#endif
