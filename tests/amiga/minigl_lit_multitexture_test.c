/*
 * MiniGL lit two-texture-unit reproducer (Domain B, target only).
 *
 * OpenCMR2's car reflection pass (the game's "Bump" environment map) draws a
 * lit mesh with both texture units in GL_MODULATE, additive ONE/ONE, over the
 * car body drawn just before. On the PiStorm (minigl.library, V3D) the pass
 * showed flickering screen-space squares; the same draw lit on the CPU and
 * sent unlit with per-vertex colours was correct, as were lit draws on one
 * texture unit. OpenCMR2 now lights such draws on the CPU (video.mt_cpu_light).
 *
 * This program draws that scene straight through MiniGL, without the game:
 *   base pass   : the curved mesh, one texture unit, lit, depth write
 *   reflection  : the same mesh, two units (luminance x flare), lit,
 *                 ONE/ONE blend, alpha test GEQUAL 1/255, depth LEQUAL
 * and compares, for several view angles:
 *   A  lit, two units          (the suspect)  drawn twice: A vs A' must match
 *   B  CPU-lit, two units      (reference)    A vs B must match within rounding
 *   C  lit, one unit           (control)      drawn twice, and C vs D
 *   D  CPU-lit, one unit       (control reference)
 * Each comparison prints the pixels over the tolerance and a map of 64x64
 * tiles that hold them. The second view's A, A', B, |A-A'| and |A-B|
 * (differences x8) are written as PPM files for the report.
 *
 *     amiga_minigl_lit_multitexture_test [load] [prefix]
 *
 * load   : background triangles drawn before the car each frame (default 0)
 * prefix : path prefix of the PPM files (default RAM:mgl-litmt)
 *
 * Exit code 0 when every check passes, 5 when a check fails.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/minigl.h>

#define WIDTH 640
#define HEIGHT 480
#define GRID_X 40
#define GRID_Y 20
#define TEX 128
#define TILE 64
#define TILES_X ((WIDTH + TILE - 1) / TILE)
#define TILES_Y ((HEIGHT + TILE - 1) / TILE)

struct Vtx {
    float x, y, z;      /* object space */
    float nx, ny, nz;   /* object-space normal */
    float u0, v0;       /* unit 0 (luminance / base) */
};

static struct Vtx g_vtx[(GRID_X + 1) * (GRID_Y + 1)];
static unsigned short g_idx[GRID_X * GRID_Y * 6];
static int g_indexCount;
static GLuint g_texBase, g_texLum, g_texFlare;
static unsigned char *g_frame[2];   /* RGB, GL row order */
static unsigned char g_row[WIDTH * 3];

