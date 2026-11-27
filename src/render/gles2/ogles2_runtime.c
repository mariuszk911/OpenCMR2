/*
 * The GLES2 renderer's window onto ogles2.library: the function table of
 * gles2_api.h from the SDK's wrappers, and the full-screen context. See
 * gles2_api.h for why this is a separate file.
 */
#include "render/gles2/gles2_api.h"
#include "render/gles2/ogles2_display.h"
#include "render/gles2/ogles2_glue.h"

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/ogles2.h>

static void *s_context;
static struct Window *s_inputWindow;

int Gles2Display_Probe(int *libraryOpens, const char **why)
{
    Ogles2Mode mode;
    const char *reason = "";
    int found = 0;

    *libraryOpens = OGLES2Open() ? 1 : 0;
    if (!*libraryOpens) {
        *why = OGLES2OpenError();
        return 0;
    }
    found = Ogles2FindMode(0, 0, &mode, &reason) ? 1 : 0;
    *why = found ? "" : reason;
    OGLES2Close();
    return found;
}

int Gles2Display_Open(int vsync, Gles2Api *api, Gles2Display *display, const char **why)
{
    Ogles2Mode mode;
    const char *reason = "";
    struct TagItem tags[6];
    ULONG error = 0;
    int n = 0;

    if (s_context != NULL) {
        *why = "a context is already open";
        return 0;
    }
    if (!OGLES2Open()) {
        *why = OGLES2OpenError();
        return 0;
    }
    if (!Ogles2FindMode(0, 0, &mode, &reason)) {
        *why = reason;
        OGLES2Close();
        return 0;
    }
    s_inputWindow = NULL;
    tags[n].ti_Tag = OGLES2_CCT_MODEID;
    tags[n++].ti_Data = mode.modeId;
    tags[n].ti_Tag = OGLES2_CCT_PISTORM_GET_WINDOW;
    tags[n++].ti_Data = (ULONG)&s_inputWindow;
    /* The default framebuffer receives the scene directly when
       GL_EXT_clip_control is available, eliminating the offscreen back-buffer
       FBO and blit pass so V3D keeps full CPU/GPU overlap. Requesting depth
       only (no stencil) gives a tile-only D32F depth buffer in ogles2.library. */
    tags[n].ti_Tag = OGLES2_CCT_DEPTH;
    tags[n++].ti_Data = 24;
    tags[n].ti_Tag = OGLES2_CCT_VSYNC;
    tags[n++].ti_Data = vsync ? 1 : 0;
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;
    s_context = aglCreateContext2(&error, tags);
    if (s_context == NULL) {
        *why = error == OGLES2_CCE_NO_DISPLAY ? "ogles2 refused the screen mode (not 32-bit?)"
               : error == OGLES2_CCE_NO_GPU   ? "no usable V3D GPU"
                                              : "aglCreateContext2 failed";
        OGLES2Close();
        return 0;
    }
    aglMakeCurrent(s_context);

#define GLES2_FILL(ret, name, params, args) api->name = OGLES2_gl##name;
    GLES2_FUNCTIONS(GLES2_FILL)
#undef GLES2_FILL

    display->width = mode.width;
    display->height = mode.height;
    display->inputWindow = s_inputWindow;
    return 1;
}

void Gles2Display_Swap(void)
{
    aglSwapBuffers();
}

void Gles2Display_SetVsync(int vsync)
{
    struct TagItem tags[2];
    tags[0].ti_Tag = OGLES2_CCT_VSYNC;
    tags[0].ti_Data = vsync ? 1 : 0;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    aglSetParams2(tags);
}

void Gles2Display_Close(void)
{
    if (s_context != NULL) {
        aglDestroyContext(s_context);
        s_context = NULL;
    }
    s_inputWindow = NULL;
    OGLES2Close();
}
