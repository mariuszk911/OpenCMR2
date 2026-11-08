// Luminance of a BUMPENVMAPLUMINANCE stage on the MiniGL backend
// (port/bump_luminance.h): the blue channel the host shader reads, scaled and
// clamped. MiniGL multiplied the whole bump texel in, which tinted the car's
// Bump environment pass red on the Amiga.
#include "port/bump_luminance.h"

#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    // CGraphics::GenerateBumpMap writes dU, dV, L, 0 into a BGRX8 texel; the
    // host uploads it as B, G, R, X, so its blue is the dU byte.
    const unsigned char bgrx[4] = { 7, 9, 102, 0 };
    REQUIRE(BumpLuminance_Blue(GFX_FORMAT_BGRX8, bgrx) == 7);
    REQUIRE(BumpLuminance_Blue(GFX_FORMAT_BGRA8, bgrx) == 7);
    // A 24-bit dU, dV, L texel: the host puts L in blue.
    const unsigned char dudvl[3] = { 7, 9, 102 };
    REQUIRE(BumpLuminance_Blue(GFX_FORMAT_BUMP_DUDVL, dudvl) == 102);

    // The game's blend mode 8 sets lscale 1, loffset 0.
    REQUIRE(BumpLuminance_Scale(0, 1.0f, 0.0f) == 0);
    REQUIRE(BumpLuminance_Scale(102, 1.0f, 0.0f) == 102);
    REQUIRE(BumpLuminance_Scale(255, 1.0f, 0.0f) == 255);
    REQUIRE(BumpLuminance_Scale(200, 2.0f, 0.0f) == 255);   // clamped high
    REQUIRE(BumpLuminance_Scale(10, 1.0f, -0.5f) == 0);     // clamped low
    REQUIRE(BumpLuminance_Scale(0, 0.0f, 1.0f) == 255);     // plain BUMPENVMAP: L = 1
    REQUIRE(BumpLuminance_Scale(51, 1.0f, 0.2f) == 102);

    // A 3x2 BGRX8 texture with a padded pitch.
    unsigned char tex[2 * 16];
    memset(tex, 0xee, sizeof(tex));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) {
            unsigned char *t = tex + y * 16 + x * 4;
            t[0] = (unsigned char)(10 * (y * 3 + x));   // dU
            t[1] = 200;                                  // dV
            t[2] = 102;                                  // L
            t[3] = 0;
        }
    unsigned char rgba[3 * 2 * 4];
    REQUIRE(BumpLuminance_Build(GFX_FORMAT_BGRX8, tex, 16, 3, 2, 1.0f, 0.0f, rgba));
    for (int i = 0; i < 6; ++i) {
        REQUIRE(rgba[i * 4] == 10 * i);
        REQUIRE(rgba[i * 4 + 1] == 10 * i);
        REQUIRE(rgba[i * 4 + 2] == 10 * i);
        REQUIRE(rgba[i * 4 + 3] == 255);
    }

    // Compressed formats are not read.
    unsigned char untouched[4] = { 1, 2, 3, 4 };
    REQUIRE(!BumpLuminance_Build(GFX_FORMAT_BC1, tex, 8, 1, 1, 1.0f, 0.0f, untouched));
    REQUIRE(untouched[0] == 1 && untouched[3] == 4);

    printf("bump_luminance: ok\n");
    return 0;
}
