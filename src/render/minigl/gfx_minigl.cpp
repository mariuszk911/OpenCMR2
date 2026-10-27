// MiniGL renderer executor.
//
// This first slice owns the MiniGL context/window and the frame lifecycle:
// SetVideoMode creates the context, BeginScene locks the display, Clear and
// Present work, and the window's IDCMP port feeds Sys_PollEvent. Fixed-function
// state is recorded and draws/textures are accepted but not rendered yet; the
// 2D/menu path is the next slice.
#include "port/gfx.h"
#include "port/sys.h"
#include "platform/amiga/amiga_sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <new>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/minigl.h>

struct GfxTexture {
    int width;
    int height;
    int format;
    int mipLevels;
    uint32_t flags;
    unsigned char *data;
    uint32_t dataSize;
    int32_t pitch;
    GfxTexture *replacement;
    GfxTexture *parent;
    unsigned int glId;
    int dirty;
};

struct GfxVertexBuffer {
    GfxVertex *vertices;
    uint32_t count;
};

namespace {

void *s_context;
int s_miniglOpen;
// Stage 0 sampler state. Direct3D keeps it per stage, GL per texture object,
// so it is stored here and applied whenever a texture is bound or uploaded.
int s_stageAddress = GFX_ADDRESS_WRAP;
int s_stageMinFilter = GFX_FILTER_POINT;
int s_stageMagFilter = GFX_FILTER_POINT;
GfxTexture *s_boundTexture;
// MiniGL has no render-to-texture. While the game targets a texture (the car
// reflection cube faces) its draws and clears are dropped so they cannot land
// on the back buffer; the texture keeps its contents.
GfxTexture *s_renderTarget;
static const float kMenuDotCircle[20][2] = {
    {1.0f, 0.0f}, {0.95106f, 0.30902f}, {0.80902f, 0.58779f}, {0.58779f, 0.80902f},
    {0.30902f, 0.95106f}, {0.0f, 1.0f}, {-0.30902f, 0.95106f}, {-0.58779f, 0.80902f},
    {-0.80902f, 0.58779f}, {-0.95106f, 0.30902f}, {-1.0f, 0.0f}, {-0.95106f, -0.30902f},
    {-0.80902f, -0.58779f}, {-0.58779f, -0.80902f}, {-0.30902f, -0.95106f}, {0.0f, -1.0f},
    {0.30902f, -0.95106f}, {0.58779f, -0.80902f}, {0.80902f, -0.58779f}, {0.95106f, -0.30902f},
};
int s_width = 640;
int s_height = 480;
int s_locked;
int s_fullscreen;
// Windowed mode: the application owns this window and MiniGL renders into it.
void *s_gameWindow;
void UploadTexture(GfxTexture *texture);
void ApplyViewport(void);
int s_virtualWidth;
int s_virtualHeight;

GfxViewport s_viewport = { 0, 0, 640, 480, 0.0f, 1.0f };
GfxMatrix s_transforms[18];
int s_transformSet[18];

GfxLight s_lights[8];
int s_lightSet[8];
int s_lightEnabled[8];

GfxMaterial s_material;
uint32_t s_ambient;
uint32_t s_fogColor;
uint32_t s_textureFactor;
int s_fogEnabled;
float s_fogStart;
float s_fogEnd;
int s_depthTest = 1;
int s_depthWrite = 1;
int s_alphaBlend;
int s_alphaTest;
int s_cullMode;

void Identity(GfxMatrix *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->_11 = matrix->_22 = matrix->_33 = matrix->_44 = 1.0f;
}

int EnsureLocked(void)
{
    if (s_locked)
        return 1;
    if (s_context == NULL)
        return 0;
    if (!mglLockDisplay()) {
        Sys_Log("gfx: mglLockDisplay failed");
        return 0;
    }
    s_locked = 1;
    return 1;
}

bool MulSize(uint32_t a, uint32_t b, uint32_t *out)
{
    if (a != 0 && b > 0xffffffffu / a)
        return false;
    *out = a * b;
    return true;
}

bool TextureBytes(int width, int height, int format, uint32_t *out)
{
    uint32_t pixels;
    uint32_t blocks;

    if (width <= 0 || height <= 0 || out == NULL)
        return false;
    switch (format) {
    case GFX_FORMAT_BGRX8:
    case GFX_FORMAT_BGRA8:
        return MulSize((uint32_t)width, (uint32_t)height, &pixels) &&
               MulSize(pixels, 4u, out);
    case GFX_FORMAT_BC1:
        if (!MulSize(((uint32_t)width + 3u) / 4u, ((uint32_t)height + 3u) / 4u, &blocks))
            return false;
        return MulSize(blocks, 8u, out);
    case GFX_FORMAT_BC3:
        if (!MulSize(((uint32_t)width + 3u) / 4u, ((uint32_t)height + 3u) / 4u, &blocks))
            return false;
        return MulSize(blocks, 16u, out);
    case GFX_FORMAT_BUMP_DUDVL:
        return MulSize((uint32_t)width, (uint32_t)height, &pixels) &&
               MulSize(pixels, 3u, out);
    default:
        return false;
    }
}

int32_t TexturePitch(int width, int format)
{
    int64_t pitch;

    switch (format) {
    case GFX_FORMAT_BGRX8:
    case GFX_FORMAT_BGRA8:
        pitch = (int64_t)width * 4;
        break;
    case GFX_FORMAT_BC1:
        pitch = ((int64_t)width + 3) / 4 * 8;
        break;
    case GFX_FORMAT_BC3:
        pitch = ((int64_t)width + 3) / 4 * 16;
        break;
    case GFX_FORMAT_BUMP_DUDVL:
        pitch = (int64_t)width * 3;
        break;
    default:
        return 0;
    }
    return pitch > 0 && pitch <= 0x7fffffff ? (int32_t)pitch : 0;
}

} // namespace

extern "C" unsigned long long Gfx_PrepareWindow(void)
{
    return 0;
}

extern "C" const char *Gfx_GetRendererName(void)
{
    return "OpenCMR2 MiniGL";
}

