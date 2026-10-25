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
};

struct GfxVertexBuffer {
    GfxVertex *vertices;
    uint32_t count;
};

namespace {

void *s_context;
int s_width = 640;
int s_height = 480;
int s_locked;
int s_fullscreen;
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

uint32_t TextureBytes(int width, int height, int format)
{
    switch (format) {
    case GFX_FORMAT_BGRX8:
    case GFX_FORMAT_BGRA8:
        return (uint32_t)width * (uint32_t)height * 4u;
    case GFX_FORMAT_BC1: {
        const uint32_t blocksWide = ((uint32_t)width + 3u) / 4u;
        const uint32_t blocksHigh = ((uint32_t)height + 3u) / 4u;
        return blocksWide * blocksHigh * 8u;
    }
    case GFX_FORMAT_BC3: {
        const uint32_t blocksWide = ((uint32_t)width + 3u) / 4u;
        const uint32_t blocksHigh = ((uint32_t)height + 3u) / 4u;
        return blocksWide * blocksHigh * 16u;
    }
    case GFX_FORMAT_BUMP_DUDVL:
        return (uint32_t)width * (uint32_t)height * 3u;
    default:
        return 0;
    }
}

int32_t TexturePitch(int width, int format)
{
    switch (format) {
    case GFX_FORMAT_BGRX8:
    case GFX_FORMAT_BGRA8:
        return width * 4;
    case GFX_FORMAT_BC1:
        return ((width + 3) / 4) * 8;
    case GFX_FORMAT_BC3:
        return ((width + 3) / 4) * 16;
    case GFX_FORMAT_BUMP_DUDVL:
        return width * 3;
    default:
        return 0;
    }
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
    s_context = mglCreateContext(0, 0, width, height);
    if (s_context == NULL) {
        Sys_Log("gfx: mglCreateContext %dx%d failed", width, height);
        MiniGLClose();
        return 0;
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
        MiniGLClose();
        return 0;
    }
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglUnlockDisplay();
    s_locked = 0;
    AmigaSys_SetWindow(mglGetWindowHandle());
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
        if (s_locked) {
            mglUnlockDisplay();
            s_locked = 0;
        }
        mglDeleteContext();
        s_context = NULL;
        MiniGLClose();
    }
    AmigaSys_SetWindow(NULL);
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
    if (!EnsureLocked())
        return;
    glClearColor(((color >> 16) & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f,
                 (color & 0xff) / 255.0f, ((color >> 24) & 0xff) / 255.0f);
    glClearDepth((double)z);
    GLbitfield bits = 0;
    if (flags & GFX_CLEAR_TARGET)
        bits |= GL_COLOR_BUFFER_BIT;
    if (flags & GFX_CLEAR_ZBUFFER)
        bits |= GL_DEPTH_BUFFER_BIT;
    glClear(bits);
}

extern "C" void Gfx_SetViewport(const GfxViewport *viewport)
{
    if (viewport == NULL)
        return;
    s_viewport = *viewport;
    if (EnsureLocked())
        glViewport((GLint)viewport->x, (GLint)(s_height - viewport->y - viewport->height),
                   (GLsizei)viewport->width, (GLsizei)viewport->height);
}

extern "C" void Gfx_GetViewport(GfxViewport *viewport)
{
    if (viewport != NULL)
        *viewport = s_viewport;
}

extern "C" void Gfx_SetVirtualScreen(int width, int height)
{
    s_virtualWidth = width;
    s_virtualHeight = height;
}

extern "C" void Gfx_PushState(void)
{
}

extern "C" void Gfx_PopState(void)
{
}

extern "C" void Gfx_SetRenderTarget(GfxTexture *target)
{
    (void)target;
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

extern "C" void Gfx_SetTexture(int stage, GfxTexture *texture)
{
    (void)stage;
    (void)texture;
}

extern "C" void Gfx_SetStageColorOp(int stage, int op) { (void)stage; (void)op; }
extern "C" void Gfx_SetStageColorArg1(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageColorArg2(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageAlphaOp(int stage, int op) { (void)stage; (void)op; }
extern "C" void Gfx_SetStageAlphaArg1(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageAlphaArg2(int stage, int arg) { (void)stage; (void)arg; }
extern "C" void Gfx_SetStageAddress(int stage, int address) { (void)stage; (void)address; }
extern "C" void Gfx_SetStageMinFilter(int stage, int filter) { (void)stage; (void)filter; }
extern "C" void Gfx_SetStageMagFilter(int stage, int filter) { (void)stage; (void)filter; }
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
    (void)primitive; (void)vertices; (void)vertexCount;
}

extern "C" void Gfx_DrawIndexedPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount,
                                           const uint16_t *indices, uint32_t indexCount)
{
    (void)primitive; (void)vertices; (void)vertexCount; (void)indices; (void)indexCount;
}

extern "C" void Gfx_GetPixelFormat(int format, GfxPixelFormat *out)
{
    if (out == NULL)
        return;
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

extern "C" GfxTexture *Gfx_CreateTexture(int width, int height, int format, int mipLevels, uint32_t flags)
{
    if (width <= 0 || height <= 0)
        return NULL;
    const uint32_t bytes = TextureBytes(width, height, format);
    if (bytes == 0)
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
    texture->pitch = TexturePitch(width, format);
    return texture;
}

extern "C" void Gfx_DestroyTexture(GfxTexture *texture)
{
    if (texture == NULL)
        return;
    if (texture->parent == NULL)
        free(texture->data);
    delete texture;
}

extern "C" int Gfx_GetTextureMipCount(GfxTexture *texture)
{
    return texture != NULL ? texture->mipLevels : 0;
}

extern "C" int Gfx_LockTexture(GfxTexture *texture, int face, int mip, GfxLockedRect *out)
{
    (void)face;
    if (texture == NULL || out == NULL || mip != 0 || texture->parent != NULL)
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
    (void)texture; (void)face; (void)mip;
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
    (void)texture;
}

extern "C" float Gfx_GetSpriteTexelInset(GfxTexture *texture)
{
    (void)texture;
    return 0.0f;
}

extern "C" void Gfx_GenerateMipmaps(GfxTexture *texture)
{
    (void)texture;
}

extern "C" void Gfx_CopyTexture(GfxTexture *dst, GfxTexture *src)
{
    (void)dst; (void)src;
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