/* Light and material, the same for GL and the CPU reference. */
static const float kLightDir[4] = { 0.35f, 0.55f, 0.76f, 0.0f };   /* eye space, normalised below */
static const float kLightDiffuse[4] = { 0.9f, 0.85f, 0.8f, 1.0f };
static const float kLightAmbient[4] = { 0.15f, 0.15f, 0.15f, 1.0f };
static const float kGlobalAmbient[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
static const float kMatDiffuse[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
static const float kMatAmbient[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
static const float kZero[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

static void BuildMesh(void)
{
    int i, j, n = 0;
    for (j = 0; j <= GRID_Y; ++j)
        for (i = 0; i <= GRID_X; ++i) {
            struct Vtx *v = &g_vtx[j * (GRID_X + 1) + i];
            const float x = -1.6f + 3.2f * (float)i / GRID_X;
            const float y = -0.8f + 1.6f * (float)j / GRID_Y;
            /* A bulging panel, like a car door: z = -0.3 x^2 - 0.2 y^2. */
            float nx = 0.6f * x, ny = 0.4f * y, nz = 1.0f;
            const float len = sqrtf(nx * nx + ny * ny + nz * nz);
            v->x = x;
            v->y = y;
            v->z = -0.3f * x * x - 0.2f * y * y;
            v->nx = nx / len;
            v->ny = ny / len;
            v->nz = nz / len;
            v->u0 = (float)i / GRID_X * 2.0f;
            v->v0 = (float)j / GRID_Y;
        }
    for (j = 0; j < GRID_Y; ++j)
        for (i = 0; i < GRID_X; ++i) {
            const unsigned short a = (unsigned short)(j * (GRID_X + 1) + i);
            const unsigned short b = (unsigned short)(a + 1);
            const unsigned short c = (unsigned short)(a + GRID_X + 1);
            const unsigned short d = (unsigned short)(c + 1);
            g_idx[n++] = a; g_idx[n++] = b; g_idx[n++] = c;
            g_idx[n++] = b; g_idx[n++] = d; g_idx[n++] = c;
        }
    g_indexCount = n;
}

static GLuint MakeTexture(int kind)
{
    static unsigned char pix[TEX * TEX * 4];
    GLuint id = 0;
    int x, y;
    for (y = 0; y < TEX; ++y)
        for (x = 0; x < TEX; ++x) {
            unsigned char *p = &pix[(y * TEX + x) * 4];
            if (kind == 0) {            /* base: livery-like blocks */
                const int blue = ((x / 32) + (y / 32)) & 1;
                p[0] = blue ? 40 : 220;
                p[1] = blue ? 60 : 220;
                p[2] = blue ? 180 : 225;
            } else if (kind == 1) {     /* luminance: 0.4 with darker panel lines */
                const unsigned char l = (x % 32 == 0 || y % 32 == 0) ? 20 : 102;
                p[0] = p[1] = p[2] = l;
            } else {                    /* flare: soft blobs on black */
                const float dx1 = (float)x - 40.0f, dy1 = (float)y - 45.0f;
                const float dx2 = (float)x - 95.0f, dy2 = (float)y - 80.0f;
                float f = 255.0f * expf(-(dx1 * dx1 * 0.25f + dy1 * dy1) / 300.0f) +
                          180.0f * expf(-(dx2 * dx2 + dy2 * dy2) / 120.0f);
                if (f > 255.0f)
                    f = 255.0f;
                p[0] = p[1] = p[2] = (unsigned char)f;
            }
            p[3] = 255;
        }
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    return id;
}

/* Rotation about Y by yaw, then about X by a fixed pitch. */
static void Rotate(float yaw, const float in[3], float out[3])
{
    const float cy = cosf(yaw), sy = sinf(yaw);
    const float cp = cosf(0.25f), sp = sinf(0.25f);
    const float x1 = in[0] * cy + in[2] * sy;
    const float z1 = -in[0] * sy + in[2] * cy;
    out[0] = x1;
    out[1] = in[1] * cp - z1 * sp;
    out[2] = in[1] * sp + z1 * cp;
}

static void LoadModelview(float yaw)
{
    float m[16];
    const float ex[3] = { 1, 0, 0 }, ey[3] = { 0, 1, 0 }, ez[3] = { 0, 0, 1 };
    float c[3];
    memset(m, 0, sizeof(m));
    Rotate(yaw, ex, c); m[0] = c[0]; m[1] = c[1]; m[2] = c[2];
    Rotate(yaw, ey, c); m[4] = c[0]; m[5] = c[1]; m[6] = c[2];
    Rotate(yaw, ez, c); m[8] = c[0]; m[9] = c[1]; m[10] = c[2];
    m[14] = -3.2f;
    m[15] = 1.0f;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

/* GL 1.1 per-vertex lighting of one directional light, no specular: the
 * reference the GL path must reproduce. */
static void CpuLight(float yaw, const struct Vtx *v, unsigned char rgba[4])
{
    const float n[3] = { v->nx, v->ny, v->nz };
    float ne[3], l[3], len, ndl, c;
    int k;
    Rotate(yaw, n, ne);
    len = sqrtf(kLightDir[0] * kLightDir[0] + kLightDir[1] * kLightDir[1] + kLightDir[2] * kLightDir[2]);
    l[0] = kLightDir[0] / len; l[1] = kLightDir[1] / len; l[2] = kLightDir[2] / len;
    ndl = ne[0] * l[0] + ne[1] * l[1] + ne[2] * l[2];
    if (ndl < 0.0f)
        ndl = 0.0f;
    for (k = 0; k < 3; ++k) {
        c = kGlobalAmbient[k] * kMatAmbient[k] + kLightAmbient[k] * kMatAmbient[k] +
            ndl * kLightDiffuse[k] * kMatDiffuse[k];
        if (c > 1.0f)
            c = 1.0f;
        rgba[k] = (unsigned char)(c * 255.0f + 0.5f);
    }
    rgba[3] = (unsigned char)(kMatDiffuse[3] * 255.0f + 0.5f);
}

/* Unit 1's coordinates: the eye-space position mapped to [0,1], as the game's
 * Graphics_ProjectMeshVertexMidpoints does. */
static void FlareCoord(float yaw, const struct Vtx *v, float *s, float *t)
{
    const float p[3] = { v->x, v->y, v->z };
    float e[3];
    Rotate(yaw, p, e);
    *s = e[0] * 0.5f + 0.5f;
    *t = 0.5f - e[1] * 0.5f;
}

static void SetLighting(int on)
{
    if (!on) {
        glDisable(GL_LIGHTING);
        return;
    }
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, kGlobalAmbient);
    glLightModelf(GL_LIGHT_MODEL_LOCAL_VIEWER, 1.0f);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, kMatDiffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, kMatAmbient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, kZero);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, kZero);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, kLightDiffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT, kLightAmbient);
    glLightfv(GL_LIGHT0, GL_SPECULAR, kZero);
}

static void SetUnits(int units, GLuint tex0, GLuint tex1)
{
    glActiveTextureARB(GL_TEXTURE1_ARB);
    if (units >= 2) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

/* Draws the mesh: lit by GL (normals, no colour) or CPU-lit (colours). With
 * one unit, unit 0 takes `oneUnitCoord` (0 = mesh u0/v0, 1 = flare coords). */
static void DrawMesh(float yaw, int units, int glLit, int oneUnitCoord)
{
    int i;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < g_indexCount; ++i) {
        const struct Vtx *v = &g_vtx[g_idx[i]];
        float s, t;
        FlareCoord(yaw, v, &s, &t);
        if (units >= 2) {
            glTexCoord2f(v->u0, v->v0);
            glMultiTexCoord2fARB(GL_TEXTURE1_ARB, s, t);
        } else if (oneUnitCoord) {
            glTexCoord2f(s, t);
        } else {
            glTexCoord2f(v->u0, v->v0);
        }
        if (glLit) {
            glNormal3f(v->nx, v->ny, v->nz);
        } else {
            unsigned char c[4];
            CpuLight(yaw, v, c);
            glColor4ub(c[0], c[1], c[2], c[3]);
        }
        glVertex3f(v->x, v->y, v->z);
    }
    glEnd();
}

/* Unlit, untextured triangles behind the car, to load the binner. */
static void DrawLoad(int count)
{
    int i;
    if (count <= 0)
        return;
    glDisable(GL_LIGHTING);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_TRIANGLES);
    for (i = 0; i < count; ++i) {
        const float x = -2.4f + 4.8f * (float)((i * 37) % 97) / 97.0f;
        const float y = -1.8f + 3.6f * (float)((i * 61) % 89) / 89.0f;
        glColor4ub((GLubyte)(40 + i % 60), 70, (GLubyte)(90 + i % 40), 255);
        glVertex3f(x, y, -6.0f);
        glVertex3f(x + 0.35f, y, -6.0f);
        glVertex3f(x, y + 0.3f, -6.0f);
    }
    glEnd();
}

