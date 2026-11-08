// Software BC1/BC3 decode used by the MiniGL renderer (port/bc_decode.h).
// A BC3 colour half is always four-colour; decoding it with the BC1 rule left
// wrong 4x4 texel squares in DXT5 textures.
#include "port/bc_decode.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

// Reference: one channel of a 565 colour, expanded as the renderers do.
static unsigned Channel(unsigned c, int which)
{
    switch (which) {
    case 0: return ((c >> 11) & 0x1f) * 255 / 31;
    case 1: return ((c >> 5) & 0x3f) * 255 / 63;
    default: return (c & 0x1f) * 255 / 31;
    }
}

// Reference texel (R,G,B,A) of colour index i for a block's endpoints.
static void RefColour(unsigned c0, unsigned c1, unsigned i, bool fourColour, unsigned char out[4])
{
    for (int ch = 0; ch < 3; ++ch) {
        const unsigned a = Channel(c0, ch), b = Channel(c1, ch);
        unsigned v;
        if (i == 0) v = a;
        else if (i == 1) v = b;
        else if (fourColour) v = i == 2 ? (2 * a + b) / 3 : (a + 2 * b) / 3;
        else v = i == 2 ? (a + b) / 2 : 0;
        out[ch] = (unsigned char)v;
    }
    out[3] = (!fourColour && i == 3) ? 0 : 255;
}

static uint32_t g_seed = 12345u;
static unsigned char Rand8() { g_seed = g_seed * 1103515245u + 12345u; return (unsigned char)(g_seed >> 16); }

int main()
{
    unsigned char texels[16 * 4];

    // A DXT5 block with c0 < c1 (white < ...: c0 = dark grey, c1 = white) and
    // every texel on index 3: four-colour, so index 3 is 1/3 c0 + 2/3 c1,
    // opaque, never black.
    {
        unsigned char block[16];
        memset(block, 0, sizeof(block));
        block[0] = 255; block[1] = 255;            // alpha endpoints: opaque
        const unsigned c0 = 0x4208, c1 = 0xffff;   // c0 <= c1
        block[8] = c0 & 0xff; block[9] = c0 >> 8;
        block[10] = c1 & 0xff; block[11] = c1 >> 8;
        block[12] = block[13] = block[14] = block[15] = 0xff; // all index 3
        BcDecode_BC3Block(block, texels, 16);
        unsigned char want[4];
        RefColour(c0, c1, 3, true, want);
        for (int t = 0; t < 16; ++t) {
            REQUIRE(texels[t * 4 + 0] == want[0]);
            REQUIRE(texels[t * 4 + 1] == want[1]);
            REQUIRE(texels[t * 4 + 2] == want[2]);
            REQUIRE(texels[t * 4 + 0] > 128);      // not the black of the BC1 rule
            REQUIRE(texels[t * 4 + 3] == 255);
        }
        // Index 2 is the 2/3 mix, not the midpoint.
        block[12] = block[13] = block[14] = block[15] = 0xaa;
        BcDecode_BC3Block(block, texels, 16);
        RefColour(c0, c1, 2, true, want);
        REQUIRE(texels[0] == want[0] && texels[1] == want[1] && texels[2] == want[2]);
    }

    // The same endpoints in DXT1 keep the three-colour rule: index 3 is
    // transparent black.
    {
        unsigned char block[8];
        const unsigned c0 = 0x4208, c1 = 0xffff;
        block[0] = c0 & 0xff; block[1] = c0 >> 8;
        block[2] = c1 & 0xff; block[3] = c1 >> 8;
        block[4] = block[5] = block[6] = block[7] = 0xff;
        BcDecode_BC1Block(block, texels, 16);
        for (int t = 0; t < 16; ++t)
            REQUIRE(texels[t * 4 + 0] == 0 && texels[t * 4 + 1] == 0 && texels[t * 4 + 2] == 0 &&
                    texels[t * 4 + 3] == 0);
    }

    // Random blocks of both formats against the reference.
    for (int n = 0; n < 20000; ++n) {
        unsigned char block[16];
        for (int i = 0; i < 16; ++i)
            block[i] = Rand8();
        const bool bc3 = (n & 1) != 0;
        const unsigned char *colour = bc3 ? block + 8 : block;
        const unsigned c0 = colour[0] | (colour[1] << 8), c1 = colour[2] | (colour[3] << 8);
        const unsigned bits = colour[4] | (colour[5] << 8) | (colour[6] << 16) | ((unsigned)colour[7] << 24);
        if (bc3)
            BcDecode_BC3Block(block, texels, 16);
        else
            BcDecode_BC1Block(block, texels, 16);
        for (int t = 0; t < 16; ++t) {
            unsigned char want[4];
            RefColour(c0, c1, (bits >> (2 * t)) & 3, bc3 || c0 > c1, want);
            REQUIRE(texels[t * 4 + 0] == want[0]);
            REQUIRE(texels[t * 4 + 1] == want[1]);
            REQUIRE(texels[t * 4 + 2] == want[2]);
            if (!bc3)
                REQUIRE(texels[t * 4 + 3] == want[3]);
        }
    }

    printf("bc_decode: ok\n");
    return 0;
}