extern "C" int Gfx_SetVideoMode(int width, int height, int fullscreen)
{
    if (width <= 0 || height <= 0)
        return 0;
    if (s_context != NULL) {
        // MiniGL's window/screen is created with the context; changing mode
        // needs a renderer restart, which M1 does not do yet.
        if (width != s_width || height != s_height)
            Sys_Log("gfx: keeping %dx%d (requested %dx%d)", s_width, s_height, width, height);
        return 1;
    }
    if (!MiniGLOpen()) {
        Sys_Log("gfx: MiniGLOpen failed");
        return 0;
    }
    s_miniglOpen = 1;
    // PORT: AMIGA: windowed mode follows reVC. The application owns the
    // visible window (close gadget, focus, IDCMP port) and MiniGL renders into
    // its inner area with mglCreateContextFromWindow.
    s_gameWindow = NULL;
    if (!fullscreen) {
        s_gameWindow = AmigaSys_CreateGameWindow(width, height, "OpenCMR2");
        if (s_gameWindow != NULL) {
            s_context = mglCreateContextFromWindow((struct Window *)s_gameWindow);
            if (s_context == NULL) {
                Sys_Log("gfx: mglCreateContextFromWindow failed, using the MiniGL screen");
                AmigaSys_SetWindow(NULL);
                s_gameWindow = NULL;
            }
        }
    }
    if (s_context == NULL) {
        s_context = mglCreateContext(0, 0, width, height);
        if (s_context == NULL) {
            Sys_Log("gfx: mglCreateContext %dx%d failed", width, height);
            if (s_gameWindow != NULL) {
                AmigaSys_SetWindow(NULL);
                s_gameWindow = NULL;
            }
            MiniGLClose();
            s_miniglOpen = 0;
            return 0;
        }
        // Fullscreen: v3dWindow is NULL and inputWindow is MiniGL's borderless
        // backdrop; it has no UserPort, so AmigaSys_SetWindow owns a 1x1 window.
        void *inputWindow = mglGetInputWindowHandle();
        AmigaSys_SetWindow(inputWindow);
        Sys_Log("gfx: MiniGL %dx%d context ready (screen), input window %lx",
                width, height, (unsigned long)inputWindow);
    } else {
        Sys_Log("gfx: MiniGL %dx%d context ready (window)", width, height);
    }
    s_width = width;
    s_height = height;
    s_fullscreen = fullscreen != 0;
    s_viewport.width = width;
    s_viewport.height = height;
    s_virtualWidth = 0;
    s_virtualHeight = 0;
    if (!EnsureLocked()) {
        mglDeleteContext();
        s_context = NULL;
        if (s_gameWindow != NULL) {
            AmigaSys_SetWindow(NULL);
            s_gameWindow = NULL;
        }
        MiniGLClose();
        s_miniglOpen = 0;
        return 0;
    }
    ApplyViewport();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglUnlockDisplay();
    s_locked = 0;
    return 1;
}

extern "C" void Gfx_SetFullscreen(int fullscreen)
{
    // MiniGL owns the screen mode; the setting is kept for the menu and a
    // future renderer restart.
    s_fullscreen = fullscreen != 0;
}

extern "C" void Gfx_Shutdown(void)
{
    if (s_context != NULL) {
        s_boundTexture = NULL;
        s_renderTarget = NULL;
        if (s_locked) {
            mglUnlockDisplay();
            s_locked = 0;
        }
        if (s_gameWindow == NULL) {
            // Fullscreen: our port lives on MiniGL's input backdrop, which
            // goes away with the context. Detach it before deleting.
            AmigaSys_SetWindow(NULL);
        }
        mglDeleteContext();
        s_context = NULL;
    }
    if (s_gameWindow != NULL) {
        // The adopted window outlives the context; close it before MiniGLClose
        // (proto/intuition.h stubs need the library's IntuitionBase).
        AmigaSys_SetWindow(NULL);
        s_gameWindow = NULL;
    }
    if (s_miniglOpen) {
        MiniGLClose();
        s_miniglOpen = 0;
    }
}

extern "C" int Gfx_GetDisplayModeCount(void)
{
    return 3;
}

extern "C" void Gfx_GetDisplayMode(int index, int *width, int *height)
{
    static const int modes[3][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 } };
    if (width == NULL || height == NULL)
        return;
    if (index < 0 || index >= 3)
        index = 0;
    *width = modes[index][0];
    *height = modes[index][1];
}

extern "C" void Gfx_GetDefaultDisplayMode(int *width, int *height)
{
    if (width != NULL)
        *width = 640;
    if (height != NULL)
        *height = 480;
}

extern "C" void Gfx_BeginScene(void)
{
    EnsureLocked();
}

extern "C" void Gfx_EndScene(void)
{
}

extern "C" void Gfx_Present(int vsync)
{
    (void)vsync;
    if (s_context == NULL)
        return;
    if (s_locked) {
        mglUnlockDisplay();
        s_locked = 0;
    }
    mglSwitchDisplay();
}

extern "C" void Gfx_PresentStretched(int vsync)
{
    Gfx_Present(vsync);
}

