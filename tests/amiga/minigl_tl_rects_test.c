/*
 * MiniGL zero-area 2D rectangle reproducer (Domain B).
 *
 * OpenCMR2's option-screen separators are one-pixel-wide rectangles emitted by
 * Sprite_FillRect as two triangles whose far edge is x0 + (w - 1); for w == 1
 * that is x0, so the quad has zero width. Direct3D and the SDL_GPU backend
 * still rasterise the shared edge as a 1px line; desktop OpenGL/MiniGL appear
 * to drop the zero-area triangles, leaving only the sprite dots and the
 * horizontal closing line visible.
 *
 * This test draws the same primitive directly against MiniGL, without the game
 * or the port layer, and reports which shapes reached the framebuffer. Build
 * with the Amiga toolchain, then run:
 *
 *     RAM:amiga_minigl_tl_rects_test RAM:minigl-tl.ppm
 *
 * The PPM and Shell log are the evidence for the MiniGL maintainer.
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/minigl.h>

#define WIDTH  640
#define HEIGHT 480

static void *g_context;

static void DrawTriangleVertices(int x0, int y0, int x1, int y1)
{
    /* The exact pair Sprite_FillRect queues: a=(x0,y0), b=(x1,y0), c=(x0,y1). */
    glBegin(GL_TRIANGLES);
    glVertex2f((float)x0, (float)y0);
    glVertex2f((float)x1, (float)y0);
    glVertex2f((float)x0, (float)y1);
    glVertex2f((float)x1, (float)y0);
    glVertex2f((float)x0, (float)y0);
    glVertex2f((float)x0, (float)y1);
    glEnd();
}

static void Draw1pxVertical(int x, int y0, int y1, unsigned char r, unsigned char g, unsigned char b)
{
    glColor4ub(r, g, b, 255);
    /* x1 == x0, like Sprite_FillRect with w == 1. */
    DrawTriangleVertices(x, y0, x, y1);
}

static void Draw1pxHorizontal(int x0, int x1, int y, unsigned char r, unsigned char g, unsigned char b)
{
    glColor4ub(r, g, b, 255);
    /* y1 == y0, like Sprite_FillRect with h == 1. */
    DrawTriangleVertices(x0, y, x1, y);
}

static void DrawWideVertical(int x, int y0, int y1, float width,
                             unsigned char r, unsigned char g, unsigned char b)
{
    glColor4ub(r, g, b, 255);
    glBegin(GL_TRIANGLES);
    glVertex2f((float)x, (float)y0);
    glVertex2f((float)x + width, (float)y0);
    glVertex2f((float)x, (float)(y1 + 1));
    glVertex2f((float)x + width, (float)y0);
    glVertex2f((float)x, (float)y0);
    glVertex2f((float)x, (float)(y1 + 1));
    glEnd();
}

enum ProbeColour { PROBE_RED, PROBE_GREEN, PROBE_BLUE, PROBE_WHITE, PROBE_YELLOW, PROBE_DARK };

static int Matches(unsigned char r, unsigned char g, unsigned char b, enum ProbeColour want)
{
    switch (want) {
    case PROBE_RED: return r > 128 && g < 96 && b < 96;
    case PROBE_GREEN: return g > 128 && r < 96 && b < 96;
    case PROBE_BLUE: return b > 128 && r < 96 && g < 96;
    case PROBE_WHITE: return r > 200 && g > 200 && b > 200;
    case PROBE_YELLOW: return r > 200 && g > 200 && b < 96;
    case PROBE_DARK: return r < 64 && g < 64 && b < 64;
    }
    return 0;
}

static void ReportBox(const char *label, int x, int y, enum ProbeColour want)
{
    unsigned char p[3];
    int found = 0;
    int dx, dy;

    /* Probe a small box: exact-edge sampling of a 1px primitive is ambiguous. */
    for (dy = -2; dy <= 2; ++dy) {
        for (dx = -2; dx <= 2; ++dx) {
            memset(p, 0, sizeof(p));
            /* glReadPixels uses the GL window origin (bottom left). */
            glReadPixels(x + dx, HEIGHT - 1 - (y + dy), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, p);
            if (Matches(p[0], p[1], p[2], want))
                ++found;
        }
    }
    printf("minigl-tl: %-28s near (%d,%d): %d/25 pixels %s\n",
           label, x, y, found, found ? "VISIBLE" : "MISSING");
}

