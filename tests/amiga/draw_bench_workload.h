/*
 * Synthetic draw workload shared by the MiniGL and ogles2 draw benchmarks
 * (minigl_draw_bench.c, ogles2_draw_bench.c; Domain B, target only).
 *
 * One frame mirrors the measured shape of an OpenCMR2 stage frame on the
 * PiStorm (attract bench, TODO.md 2026-11-01 rows): about 411 3D draws of
 * about 10 triangles, a new modelview on about 60% of them (mvLoads), a
 * texture switch on about half, about 30% lit by one directional light and
 * the rest pre-lit vertex colours. The per-draw CPU cost, not the GPU, is
 * what the comparison measures. Both programs draw at the Workbench screen's
 * size, in its mode (the native mode).
 */
#ifndef OPENCMR2_DRAW_BENCH_WORKLOAD_H
#define OPENCMR2_DRAW_BENCH_WORKLOAD_H

#include <math.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/screens.h>
#include <proto/dos.h>
#include <proto/intuition.h>

enum {
    kBenchDraws = 411,
    kBenchObjects = 64,
    kBenchTextures = 16,
    kBenchTexSize = 64,
    kBenchVertsPerDraw = 12,   /* a strip of five quads */
    kBenchIndicesPerDraw = 30, /* ten triangles */
    kBenchVertices = kBenchDraws * kBenchVertsPerDraw
};

struct BenchVertex {
    float x, y, z;
    float nx, ny, nz;
    unsigned char colour[4];   /* byte order chosen by the program */
    float u, v;
};

struct BenchDraw {
    int object;    /* modelview index */
    int texture;
    int lit;
    int firstVertex;
};

static struct BenchVertex g_benchVertices[kBenchVertices];
static unsigned short g_benchLocalIndices[kBenchIndicesPerDraw];   /* 0..11 */
static unsigned short g_benchIndices[kBenchDraws * kBenchIndicesPerDraw];   /* absolute */
static struct BenchDraw g_benchDraws[kBenchDraws];

static unsigned long g_benchSeed = 12345;
static unsigned long BenchRandom(void)
{
    g_benchSeed = g_benchSeed * 1103515245UL + 12345UL;
    return (g_benchSeed >> 16) & 0x7fff;
}

/* argb: 1 = A,R,G,B bytes (MiniGL MGL_UBYTE_ARGB), 0 = R,G,B,A (GLES2). */
static void Bench_Build(int argb)
{
    int d, k, object = 0, texture = 0;
    for (k = 0; k < 5; ++k) {
        unsigned short *q = &g_benchLocalIndices[k * 6];
        const unsigned short a = (unsigned short)(k * 2), b = (unsigned short)(a + 1);
        q[0] = a; q[1] = (unsigned short)(a + 2); q[2] = b;
        q[3] = b; q[4] = (unsigned short)(a + 2); q[5] = (unsigned short)(a + 3);
    }
    for (d = 0; d < kBenchDraws; ++d) {
        struct BenchDraw *draw = &g_benchDraws[d];
        if (d == 0 || BenchRandom() % 100 < 60)
            object = (int)(BenchRandom() % kBenchObjects);
        if (d == 0 || BenchRandom() % 100 < 50)
            texture = (int)(BenchRandom() % kBenchTextures);
        draw->object = object;
        draw->texture = texture;
        draw->lit = (object % 10) < 3;
        draw->firstVertex = d * kBenchVertsPerDraw;
        /* A small strip near the object's origin, offset per draw. */
        {
            const float ox = ((float)(BenchRandom() % 100) - 50.0f) * 0.004f;
            const float oy = ((float)(BenchRandom() % 100) - 50.0f) * 0.004f;
            for (k = 0; k < kBenchVertsPerDraw; ++k) {
                struct BenchVertex *v = &g_benchVertices[draw->firstVertex + k];
                const int col = k / 2, row = k & 1;
                const unsigned char shade = (unsigned char)(120 + (d * 7 + k * 13) % 120);
                v->x = ox + (float)col * 0.06f;
                v->y = oy + (float)row * 0.12f;
                v->z = 0.0f;
                v->nx = 0.0f;
                v->ny = 0.3f;
                v->nz = 0.95f;
                if (argb) {
                    v->colour[0] = 255; v->colour[1] = shade;
                    v->colour[2] = (unsigned char)(shade - 40); v->colour[3] = (unsigned char)(shade - 80);
                } else {
                    v->colour[0] = shade; v->colour[1] = (unsigned char)(shade - 40);
                    v->colour[2] = (unsigned char)(shade - 80); v->colour[3] = 255;
                }
                v->u = (float)col * 0.2f;
                v->v = (float)row;
            }
        }
        for (k = 0; k < kBenchIndicesPerDraw; ++k)
            g_benchIndices[d * kBenchIndicesPerDraw + k] =
                (unsigned short)(draw->firstVertex + g_benchLocalIndices[k]);
    }
}