extern "C" void Gfx_Clear(uint32_t flags, uint32_t color, float z)
{
    if (s_renderTarget != NULL || !EnsureLocked())
        return;
    glClearColor(((color >> 16) & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f,
                 (color & 0xff) / 255.0f, ((color >> 24) & 0xff) / 255.0f);
    // All current callers pass z = 1.0, the GL default clear depth. GLClearDepth
    // is not exercised by the M0.9 smoke and crashed on the target; clearing to
    // the default is equivalent for every call site in the game.
    (void)z;
    GLbitfield bits = 0;
    if (flags & GFX_CLEAR_TARGET)
        bits |= GL_COLOR_BUFFER_BIT;
    if (flags & GFX_CLEAR_ZBUFFER)
        bits |= GL_DEPTH_BUFFER_BIT;
    glClear(bits);
}

namespace {

// The game's 2D coordinates and viewports are in the virtual screen while one
// is set (the menus' screen, see Gfx_SetVirtualScreen), otherwise in the
// back buffer's pixels.
void GetScreenSize(int *width, int *height)
{
    const bool scaled = s_virtualWidth > 0 && s_virtualHeight > 0;
    *width = scaled ? s_virtualWidth : s_width;
    *height = scaled ? s_virtualHeight : s_height;
}

// Maps a screen coordinate to back-buffer pixels, rounding half up like the
// host executors' lroundf, and clamps it to [low, size].
int ScaleToTarget(uint32_t value, int target, int screen, int low)
{
    const int64_t scaled = ((int64_t)value * target + screen / 2) / screen;
    if (scaled < low)
        return low;
    return scaled > target ? target : (int)scaled;
}

// The game's viewport in back-buffer pixels, GL (bottom-left) origin.
struct TargetRect {
    int x, y, width, height;
};
TargetRect s_targetViewport;

// Applies s_viewport to GL, scaled from the screen to the back buffer and
// clamped to it as the host executors do. As on the host the viewport also
// clips: the scissor test stays on with the same rectangle, bounding clears
// and TL draws (which use the whole back buffer as their viewport).
// The caller holds the display lock.
void ApplyViewport(void)
{
    int screenW, screenH;
    GetScreenSize(&screenW, &screenH);
    if (screenW <= 0 || screenH <= 0)
        return;
    const int x0 = ScaleToTarget(s_viewport.x, s_width, screenW, 0);
    const int y0 = ScaleToTarget(s_viewport.y, s_height, screenH, 0);
    const int x1 = ScaleToTarget(s_viewport.x + s_viewport.width, s_width, screenW, x0);
    const int y1 = ScaleToTarget(s_viewport.y + s_viewport.height, s_height, screenH, y0);
    // D3D's origin is the top-left corner, GL's the bottom-left one.
    s_targetViewport.x = x0;
    s_targetViewport.y = s_height - y1;
    s_targetViewport.width = x1 - x0;
    s_targetViewport.height = y1 - y0;
    glViewport(s_targetViewport.x, s_targetViewport.y, s_targetViewport.width, s_targetViewport.height);
    glScissor(s_targetViewport.x, s_targetViewport.y, s_targetViewport.width, s_targetViewport.height);
    glEnable(GL_SCISSOR_TEST);
}

// TL vertices are in screen coordinates over the whole screen, like the host
// executors' fixedtl shader, and are clipped (not scaled) by the game's
// viewport through the scissor test. The caller holds the display lock and
// calls EndScreenDraw afterwards.
void BeginScreenDraw(void)
{
    int screenW, screenH;
    GetScreenSize(&screenW, &screenH);
    glViewport(0, 0, s_width, s_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)screenW, (double)screenH, 0.0, -1.0, 1.0);
}

// Restores the game's viewport for the draws that use it.
void EndScreenDraw(void)
{
    glViewport(s_targetViewport.x, s_targetViewport.y, s_targetViewport.width, s_targetViewport.height);
}

} // namespace

extern "C" void Gfx_SetViewport(const GfxViewport *viewport)
{
    if (viewport == NULL)
        return;
    s_viewport = *viewport;
    if (s_renderTarget == NULL && s_context != NULL && EnsureLocked())
        ApplyViewport();
}

extern "C" void Gfx_GetViewport(GfxViewport *viewport)
{
    if (viewport != NULL)
        *viewport = s_viewport;
}

extern "C" void Gfx_SetVirtualScreen(int width, int height)
{
    if (width <= 0 || height <= 0)
        width = height = 0;
    if (width == s_virtualWidth && height == s_virtualHeight)
        return;
    s_virtualWidth = width;
    s_virtualHeight = height;
    // The stored viewport is in the screen's coordinates; remap it.
    if (s_renderTarget == NULL && s_context != NULL && EnsureLocked())
        ApplyViewport();
}

extern "C" void Gfx_PushState(void)
{
}

extern "C" void Gfx_PopState(void)
{
}

extern "C" void Gfx_SetRenderTarget(GfxTexture *target)
{
    if (target == s_renderTarget)
        return;
    if (target != NULL) {
        static bool logged;
        if (!logged) {
            logged = true;
            Sys_Log("AMIGA: render targets are not supported; their draws are dropped "
                    "(no car reflections)");
        }
    }
    s_renderTarget = target;
    // Back on the back buffer, restore the viewport the game set for it.
    if (target == NULL && s_context != NULL && EnsureLocked())
        ApplyViewport();
}

extern "C" void Gfx_CaptureBackdrop(void)
{
}

extern "C" void Gfx_SetDistanceFade(float start, float end)
{
    (void)start;
    (void)end;
}

extern "C" void Gfx_SetTransform(int type, const GfxMatrix *matrix)
{
    if (type < 0 || type >= 18)
        return;
    if (matrix != NULL) {
        s_transforms[type] = *matrix;
        s_transformSet[type] = 1;
    }
}

extern "C" void Gfx_GetTransform(int type, GfxMatrix *matrix)
{
    if (matrix == NULL)
        return;
    if (type >= 0 && type < 18 && s_transformSet[type])
        *matrix = s_transforms[type];
    else
        Identity(matrix);
}

extern "C" void Gfx_SetLight(int index, const GfxLight *light)
{
    if (index < 0 || index >= 8 || light == NULL)
        return;
    s_lights[index] = *light;
    s_lightSet[index] = 1;
}

extern "C" void Gfx_GetLight(int index, GfxLight *light)
{
    if (index < 0 || index >= 8 || light == NULL)
        return;
    if (s_lightSet[index])
        *light = s_lights[index];
    else
        memset(light, 0, sizeof(*light));
}

extern "C" void Gfx_EnableLight(int index, int enable)
{
    if (index >= 0 && index < 8)
        s_lightEnabled[index] = enable != 0;
}

extern "C" void Gfx_SetMaterial(const GfxMaterial *material)
{
    if (material != NULL)
        s_material = *material;
}

