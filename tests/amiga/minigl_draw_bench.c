/*
 * MiniGL side of the draw-cost benchmark (Domain B, target only). Draws the
 * workload of draw_bench_workload.h as OpenCMR2's MiniGL renderer submits a
 * 3D draw: client-array pointers set per draw and glDrawElements, the
 * modelview loaded when it changes, the texture bound when it changes, GL
 * lighting switched with the draw. Compare with ogles2_draw_bench.c.
 *
 *     amiga_minigl_draw_bench [frames]
 */
#include <stdio.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/minigl.h>

#include "draw_bench_workload.h"

struct IntuitionBase *IntuitionBase;

int main(int argc, char **argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 300;
    const int warmup = 20;
    static unsigned char texels[kBenchTexSize * kBenchTexSize * 4];
    GLuint textures[kBenchTextures];
    float projection[16], modelview[16];
    struct DateStamp start, end;
    const GLfloat lightDir[4] = { 0.3f, 0.6f, 0.75f, 0.0f };
    const GLfloat lightColour[4] = { 0.9f, 0.85f, 0.8f, 1.0f };
    const GLfloat ambient[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    int f, d, t, width, height;
    long ms;

    Bench_Build(1);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR) "intuition.library", 39);
    if (IntuitionBase == NULL)
        return 20;
    Bench_NativeSize(&width, &height);
    CloseLibrary((struct Library *)IntuitionBase);
    printf("mgl-bench: native size %dx%d\n", width, height);
    if (!MiniGLOpen()) {
        printf("mgl-bench: MiniGLOpen failed\n");
        return 10;
    }
    /* As OpenCMR2's renderer with video.client_arrays = 1. */
    mglChooseVertexBufferSize(32768);
    mglChooseTextureBufferSize(4096);
    if (mglCreateContext(0, 0, width, height) == NULL) {
        printf("mgl-bench: mglCreateContext failed\n");
        MiniGLClose();
        return 11;
    }
    mglEnableSync(GL_FALSE);
    if (!mglLockDisplay()) {
        printf("mgl-bench: mglLockDisplay failed\n");
        mglDeleteContext();
        MiniGLClose();
        return 12;
    }

    glViewport(0, 0, width, height);
    Bench_Projection(projection, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColour);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 0.5f);
    glGenTextures(kBenchTextures, textures);
    for (t = 0; t < kBenchTextures; ++t) {
        Bench_Texture(t, texels);
        glBindTexture(GL_TEXTURE_2D, textures[t]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kBenchTexSize, kBenchTexSize, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, texels);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClearColor(0.1f, 0.12f, 0.15f, 1.0f);

    for (f = 0; f < warmup + frames; ++f) {
        int object = -1, texture = -1, lit = -1;
        if (f == warmup)
            DateStamp(&start);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (d = 0; d < kBenchDraws; ++d) {
            const struct BenchDraw *draw = &g_benchDraws[d];
            const struct BenchVertex *v = &g_benchVertices[draw->firstVertex];
            const GLsizei stride = (GLsizei)sizeof(struct BenchVertex);
            if (draw->object != object) {
                object = draw->object;
                Bench_Modelview(object, f, modelview);
                glLoadMatrixf(modelview);
            }
            if (draw->texture != texture) {
                texture = draw->texture;
                glBindTexture(GL_TEXTURE_2D, textures[texture]);
            }
            if (draw->lit != lit) {
                lit = draw->lit;
                if (lit) {
                    glEnable(GL_LIGHTING);
                    glDisableClientState(GL_COLOR_ARRAY);
                    glEnableClientState(GL_NORMAL_ARRAY);
                } else {
                    glDisable(GL_LIGHTING);
                    glDisableClientState(GL_NORMAL_ARRAY);
                    glEnableClientState(GL_COLOR_ARRAY);
                }
            }
            glVertexPointer(3, GL_FLOAT, stride, &v->x);
            if (lit)
                glNormalPointer(GL_FLOAT, stride, &v->nx);
            else
                glColorPointer(4, MGL_UBYTE_ARGB, stride, v->colour);
            glTexCoordPointer(2, GL_FLOAT, stride, &v->u);
            glDrawElements(GL_TRIANGLES, kBenchIndicesPerDraw, GL_UNSIGNED_SHORT, g_benchLocalIndices);
        }
        mglUnlockDisplay();
        mglSwitchDisplay();
        mglLockDisplay();
    }
    glFinish();
    DateStamp(&end);
    ms = Bench_ElapsedMs(&start, &end);
    printf("mgl-bench: %dx%d, %d frames, %d draws x %d triangles each: %ld ms, %ld.%02ld ms/frame\n",
           width, height, frames, kBenchDraws, kBenchIndicesPerDraw / 3, ms, ms / frames, (ms * 100 / frames) % 100);
    fflush(stdout);

    mglUnlockDisplay();
    mglDeleteContext();
    MiniGLClose();
    return 0;
}
