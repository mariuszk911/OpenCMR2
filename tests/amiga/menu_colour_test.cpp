/*
 * 680x0 check for the packed colour word used by the menu and split-panel
 * globals. The host build cannot exercise the real big-endian branch, only
 * its value, so run this on the target after a build.
 */
#include "port/colour.h"

#include <stdio.h>

int main(void)
{
    static const struct {
        int word;
        unsigned char r, g, b, a;
    } cases[] = {
        { OPENCMR2_RGBA32(0x4f, 0x4f, 0x4f, 0xff), 0x4f, 0x4f, 0x4f, 0xff },
        { OPENCMR2_RGBA32(0xc6, 0x54, 0x54, 0xff), 0xc6, 0x54, 0x54, 0xff },
        { OPENCMR2_RGBA32(0x4f, 0x4f, 0x4f, 0x80), 0x4f, 0x4f, 0x4f, 0x80 },
    };
    unsigned int i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const unsigned char *bytes = (const unsigned char *)&cases[i].word;

        if (sizeof(cases[i].word) != 4 || bytes[0] != cases[i].r || bytes[1] != cases[i].g ||
            bytes[2] != cases[i].b || bytes[3] != cases[i].a) {
            printf("menu_colour: FAIL case %u (%02x %02x %02x %02x)\n", i, bytes[0], bytes[1],
                   bytes[2], bytes[3]);
            return 1;
        }
    }
    puts("menu_colour: OK");
    return 0;
}