extern "C" void Gfx_SetLighting(int enable) { (void)enable; }
extern "C" void Gfx_SetAmbient(uint32_t color) { s_ambient = color; }
extern "C" void Gfx_SetSpecular(int enable) { (void)enable; }
extern "C" void Gfx_SetLocalViewer(int enable) { (void)enable; }
extern "C" void Gfx_SetNormalizeNormals(int enable) { (void)enable; }
extern "C" void Gfx_SetColorVertex(int enable) { (void)enable; }
extern "C" void Gfx_SetDiffuseMaterialSource(int source) { (void)source; }
extern "C" void Gfx_SetSpecularMaterialSource(int source) { (void)source; }
extern "C" void Gfx_SetAmbientMaterialSource(int source) { (void)source; }
extern "C" void Gfx_SetEmissiveMaterialSource(int source) { (void)source; }

extern "C" void Gfx_SetFog(int enable) { s_fogEnabled = enable != 0; }
extern "C" void Gfx_SetFogColor(uint32_t color) { s_fogColor = color; }
extern "C" void Gfx_SetFogVertexMode(int mode) { (void)mode; }
extern "C" void Gfx_SetFogTableMode(int mode) { (void)mode; }
extern "C" void Gfx_SetFogStart(float start) { s_fogStart = start; }
extern "C" void Gfx_SetFogEnd(float end) { s_fogEnd = end; }

extern "C" void Gfx_SetDepthTest(int enable) { s_depthTest = enable != 0; }
extern "C" void Gfx_SetDepthWrite(int enable) { s_depthWrite = enable != 0; }
extern "C" void Gfx_SetDepthFunc(int compare) { (void)compare; }
extern "C" void Gfx_SetCullMode(int cull) { s_cullMode = cull; }
extern "C" void Gfx_SetAlphaBlend(int enable) { s_alphaBlend = enable != 0; }
extern "C" void Gfx_SetSrcBlend(int blend) { (void)blend; }
extern "C" void Gfx_SetDestBlend(int blend) { (void)blend; }
extern "C" void Gfx_SetAlphaTest(int enable) { s_alphaTest = enable != 0; }
extern "C" void Gfx_SetAlphaFunc(int compare) { (void)compare; }
extern "C" void Gfx_SetAlphaRef(uint32_t reference) { (void)reference; }
extern "C" void Gfx_SetTextureFactor(uint32_t color) { s_textureFactor = color; }

static void ApplyStageSampler(void);

extern "C" void Gfx_SetTexture(int stage, GfxTexture *texture)
{
    if (stage != 0)
        return;
    // Without the display lock nothing can be uploaded or bound; record that
    // no texture is bound so the TL path never reads a stale one.
    if (!EnsureLocked()) {
        s_boundTexture = NULL;
        return;
    }
    GfxTexture *drawn = NULL;
    if (texture != NULL && texture->parent == NULL) {
        drawn = texture->replacement != NULL ? texture->replacement : texture;
        if (drawn->dirty)
            UploadTexture(drawn);
        if (drawn->glId == 0)
            drawn = NULL;
    }
    s_boundTexture = drawn;
    if (drawn == NULL) {
        glDisable(GL_TEXTURE_2D);
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, drawn->glId);
    ApplyStageSampler();
}

