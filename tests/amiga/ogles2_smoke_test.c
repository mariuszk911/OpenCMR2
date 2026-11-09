/*
 * ogles2.library smoke test (Domain B, target only, not staged).
 *
 * Opens ogles2.library through the project's own glue, finds a 32-bit RTG
 * mode, creates a full-screen context in it, clears with a changing colour,
 * reads a pixel back, swaps, and reports the GL strings. With ogles2 or a
 * 32-bit mode missing it says why and exits with 5 (WARN), the fall-back case
 * the renderer switch handles.
 *
 *     amiga_ogles2_smoke_test [frames N] [state]
 *
 * "state" runs the context with OGLES2_CCT_PISTORM_STATE_ONLY: no screen, no
 * GPU, for an emulator. The default opens a full screen: tell the owner first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ogles2.h>

#include "render/gles2/ogles2_display.h"
#include "render/gles2/ogles2_glue.h"

int main(int argc, char **argv)
{
    int frames = 60;
    int stateOnly = 0;
    int i;
    int failures = 0;
    ULONG err = 0;
    struct Window *inputWindow = NULL;
    Ogles2Mode mode;
    const char *why = "";
    void *context;
    struct TagItem tags[8];
    int n = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "frames") == 0 && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "state") == 0)
            stateOnly = 1;
    }

    if (!OGLES2Open()) {
        printf("ogles2: unavailable: %s\n", OGLES2OpenError());
        return RETURN_WARN;
    }
    printf("ogles2: opened %s, version %ld.%ld\n", OGLES2OpenedFrom(),
           (long)OGLES2Base->lib_Version, (long)OGLES2Base->lib_Revision);

    memset(&mode, 0, sizeof(mode));
    if (!stateOnly) {
        if (!Ogles2FindMode(0, 0, &mode, &why)) {
            printf("ogles2: no usable display: %s\n", why);
            OGLES2Close();
            return RETURN_WARN;
        }
        printf("ogles2: mode %08lx %lux%lu depth %lu\n", (unsigned long)mode.modeId,
               (unsigned long)mode.width, (unsigned long)mode.height, (unsigned long)mode.depth);
        tags[n].ti_Tag = OGLES2_CCT_MODEID;
        tags[n++].ti_Data = mode.modeId;
        tags[n].ti_Tag = OGLES2_CCT_PISTORM_GET_WINDOW;
        tags[n++].ti_Data = (ULONG)&inputWindow;
        tags[n].ti_Tag = OGLES2_CCT_VSYNC;
        tags[n++].ti_Data = 1;
    } else {
        tags[n].ti_Tag = OGLES2_CCT_PISTORM_STATE_ONLY;
        tags[n++].ti_Data = 1;
    }
    tags[n].ti_Tag = OGLES2_CCT_DEPTH;
    tags[n++].ti_Data = 24;
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;

    context = aglCreateContext2(&err, tags);
    if (context == NULL) {
        printf("ogles2: aglCreateContext2 failed, error %lu\n", (unsigned long)err);
        OGLES2Close();
        return RETURN_WARN;
    }
    aglMakeCurrent(context);
    printf("GL_VERSION  %s\n", (const char *)glGetString(GL_VERSION));
    printf("GL_RENDERER %s\n", (const char *)glGetString(GL_RENDERER));
    printf("GL_VENDOR   %s\n", (const char *)glGetString(GL_VENDOR));
    printf("GL_EXTENSIONS %s\n", (const char *)glGetString(GL_EXTENSIONS));

    for (i = 0; i < frames; ++i) {
        const float t = frames > 1 ? (float)i / (float)(frames - 1) : 0.0f;
        glClearColor(t, 1.0f - t, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (i == frames - 1 && !stateOnly) {
            unsigned char px[4] = { 0, 0, 0, 0 };
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            printf("pixel(0,0) after the last clear: %u %u %u %u (expect ~255 0 64 255)\n",
                   px[0], px[1], px[2], px[3]);
            if (px[0] < 250 || px[1] > 5 || px[2] < 60 || px[2] > 68)
                ++failures;
        }
        aglSwapBuffers();
        if (!stateOnly)
            Delay(1);
    }
    {
        const GLenum glErr = glGetError();
        printf("glGetError %04lx\n", (unsigned long)glErr);
        if (glErr != GL_NO_ERROR)
            ++failures;
    }
    aglDestroyContext(context);
    OGLES2Close();
    printf("ogles2 smoke test: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? RETURN_OK : RETURN_FAIL;
}
