/*
 * Helpers for the movie-upload target test: a known RGBA pattern, the two
 * replaced row bands and the read-back comparison.
 */
#ifndef OPENCMR2_TESTS_AMIGA_MOVIE_UPLOAD_COMMON_H
#define OPENCMR2_TESTS_AMIGA_MOVIE_UPLOAD_COMMON_H

#include <stdio.h>
#include <string.h>

#define WIDTH  640
#define HEIGHT 480
#define BAND_A_Y 100
#define BAND_A_ROWS 32
#define BAND_B_Y 400
#define BAND_B_ROWS 16

static unsigned char *g_frame;      /* the uploaded frame, tightly packed RGBA8 */
static unsigned char *g_readback;   /* glReadPixels target, bottom row first */

static void Pattern1(int x, int y, unsigned char *p)
{
    p[0] = (unsigned char)x;
    p[1] = (unsigned char)y;
    p[2] = (unsigned char)(x * 3 + y * 5);
    p[3] = 255;
}

static void Pattern2(int x, int y, unsigned char *p)
{
    p[0] = (unsigned char)(255 - x);
    p[1] = (unsigned char)(y * 7);
    p[2] = (unsigned char)(x + 128);
    p[3] = 255;
}

static int InBand(int y)
{
    return (y >= BAND_A_Y && y < BAND_A_Y + BAND_A_ROWS) || (y >= BAND_B_Y && y < BAND_B_Y + BAND_B_ROWS);
}

static void FillFrame(int bandsOnly)
{
    int x, y;
    for (y = 0; y < HEIGHT; ++y) {
        if (bandsOnly && !InBand(y))
            continue;
        for (x = 0; x < WIDTH; ++x) {
            unsigned char *p = g_frame + ((size_t)y * WIDTH + x) * 4;
            if (bandsOnly)
                Pattern2(x, y, p);
            else
                Pattern1(x, y, p);
        }
    }
}

/* Compares the read-back screen with the expected image. Counts pixels that
 * differ by more than `tolerance` in any colour channel. */
static unsigned Compare(const char *label, int expectBands, int tolerance, int *worst)
{
    unsigned bad = 0;
    int x, y, firstX = -1, firstY = -1;
    *worst = 0;
    for (y = 0; y < HEIGHT; ++y) {
        for (x = 0; x < WIDTH; ++x) {
            unsigned char want[4];
            const unsigned char *got = g_readback + ((size_t)(HEIGHT - 1 - y) * WIDTH + x) * 4;
            int c, diff = 0;
            if (expectBands && InBand(y))
                Pattern2(x, y, want);
            else
                Pattern1(x, y, want);
            for (c = 0; c < 3; ++c) {
                int d = (int)got[c] - (int)want[c];
                if (d < 0) d = -d;
                if (d > diff) diff = d;
            }
            if (diff > *worst)
                *worst = diff;
            if (diff > tolerance) {
                if (bad == 0) { firstX = x; firstY = y; }
                ++bad;
            }
        }
    }
    {   /* a few raw samples, so a format or orientation problem is visible */
        static const int probe[][2] = {{0, 0}, {1, 0}, {31, 0}, {32, 0}, {33, 0}, {320, 240}, {639, 479}, {0, 479}};
        unsigned i;
        for (i = 0; i < sizeof(probe) / sizeof(probe[0]); ++i) {
            unsigned char want[4];
            const int px = probe[i][0], py = probe[i][1];
            const unsigned char *got = g_readback + ((size_t)(HEIGHT - 1 - py) * WIDTH + px) * 4;
            if (expectBands && InBand(py))
                Pattern2(px, py, want);
            else
                Pattern1(px, py, want);
            printf("movie-upload:   (%3d,%3d) got %02x%02x%02x%02x want %02x%02x%02x%02x\n", px, py,
                   got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
        }
    }
    printf("movie-upload: %s: %u pixels over tolerance %d, worst channel difference %d", label, bad, tolerance, *worst);
    if (bad != 0)
        printf(" (first at %d,%d)", firstX, firstY);
    printf("\n");
    return bad;
}


#endif
