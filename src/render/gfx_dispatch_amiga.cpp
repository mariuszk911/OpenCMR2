// Gfx_* dispatcher for the AmigaOS build (Domain B).
//
// The binary holds two renderers, each exporting the whole API of port/gfx.h
// under its own prefix (MiniGL_Gfx_*, Core_Gfx_*; render/gfx_api_prefix.h).
// This file defines the Gfx_* names the game calls and forwards each to the
// backend chosen once, on the first call, from opencmr2.ini:
//
//   [video]
//   renderer = minigl | gles2
//
// Anything unusable falls back to MiniGL with a log line (port/gfx_renderer.h).
// The list of entry points is render/gfx_api.inc.
#include "port/gfx.h"
#include "port/gfx_renderer.h"
#include "render/gfx_stream.h"
#include "render/minigl/gfx_minigl_movie.h"
#include "port/sys.h"

#ifdef OPENCMR2_GLES2_RENDERER
#include "render/gles2/gles2_api.h"
#endif

namespace {

struct GfxBackend {
    const char *name;
    GfxTexture *(*streamCreate)(int width, int height);
    int (*streamGetSize)(const GfxTexture *texture, int *glWidth, int *glHeight);
    int (*streamUploadRows)(GfxTexture *texture, const uint8_t *rgba, int y0, int rows);
#define GFX_API(ret, name, params, args) ret(*name) params;
#include "render/gfx_api.inc"
#undef GFX_API
};

} // namespace

extern "C" {
#define GFX_API(ret, name, params, args) ret MiniGL_##name params;
#include "render/gfx_api.inc"
#undef GFX_API
#ifdef OPENCMR2_GLES2_RENDERER
#define GFX_API(ret, name, params, args) ret Core_##name params;
#include "render/gfx_api.inc"
#undef GFX_API
GfxTexture *Core_GfxStream_Create(int width, int height);
int Core_GfxStream_GetSize(const GfxTexture *texture, int *glWidth, int *glHeight);
int Core_GfxStream_UploadRows(GfxTexture *texture, const uint8_t *rgba, int y0, int rows);
#endif
}

namespace {

const GfxBackend s_minigl = {
    "minigl",
    &GfxMiniGL_CreateStreamTexture,
    &GfxMiniGL_GetStreamTextureSize,
    &GfxMiniGL_UploadRows,
#define GFX_API(ret, name, params, args) &MiniGL_##name,
#include "render/gfx_api.inc"
#undef GFX_API
};

#ifdef OPENCMR2_GLES2_RENDERER
const GfxBackend s_gles2 = {
    "gles2",
    &Core_GfxStream_Create,
    &Core_GfxStream_GetSize,
    &Core_GfxStream_UploadRows,
#define GFX_API(ret, name, params, args) &Core_##name,
#include "render/gfx_api.inc"
#undef GFX_API
};
#endif

const GfxBackend *s_active = NULL;

void SelectBackend()
{
    const char *text = Sys_GetOptionText("video.renderer", "minigl");
    int known = 1;
    const int requested = GfxRenderer_Parse(text, &known);
    int gles2Built = 0;
    int ogles2Opens = 0;
    int modeFound = 0;
    const char *why = NULL;

    if (!known)
        Sys_Log("gfx: video.renderer = '%s' is not minigl or gles2; using minigl", text);
#ifdef OPENCMR2_GLES2_RENDERER
    gles2Built = 1;
    if (requested == GFX_RENDERER_GLES2) {
        const char *reason = "";
        modeFound = Gles2Display_Probe(&ogles2Opens, &reason);
        if (!modeFound)
            Sys_Log("gfx: ogles2: %s", reason);
    }
#endif
    const int chosen = GfxRenderer_Choose(requested, gles2Built, ogles2Opens, modeFound, &why);
    if (why != NULL)
        Sys_Log("gfx: video.renderer = gles2 is not used: %s; using minigl", why);
#ifdef OPENCMR2_GLES2_RENDERER
    s_active = chosen == GFX_RENDERER_GLES2 ? &s_gles2 : &s_minigl;
#else
    s_active = &s_minigl;
    (void)chosen;
#endif
    Sys_Log("gfx: renderer %s", s_active->name);
}

inline const GfxBackend *Active()
{
    if (s_active == NULL)
        SelectBackend();
    return s_active;
}

} // namespace

#define GFX_API(ret, name, params, args) \
    extern "C" ret name params { return Active()->name args; }
#include "render/gfx_api.inc"
#undef GFX_API

extern "C" GfxTexture *GfxStream_Create(int width, int height)
{
    return Active()->streamCreate(width, height);
}

extern "C" int GfxStream_GetSize(const GfxTexture *texture, int *glWidth, int *glHeight)
{
    return Active()->streamGetSize(texture, glWidth, glHeight);
}

extern "C" int GfxStream_UploadRows(GfxTexture *texture, const uint8_t *rgba, int y0, int rows)
{
    return Active()->streamUploadRows(texture, rgba, y0, rows);
}