extern "C" void Gfx_SetStageColorOp(int stage, int op) { (void)stage; (void)op; }
extern "C" void Gfx_SetStageColorArg1(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageColorArg2(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageAlphaOp(int stage, int op) { (void)stage; (void)op; }
extern "C" void Gfx_SetStageAlphaArg1(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageAlphaArg2(int stage, int arg) { (void)stage; (void)arg; }
static GLint StageWrapMode(void)
{
    switch (s_stageAddress) {
    case GFX_ADDRESS_CLAMP: return GL_CLAMP_TO_EDGE;
    case GFX_ADDRESS_MIRROR: return GL_REPEAT;
    default: return GL_REPEAT;
    }
}

static GLint FilterMode(int filter)
{
    return filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
}

// Applies the stage 0 sampler state to the texture object bound to
// GL_TEXTURE_2D. The caller holds the display lock.
static void ApplyStageSampler(void)
{
    const GLint wrap = StageWrapMode();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, FilterMode(s_stageMinFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, FilterMode(s_stageMagFilter));
}

// A sampler change reaches the texture already bound; textures bound later
// pick the stored state up in Gfx_SetTexture.
static void ReapplyStageSampler(void)
{
    if (s_boundTexture != NULL && EnsureLocked())
        ApplyStageSampler();
}

extern "C" void Gfx_SetStageAddress(int stage, int address)
{
    if (stage != 0 || address == s_stageAddress)
        return;
    s_stageAddress = address;
    ReapplyStageSampler();
}
extern "C" void Gfx_SetStageMinFilter(int stage, int filter)
{
    if (stage != 0 || filter == s_stageMinFilter)
        return;
    s_stageMinFilter = filter;
    ReapplyStageSampler();
}
extern "C" void Gfx_SetStageMagFilter(int stage, int filter)
{
    if (stage != 0 || filter == s_stageMagFilter)
        return;
    s_stageMagFilter = filter;
    ReapplyStageSampler();
}
extern "C" void Gfx_SetStageMipFilter(int stage, int filter) { (void)stage; (void)filter; }
extern "C" void Gfx_SetStageMaxMipLevel(int stage, int level) { (void)stage; (void)level; }
extern "C" void Gfx_SetStageTexCoordIndex(int stage, uint32_t index) { (void)stage; (void)index; }
extern "C" void Gfx_SetStageTextureTransform(int stage, int flags) { (void)stage; (void)flags; }
extern "C" void Gfx_SetStageBumpEnvMat(int stage, int index, float value) { (void)stage; (void)index; (void)value; }
extern "C" void Gfx_SetStageBumpEnvLScale(int stage, float value) { (void)stage; (void)value; }
extern "C" void Gfx_SetStageBumpEnvLOffset(int stage, float value) { (void)stage; (void)value; }

extern "C" GfxVertexBuffer *Gfx_CreateVertexBuffer(uint32_t vertexCount)
{
    GfxVertexBuffer *buffer = new (std::nothrow) GfxVertexBuffer;
    if (buffer == NULL)
        return NULL;
    buffer->count = vertexCount;
    buffer->vertices = vertexCount != 0 ? new (std::nothrow) GfxVertex[vertexCount] : NULL;
    if (vertexCount != 0 && buffer->vertices == NULL) {
        delete buffer;
        return NULL;
    }
    return buffer;
}

extern "C" void Gfx_DestroyVertexBuffer(GfxVertexBuffer *buffer)
{
    if (buffer == NULL)
        return;
    delete[] buffer->vertices;
    delete buffer;
}

extern "C" GfxVertex *Gfx_LockVertexBuffer(GfxVertexBuffer *buffer)
{
    return buffer != NULL ? buffer->vertices : NULL;
}

extern "C" void Gfx_UnlockVertexBuffer(GfxVertexBuffer *buffer)
{
    (void)buffer;
}

extern "C" void Gfx_DrawPrimitiveVB(int primitive, GfxVertexBuffer *buffer, uint32_t firstVertex, uint32_t vertexCount)
{
    (void)primitive; (void)buffer; (void)firstVertex; (void)vertexCount;
}

extern "C" void Gfx_DrawIndexedPrimitiveVB(int primitive, GfxVertexBuffer *buffer, uint32_t firstVertex,
                                           uint32_t vertexCount, const uint16_t *indices, uint32_t indexCount)
{
    (void)primitive; (void)buffer; (void)firstVertex; (void)vertexCount; (void)indices; (void)indexCount;
}

extern "C" void Gfx_DrawPrimitive(int primitive, const GfxVertex *vertices, uint32_t vertexCount)
{
    (void)primitive; (void)vertices; (void)vertexCount;
}

extern "C" void Gfx_DrawIndexedPrimitive(int primitive, const GfxVertex *vertices, uint32_t vertexCount,
                                         const uint16_t *indices, uint32_t indexCount)
{
    (void)primitive; (void)vertices; (void)vertexCount; (void)indices; (void)indexCount;
}

extern "C" void Gfx_DrawPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount)
{
    if (vertices == NULL || vertexCount == 0 || s_renderTarget != NULL || !EnsureLocked())
        return;
    GLenum mode;
    switch (primitive) {
    case GFX_TRIANGLELIST: mode = GL_TRIANGLES; break;
    case GFX_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; break;
    case GFX_LINELIST: mode = GL_LINES; break;
    case GFX_LINESTRIP: mode = GL_LINE_STRIP; break;
    default: return;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    BeginScreenDraw();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (s_boundTexture != NULL && (s_boundTexture->flags & GFX_TEXTURE_MENU_DOT) != 0 &&
        vertexCount >= 4) {
        float minX = vertices[0].sx, maxX = minX;
        float minY = vertices[0].sy, maxY = minY;
        for (uint32_t i = 1; i < vertexCount; ++i) {
            if (vertices[i].sx < minX) minX = vertices[i].sx;
            if (vertices[i].sx > maxX) maxX = vertices[i].sx;
            if (vertices[i].sy < minY) minY = vertices[i].sy;
            if (vertices[i].sy > maxY) maxY = vertices[i].sy;
        }
        const float cx = (minX + maxX) * 0.5f;
        const float cy = (minY + maxY) * 0.5f;
        const float rx = (maxX - minX) * 0.5f;
        const float ry = (maxY - minY) * 0.5f;
        const uint32_t c = vertices[0].color;
        glDisable(GL_TEXTURE_2D);
        glColor4ub((GLubyte)((c >> 16) & 0xff), (GLubyte)((c >> 8) & 0xff),
                   (GLubyte)(c & 0xff), (GLubyte)((c >> 24) & 0xff));
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 20; ++i) {
            const float *p = kMenuDotCircle[i % 20];
            glVertex2f(cx + rx * p[0], cy + ry * p[1]);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        EndScreenDraw();
        return;
    }
    glBegin(mode);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        const GfxTLVertex *v = &vertices[i];
        glColor4ub((GLubyte)((v->color >> 16) & 0xff), (GLubyte)((v->color >> 8) & 0xff),
                   (GLubyte)(v->color & 0xff), (GLubyte)((v->color >> 24) & 0xff));
        glTexCoord2f(v->tu, v->tv);
        glVertex2f(v->sx, v->sy);
    }
    glEnd();
    EndScreenDraw();
}

extern "C" void Gfx_DrawIndexedPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount,
                                           const uint16_t *indices, uint32_t indexCount)
{
    (void)vertexCount;
    if (vertices == NULL || indices == NULL || indexCount == 0 || s_renderTarget != NULL || !EnsureLocked())
        return;
    GLenum mode = primitive == GFX_TRIANGLESTRIP ? GL_TRIANGLE_STRIP : GL_TRIANGLES;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    BeginScreenDraw();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(mode);
    for (uint32_t i = 0; i < indexCount; ++i) {
        const GfxTLVertex *v = &vertices[indices[i]];
        glColor4ub((GLubyte)((v->color >> 16) & 0xff), (GLubyte)((v->color >> 8) & 0xff),
                   (GLubyte)(v->color & 0xff), (GLubyte)((v->color >> 24) & 0xff));
        glTexCoord2f(v->tu, v->tv);
        glVertex2f(v->sx, v->sy);
    }
    glEnd();
    EndScreenDraw();
}

extern "C" void Gfx_GetPixelFormat(int format, GfxPixelFormat *out)
{
    if (out == NULL)
        return;
    if (format == GFX_FORMAT_BC1 || format == GFX_FORMAT_BC3) {
        static int logged;
        if (!logged) {
            logged = 1;
            Sys_Log("gfx: DXT1/DXT5 advertised");
        }
    }
    memset(out, 0, sizeof(*out));
    switch (format) {
    case GFX_FORMAT_BGRX8:
        out->flags = GFX_PF_RGB;
        out->bitsPerPixel = 32;
        out->redMask = 0x00ff0000;
        out->greenMask = 0x0000ff00;
        out->blueMask = 0x000000ff;
        break;
    case GFX_FORMAT_BGRA8:
        out->flags = GFX_PF_RGB | GFX_PF_ALPHA;
        out->bitsPerPixel = 32;
        out->redMask = 0x00ff0000;
        out->greenMask = 0x0000ff00;
        out->blueMask = 0x000000ff;
        out->alphaMask = 0xff000000;
        break;
    case GFX_FORMAT_BC1:
        out->flags = GFX_PF_FOURCC;
        out->fourCC = 0x31545844; // 'DXT1'
        break;
    case GFX_FORMAT_BC3:
        out->flags = GFX_PF_FOURCC;
        out->fourCC = 0x35545844; // 'DXT5'
        break;
    case GFX_FORMAT_BUMP_DUDVL:
        out->flags = GFX_PF_BUMP;
        out->bitsPerPixel = 24;
        out->redMask = 0x0000ff;
        out->greenMask = 0x00ff00;
        out->blueMask = 0xff0000;
        break;
    default:
        break;
    }
}

namespace {

void PutPixelRGBA(unsigned char *dst, const unsigned char *src, int format)
{
    switch (format) {
    case GFX_FORMAT_BGRA8:
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
        break;
    case GFX_FORMAT_BGRX8:
    case GFX_FORMAT_BUMP_DUDVL:
        // A bump texel is du, dv, luminance (3 bytes), read as the host
        // executors do: blue, green, red, opaque.
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 0xff;
        break;
    default:
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        break;
    }
}

void DecodeBCColor(unsigned int c0, unsigned int c1, int alpha, unsigned int *palette)
{
    const unsigned int r0 = ((c0 >> 11) & 0x1f) * 255 / 31;
    const unsigned int g0 = ((c0 >> 5) & 0x3f) * 255 / 63;
    const unsigned int b0 = (c0 & 0x1f) * 255 / 31;
    const unsigned int r1 = ((c1 >> 11) & 0x1f) * 255 / 31;
    const unsigned int g1 = ((c1 >> 5) & 0x3f) * 255 / 63;
    const unsigned int b1 = (c1 & 0x1f) * 255 / 31;
    const unsigned int p0 = r0 | (g0 << 8) | (b0 << 16) | 0xff000000u;
    const unsigned int p1 = r1 | (g1 << 8) | (b1 << 16) | 0xff000000u;
    palette[0] = p0;
    palette[1] = p1;
    if (alpha) {
        palette[2] = (((p0 & 0xff) + (p1 & 0xff)) / 2) |
                     ((((p0 >> 8) & 0xff) + ((p1 >> 8) & 0xff)) / 2) << 8 |
                     ((((p0 >> 16) & 0xff) + ((p1 >> 16) & 0xff)) / 2) << 16 |
                     0xff000000u;
        palette[3] = 0;
    } else if (c0 > c1) {
        palette[2] = (((p0 & 0xff) * 2 + (p1 & 0xff)) / 3) |
                     ((((p0 >> 8) & 0xff) * 2 + ((p1 >> 8) & 0xff)) / 3) << 8 |
                     ((((p0 >> 16) & 0xff) * 2 + ((p1 >> 16) & 0xff)) / 3) << 16 |
                     0xff000000u;
        palette[3] = (((p0 & 0xff) + (p1 & 0xff) * 2) / 3) |
                     ((((p0 >> 8) & 0xff) + ((p1 >> 8) & 0xff) * 2) / 3) << 8 |
                     ((((p0 >> 16) & 0xff) + ((p1 >> 16) & 0xff) * 2) / 3) << 16 |
                     0xff000000u;
    } else {
        palette[2] = (((p0 & 0xff) + (p1 & 0xff)) / 2) |
                     ((((p0 >> 8) & 0xff) + ((p1 >> 8) & 0xff)) / 2) << 8 |
                     ((((p0 >> 16) & 0xff) + ((p1 >> 16) & 0xff)) / 2) << 16 |
                     0xff000000u;
        palette[3] = 0;
    }
}

void DecodeBC1Block(const unsigned char *src, unsigned char *dst, int dstPitch)
{
    unsigned int palette[4];
    const unsigned int c0 = src[0] | (src[1] << 8);
    const unsigned int c1 = src[2] | (src[3] << 8);
    DecodeBCColor(c0, c1, 0, palette);
    const unsigned int bits = src[4] | (src[5] << 8) | (src[6] << 16) | ((unsigned int)src[7] << 24);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const unsigned int p = palette[(bits >> (2 * (y * 4 + x))) & 3];
            unsigned char *d = dst + y * dstPitch + x * 4;
            d[0] = (unsigned char)(p & 0xff);
            d[1] = (unsigned char)((p >> 8) & 0xff);
            d[2] = (unsigned char)((p >> 16) & 0xff);
            d[3] = (unsigned char)((p >> 24) & 0xff);
        }
}

void DecodeBC3Block(const unsigned char *src, unsigned char *dst, int dstPitch)
{
    unsigned char alpha[8];
    alpha[0] = src[0];
    alpha[1] = src[1];
    if (alpha[0] > alpha[1]) {
        for (int i = 1; i < 7; ++i)
            alpha[i + 1] = (unsigned char)(((7 - i) * alpha[0] + i * alpha[1]) / 7);
    } else {
        for (int i = 1; i < 5; ++i)
            alpha[i + 1] = (unsigned char)(((5 - i) * alpha[0] + i * alpha[1]) / 5);
        alpha[6] = 0;
        alpha[7] = 255;
    }
    unsigned long long bits = 0;
    for (int i = 0; i < 6; ++i)
        bits |= (unsigned long long)src[2 + i] << (8 * i);
    unsigned int palette[4];
    const unsigned int c0 = src[8] | (src[9] << 8);
    const unsigned int c1 = src[10] | (src[11] << 8);
    DecodeBCColor(c0, c1, 0, palette);
    const unsigned int colorBits = src[12] | (src[13] << 8) | (src[14] << 16) | ((unsigned int)src[15] << 24);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int index = y * 4 + x;
            const unsigned int p = palette[(colorBits >> (2 * index)) & 3];
            const unsigned char a = alpha[(bits >> (3 * index)) & 7];
            unsigned char *d = dst + y * dstPitch + x * 4;
            d[0] = (unsigned char)(p & 0xff);
            d[1] = (unsigned char)((p >> 8) & 0xff);
            d[2] = (unsigned char)((p >> 16) & 0xff);
            d[3] = a;
        }
}

