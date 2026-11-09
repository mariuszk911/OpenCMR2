// Byte-wise texel conversions of port/gfx_texel.h.
#include "port/gfx_texel.h"

#include <cstdio>
#include <cstring>

namespace {
int failures;
void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
}

int main()
{
    // B, G, R, A in memory.
    const uint8_t bgra[8] = { 0x10, 0x20, 0x30, 0x40, 0xa0, 0xb0, 0xc0, 0xd0 };
    uint8_t rgba[8];
    GfxTexel_BgraToRgba(bgra, rgba, 2, 0);
    const uint8_t expect[8] = { 0x30, 0x20, 0x10, 0x40, 0xc0, 0xb0, 0xa0, 0xd0 };
    Check(std::memcmp(rgba, expect, 8) == 0, "BGRA to RGBA");
    GfxTexel_BgraToRgba(bgra, rgba, 2, 1);
    Check(rgba[3] == 0xff && rgba[7] == 0xff && rgba[0] == 0x30 && rgba[6] == 0xa0, "BGRX is opaque");
    uint8_t back[8];
    GfxTexel_RgbaToBgra(rgba, back, 2);
    Check(back[0] == 0x10 && back[2] == 0x30 && back[3] == 0xff, "RGBA back to BGRA");

    const uint8_t bump[6] = { 1, 2, 3, 4, 5, 6 };
    uint8_t out[8];
    GfxTexel_BumpToRgba(bump, out, 2);
    const uint8_t bumpExpect[8] = { 1, 2, 3, 0xff, 4, 5, 6, 0xff };   // dU, dV, luminance as R, G, B
    Check(std::memcmp(out, bumpExpect, 8) == 0, "bump to RGBA");

    const uint8_t pixel[4] = { 0x11, 0x22, 0x33, 0x80 };   // R, G, B, A
    Check(GfxTexel_RgbaBytesToArgb(pixel) == 0x80112233u, "RGBA bytes to 0xAARRGGBB");
    if (failures == 0)
        std::puts("gfx_texel: ok");
    return failures != 0;
}