enum { VAR_A, VAR_B, VAR_C, VAR_D };

/* One complete frame of one variant, read back into g_frame[slot]. */
static void RenderFrame(int variant, float yaw, int load, int slot)
{
    int y;
    const int units = (variant == VAR_A || variant == VAR_B) ? 2 : 1;
    const int glLit = (variant == VAR_A || variant == VAR_C);

    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);

    DrawLoad(load);

    /* Base pass: the body, one unit, lit the same way as the variant. */
    LoadModelview(yaw);
    SetLighting(glLit);
    SetUnits(1, g_texBase, 0);
    DrawMesh(yaw, 1, glLit, 0);

    /* Reflection pass. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 1.0f / 255.0f);
    if (units >= 2)
        SetUnits(2, g_texLum, g_texFlare);
    else
        SetUnits(1, g_texFlare, 0);
    DrawMesh(yaw, units, glLit, 1);

    glFinish();
    for (y = 0; y < HEIGHT; ++y) {
        glReadPixels(0, y, WIDTH, 1, GL_RGB, GL_UNSIGNED_BYTE, g_row);
        memcpy(g_frame[slot] + (size_t)y * WIDTH * 3, g_row, WIDTH * 3);
    }
    /* Present it, so the frame really goes through the display path. */
    mglUnlockDisplay();
    mglSwitchDisplay();
    mglLockDisplay();
}