unsigned char *TextureToRGBA(const GfxTexture *texture)
{
    const size_t width = (size_t)texture->width;
    const size_t height = (size_t)texture->height;
    const size_t maxSize = (size_t)-1;
    size_t pixels;
    unsigned char *rgba;

    if (width == 0 || height == 0 || width > maxSize / height)
        return NULL;
    pixels = width * height;
    if (pixels > maxSize / 4)
        return NULL;
    rgba = (unsigned char *)malloc(pixels * 4);
    if (rgba == NULL)
        return NULL;

    if (texture->format == GFX_FORMAT_BC1 || texture->format == GFX_FORMAT_BC3) {
        const size_t blocksWide = (width + 3) / 4;
        const size_t blocksHigh = (height + 3) / 4;
        const size_t blockSize = texture->format == GFX_FORMAT_BC1 ? 8 : 16;
        if (blocksWide > maxSize / blocksHigh ||
            blocksWide * blocksHigh > maxSize / blockSize ||
            (uint64_t)blocksWide * blocksHigh * blockSize > texture->dataSize) {
            free(rgba);
            return NULL;
        }
        for (size_t by = 0; by < blocksHigh; ++by) {
            for (size_t bx = 0; bx < blocksWide; ++bx) {
                const unsigned char *block = texture->data + (by * blocksWide + bx) * blockSize;
                unsigned char pixels[16 * 4];
                if (texture->format == GFX_FORMAT_BC1)
                    DecodeBC1Block(block, pixels, 16);
                else
                    DecodeBC3Block(block, pixels, 16);
                // Edge blocks of a texture whose size is not a multiple of
                // four only cover part of the 4x4 block.
                const size_t rows = height - by * 4 < 4 ? height - by * 4 : 4;
                const size_t columns = width - bx * 4 < 4 ? width - bx * 4 : 4;
                for (size_t y = 0; y < rows; ++y) {
                    unsigned char *dst = rgba + ((by * 4 + y) * width + bx * 4) * 4;
                    memcpy(dst, pixels + y * 16, columns * 4);
                }
            }
        }
    } else {
        const size_t srcBytes = texture->format == GFX_FORMAT_BUMP_DUDVL ? 3 : 4;
        if (texture->pitch <= 0 || width > maxSize / srcBytes ||
            (size_t)texture->pitch < width * srcBytes ||
            (uint64_t)texture->pitch * height > texture->dataSize) {
            free(rgba);
            return NULL;
        }
        for (size_t y = 0; y < height; ++y) {
            const unsigned char *src = texture->data + y * texture->pitch;
            unsigned char *dst = rgba + y * width * 4;
            for (size_t x = 0; x < width; ++x) {
                PutPixelRGBA(dst + x * 4, src + x * srcBytes, texture->format);
            }
        }
    }
    return rgba;
}