int main(int argc, char **argv)
{
    const char *shot = argc > 1 ? argv[1] : (const char *)"RAM:minigl-tl.ppm";

    if (!MiniGLOpen()) {
        printf("minigl-tl: MiniGLOpen failed\n");
        return 10;
    }
    g_context = mglCreateContext(0, 0, WIDTH, HEIGHT);
    if (g_context == NULL) {
        printf("minigl-tl: mglCreateContext failed\n");
        MiniGLClose();
        return 11;
    }

    if (!mglLockDisplay()) {
        printf("minigl-tl: mglLockDisplay failed\n");
        mglDeleteContext();
        MiniGLClose();
        return 12;
    }

    glViewport(0, 0, WIDTH, HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)WIDTH, (double)HEIGHT, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glLineWidth(1.0f);

    /* Control: a real GL line. */
    glColor4ub(0, 255, 0, 255);
    glBegin(GL_LINES);
    glVertex2f(50.0f, 80.0f);
    glVertex2f(50.0f, 200.0f);
    glEnd();

    /* The failing primitive: a zero-width vertical quad (w == 1). */
    Draw1pxVertical(120, 80, 200, 255, 0, 0);

    /* The same zero-height horizontal quad (h == 1). */
    Draw1pxHorizontal(50, 250, 240, 0, 0, 255);

    /* Width sweep: 0.5px (the game's current far edge), 1px, 2px, 3px. */
    DrawWideVertical(200, 80, 200, 0.5f, 255, 255, 255);
    DrawWideVertical(240, 80, 200, 2.0f, 255, 255, 255);
    DrawWideVertical(280, 80, 200, 3.0f, 255, 255, 255);
    DrawWideVertical(320, 80, 200, 1.0f, 255, 255, 255);

    /* Control: a normal filled square. */
    glColor4ub(255, 255, 0, 255);
    glBegin(GL_TRIANGLES);
    glVertex2f(400.0f, 340.0f);
    glVertex2f(460.0f, 340.0f);
    glVertex2f(400.0f, 400.0f);
    glVertex2f(460.0f, 340.0f);
    glVertex2f(460.0f, 400.0f);
    glVertex2f(400.0f, 400.0f);
    glEnd();

    glFinish();

    ReportBox("normal GL line", 50, 140, PROBE_GREEN);
    ReportBox("1px vertical quad", 120, 140, PROBE_RED);
    ReportBox("1px horizontal quad", 150, 240, PROBE_BLUE);
    ReportBox("0.5px vertical quad", 200, 140, PROBE_WHITE);
    ReportBox("2px vertical quad", 240, 140, PROBE_WHITE);
    ReportBox("3px vertical quad", 280, 140, PROBE_WHITE);
    ReportBox("1px widened vertical quad", 320, 140, PROBE_WHITE);
    ReportBox("normal filled square", 430, 370, PROBE_YELLOW);
    ReportBox("background", 550, 450, PROBE_DARK);

    mglUnlockDisplay();
    mglSwitchDisplay();

    /* The shot must be taken from the presented front buffer, as in the
     * startup smoke harness; writing it before the switch captures the
     * backdrop instead of the drawing. */
    if (mglLockDisplay()) {
        mglWriteShotPPM((char *)shot);
        printf("minigl-tl: screenshot %s\n", shot);
        mglUnlockDisplay();
    } else {
        printf("minigl-tl: screenshot lock failed\n");
    }
    fflush(stdout);

    mglDeleteContext();
    g_context = NULL;
    MiniGLClose();
    printf("minigl-tl: done\n");
    fflush(stdout);
    return 0;
}
