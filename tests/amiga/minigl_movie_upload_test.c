/*
 * MiniGL movie-frame upload check (Domain B, target only).
 *
 * The Amiga movie path keeps one RGBA8 frame buffer and uploads changed row
 * runs into a GL texture (src/render/minigl/gfx_minigl_movie.h). This test
 * drives MiniGL directly with the same call sequence and checks what reaches
 * the framebuffer:
 *
 *   1. a 640x480 RGBA8 texture is created from a known pattern (glTexImage2D),
 *      drawn 1:1 over the screen and read back in full;
 *   2. two row runs are replaced with a different pattern (glTexSubImage2D)
 *      and the frame is drawn and read back again; only those runs may change.
 *
 * It also times a full-frame upload and the two partial ones. Run from a Shell:
 *
 *     amiga_minigl_movie_upload_test [npot] > movie-upload.log
 *
 * By default the texture is 1024x512 and only its top-left 640x480 is filled
 * and sampled; "npot" makes it exactly 640x480 instead. MiniGL 29.1 on the
 * PiStorm does not sample a 640x480 texture correctly (rows shift from x = 32
 * on), which is why the default pads to powers of two. The log is the
 * evidence: "OK" or "FAIL" lines and the timings.
 */
#include "movie_upload_common.h"
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/minigl.h>

static int g_texW = 1024, g_texH = 512;   /* the GL texture; the frame is WIDTH x HEIGHT */

static void DrawTexturedScreen(void)
{
    const float u1 = (float)WIDTH / (float)g_texW, v1 = (float)HEIGHT / (float)g_texH;
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(u1, 0.0f); glVertex2f((float)WIDTH, 0.0f);
    glTexCoord2f(0.0f, v1); glVertex2f(0.0f, (float)HEIGHT);
    glTexCoord2f(u1, 0.0f); glVertex2f((float)WIDTH, 0.0f);
    glTexCoord2f(u1, v1); glVertex2f((float)WIDTH, (float)HEIGHT);
    glTexCoord2f(0.0f, v1); glVertex2f(0.0f, (float)HEIGHT);
    glEnd();
}

int main(int argc, char **argv)
{
    GLuint texture = 0;
    unsigned failures = 0;
    double start, fullMs, bandMs;
    int worst;

    AmigaSys_SetCommandLine("amiga_minigl_movie_upload_test");
    if (argc > 1 && strcmp(argv[1], "npot") == 0) {
        g_texW = WIDTH;
        g_texH = HEIGHT;
    }
    printf("movie-upload: texture %dx%d, frame %dx%d\n", g_texW, g_texH, WIDTH, HEIGHT);
    g_frame = (unsigned char *)malloc((size_t)WIDTH * HEIGHT * 4);
    g_readback = (unsigned char *)malloc((size_t)WIDTH * HEIGHT * 4);
    if (g_frame == NULL || g_readback == NULL) {
        printf("movie-upload: FAIL out of memory\n");
        free(g_frame);
        free(g_readback);
        return 10;
    }
    if (!MiniGLOpen()) {
        printf("movie-upload: FAIL MiniGLOpen\n");
        free(g_frame);
        free(g_readback);
        return 10;
    }
    if (mglCreateContext(0, 0, WIDTH, HEIGHT) == NULL) {
        printf("movie-upload: FAIL mglCreateContext\n");
        MiniGLClose();
        free(g_frame);
        free(g_readback);
        return 10;
    }

    /* ---- frame 1: full upload ---- */
    if (!mglLockDisplay()) {
        printf("movie-upload: FAIL mglLockDisplay\n");
        mglDeleteContext();
        MiniGLClose();
        free(g_frame);
        free(g_readback);
        return 10;
    }
    glViewport(0, 0, WIDTH, HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)WIDTH, (double)HEIGHT, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    FillFrame(0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    start = Sys_GetTicksPrecise();
    if (g_texW == WIDTH && g_texH == HEIGHT) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_frame);
    } else {
        unsigned char *zero = (unsigned char *)calloc((size_t)g_texW * g_texH, 4);
        if (zero == NULL) {
            printf("movie-upload: FAIL out of memory (padded texture)\n");
            ++failures;
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_texW, g_texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
            free(zero);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_frame);
        }
    }
    fullMs = Sys_GetTicksPrecise() - start;
    glEnable(GL_TEXTURE_2D);
    DrawTexturedScreen();
    glFinish();
    memset(g_readback, 0, (size_t)WIDTH * HEIGHT * 4);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_readback);
    if (Compare("frame 1 (full upload)", 0, 2, &worst) != 0)
        ++failures;
    mglUnlockDisplay();
    mglSwitchDisplay();

    /* ---- frame 2: two row runs replaced ---- */
    if (!mglLockDisplay()) {
        printf("movie-upload: FAIL mglLockDisplay (frame 2)\n");
        ++failures;
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
        FillFrame(1);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        start = Sys_GetTicksPrecise();
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, BAND_A_Y, WIDTH, BAND_A_ROWS, GL_RGBA, GL_UNSIGNED_BYTE,
                        g_frame + (size_t)BAND_A_Y * WIDTH * 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, BAND_B_Y, WIDTH, BAND_B_ROWS, GL_RGBA, GL_UNSIGNED_BYTE,
                        g_frame + (size_t)BAND_B_Y * WIDTH * 4);
        bandMs = Sys_GetTicksPrecise() - start;
        glEnable(GL_TEXTURE_2D);
        DrawTexturedScreen();
        glFinish();
        memset(g_readback, 0, (size_t)WIDTH * HEIGHT * 4);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_readback);
        if (Compare("frame 2 (two row runs replaced)", 1, 2, &worst) != 0)
            ++failures;
        mglUnlockDisplay();
        mglSwitchDisplay();

        printf("movie-upload: full-frame glTexImage2D 640x480: %lu us; two sub-uploads (%d + %d rows): %lu us\n",
               (unsigned long)(fullMs * 1000.0), BAND_A_ROWS, BAND_B_ROWS, (unsigned long)(bandMs * 1000.0));
    }

    /* ---- timing: ten full-frame sub-uploads in a row ---- */
    if (mglLockDisplay()) {
        int i;
        FillFrame(0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        start = Sys_GetTicksPrecise();
        for (i = 0; i < 10; ++i)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, g_frame);
        fullMs = Sys_GetTicksPrecise() - start;
        printf("movie-upload: full-frame glTexSubImage2D 640x480, average of 10: %lu us\n",
               (unsigned long)(fullMs * 1000.0 / 10.0));
        mglUnlockDisplay();
        mglSwitchDisplay();
    }

    glDeleteTextures(1, &texture);
    mglDeleteContext();
    MiniGLClose();
    free(g_frame);
    free(g_readback);
    printf("movie-upload: %s\n", failures == 0 ? "OK" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 10;
}