static int g_failures;

/* Compares g_frame[0] and g_frame[1]; reports pixels whose largest channel
 * difference exceeds `tolerance`, and the 64x64 tiles holding them. */
static void Compare(const char *what, int tolerance)
{
    int tiles[TILES_Y][TILES_X];
    long bad = 0;
    int maxDiff = 0, x, y;
    memset(tiles, 0, sizeof(tiles));
    for (y = 0; y < HEIGHT; ++y)
        for (x = 0; x < WIDTH; ++x) {
            const unsigned char *a = g_frame[0] + ((size_t)y * WIDTH + x) * 3;
            const unsigned char *b = g_frame[1] + ((size_t)y * WIDTH + x) * 3;
            int k, d = 0;
            for (k = 0; k < 3; ++k) {
                const int dk = abs((int)a[k] - (int)b[k]);
                if (dk > d)
                    d = dk;
            }
            if (d > maxDiff)
                maxDiff = d;
            if (d > tolerance) {
                ++bad;
                /* Tile rows from the top of the screen. */
                tiles[(HEIGHT - 1 - y) / TILE][x / TILE]++;
            }
        }
    printf("mgl-litmt: %-34s bad=%-6ld max=%-3d tol=%d %s\n", what, bad, maxDiff, tolerance,
           bad ? "FAIL" : "ok");
    if (bad) {
        int tx, ty;
        ++g_failures;
        for (ty = 0; ty < TILES_Y; ++ty) {
            printf("mgl-litmt:   ");
            for (tx = 0; tx < TILES_X; ++tx)
                printf("%5d", tiles[ty][tx]);
            printf("\n");
        }
    }
    fflush(stdout);
}