void UploadTexture(GfxTexture *texture)
{
    if (texture == NULL || texture->parent != NULL || texture->data == NULL)
        return;
    if ((texture->flags & GFX_TEXTURE_STAGING) != 0)
        return;
    if (!EnsureLocked())
        return;
    if (texture->glId == 0)
        glGenTextures(1, &texture->glId);
    if (texture->glId == 0) {
        Sys_Log("gfx: glGenTextures failed for %dx%d", texture->width, texture->height);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, texture->glId);
    ApplyStageSampler();
    unsigned char *rgba = TextureToRGBA(texture);
    if (rgba != NULL) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    }
    texture->dirty = 0;
}

} // namespace

extern "C" GfxTexture *Gfx_CreateTexture(int width, int height, int format, int mipLevels, uint32_t flags)
{
    if (width <= 0 || height <= 0)
        return NULL;
    uint32_t bytes;
    const int32_t pitch = TexturePitch(width, format);
    if (!TextureBytes(width, height, format, &bytes) || bytes == 0 || pitch == 0)
        return NULL;
    GfxTexture *texture = new (std::nothrow) GfxTexture;
    if (texture == NULL)
        return NULL;
    memset(texture, 0, sizeof(*texture));
    texture->data = (unsigned char *)calloc(1, bytes);
    if (texture->data == NULL) {
        delete texture;
        return NULL;
    }
    texture->width = width;
    texture->height = height;
    texture->format = format;
    texture->mipLevels = mipLevels > 0 ? mipLevels : 1;
    texture->flags = flags;
    texture->dataSize = bytes;
    texture->pitch = pitch;
    texture->dirty = 1;
    return texture;
}

extern "C" void Gfx_DestroyTexture(GfxTexture *texture)
{
    if (texture == NULL)
        return;
    // The TL path reads s_boundTexture's flags; never leave it dangling. A
    // bound texture always has a GL name, so texturing is disabled below
    // while the display is locked for glDeleteTextures.
    if (s_renderTarget == texture ||
        (texture->replacement != NULL && s_renderTarget == texture->replacement))
        s_renderTarget = NULL;
    const bool wasBound = s_boundTexture == texture;
    if (wasBound)
        s_boundTexture = NULL;
    if (texture->parent == NULL) {
        if (texture->replacement != NULL) {
            Gfx_DestroyTexture(texture->replacement);
            texture->replacement = NULL;
        }
        if (texture->glId != 0 && s_context != NULL) {
            const int wasLocked = s_locked;
            if (EnsureLocked()) {
                if (wasBound)
                    glDisable(GL_TEXTURE_2D);
                glDeleteTextures(1, &texture->glId);
                if (!wasLocked) {
                    mglUnlockDisplay();
                    s_locked = 0;
                }
            }
        }
        free(texture->data);
    }
    delete texture;
}

extern "C" int Gfx_GetTextureMipCount(GfxTexture *texture)
{
    // Only the top level has CPU storage; reporting the requested count made
    // callers believe levels 1..n exist.
    return texture != NULL && texture->data != NULL ? 1 : 0;
}

