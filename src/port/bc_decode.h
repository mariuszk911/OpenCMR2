/*
 * Software decode of BC1 (DXT1) and BC3 (DXT5) blocks into R,G,B,A bytes, for
 * renderers without compressed textures (MiniGL on the Amiga).
 *
 * A BC1 block with c0 <= c1 is three-colour: index 2 is the midpoint and index
 * 3 transparent black. A BC3 block's colour half is always four-colour,
 * whatever the order of c0 and c1 (its alpha comes from the alpha half).
 * Decoding BC3 with the BC1 rule turned such blocks' index 3 black and index 2
 * into the midpoint: wrong 4x4 texel squares in DXT5 textures.
 */
#ifndef OPENCMR2_PORT_BC_DECODE_H
#define OPENCMR2_PORT_BC_DECODE_H

#include <stdint.h>

/* Palette entries are packed R | G << 8 | B << 16 | A << 24. */
static inline void BcDecode_Colours(unsigned int c0, unsigned int c1, int fourColour,
                                    unsigned int palette[4])
{
    const unsigned int r0 = ((c0 >> 11) & 0x1f) * 255 / 31;
    const unsigned int g0 = ((c0 >> 5) & 0x3f) * 255 / 63;
    const unsigned int b0 = (c0 & 0x1f) * 255 / 31;
    const unsigned int r1 = ((c1 >> 11) & 0x1f) * 255 / 31;
    const unsigned int g1 = ((c1 >> 5) & 0x3f) * 255 / 63;
    const unsigned int b1 = (c1 & 0x1f) * 255 / 31;

    palette[0] = r0 | (g0 << 8) | (b0 << 16) | 0xff000000u;
    palette[1] = r1 | (g1 << 8) | (b1 << 16) | 0xff000000u;
    if (fourColour || c0 > c1) {
        palette[2] = ((r0 * 2 + r1) / 3) | (((g0 * 2 + g1) / 3) << 8) |
                     (((b0 * 2 + b1) / 3) << 16) | 0xff000000u;
        palette[3] = ((r0 + r1 * 2) / 3) | (((g0 + g1 * 2) / 3) << 8) |
                     (((b0 + b1 * 2) / 3) << 16) | 0xff000000u;
    } else {
        palette[2] = ((r0 + r1) / 2) | (((g0 + g1) / 2) << 8) |
                     (((b0 + b1) / 2) << 16) | 0xff000000u;
        palette[3] = 0;
    }
}

/* Writes the 4x4 texels of an 8-byte BC1 block as R,G,B,A rows dstPitch apart. */
static inline void BcDecode_BC1Block(const unsigned char *src, unsigned char *dst, int dstPitch)
{
    unsigned int palette[4];
    const unsigned int c0 = src[0] | (src[1] << 8);
    const unsigned int c1 = src[2] | (src[3] << 8);
    const unsigned int bits = src[4] | (src[5] << 8) | (src[6] << 16) | ((unsigned int)src[7] << 24);
    int x, y;

    BcDecode_Colours(c0, c1, 0, palette);
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x) {
            const unsigned int p = palette[(bits >> (2 * (y * 4 + x))) & 3];
            unsigned char *d = dst + y * dstPitch + x * 4;
            d[0] = (unsigned char)(p & 0xff);
            d[1] = (unsigned char)((p >> 8) & 0xff);
            d[2] = (unsigned char)((p >> 16) & 0xff);
            d[3] = (unsigned char)((p >> 24) & 0xff);
        }
}

/* Writes the 4x4 texels of a 16-byte BC3 block as R,G,B,A rows dstPitch apart. */
static inline void BcDecode_BC3Block(const unsigned char *src, unsigned char *dst, int dstPitch)
{
    unsigned char alpha[8];
    uint64_t bits = 0;
    unsigned int palette[4];
    unsigned int c0, c1, colorBits;
    int i, x, y;

    alpha[0] = src[0];
    alpha[1] = src[1];
    if (alpha[0] > alpha[1]) {
        for (i = 1; i < 7; ++i)
            alpha[i + 1] = (unsigned char)(((7 - i) * alpha[0] + i * alpha[1]) / 7);
    } else {
        for (i = 1; i < 5; ++i)
            alpha[i + 1] = (unsigned char)(((5 - i) * alpha[0] + i * alpha[1]) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }
    for (i = 0; i < 6; ++i)
        bits |= (uint64_t)src[2 + i] << (8 * i);
    c0 = src[8] | (src[9] << 8);
    c1 = src[10] | (src[11] << 8);
    colorBits = src[12] | (src[13] << 8) | (src[14] << 16) | ((unsigned int)src[15] << 24);
    BcDecode_Colours(c0, c1, 1, palette);
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x) {
            const int index = y * 4 + x;
            const unsigned int p = palette[(colorBits >> (2 * index)) & 3];
            unsigned char *d = dst + y * dstPitch + x * 4;
            d[0] = (unsigned char)(p & 0xff);
            d[1] = (unsigned char)((p >> 8) & 0xff);
            d[2] = (unsigned char)((p >> 16) & 0xff);
            d[3] = alpha[(bits >> (3 * index)) & 7];
        }
}

#endif