static void WritePPM(const char *prefix, const char *name, const unsigned char *rgb, int diffOf2)
{
    char path[160];
    FILE *f;
    int x, y;
    sprintf(path, "%s-%s.ppm", prefix, name);
    f = fopen(path, "wb");
    if (f == NULL) {
        printf("mgl-litmt: cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (y = HEIGHT - 1; y >= 0; --y) {
        for (x = 0; x < WIDTH; ++x) {
            const size_t o = ((size_t)y * WIDTH + x) * 3;
            unsigned char p[3];
            int k;
            for (k = 0; k < 3; ++k) {
                if (diffOf2) {
                    int d = abs((int)g_frame[0][o + k] - (int)g_frame[1][o + k]) * 8;
                    p[k] = (unsigned char)(d > 255 ? 255 : d);
                } else {
                    p[k] = rgb[o + k];
                }
            }
            fwrite(p, 1, 3, f);
        }
    }
    fclose(f);
    printf("mgl-litmt: wrote %s\n", path);
}

int main(int argc, char **argv)
{
    static const int kYawsMilli[] = { 0, 300, -450, 800 };
    const int kShotView = 1;   /* yaw +300: the first view that fails on the PiStorm */
    const int load = argc > 1 ? atoi(argv[1]) : 0;
    const char *prefix = argc > 2 ? argv[2] : "RAM:mgl-litmt";
    int i;

    g_frame[0] = (unsigned char *)malloc(WIDTH * HEIGHT * 3);
    g_frame[1] = (unsigned char *)malloc(WIDTH * HEIGHT * 3);
    if (g_frame[0] == NULL || g_frame[1] == NULL) {
        printf("mgl-litmt: out of memory\n");
        return 10;
    }
    if (!MiniGLOpen()) {
        printf("mgl-litmt: MiniGLOpen failed\n");
        return 10;
    }
    mglChooseVertexBufferSize(32768);
    if (mglCreateContext(0, 0, WIDTH, HEIGHT) == NULL) {
        printf("mgl-litmt: mglCreateContext failed\n");
        MiniGLClose();
        return 11;
    }
    if (!mglLockDisplay()) {
        printf("mgl-litmt: mglLockDisplay failed\n");
        mglDeleteContext();
        MiniGLClose();
        return 12;
    }
    BuildMesh();
    printf("mgl-litmt: mesh %d triangles per pass, load %d\n", g_indexCount / 3, load);
    glViewport(0, 0, WIDTH, HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.8, 0.8, -0.6, 0.6, 1.0, 20.0);
    /* The light's position is given in eye space (identity modelview). */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    SetLighting(1);
    glLightfv(GL_LIGHT0, GL_POSITION, kLightDir);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_CULL_FACE);
    g_texBase = MakeTexture(0);
    g_texLum = MakeTexture(1);
    g_texFlare = MakeTexture(2);

    for (i = 0; i < (int)(sizeof(kYawsMilli) / sizeof(kYawsMilli[0])); ++i) {
        char what[64];
        const float yaw = (float)kYawsMilli[i] / 1000.0f;
        const int ym = kYawsMilli[i];

        RenderFrame(VAR_A, yaw, load, 0);
        RenderFrame(VAR_A, yaw, load, 1);
        sprintf(what, "yaw %+5d mrad lit 2 units: A vs A'", ym);
        Compare(what, 0);
        if (i == kShotView) {
            WritePPM(prefix, "A", g_frame[0], 0);
            WritePPM(prefix, "A2", g_frame[1], 0);
            WritePPM(prefix, "diffAA2", NULL, 1);
        }

        RenderFrame(VAR_B, yaw, load, 1);
        sprintf(what, "yaw %+5d mrad lit 2 units vs CPU-lit", ym);
        Compare(what, 10);
        if (i == kShotView) {
            WritePPM(prefix, "B", g_frame[1], 0);
            WritePPM(prefix, "diffAB", NULL, 1);
        }

        RenderFrame(VAR_C, yaw, load, 0);
        RenderFrame(VAR_C, yaw, load, 1);
        sprintf(what, "yaw %+5d mrad lit 1 unit: C vs C'", ym);
        Compare(what, 0);
        RenderFrame(VAR_D, yaw, load, 1);
        sprintf(what, "yaw %+5d mrad lit 1 unit vs CPU-lit", ym);
        Compare(what, 10);
    }

    mglUnlockDisplay();
    mglDeleteContext();
    MiniGLClose();
    printf("mgl-litmt: %s (%d failing checks)\n", g_failures ? "DEFECT REPRODUCED" : "all checks pass",
           g_failures);
    fflush(stdout);
    free(g_frame[0]);
    free(g_frame[1]);
    return g_failures ? 5 : 0;
}