extern "C" int Gfx_LockTexture(GfxTexture *texture, int face, int mip, GfxLockedRect *out)
{
    (void)face;
    if (texture == NULL || out == NULL || mip != 0 || texture->parent != NULL ||
        texture->data == NULL || texture->pitch <= 0)
        return 0;
    out->pixels = texture->data;
    out->pitch = texture->pitch;
    out->width = (uint32_t)texture->width;
    out->height = (uint32_t)texture->height;
    Gfx_GetPixelFormat(texture->format, &out->format);
    return 1;
}

extern "C" void Gfx_UnlockTexture(GfxTexture *texture, int face, int mip)
{
    (void)face; (void)mip;
    if (texture != NULL && texture->parent == NULL)
        texture->dirty = 1;
}

extern "C" void Gfx_SetTextureReplacement(GfxTexture *texture, GfxTexture *replacement)
{
    if (texture != NULL)
        texture->replacement = replacement;
}

extern "C" int Gfx_GetDistanceFieldScale(GfxTexture *texture)
{
    (void)texture;
    return 0;
}

extern "C" void Gfx_VectorizeMenuDot(GfxTexture *texture)
{
    if (texture == NULL || texture->replacement != NULL)
        return;
    if (texture->width != texture->height ||
        (texture->width != 16 && texture->width != 32))
        return;
    if ((texture->flags & GFX_TEXTURE_MENU_DOT) != 0)
        return;
    if (Sys_GetOption("video.vector_flags", 1) == 0)
        return;
    GfxTexture *dot = Gfx_CreateTexture(texture->width, texture->height, texture->format, 1,
                                       GFX_TEXTURE_MENU_DOT);
    if (dot == NULL)
        return;
    Gfx_CopyTexture(dot, texture);
    Gfx_SetTextureReplacement(texture, dot);
}

extern "C" float Gfx_GetSpriteTexelInset(GfxTexture *texture)
{
    // The sprite path now applies the inset to the UVs only, keeping the
    // full destination; this excludes the atlas edge without shrinking
    // glyphs. The menu dot is drawn from vector geometry and needs none.
    GfxTexture *drawn = texture != NULL && texture->replacement != NULL ? texture->replacement : texture;
    if (drawn != NULL && (drawn->flags & GFX_TEXTURE_MENU_DOT))
        return 0.0f;
    return 0.5f;
}

extern "C" void Gfx_GenerateMipmaps(GfxTexture *texture)
{
    (void)texture;
}

extern "C" void Gfx_CopyTexture(GfxTexture *dst, GfxTexture *src)
{
    if (dst == NULL || src == NULL)
        return;
    // A cube-face view shares its cube's storage, as Root() does on the host.
    if (dst->parent != NULL)
        dst = dst->parent;
    if (src->parent != NULL)
        src = src->parent;
    if (dst == src)
        return;
    if (src->data == NULL || dst->data == NULL) {
        Sys_Log("gfx: copy between textures without CPU storage ignored");
        return;
    }
    if (dst->width == src->width && dst->height == src->height && dst->format == src->format) {
        if (dst->dataSize < src->dataSize) {
            Sys_Log("gfx: copy destination is smaller than the source");
            return;
        }
        memcpy(dst->data, src->data, src->dataSize);
        dst->dirty = 1;
        return;
    }
    // Like the host executors: convert through 32-bit pixels and scale with
    // nearest sampling. Nothing converts to a compressed format.
    if (dst->format == GFX_FORMAT_BC1 || dst->format == GFX_FORMAT_BC3) {
        Sys_Log("gfx: cannot convert a texture to a compressed format");
        return;
    }
    // Keep the scaling products in 32 bits on the 68060. Game textures are at
    // most 2048 wide; the guard rejects only malformed geometry, which the
    // caller could not have locked anyway.
    if (src->width > 32767 || src->height > 32767 ||
        dst->width > 32767 || dst->height > 32767) {
        Sys_Log("gfx: refusing to scale a texture with invalid dimensions");
        return;
    }
    unsigned char *rgba = TextureToRGBA(src);
    if (rgba == NULL) {
        Sys_Log("gfx: out of memory converting a %dx%d texture", src->width, src->height);
        return;
    }
    const size_t dstBytes = dst->format == GFX_FORMAT_BUMP_DUDVL ? 3 : 4;
    if (dst->pitch <= 0 || (size_t)dst->pitch < (size_t)dst->width * dstBytes ||
        (uint64_t)dst->pitch * (size_t)dst->height > dst->dataSize) {
        Sys_Log("gfx: destination texture geometry is inconsistent");
        free(rgba);
        return;
    }
    for (int y = 0; y < dst->height; ++y) {
        const int sy = (int)((uint32_t)y * (uint32_t)src->height / (uint32_t)dst->height);
        unsigned char *out = dst->data + y * dst->pitch;
        for (int x = 0; x < dst->width; ++x) {
            const int sx = (int)((uint32_t)x * (uint32_t)src->width / (uint32_t)dst->width);
            const unsigned char *p = rgba + ((size_t)sy * src->width + sx) * 4;
            // The CPU copy is stored as in Direct3D memory: blue, green, red
            // (then alpha for the 32-bit formats).
            out[0] = p[2];
            out[1] = p[1];
            out[2] = p[0];
            if (dstBytes == 4)
                out[3] = p[3];
            out += dstBytes;
        }
    }
    free(rgba);
    dst->dirty = 1;
}

extern "C" GfxTexture *Gfx_GetCubeFace(GfxTexture *cube, int face)
{
    if (cube == NULL || face <= 0)
        return cube;
    GfxTexture *faceTexture = new (std::nothrow) GfxTexture;
    if (faceTexture == NULL)
        return NULL;
    *faceTexture = *cube;
    faceTexture->data = NULL;
    faceTexture->dataSize = 0;
    faceTexture->parent = cube;
    return faceTexture;
}

extern "C" int Gfx_ReadBackBuffer(int x, int y, int width, int height, uint32_t *pixels)
{
    (void)x; (void)y; (void)width; (void)height; (void)pixels;
    return 0;
}
