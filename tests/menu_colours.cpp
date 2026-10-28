/*
 * The option-menu and split-panel colours are packed ints that Sprite_Queue
 * and Font_DrawText read through a BYTE pointer. On a big-endian target the
 * original x86 literals would put the alpha byte first, so the menu text and
 * rows rendered pink/translucent. Check the in-memory bytes of the real
 * globals, on every host the tests run on.
 */
#include "port/types.h"

#include <cstdio>
#include <cstdlib>

extern int g_optionTextColours[3];
extern int g_optionTextBrightColours[3];
extern int g_unk0x005297ac[6];
extern int g_unk0x00527378;
extern int g_highlightRowColour;
extern int g_unk0x005270ec;
extern int g_unk0x005270f0;
extern int g_unk0x005270fc;
extern int g_unk0x00527100;

static void Check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "menu_colours: %s\n", what);
        std::exit(1);
    }
}

static void ExpectRgba(const int *word, BYTE r, BYTE g, BYTE b, BYTE a, const char *what)
{
    const BYTE *bytes = (const BYTE *)word;

    Check(bytes[0] == r && bytes[1] == g && bytes[2] == b && bytes[3] == a, what);
}

int main()
{
    for (int i = 0; i < 3; ++i)
        ExpectRgba(&g_optionTextColours[i], 0x4f, 0x4f, 0x4f, 0xff, "option text is opaque charcoal");

    ExpectRgba(&g_optionTextBrightColours[0], 0xff, 0xff, 0xff, 0xff, "bright colour 0 is opaque white");
    ExpectRgba(&g_optionTextBrightColours[1], 0xff, 0xff, 0xff, 0x80, "bright colour 1 is half-transparent white");
    ExpectRgba(&g_optionTextBrightColours[2], 0x4f, 0x4f, 0x4f, 0x40, "bright colour 2 is quarter-transparent charcoal");

    ExpectRgba(&g_unk0x005297ac[0], 0x4f, 0x4f, 0x4f, 0xff, "results colour 0");
    ExpectRgba(&g_unk0x005297ac[1], 0x4f, 0x4f, 0x4f, 0x40, "results colour 1");
    ExpectRgba(&g_unk0x005297ac[2], 0xff, 0xff, 0xff, 0xff, "results colour 2");
    ExpectRgba(&g_unk0x005297ac[3], 0x4f, 0x4f, 0x4f, 0xff, "results colour 3");
    ExpectRgba(&g_unk0x005297ac[4], 0x20, 0x20, 0x20, 0x20, "results colour 4");
    ExpectRgba(&g_unk0x005297ac[5], 0x00, 0x00, 0x00, 0x00, "results colour 5");

    ExpectRgba(&g_unk0x00527378, 0xc6, 0x54, 0x54, 0xff, "highlight row colour");
    ExpectRgba(&g_highlightRowColour, 0x4f, 0x4f, 0x4f, 0x80, "selected row colour");

    ExpectRgba(&g_unk0x005270ec, 0xf3, 0x5c, 0x00, 0xff, "split panel accent");
    ExpectRgba(&g_unk0x005270f0, 0xff, 0x00, 0x00, 0xff, "split panel red");
    ExpectRgba(&g_unk0x005270fc, 0x4f, 0x4f, 0x4f, 0xff, "split panel text");
    ExpectRgba(&g_unk0x00527100, 0x4f, 0x4f, 0x4f, 0x80, "split panel shadow");

    std::puts("menu_colours: OK");
    return 0;
}