/* Column-major modelview of an object in frame `frame`: a translation on an
 * 8 x 8 grid 6 units away, turned about Y. */
static void Bench_Modelview(int object, int frame, float m[16])
{
    const float a = (float)frame * 0.02f + (float)object;
    const float c = cosf(a), s = sinf(a);
    memset(m, 0, 16 * sizeof(float));
    m[0] = c;  m[2] = -s;
    m[5] = 1.0f;
    m[8] = s;  m[10] = c;
    m[12] = -2.8f + 0.8f * (float)(object % 8);
    m[13] = -2.1f + 0.6f * (float)(object / 8);
    m[14] = -6.0f;
    m[15] = 1.0f;
}

/* glFrustum(-0.6 * aspect, 0.6 * aspect, -0.6, 0.6, 1, 50), column-major. */
static void Bench_Projection(float p[16], int width, int height)
{
    const float r = 0.6f * (float)width / (float)height, l = -r;
    const float b = -0.6f, t = 0.6f, n = 1.0f, f = 50.0f;
    memset(p, 0, 16 * sizeof(float));
    p[0] = 2.0f * n / (r - l);
    p[5] = 2.0f * n / (t - b);
    p[8] = (r + l) / (r - l);
    p[9] = (t + b) / (t - b);
    p[10] = -(f + n) / (f - n);
    p[11] = -1.0f;
    p[14] = -2.0f * f * n / (f - n);
}

static void Bench_Multiply(float out[16], const float a[16], const float b[16])
{
    int i, j, k;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (k = 0; k < 4; ++k)
                s += a[k * 4 + j] * b[i * 4 + k];
            out[i * 4 + j] = s;
        }
}

/* RGBA texel data of texture t. */
static void Bench_Texture(int t, unsigned char *rgba)
{
    int x, y;
    for (y = 0; y < kBenchTexSize; ++y)
        for (x = 0; x < kBenchTexSize; ++x) {
            unsigned char *p = &rgba[(y * kBenchTexSize + x) * 4];
            const int on = ((x >> 3) ^ (y >> 3)) & 1;
            p[0] = (unsigned char)(on ? 230 : 40 + t * 10);
            p[1] = (unsigned char)(on ? 220 - t * 8 : 60);
            p[2] = (unsigned char)(on ? 200 : 120 + t * 5);
            p[3] = 255;
        }
}

/* The Workbench screen's size: both benchmarks draw at the native mode.
 * Needs IntuitionBase open. */
static void Bench_NativeSize(int *width, int *height)
{
    struct Screen *wb = LockPubScreen(NULL);
    *width = 640;
    *height = 480;
    if (wb != NULL) {
        *width = wb->Width;
        *height = wb->Height;
        UnlockPubScreen(NULL, wb);
    }
}

/* Elapsed time in milliseconds between two DateStamps (50 ticks/s). */
static long Bench_ElapsedMs(const struct DateStamp *a, const struct DateStamp *b)
{
    const long days = b->ds_Days - a->ds_Days;
    const long minutes = b->ds_Minute - a->ds_Minute + days * 1440L;
    const long ticks = b->ds_Tick - a->ds_Tick + minutes * 60L * TICKS_PER_SECOND;
    return ticks * (1000L / TICKS_PER_SECOND);
}

#endif
