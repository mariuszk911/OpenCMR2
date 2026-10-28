// MiniGL renderer executor.
//
// Owns the MiniGL context/window, frame lifecycle, 2D TL rendering, and the
// M3.2 capability-tiered 3D fixed-function pipeline.
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
    // Sampler state last applied to this GL texture object. GL keeps the wrap
    // and filter per object, so re-applying an unchanged state on every draw
    // is pure overhead; the stage state is compared against this cache.
    int samplerValid;
    int samplerWrap;
    int samplerMinFilter;
    int samplerMagFilter;
};

struct GfxVertexBuffer {
    GfxVertex *vertices;
    uint32_t count;
};

namespace {

// MiniGL copies each glBegin..glEnd block into a fixed vertex buffer and
// silently drops vertices past its size. The documented default is 256; the
// backend keeps every block within that default instead of depending on
// mglChooseVertexBufferSize taking effect in whatever library version is
// installed. Lists are split at a whole primitive, strips/fans are flattened.
enum { kMaxBlockVertices = 256 };

struct CapabilityPolicy {
    int probed;
    int tier;
    int maxTexUnits;
    int hasFogLinear;
    int hasAlphaTest;
    int hasModulate;
    int hasAdd;
    int hasColorMaterial;
    int hasBlendSubtract;
};

struct StageState {
    int colorOp;
    int colorArg1;
    int colorArg2;
    int alphaOp;
    int alphaArg1;
    int alphaArg2;
    int address;
    int minFilter;
    int magFilter;
    uint32_t texCoordIndex;
    int textureTransform;
    GfxTexture *texture;
};

struct GlStateCache {
    int depthTest;
    int depthWrite;
    int depthFunc;
    int cullFace;
    GLenum cullMode;
    GLenum frontFace;
    int alphaBlend;
    GLenum srcBlend;
    GLenum dstBlend;
    int alphaTest;
    GLenum alphaFunc;
    int alphaRefByte;
    int lighting;
    int colorMaterial;      // 0, or the material channel tracking glColor
    int fog;
    uint32_t fogColor;
    float fogStart;
    float fogEnd;
    int activeUnit;
    unsigned int boundTex[2];
    int texEnabled[2];
    GLint texEnvMode[2];
    int projectionValid;
    GLfloat projection[16];
};

// Lighting state is uploaded only when the game changes it. The view matrix
// is part of the key because GL transforms light positions to eye space when
// glLight* is called.
struct LightingCache {
    int valid;
    uint32_t ambient;
    int localViewer;
    int specularEnabled;
    GfxMaterial material;
    GfxLight lights[8];
    int lightEnabled[8];
    int lightSet[8];
    GfxMatrix view;
};

struct StageDiagnostics {
    int inStage;
    uint32_t frames;
    uint32_t drawCalls3D;
    uint32_t drawCallsTL;
    uint32_t triangles3D;
    uint32_t textureUploads;
    uint32_t suppressedRttDraws;
    uint32_t suppressedRttClears;
};

void *s_context;
int s_miniglOpen;
CapabilityPolicy s_caps;
GlStateCache s_glCache;
LightingCache s_lightingCache;
StageDiagnostics s_diag;
// Set when the game leaves the frontend's virtual screen for gameplay. The
// first 3D draw consumes it with one log line, so a crash log can tell scene
// construction apart from rendering.
int s_stageEntryPending;
unsigned int s_neutralReflectionTex;

StageState s_stages[2];
GfxTexture *s_boundTexture;
// MiniGL has no render-to-texture. While the game targets a texture (the car
// reflection cube faces) its draws and clears are dropped so they cannot land
// on the back buffer; the texture keeps its contents and consumers bind a
// neutral reflection texture.
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
int s_tlScreenActive;

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

static const int kMaxStoredLights = 64;
GfxLight s_lights[kMaxStoredLights];
int s_lightSet[kMaxStoredLights];
int s_lightEnabled[kMaxStoredLights];

GfxMaterial s_material;
int s_lightingEnabled = 1;
uint32_t s_ambient;
int s_specularEnabled;
int s_localViewer = 1;
int s_normalizeNormals;
int s_colorVertex = 1;
int s_diffuseSource = GFX_MCS_COLOR1;
int s_specularSource = GFX_MCS_COLOR2;
int s_ambientSource = GFX_MCS_MATERIAL;
int s_emissiveSource = GFX_MCS_MATERIAL;

uint32_t s_fogColor;
uint32_t s_textureFactor = 0xffffffffu;
int s_fogEnabled;
int s_fogVertexMode = GFX_FOG_NONE;
int s_fogTableMode = GFX_FOG_NONE;
float s_fogStart;
float s_fogEnd = 1.0f;
int s_depthTest = 1;
int s_depthWrite = 1;
int s_depthFunc = GFX_CMP_LESSEQUAL;
int s_alphaBlend;
int s_srcBlend = GFX_BLEND_ONE;
int s_dstBlend = GFX_BLEND_ZERO;
int s_alphaTest;
int s_alphaFunc = GFX_CMP_ALWAYS;
uint32_t s_alphaRef;
int s_cullMode = GFX_CULL_CCW;

struct SavedGfxState {
    int valid;
    GfxViewport viewport;
    int virtualW;
    int virtualH;
    GfxMatrix transforms[18];
    int transformSet[18];
    GfxMaterial material;
    int lightingEnabled;
    uint32_t ambient;
    int specularEnabled;
    int localViewer;
    int normalizeNormals;
    int colorVertex;
    int diffuseSource;
    int specularSource;
    int ambientSource;
    int emissiveSource;
    uint32_t fogColor;
    uint32_t textureFactor;
    int fogEnabled;
    int fogVertexMode;
    int fogTableMode;
    float fogStart;
    float fogEnd;
    int depthTest;
    int depthWrite;
    int depthFunc;
    int alphaBlend;
    int srcBlend;
    int dstBlend;
    int alphaTest;
    int alphaFunc;
    uint32_t alphaRef;
    int cullMode;
    StageState stages[2];
    GfxTexture *renderTarget;
};
SavedGfxState s_savedState;

enum DegradeLogKey {
    DEGRADE_LOG_ADDSIGNED = 0,
    DEGRADE_LOG_SUBTRACT = 1,
    DEGRADE_LOG_BUMPENVMAP = 2,
    DEGRADE_LOG_MODULATE_SCALE = 3,
    DEGRADE_LOG_COMPLEMENT_ARG = 4,
    DEGRADE_LOG_ALPHAREPLICATE_ARG = 5,
    DEGRADE_LOG_TEXGEN_REFLECTION = 6,
    DEGRADE_LOG_EXCESS_LIGHTS = 7,
    DEGRADE_LOG_DISTANCE_FADE = 8,
    DEGRADE_LOG_BAD_INDEX = 9,
    DEGRADE_LOG_COLOR_SOURCE = 10,
    DEGRADE_LOG_NORMALIZE = 11,
    DEGRADE_LOG_ALPHA_OP = 12,
    DEGRADE_LOG_SELECT_CURRENT = 13,
    DEGRADE_LOG_COUNT
};
uint32_t s_degradeLoggedMask;

void LogDegradationOnce(DegradeLogKey key, const char *message)
{
    const uint32_t bit = 1u << (unsigned int)key;
    if ((s_degradeLoggedMask & bit) != 0)
        return;
    s_degradeLoggedMask |= bit;
    Sys_Log("%s", message);
}

void Identity(GfxMatrix *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->_11 = matrix->_22 = matrix->_33 = matrix->_44 = 1.0f;
}

void ResetStageStates(void)
{
    for (int i = 0; i < 2; ++i) {
        memset(&s_stages[i], 0, sizeof(s_stages[i]));
        s_stages[i].colorOp = i == 0 ? GFX_TOP_MODULATE : GFX_TOP_DISABLE;
        s_stages[i].colorArg1 = GFX_TA_TEXTURE;
        s_stages[i].colorArg2 = GFX_TA_CURRENT;
        s_stages[i].alphaOp = i == 0 ? GFX_TOP_SELECTARG1 : GFX_TOP_DISABLE;
        s_stages[i].alphaArg1 = GFX_TA_TEXTURE;
        s_stages[i].alphaArg2 = GFX_TA_CURRENT;
        s_stages[i].address = GFX_ADDRESS_WRAP;
        s_stages[i].minFilter = GFX_FILTER_POINT;
        s_stages[i].magFilter = GFX_FILTER_POINT;
        s_stages[i].texCoordIndex = (uint32_t)i;
        s_stages[i].textureTransform = GFX_TTFF_DISABLE;
        s_stages[i].texture = NULL;
    }
    s_boundTexture = NULL;
}

void InvalidateLightingCache(void)
{
    s_lightingCache.valid = 0;
}

void InvalidateGlCache(void)
{
    memset(&s_glCache, 0, sizeof(s_glCache));
    s_glCache.depthTest = -1;
    s_glCache.depthWrite = -1;
    s_glCache.depthFunc = -1;
    s_glCache.cullFace = -1;
    s_glCache.cullMode = 0;
    s_glCache.frontFace = 0;
    s_glCache.alphaBlend = -1;
    s_glCache.srcBlend = 0;
    s_glCache.dstBlend = 0;
    s_glCache.alphaTest = -1;
    s_glCache.alphaFunc = 0;
    s_glCache.alphaRefByte = -1;
    s_glCache.lighting = -1;
    s_glCache.colorMaterial = -1;
    s_glCache.fog = -1;
    s_glCache.activeUnit = -1;
    s_glCache.boundTex[0] = ~0u;
    s_glCache.boundTex[1] = ~0u;
    s_glCache.texEnabled[0] = -1;
    s_glCache.texEnabled[1] = -1;
    s_glCache.texEnvMode[0] = 0;
    s_glCache.texEnvMode[1] = 0;
    s_glCache.projectionValid = 0;
    InvalidateLightingCache();
}

void FlushStageDiagnostics(const char *reason)
{
    if (!s_diag.inStage)
        return;
    Sys_Log("AMIGA: M3.2 stage summary (%s): frames=%u draws3d=%u drawsTL=%u tris3d=%u uploads=%u rttDropped=%u/%u",
            reason != NULL ? reason : "end",
            (unsigned)s_diag.frames,
            (unsigned)s_diag.drawCalls3D,
            (unsigned)s_diag.drawCallsTL,
            (unsigned)s_diag.triangles3D,
            (unsigned)s_diag.textureUploads,
            (unsigned)s_diag.suppressedRttDraws,
            (unsigned)s_diag.suppressedRttClears);
    memset(&s_diag, 0, sizeof(s_diag));
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

void SelectActiveUnit(int stage)
{
    if (s_caps.maxTexUnits <= 1)
        stage = 0;
    if (s_glCache.activeUnit != stage) {
        if (s_caps.maxTexUnits > 1)
            glActiveTextureARB(stage == 0 ? GL_TEXTURE0_ARB : GL_TEXTURE1_ARB);
        s_glCache.activeUnit = stage;
    }
}

void EnsureNeutralReflectionTexture(void)
{
    if (s_neutralReflectionTex != 0)
        return;
    glGenTextures(1, &s_neutralReflectionTex);
    if (s_neutralReflectionTex == 0)
        return;
    static const unsigned char kNeutralTexels[4 * 4 * 4] = {
        112, 118, 124, 255,  112, 118, 124, 255,  112, 118, 124, 255,  112, 118, 124, 255,
        104, 110, 116, 255,  104, 110, 116, 255,  104, 110, 116, 255,  104, 110, 116, 255,
         96, 100, 106, 255,   96, 100, 106, 255,   96, 100, 106, 255,   96, 100, 106, 255,
         88,  92,  96, 255,   88,  92,  96, 255,   88,  92,  96, 255,   88,  92,  96, 255,
    };
    SelectActiveUnit(0);
    glBindTexture(GL_TEXTURE_2D, s_neutralReflectionTex);
    s_glCache.boundTex[0] = s_neutralReflectionTex;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, kNeutralTexels);
}

void ProbeCapabilities(void)
{
    if (s_caps.probed)
        return;
    memset(&s_caps, 0, sizeof(s_caps));
    s_caps.probed = 1;

    while (glGetError() != GL_NO_ERROR) {}

    GLint units = 1;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units);
    if (glGetError() != GL_NO_ERROR || units < 1)
        units = 1;
    if (units > 2)
        units = 2;
    s_caps.maxTexUnits = (int)units;

    glFogi(GL_FOG_MODE, GL_LINEAR);
    s_caps.hasFogLinear = (glGetError() == GL_NO_ERROR) ? 1 : 0;

    glAlphaFunc(GL_GEQUAL, 0.5f);
    s_caps.hasAlphaTest = (glGetError() == GL_NO_ERROR) ? 1 : 0;

    SelectActiveUnit(0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    s_caps.hasModulate = (glGetError() == GL_NO_ERROR) ? 1 : 0;

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
    s_caps.hasAdd = (glGetError() == GL_NO_ERROR) ? 1 : 0;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    s_glCache.texEnvMode[0] = GL_MODULATE;

    glEnable(GL_COLOR_MATERIAL);
    if (glGetError() == GL_NO_ERROR) {
        glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
        s_caps.hasColorMaterial = (glGetError() == GL_NO_ERROR) ? 1 : 0;
        glDisable(GL_COLOR_MATERIAL);
    } else {
        s_caps.hasColorMaterial = 0;
    }

    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
    if (glGetError() == GL_NO_ERROR) {
        s_caps.hasBlendSubtract = 1;
        glBlendEquation(GL_FUNC_ADD);
    } else {
        s_caps.hasBlendSubtract = 0;
    }
    while (glGetError() != GL_NO_ERROR) {}

    // Tier selection: Tier 1 when 2+ texture units, GL_ADD, linear fog and
    // alpha test all probe cleanly; Tier 0 baseline otherwise.
    s_caps.tier = (s_caps.maxTexUnits >= 2 && s_caps.hasAdd && s_caps.hasFogLinear && s_caps.hasAlphaTest) ? 1 : 0;

    EnsureNeutralReflectionTexture();

    const char *vendor = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    Sys_Log("AMIGA: MiniGL capability snapshot: tier=%d units=%d modulate=%d add=%d fogLinear=%d alphaTest=%d colorMat=%d revSub=%d vendor=\"%s\" renderer=\"%s\" version=\"%s\"",
            s_caps.tier,
            s_caps.maxTexUnits,
            s_caps.hasModulate,
            s_caps.hasAdd,
            s_caps.hasFogLinear,
            s_caps.hasAlphaTest,
            s_caps.hasColorMaterial,
            s_caps.hasBlendSubtract,
            vendor != NULL ? vendor : "?",
            renderer != NULL ? renderer : "?",
            version != NULL ? version : "?");
    Sys_Log("AMIGA: M3.2 policy: RTT/cubemap=disabled(neutral) ADDSIGNED->%s SUBTRACT->SELECTARG1 BUMPENVMAP->MODULATE",
            s_caps.hasAdd ? "ADD" : "MODULATE");
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
    s_viewport.x = 0;
    s_viewport.y = 0;
    s_viewport.width = width;
    s_viewport.height = height;
    s_viewport.minZ = 0.0f;
    s_viewport.maxZ = 1.0f;
    s_virtualWidth = 0;
    s_virtualHeight = 0;
    for (int i = 0; i < 18; ++i) {
        Identity(&s_transforms[i]);
        s_transformSet[i] = 1;
    }
    memset(&s_material, 0, sizeof(s_material));
    s_material.diffuse.r = s_material.diffuse.g = s_material.diffuse.b = s_material.diffuse.a = 1.0f;
    s_material.ambient.r = s_material.ambient.g = s_material.ambient.b = s_material.ambient.a = 1.0f;
    ResetStageStates();
    InvalidateGlCache();
    s_caps.probed = 0;
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
    ProbeCapabilities();
    ApplyViewport();
    glShadeModel(GL_SMOOTH);
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
    FlushStageDiagnostics("shutdown");
    if (s_context != NULL) {
        s_boundTexture = NULL;
        s_renderTarget = NULL;
        s_stages[0].texture = NULL;
        s_stages[1].texture = NULL;
        if (s_neutralReflectionTex != 0 && EnsureLocked()) {
            glDeleteTextures(1, &s_neutralReflectionTex);
            s_neutralReflectionTex = 0;
        }
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
    s_neutralReflectionTex = 0;
    s_caps.probed = 0;
    InvalidateGlCache();
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
    if (s_diag.inStage)
        s_diag.frames++;
    s_tlScreenActive = 0;
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
    if (s_renderTarget != NULL) {
        s_diag.suppressedRttClears++;
        return;
    }
    if (!EnsureLocked())
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
    if (flags & GFX_CLEAR_ZBUFFER) {
        if (s_glCache.depthWrite != 1) {
            glDepthMask(GL_TRUE);
            s_glCache.depthWrite = 1;
        }
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if (bits != 0)
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
    glDepthRange((GLclampd)s_viewport.minZ, (GLclampd)s_viewport.maxZ);
}

// TL vertices are in screen coordinates over the whole screen, like the host
// executors' fixedtl shader, and are clipped (not scaled) by the game's
// viewport through the scissor test. The full-screen projection stays active
// until a 3D draw or a viewport/render-target change ends the TL batch.
void BeginScreenDraw(void)
{
    int screenW, screenH;
    GetScreenSize(&screenW, &screenH);
    glViewport(0, 0, s_width, s_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)screenW, (double)screenH, 0.0, -1.0, 1.0);
    // The 3D projection is no longer loaded; make the next 3D draw reload it.
    s_glCache.projectionValid = 0;
}


} // namespace

extern "C" void Gfx_SetViewport(const GfxViewport *viewport)
{
    if (viewport == NULL)
        return;
    s_viewport = *viewport;
    s_tlScreenActive = 0;
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
    s_tlScreenActive = 0;
    const bool wasMenus = s_virtualWidth > 0 && s_virtualHeight > 0;
    if (wasMenus && width == 0) {
        s_stageEntryPending = 1;
        Sys_Log("AMIGA: stage: gameplay screen entered");
    }
    if (width > 0 && height > 0 && s_diag.inStage) {
        // Returning from gameplay (virtual screen 0x0) to frontend menus.
        FlushStageDiagnostics("stage-exit");
    }
    s_virtualWidth = width;
    s_virtualHeight = height;
    // The stored viewport is in the screen's coordinates; remap it.
    if (s_renderTarget == NULL && s_context != NULL && EnsureLocked())
        ApplyViewport();
}

extern "C" void Gfx_PushState(void)
{
    s_savedState.valid = 1;
    s_savedState.viewport = s_viewport;
    s_savedState.virtualW = s_virtualWidth;
    s_savedState.virtualH = s_virtualHeight;
    memcpy(s_savedState.transforms, s_transforms, sizeof(s_transforms));
    memcpy(s_savedState.transformSet, s_transformSet, sizeof(s_transformSet));
    s_savedState.material = s_material;
    s_savedState.lightingEnabled = s_lightingEnabled;
    s_savedState.ambient = s_ambient;
    s_savedState.specularEnabled = s_specularEnabled;
    s_savedState.localViewer = s_localViewer;
    s_savedState.normalizeNormals = s_normalizeNormals;
    s_savedState.colorVertex = s_colorVertex;
    s_savedState.diffuseSource = s_diffuseSource;
    s_savedState.specularSource = s_specularSource;
    s_savedState.ambientSource = s_ambientSource;
    s_savedState.emissiveSource = s_emissiveSource;
    s_savedState.fogColor = s_fogColor;
    s_savedState.textureFactor = s_textureFactor;
    s_savedState.fogEnabled = s_fogEnabled;
    s_savedState.fogVertexMode = s_fogVertexMode;
    s_savedState.fogTableMode = s_fogTableMode;
    s_savedState.fogStart = s_fogStart;
    s_savedState.fogEnd = s_fogEnd;
    s_savedState.depthTest = s_depthTest;
    s_savedState.depthWrite = s_depthWrite;
    s_savedState.depthFunc = s_depthFunc;
    s_savedState.alphaBlend = s_alphaBlend;
    s_savedState.srcBlend = s_srcBlend;
    s_savedState.dstBlend = s_dstBlend;
    s_savedState.alphaTest = s_alphaTest;
    s_savedState.alphaFunc = s_alphaFunc;
    s_savedState.alphaRef = s_alphaRef;
    s_savedState.cullMode = s_cullMode;
    s_savedState.stages[0] = s_stages[0];
    s_savedState.stages[1] = s_stages[1];
    s_savedState.renderTarget = s_renderTarget;
}

extern "C" void Gfx_PopState(void)
{
    if (!s_savedState.valid)
        return;
    s_savedState.valid = 0;
    s_viewport = s_savedState.viewport;
    s_virtualWidth = s_savedState.virtualW;
    s_virtualHeight = s_savedState.virtualH;
    memcpy(s_transforms, s_savedState.transforms, sizeof(s_transforms));
    memcpy(s_transformSet, s_savedState.transformSet, sizeof(s_transformSet));
    s_material = s_savedState.material;
    s_lightingEnabled = s_savedState.lightingEnabled;
    s_ambient = s_savedState.ambient;
    s_specularEnabled = s_savedState.specularEnabled;
    s_localViewer = s_savedState.localViewer;
    s_normalizeNormals = s_savedState.normalizeNormals;
    s_colorVertex = s_savedState.colorVertex;
    s_diffuseSource = s_savedState.diffuseSource;
    s_specularSource = s_savedState.specularSource;
    s_ambientSource = s_savedState.ambientSource;
    s_emissiveSource = s_savedState.emissiveSource;
    s_fogColor = s_savedState.fogColor;
    s_textureFactor = s_savedState.textureFactor;
    s_fogEnabled = s_savedState.fogEnabled;
    s_fogVertexMode = s_savedState.fogVertexMode;
    s_fogTableMode = s_savedState.fogTableMode;
    s_fogStart = s_savedState.fogStart;
    s_fogEnd = s_savedState.fogEnd;
    s_depthTest = s_savedState.depthTest;
    s_depthWrite = s_savedState.depthWrite;
    s_depthFunc = s_savedState.depthFunc;
    s_alphaBlend = s_savedState.alphaBlend;
    s_srcBlend = s_savedState.srcBlend;
    s_dstBlend = s_savedState.dstBlend;
    s_alphaTest = s_savedState.alphaTest;
    s_alphaFunc = s_savedState.alphaFunc;
    s_alphaRef = s_savedState.alphaRef;
    s_cullMode = s_savedState.cullMode;
    s_stages[0] = s_savedState.stages[0];
    s_stages[1] = s_savedState.stages[1];
    s_boundTexture = s_stages[0].texture;
    s_renderTarget = s_savedState.renderTarget;
    InvalidateGlCache();
    if (s_renderTarget == NULL && s_context != NULL && EnsureLocked())
        ApplyViewport();
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
                    "(no car reflections; neutral fallback texture bound)");
        }
    }
    s_renderTarget = target;
    s_tlScreenActive = 0;
    // Back on the back buffer, restore the viewport the game set for it.
    if (target == NULL && s_context != NULL && EnsureLocked())
        ApplyViewport();
}

extern "C" void Gfx_CaptureBackdrop(void)
{
}

extern "C" void Gfx_SetDistanceFade(float start, float end)
{
    if (end > start) {
        LogDegradationOnce(DEGRADE_LOG_DISTANCE_FADE,
                           "AMIGA: distance-fade backdrop capture is disabled in M3.2; using fixed-function fog");
    }
}

extern "C" void Gfx_SetTransform(int type, const GfxMatrix *matrix)
{
    if (type < 0 || type >= 18)
        return;
    if (matrix != NULL) {
        s_transforms[type] = *matrix;
        s_transformSet[type] = 1;
        if (type == GFX_TRANSFORM_VIEW)
            InvalidateLightingCache();
        else if (type == GFX_TRANSFORM_PROJECTION)
            s_glCache.projectionValid = 0;
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
    if (index < 0 || light == NULL)
        return;
    if (index >= 8) {
        LogDegradationOnce(DEGRADE_LOG_EXCESS_LIGHTS,
                           "AMIGA: scene light index >= 8 clamped to GL_LIGHT0..GL_LIGHT7");
    }
    if (index < kMaxStoredLights) {
        s_lights[index] = *light;
        s_lightSet[index] = 1;
        InvalidateLightingCache();
    }
}

extern "C" void Gfx_GetLight(int index, GfxLight *light)
{
    if (index < 0 || light == NULL)
        return;
    if (index < kMaxStoredLights && s_lightSet[index])
        *light = s_lights[index];
    else
        memset(light, 0, sizeof(*light));
}

extern "C" void Gfx_EnableLight(int index, int enable)
{
    if (index < 0)
        return;
    if (index >= 8 && enable != 0) {
        LogDegradationOnce(DEGRADE_LOG_EXCESS_LIGHTS,
                           "AMIGA: scene light index >= 8 clamped to GL_LIGHT0..GL_LIGHT7");
    }
    if (index < kMaxStoredLights) {
        s_lightEnabled[index] = enable != 0;
        InvalidateLightingCache();
    }
}

extern "C" void Gfx_SetMaterial(const GfxMaterial *material)
{
    if (material != NULL) {
        s_material = *material;
        InvalidateLightingCache();
    }
}

extern "C" void Gfx_SetLighting(int enable)
{
    s_lightingEnabled = enable != 0;
}
extern "C" void Gfx_SetAmbient(uint32_t color)
{
    s_ambient = color;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetSpecular(int enable)
{
    s_specularEnabled = enable != 0;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetLocalViewer(int enable)
{
    s_localViewer = enable != 0;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetNormalizeNormals(int enable) { s_normalizeNormals = enable != 0; }
extern "C" void Gfx_SetColorVertex(int enable) { s_colorVertex = enable != 0; }
extern "C" void Gfx_SetDiffuseMaterialSource(int source)
{
    s_diffuseSource = source;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetSpecularMaterialSource(int source)
{
    s_specularSource = source;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetAmbientMaterialSource(int source)
{
    s_ambientSource = source;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetEmissiveMaterialSource(int source)
{
    s_emissiveSource = source;
    InvalidateLightingCache();
}

extern "C" void Gfx_SetFog(int enable) { s_fogEnabled = enable != 0; }
extern "C" void Gfx_SetFogColor(uint32_t color) { s_fogColor = color; }
extern "C" void Gfx_SetFogVertexMode(int mode) { s_fogVertexMode = mode; }
extern "C" void Gfx_SetFogTableMode(int mode) { s_fogTableMode = mode; }
extern "C" void Gfx_SetFogStart(float start) { s_fogStart = start; }
extern "C" void Gfx_SetFogEnd(float end) { s_fogEnd = end; }

extern "C" void Gfx_SetDepthTest(int enable) { s_depthTest = enable != 0; }
extern "C" void Gfx_SetDepthWrite(int enable) { s_depthWrite = enable != 0; }
extern "C" void Gfx_SetDepthFunc(int compare) { s_depthFunc = compare; }
extern "C" void Gfx_SetCullMode(int cull) { s_cullMode = cull; }
extern "C" void Gfx_SetAlphaBlend(int enable) { s_alphaBlend = enable != 0; }
extern "C" void Gfx_SetSrcBlend(int blend) { s_srcBlend = blend; }
extern "C" void Gfx_SetDestBlend(int blend) { s_dstBlend = blend; }
extern "C" void Gfx_SetAlphaTest(int enable) { s_alphaTest = enable != 0; }
extern "C" void Gfx_SetAlphaFunc(int compare) { s_alphaFunc = compare; }
extern "C" void Gfx_SetAlphaRef(uint32_t reference) { s_alphaRef = reference & 0xffu; }
extern "C" void Gfx_SetTextureFactor(uint32_t color) { s_textureFactor = color; }

static void ApplyStageSampler(int stage, GfxTexture *object, unsigned int texId);

static GfxTexture *ResolveDrawnTexture(GfxTexture *texture)
{
    if (texture == NULL)
        return NULL;
    if (texture->parent != NULL)
        texture = texture->parent;
    return texture->replacement != NULL ? texture->replacement : texture;
}

extern "C" void Gfx_SetTexture(int stage, GfxTexture *texture)
{
    if (stage < 0 || stage >= 2)
        return;
    GfxTexture *drawn = ResolveDrawnTexture(texture);
    s_stages[stage].texture = drawn;
    if (stage == 0)
        s_boundTexture = drawn;
    // Without the display lock nothing can be uploaded or bound; record that
    // no texture is bound on stage 0 so the TL path never reads a stale one.
    if (!EnsureLocked()) {
        if (stage == 0)
            s_boundTexture = NULL;
        return;
    }
    if (s_caps.probed && stage >= s_caps.maxTexUnits)
        return;
    // Select the unit first: uploads and the neutral fallback operate on the
    // active unit, and the fallback switches to unit 0 when it is created.
    SelectActiveUnit(stage);
    unsigned int texId = 0;
    if (drawn != NULL) {
        if ((drawn->flags & (GFX_TEXTURE_CUBE | GFX_TEXTURE_RENDER_TARGET)) != 0) {
            EnsureNeutralReflectionTexture();
            SelectActiveUnit(stage);
            texId = s_neutralReflectionTex;
        } else {
            if (drawn->dirty)
                UploadTexture(drawn);
            texId = drawn->glId;
        }
        if (texId == 0)
            drawn = NULL;
    }
    if (stage == 0)
        s_boundTexture = drawn;
    if (drawn == NULL) {
        if (s_glCache.texEnabled[stage] != 0) {
            glDisable(GL_TEXTURE_2D);
            s_glCache.texEnabled[stage] = 0;
        }
        s_glCache.boundTex[stage] = 0;
        return;
    }
    if (s_glCache.texEnabled[stage] != 1) {
        glEnable(GL_TEXTURE_2D);
        s_glCache.texEnabled[stage] = 1;
    }
    if (s_glCache.boundTex[stage] != texId) {
        glBindTexture(GL_TEXTURE_2D, texId);
        s_glCache.boundTex[stage] = texId;
    }
    ApplyStageSampler(stage, drawn, texId);
}

extern "C" void Gfx_SetStageColorOp(int stage, int op)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].colorOp = op;
}
extern "C" void Gfx_SetStageColorArg1(int stage, int arg)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].colorArg1 = arg;
}
extern "C" void Gfx_SetStageColorArg2(int stage, int arg)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].colorArg2 = arg;
}
extern "C" void Gfx_SetStageAlphaOp(int stage, int op)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].alphaOp = op;
}
extern "C" void Gfx_SetStageAlphaArg1(int stage, int arg)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].alphaArg1 = arg;
}
extern "C" void Gfx_SetStageAlphaArg2(int stage, int arg)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].alphaArg2 = arg;
}

static GLint StageWrapMode(int stage)
{
    switch (s_stages[stage].address) {
    case GFX_ADDRESS_CLAMP: return GL_CLAMP_TO_EDGE;
    case GFX_ADDRESS_MIRROR: return GL_REPEAT;
    default: return GL_REPEAT;
    }
}

static GLint FilterMode(int filter)
{
    return filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
}

// Applies the stage sampler state to the texture object currently bound on
// that stage's texture unit. GL keeps wrap and filter per texture object, so
// the last applied values are cached on the object and unchanged draws issue
// no glTexParameteri calls. Mipmaps remain off by default in M3.2.
static void ApplyStageSampler(int stage, GfxTexture *object, unsigned int texId)
{
    const GLint wrap = StageWrapMode(stage);
    const GLint minFilter = FilterMode(s_stages[stage].minFilter);
    const GLint magFilter = FilterMode(s_stages[stage].magFilter);
    const bool tracked = object != NULL && texId != 0 && texId == object->glId;
    if (tracked && object->samplerValid && object->samplerWrap == wrap &&
        object->samplerMinFilter == minFilter && object->samplerMagFilter == magFilter)
        return;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    if (tracked) {
        object->samplerValid = 1;
        object->samplerWrap = wrap;
        object->samplerMinFilter = minFilter;
        object->samplerMagFilter = magFilter;
    }
}

// A sampler change reaches the texture already bound; textures bound later
// pick the stored state up in Gfx_SetTexture.
static void ReapplyStageSampler(int stage)
{
    if (stage < 0 || stage >= 2)
        return;
    if (s_caps.probed && stage >= s_caps.maxTexUnits)
        return;
    GfxTexture *texture = s_stages[stage].texture;
    if (texture == NULL || !EnsureLocked())
        return;
    SelectActiveUnit(stage);
    unsigned int texId;
    if ((texture->flags & (GFX_TEXTURE_CUBE | GFX_TEXTURE_RENDER_TARGET)) != 0) {
        EnsureNeutralReflectionTexture();
        SelectActiveUnit(stage);
        texId = s_neutralReflectionTex;
    } else {
        if (texture->dirty)
            UploadTexture(texture);
        texId = texture->glId;
    }
    if (texId == 0)
        return;
    if (s_glCache.boundTex[stage] != texId) {
        glBindTexture(GL_TEXTURE_2D, texId);
        s_glCache.boundTex[stage] = texId;
    }
    ApplyStageSampler(stage, texture, texId);
}

extern "C" void Gfx_SetStageAddress(int stage, int address)
{
    if (stage < 0 || stage >= 2 || address == s_stages[stage].address)
        return;
    s_stages[stage].address = address;
    ReapplyStageSampler(stage);
}
extern "C" void Gfx_SetStageMinFilter(int stage, int filter)
{
    if (stage < 0 || stage >= 2 || filter == s_stages[stage].minFilter)
        return;
    s_stages[stage].minFilter = filter;
    ReapplyStageSampler(stage);
}
extern "C" void Gfx_SetStageMagFilter(int stage, int filter)
{
    if (stage < 0 || stage >= 2 || filter == s_stages[stage].magFilter)
        return;
    s_stages[stage].magFilter = filter;
    ReapplyStageSampler(stage);
}
// Mipmaps and bump-environment mapping are outside the M3.2 policy: textures
// upload one level and BUMPENVMAP degrades to MODULATE. The entry points still
// accept the calls so the game's state cache stays consistent.
extern "C" void Gfx_SetStageMipFilter(int stage, int filter)
{
    (void)stage;
    (void)filter;
}
extern "C" void Gfx_SetStageMaxMipLevel(int stage, int level)
{
    (void)stage;
    (void)level;
}
extern "C" void Gfx_SetStageTexCoordIndex(int stage, uint32_t index)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].texCoordIndex = index;
}
extern "C" void Gfx_SetStageTextureTransform(int stage, int flags)
{
    if (stage >= 0 && stage < 2)
        s_stages[stage].textureTransform = flags;
}
extern "C" void Gfx_SetStageBumpEnvMat(int stage, int index, float value)
{
    (void)stage;
    (void)index;
    (void)value;
}
extern "C" void Gfx_SetStageBumpEnvLScale(int stage, float value)
{
    (void)stage;
    (void)value;
}
extern "C" void Gfx_SetStageBumpEnvLOffset(int stage, float value)
{
    (void)stage;
    (void)value;
}

extern "C" GfxVertexBuffer *Gfx_CreateVertexBuffer(uint32_t vertexCount)
{
    GfxVertexBuffer *buffer = new (std::nothrow) GfxVertexBuffer;
    if (buffer == NULL)
        return NULL;
    buffer->count = vertexCount;
    buffer->vertices = NULL;
    if (vertexCount != 0) {
        buffer->vertices = new (std::nothrow) GfxVertex[vertexCount];
        if (buffer->vertices == NULL) {
            delete buffer;
            return NULL;
        }
        memset(buffer->vertices, 0, (size_t)vertexCount * sizeof(GfxVertex));
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

namespace {

GLenum MapBlendFactor(int d3d)
{
    switch (d3d) {
    case GFX_BLEND_ZERO: return GL_ZERO;
    case GFX_BLEND_ONE: return GL_ONE;
    case GFX_BLEND_SRCCOLOR: return GL_SRC_COLOR;
    case GFX_BLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case GFX_BLEND_SRCALPHA: return GL_SRC_ALPHA;
    case GFX_BLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case GFX_BLEND_DESTALPHA: return GL_DST_ALPHA;
    case GFX_BLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case GFX_BLEND_DESTCOLOR: return GL_DST_COLOR;
    case GFX_BLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case GFX_BLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

GLenum MapCompareFunc(int d3d)
{
    switch (d3d) {
    case GFX_CMP_NEVER: return GL_NEVER;
    case GFX_CMP_LESS: return GL_LESS;
    case GFX_CMP_EQUAL: return GL_EQUAL;
    case GFX_CMP_LESSEQUAL: return GL_LEQUAL;
    case GFX_CMP_GREATER: return GL_GREATER;
    case GFX_CMP_NOTEQUAL: return GL_NOTEQUAL;
    case GFX_CMP_GREATEREQUAL: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

bool MapPrimitiveMode(int primitive, GLenum *outMode)
{
    switch (primitive) {
    case GFX_POINTLIST: *outMode = GL_POINTS; return true;
    case GFX_LINELIST: *outMode = GL_LINES; return true;
    case GFX_LINESTRIP: *outMode = GL_LINE_STRIP; return true;
    case GFX_TRIANGLELIST: *outMode = GL_TRIANGLES; return true;
    case GFX_TRIANGLESTRIP: *outMode = GL_TRIANGLE_STRIP; return true;
    case GFX_TRIANGLEFAN: *outMode = GL_TRIANGLE_FAN; return true;
    default: return false;
    }
}

void UnpackColorFloats(uint32_t argb, float out[4])
{
    out[0] = ((argb >> 16) & 0xffu) / 255.0f;
    out[1] = ((argb >> 8) & 0xffu) / 255.0f;
    out[2] = (argb & 0xffu) / 255.0f;
    out[3] = ((argb >> 24) & 0xffu) / 255.0f;
}

void MultiplyRowMatrices(const GfxMatrix &a, const GfxMatrix &b, GfxMatrix *out)
{
    const float *pa = &a._11;
    const float *pb = &b._11;
    float res[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            res[r * 4 + c] = pa[r * 4 + 0] * pb[0 * 4 + c] +
                             pa[r * 4 + 1] * pb[1 * 4 + c] +
                             pa[r * 4 + 2] * pb[2 * 4 + c] +
                             pa[r * 4 + 3] * pb[3 * 4 + c];
        }
    }
    memcpy(out, res, sizeof(res));
}

// D3D row-vector matrix (v' = v * M, translation in _41.._43) to GL
// column-major array: M_gl = M_d3d^T, so reading row-major floats directly
// supplies the exact column-major layout glLoadMatrixf expects.
// For projection, D3D maps Z_ndc in [0, 1] while OpenGL clips Z_ndc in [-1, 1]
// before mapping to [minZ, maxZ], so column 2 (row/col z output) is remapped
// with z_clip' = 2 * z_clip - w_clip.
void BuildGlProjectionMatrix(const GfxMatrix &proj, GLfloat out[16])
{
    const float *m = &proj._11;
    for (int i = 0; i < 16; ++i)
        out[i] = m[i];
    for (int r = 0; r < 4; ++r)
        out[r * 4 + 2] = 2.0f * m[r * 4 + 2] - m[r * 4 + 3];
}

void Apply3DTransforms(GfxMatrix *outWorldView)
{
    GfxMatrix world, view, proj;
    if (s_transformSet[GFX_TRANSFORM_WORLD])
        world = s_transforms[GFX_TRANSFORM_WORLD];
    else
        Identity(&world);
    if (s_transformSet[GFX_TRANSFORM_VIEW])
        view = s_transforms[GFX_TRANSFORM_VIEW];
    else
        Identity(&view);
    if (s_transformSet[GFX_TRANSFORM_PROJECTION])
        proj = s_transforms[GFX_TRANSFORM_PROJECTION];
    else
        Identity(&proj);

    MultiplyRowMatrices(world, view, outWorldView);

    // The projection changes per camera/pass, not per draw. It is also
    // replaced by the 2D ortho projection, so BeginScreenDraw and the
    // TL-to-3D transition clear the flag.
    GLfloat glProj[16];
    BuildGlProjectionMatrix(proj, glProj);
    if (!s_glCache.projectionValid || memcmp(s_glCache.projection, glProj, sizeof(glProj)) != 0) {
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(glProj);
        memcpy(s_glCache.projection, glProj, sizeof(glProj));
        s_glCache.projectionValid = 1;
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(&outWorldView->_11);
}

// Blend and alpha test are the same render states for 3D and the TL path, so
// both apply them from the game's current values through one cached helper.
void ApplyBlendAndAlphaTest(void)
{
    const int wantBlend = s_alphaBlend ? 1 : 0;
    if (s_glCache.alphaBlend != wantBlend) {
        if (wantBlend)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        s_glCache.alphaBlend = wantBlend;
    }
    if (wantBlend) {
        const GLenum src = MapBlendFactor(s_srcBlend);
        const GLenum dst = MapBlendFactor(s_dstBlend);
        if (s_glCache.srcBlend != src || s_glCache.dstBlend != dst) {
            glBlendFunc(src, dst);
            s_glCache.srcBlend = src;
            s_glCache.dstBlend = dst;
        }
    }

    const int wantAlphaTest = (s_alphaTest && s_caps.hasAlphaTest) ? 1 : 0;
    if (s_glCache.alphaTest != wantAlphaTest) {
        if (wantAlphaTest)
            glEnable(GL_ALPHA_TEST);
        else
            glDisable(GL_ALPHA_TEST);
        s_glCache.alphaTest = wantAlphaTest;
    }
    if (wantAlphaTest) {
        const GLenum afunc = MapCompareFunc(s_alphaFunc);
        const int aref = (int)(s_alphaRef & 0xffu);
        if (s_glCache.alphaFunc != afunc || s_glCache.alphaRefByte != aref) {
            glAlphaFunc(afunc, (GLclampf)(aref / 255.0f));
            s_glCache.alphaFunc = afunc;
            s_glCache.alphaRefByte = aref;
        }
    }
}

void Apply3DRasterState(void)
{
    const int wantDepthTest = s_depthTest ? 1 : 0;
    if (s_glCache.depthTest != wantDepthTest) {
        if (wantDepthTest)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        s_glCache.depthTest = wantDepthTest;
    }
    const int wantDepthWrite = (s_depthTest && s_depthWrite) ? 1 : 0;
    if (s_glCache.depthWrite != wantDepthWrite) {
        glDepthMask(wantDepthWrite ? GL_TRUE : GL_FALSE);
        s_glCache.depthWrite = wantDepthWrite;
    }
    if (wantDepthTest) {
        const GLenum func = MapCompareFunc(s_depthFunc);
        if (s_glCache.depthFunc != (int)func) {
            glDepthFunc(func);
            s_glCache.depthFunc = (int)func;
        }
    }

    // Direct3D's window origin is top-left and OpenGL's bottom-left, so the
    // same NDC triangle winds the other way on screen: a Direct3D clockwise
    // front face arrives counter-clockwise in OpenGL. This matches the host
    // executor's FrontFace(GL_CCW) and its cull mapping below.
    if (s_glCache.frontFace != GL_CCW) {
        glFrontFace(GL_CCW);
        s_glCache.frontFace = GL_CCW;
    }
    const int wantCull = (s_cullMode == GFX_CULL_CW || s_cullMode == GFX_CULL_CCW) ? 1 : 0;
    if (s_glCache.cullFace != wantCull) {
        if (wantCull)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
        s_glCache.cullFace = wantCull;
    }
    if (wantCull) {
        const GLenum cullFace = s_cullMode == GFX_CULL_CW ? GL_FRONT : GL_BACK;
        if (s_glCache.cullMode != cullFace) {
            glCullFace(cullFace);
            s_glCache.cullMode = cullFace;
        }
    }

    ApplyBlendAndAlphaTest();
}

// Locked M3.2 lighting policy:
// - lighting off (modes 4/5): prelit, GL lighting is disabled and the vertex
//   colour goes straight to glColor.
// - lighting on: GL fixed-function lighting with up to 8 lights. D3D material
//   sources that name a vertex colour are expressed through GL_COLOR_MATERIAL.
//   The game uses one such source at a time: emissive=COLOR1 for the baked
//   world meshes (modes 0/1) and diffuse=COLOR1 for mode 3.
bool Apply3DLightingAndFog(void)
{
    const bool usePrelitPath = !s_lightingEnabled;
    const int wantGlLighting = usePrelitPath ? 0 : 1;

    if (s_glCache.lighting != wantGlLighting) {
        if (wantGlLighting)
            glEnable(GL_LIGHTING);
        else
            glDisable(GL_LIGHTING);
        s_glCache.lighting = wantGlLighting;
    }

    if (wantGlLighting) {
        // One material channel can track glColor; pick the source the game
        // actually routes through a vertex colour.
        GLenum colorMaterial = 0;
        if (s_colorVertex && s_caps.hasColorMaterial) {
            int vertexSources = 0;
            if (s_diffuseSource == GFX_MCS_COLOR1) {
                colorMaterial = GL_DIFFUSE;
                ++vertexSources;
            } else if (s_emissiveSource == GFX_MCS_COLOR1) {
                colorMaterial = GL_EMISSION;
                ++vertexSources;
            }
            if (s_ambientSource == GFX_MCS_COLOR1)
                ++vertexSources;
            if (s_specularSource == GFX_MCS_COLOR1)
                ++vertexSources;
            if (vertexSources > 1) {
                LogDegradationOnce(DEGRADE_LOG_COLOR_SOURCE,
                                   "AMIGA: vertex colour feeds more than one material source; GL tracks one");
            }
            if (s_specularEnabled &&
                (s_specularSource == GFX_MCS_COLOR1 || s_specularSource == GFX_MCS_COLOR2)) {
                LogDegradationOnce(DEGRADE_LOG_COLOR_SOURCE,
                                   "AMIGA: per-vertex specular colour is not available in GL fixed function");
            }
            if (s_diffuseSource == GFX_MCS_COLOR2 || s_ambientSource == GFX_MCS_COLOR2 ||
                s_emissiveSource == GFX_MCS_COLOR2) {
                LogDegradationOnce(DEGRADE_LOG_COLOR_SOURCE,
                                   "AMIGA: vertex colour 2 as a material source is not available in GL");
            }
        } else if (s_colorVertex &&
                   (s_diffuseSource == GFX_MCS_COLOR1 || s_ambientSource == GFX_MCS_COLOR1 ||
                    s_emissiveSource == GFX_MCS_COLOR1)) {
            LogDegradationOnce(DEGRADE_LOG_COLOR_SOURCE,
                               "AMIGA: GL_COLOR_MATERIAL unavailable; vertex material colour ignored");
        }
        if (s_glCache.colorMaterial != (int)colorMaterial) {
            if (colorMaterial != 0) {
                glEnable(GL_COLOR_MATERIAL);
                glColorMaterial(GL_FRONT_AND_BACK, colorMaterial);
            } else {
                glDisable(GL_COLOR_MATERIAL);
            }
            s_glCache.colorMaterial = (int)colorMaterial;
            InvalidateLightingCache();
        }

        // MiniGL deliberately has no GL_NORMALIZE: its lighting shader uses
        // the normal transform without a per-vertex renormalisation. The D3D
        // state is accepted but cannot be honoured; log it once.
        if (s_normalizeNormals) {
            LogDegradationOnce(DEGRADE_LOG_NORMALIZE,
                               "AMIGA: GL_NORMALIZE is not implemented by MiniGL; normals are used as supplied");
        }

        GfxMatrix view;
        if (s_transformSet[GFX_TRANSFORM_VIEW])
            view = s_transforms[GFX_TRANSFORM_VIEW];
        else
            Identity(&view);
        if (!s_lightingCache.valid ||
            memcmp(&s_lightingCache.view, &view, sizeof(view)) != 0 ||
            s_lightingCache.ambient != s_ambient ||
            s_lightingCache.localViewer != s_localViewer ||
            s_lightingCache.specularEnabled != s_specularEnabled ||
            memcmp(&s_lightingCache.material, &s_material, sizeof(s_material)) != 0 ||
            memcmp(s_lightingCache.lights, s_lights, sizeof(s_lightingCache.lights)) != 0 ||
            memcmp(s_lightingCache.lightEnabled, s_lightEnabled, sizeof(s_lightingCache.lightEnabled)) != 0 ||
            memcmp(s_lightingCache.lightSet, s_lightSet, sizeof(s_lightingCache.lightSet)) != 0) {
            float globalAmb[4];
            UnpackColorFloats(s_ambient, globalAmb);
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmb);
            glLightModelf(GL_LIGHT_MODEL_LOCAL_VIEWER, s_localViewer ? 1.0f : 0.0f);

            const bool colorDiffuse = colorMaterial == GL_DIFFUSE;
            const bool colorEmissive = colorMaterial == GL_EMISSION;
            const float matDiffuse[4] = {
                s_material.diffuse.r, s_material.diffuse.g, s_material.diffuse.b, s_material.diffuse.a
            };
            const float matAmbient[4] = {
                s_material.ambient.r, s_material.ambient.g, s_material.ambient.b, s_material.ambient.a
            };
            // Without specular the material specular is black, which is how
            // the game's GL_SetSpecular(0) default is expressed here.
            const float matSpecular[4] = {
                s_specularEnabled ? s_material.specular.r : 0.0f,
                s_specularEnabled ? s_material.specular.g : 0.0f,
                s_specularEnabled ? s_material.specular.b : 0.0f,
                s_specularEnabled ? s_material.specular.a : 0.0f
            };
            const float matEmissive[4] = {
                s_material.emissive.r, s_material.emissive.g, s_material.emissive.b, s_material.emissive.a
            };
            if (!colorDiffuse)
                glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, matDiffuse);
            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, matAmbient);
            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matSpecular);
            if (!colorEmissive)
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, matEmissive);
            const float shininess = s_material.power < 0.0f ? 0.0f : (s_material.power > 128.0f ? 128.0f : s_material.power);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);

            // GL transforms light positions by the modelview in force when
            // glLight* is called; the stored eye-space position survives later
            // modelview changes, so this runs only when lighting changed.
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadMatrixf(&view._11);

            for (int i = 0; i < 8; ++i) {
                const GLenum lightEnum = (GLenum)(GL_LIGHT0 + i);
                if (!s_lightEnabled[i] || !s_lightSet[i]) {
                    glDisable(lightEnum);
                    continue;
                }
                const GfxLight &l = s_lights[i];
                glEnable(lightEnum);
                const float lDiff[4] = { l.diffuse.r, l.diffuse.g, l.diffuse.b, l.diffuse.a };
                const float lAmb[4] = { l.ambient.r, l.ambient.g, l.ambient.b, l.ambient.a };
                const float lSpec[4] = { l.specular.r, l.specular.g, l.specular.b, l.specular.a };
                glLightfv(lightEnum, GL_DIFFUSE, lDiff);
                glLightfv(lightEnum, GL_AMBIENT, lAmb);
                glLightfv(lightEnum, GL_SPECULAR, lSpec);

                if (l.type == GFX_LIGHT_DIRECTIONAL) {
                    const float pos[4] = { -l.direction.x, -l.direction.y, -l.direction.z, 0.0f };
                    glLightfv(lightEnum, GL_POSITION, pos);
                    glLightf(lightEnum, GL_SPOT_CUTOFF, 180.0f);
                } else {
                    const float pos[4] = { l.position.x, l.position.y, l.position.z, 1.0f };
                    glLightfv(lightEnum, GL_POSITION, pos);
                    glLightf(lightEnum, GL_CONSTANT_ATTENUATION, l.attenuation0 > 0.0f ? l.attenuation0 : 1.0f);
                    glLightf(lightEnum, GL_LINEAR_ATTENUATION, l.attenuation1 >= 0.0f ? l.attenuation1 : 0.0f);
                    glLightf(lightEnum, GL_QUADRATIC_ATTENUATION, l.attenuation2 >= 0.0f ? l.attenuation2 : 0.0f);
                    if (l.type == GFX_LIGHT_SPOT) {
                        const float dir[3] = { l.direction.x, l.direction.y, l.direction.z };
                        glLightfv(lightEnum, GL_SPOT_DIRECTION, dir);
                        float cutoffDeg = l.phi * (180.0f / 3.14159265f) * 0.5f;
                        if (cutoffDeg < 0.0f) cutoffDeg = 0.0f;
                        if (cutoffDeg > 90.0f) cutoffDeg = 90.0f;
                        glLightf(lightEnum, GL_SPOT_CUTOFF, cutoffDeg);
                        glLightf(lightEnum, GL_SPOT_EXPONENT, l.falloff >= 0.0f ? l.falloff : 0.0f);
                    } else {
                        glLightf(lightEnum, GL_SPOT_CUTOFF, 180.0f);
                    }
                }
            }
            glPopMatrix();

            s_lightingCache.valid = 1;
            s_lightingCache.ambient = s_ambient;
            s_lightingCache.localViewer = s_localViewer;
            s_lightingCache.specularEnabled = s_specularEnabled;
            s_lightingCache.material = s_material;
            memcpy(s_lightingCache.lights, s_lights, sizeof(s_lightingCache.lights));
            memcpy(s_lightingCache.lightEnabled, s_lightEnabled, sizeof(s_lightingCache.lightEnabled));
            memcpy(s_lightingCache.lightSet, s_lightSet, sizeof(s_lightingCache.lightSet));
            s_lightingCache.view = view;
        }
    }

    const bool activeFogMode = s_fogTableMode == GFX_FOG_LINEAR || s_fogVertexMode == GFX_FOG_LINEAR;
    const int wantFog = (s_fogEnabled && activeFogMode && s_caps.hasFogLinear && s_fogEnd > s_fogStart) ? 1 : 0;
    if (s_glCache.fog != wantFog) {
        if (wantFog)
            glEnable(GL_FOG);
        else
            glDisable(GL_FOG);
        s_glCache.fog = wantFog;
    }
    if (wantFog) {
        if (s_glCache.fogColor != s_fogColor) {
            float fogRgba[4];
            UnpackColorFloats(s_fogColor, fogRgba);
            glFogfv(GL_FOG_COLOR, fogRgba);
            s_glCache.fogColor = s_fogColor;
        }
        if (s_glCache.fogStart != s_fogStart || s_glCache.fogEnd != s_fogEnd) {
            glFogi(GL_FOG_MODE, GL_LINEAR);
            glFogf(GL_FOG_START, s_fogStart);
            glFogf(GL_FOG_END, s_fogEnd);
            s_glCache.fogStart = s_fogStart;
            s_glCache.fogEnd = s_fogEnd;
        }
    }

    return usePrelitPath;
}

GLint MapStageEnvMode(int stage)
{
    const StageState &st = s_stages[stage];
    if ((st.colorArg1 & GFX_TA_COMPLEMENT) != 0 || (st.colorArg2 & GFX_TA_COMPLEMENT) != 0) {
        LogDegradationOnce(DEGRADE_LOG_COMPLEMENT_ARG,
                           "AMIGA: GFX_TA_COMPLEMENT degraded to uninverted stage argument");
    }
    if ((st.colorArg1 & GFX_TA_ALPHAREPLICATE) != 0 || (st.colorArg2 & GFX_TA_ALPHAREPLICATE) != 0) {
        LogDegradationOnce(DEGRADE_LOG_ALPHAREPLICATE_ARG,
                           "AMIGA: GFX_TA_ALPHAREPLICATE degraded to standard RGBA modulate");
    }
    // GL 1.1 texture environments apply one operation to RGB and alpha, so a
    // D3D stage whose alpha arguments differ from its colour arguments cannot
    // be expressed exactly. Cases where the operations match (the common
    // MODULATE texture x current) are exact.
    if (st.alphaOp != st.colorOp) {
        LogDegradationOnce(DEGRADE_LOG_ALPHA_OP,
                           "AMIGA: separate alpha stage operation approximated by the colour operation");
    }

    switch (st.colorOp) {
    case GFX_TOP_SELECTARG1: {
        const int a1 = st.colorArg1 & 0xf;
        if (a1 != GFX_TA_TEXTURE) {
            LogDegradationOnce(DEGRADE_LOG_SELECT_CURRENT,
                               "AMIGA: SELECTARG with a non-texture argument degraded to MODULATE");
            return GL_MODULATE;
        }
        return GL_REPLACE;
    }
    case GFX_TOP_SELECTARG2: {
        const int a2 = st.colorArg2 & 0xf;
        if (a2 != GFX_TA_TEXTURE) {
            LogDegradationOnce(DEGRADE_LOG_SELECT_CURRENT,
                               "AMIGA: SELECTARG with a non-texture argument degraded to MODULATE");
            return GL_MODULATE;
        }
        return GL_REPLACE;
    }
    case GFX_TOP_MODULATE:
        return GL_MODULATE;
    case GFX_TOP_MODULATE2X:
    case GFX_TOP_MODULATE4X:
        LogDegradationOnce(DEGRADE_LOG_MODULATE_SCALE,
                           "AMIGA: MODULATE2X/4X degraded to GL_MODULATE");
        return GL_MODULATE;
    case GFX_TOP_ADD:
        return s_caps.hasAdd ? GL_ADD : GL_MODULATE;
    case GFX_TOP_ADDSIGNED:
    case GFX_TOP_ADDSIGNED2X:
        LogDegradationOnce(DEGRADE_LOG_ADDSIGNED,
                           "AMIGA: ADDSIGNED degraded to GL_ADD (per M3.2 policy)");
        return s_caps.hasAdd ? GL_ADD : GL_MODULATE;
    case GFX_TOP_SUBTRACT:
        LogDegradationOnce(DEGRADE_LOG_SUBTRACT,
                           "AMIGA: SUBTRACT degraded to SELECTARG1/REPLACE (per M3.2 policy)");
        return (st.colorArg1 & 0xf) == GFX_TA_TEXTURE ? GL_REPLACE : GL_MODULATE;
    case GFX_TOP_BUMPENVMAP:
    case GFX_TOP_BUMPENVMAPLUMINANCE:
        LogDegradationOnce(DEGRADE_LOG_BUMPENVMAP,
                           "AMIGA: BUMPENVMAP perturbation disabled; using base MODULATE path (per M3.2 policy)");
        return GL_MODULATE;
    default:
        return GL_MODULATE;
    }
}

// Binds textures and configures texture environment for stages 0 and 1.
// Returns the number of active texture units bound for the draw (0, 1 or 2).
int Apply3DTextureStages(void)
{
    int activeStages = 0;
    const int maxUnits = s_caps.probed ? s_caps.maxTexUnits : 1;

    for (int stage = 0; stage < 2; ++stage) {
        if (stage >= maxUnits)
            break;
        const StageState &st = s_stages[stage];
        SelectActiveUnit(stage);

        const bool stageDisabled = (stage == 0 && st.colorOp == GFX_TOP_DISABLE) ||
                                   (stage == 1 && (s_stages[0].colorOp == GFX_TOP_DISABLE || st.colorOp == GFX_TOP_DISABLE)) ||
                                   st.texture == NULL;
        if (stageDisabled) {
            if (s_glCache.texEnabled[stage] != 0) {
                glDisable(GL_TEXTURE_2D);
                s_glCache.texEnabled[stage] = 0;
            }
            continue;
        }

        GfxTexture *drawn = st.texture;
        unsigned int texId = 0;
        if ((drawn->flags & (GFX_TEXTURE_CUBE | GFX_TEXTURE_RENDER_TARGET)) != 0) {
            // The fallback initializer selects unit 0, so re-select this one.
            EnsureNeutralReflectionTexture();
            SelectActiveUnit(stage);
            texId = s_neutralReflectionTex;
        } else {
            if (drawn->dirty)
                UploadTexture(drawn);
            texId = drawn->glId;
        }

        if (texId == 0) {
            if (s_glCache.texEnabled[stage] != 0) {
                glDisable(GL_TEXTURE_2D);
                s_glCache.texEnabled[stage] = 0;
            }
            continue;
        }

        if (s_glCache.texEnabled[stage] != 1) {
            glEnable(GL_TEXTURE_2D);
            s_glCache.texEnabled[stage] = 1;
        }
        if (s_glCache.boundTex[stage] != texId) {
            glBindTexture(GL_TEXTURE_2D, texId);
            s_glCache.boundTex[stage] = texId;
        }
        ApplyStageSampler(stage, drawn, texId);

        const GLint envMode = MapStageEnvMode(stage);
        if (s_glCache.texEnvMode[stage] != envMode) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, envMode);
            s_glCache.texEnvMode[stage] = envMode;
        }
        activeStages = stage + 1;
    }

    if (maxUnits > 1 && s_glCache.activeUnit != 0)
        SelectActiveUnit(0);

    return activeStages;
}

void ResetPost3DStages(void)
{
    if (s_caps.maxTexUnits > 1) {
        SelectActiveUnit(1);
        if (s_glCache.texEnabled[1] != 0) {
            glDisable(GL_TEXTURE_2D);
            s_glCache.texEnabled[1] = 0;
        }
        SelectActiveUnit(0);
    }
    if (s_glCache.texEnvMode[0] != GL_MODULATE) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        s_glCache.texEnvMode[0] = GL_MODULATE;
    }
}

void ComputeStageTexCoord(int stage, const GfxVertex *v, const GfxMatrix &worldView, float *outU, float *outV)
{
    const StageState &st = s_stages[stage];
    float u = 0.0f;
    float t = 0.0f;
    float r = 1.0f;

    if ((st.texCoordIndex & 0xffff0000u) == GFX_TCI_CAMERASPACEREFLECTIONVECTOR) {
        LogDegradationOnce(DEGRADE_LOG_TEXGEN_REFLECTION,
                           "AMIGA: reflection texgen mapped to eye-space normal spherical lookup with neutral fallback");
        const float nx = v->nx * worldView._11 + v->ny * worldView._21 + v->nz * worldView._31;
        const float ny = v->nx * worldView._12 + v->ny * worldView._22 + v->nz * worldView._32;
        u = nx * 0.5f + 0.5f;
        t = 0.5f - ny * 0.5f;
    } else if ((st.texCoordIndex & 0xffffu) == 1u) {
        u = v->u1;
        t = v->v1;
    } else {
        u = v->u0;
        t = v->v0;
    }

    const int xformType = stage == 0 ? GFX_TRANSFORM_TEXTURE0 : GFX_TRANSFORM_TEXTURE1;
    if (st.textureTransform != GFX_TTFF_DISABLE && s_transformSet[xformType]) {
        const GfxMatrix &tm = s_transforms[xformType];
        const float tu = u * tm._11 + t * tm._21 + r * tm._31 + tm._41;
        const float tv = u * tm._12 + t * tm._22 + r * tm._32 + tm._42;
        u = tu;
        t = tv;
    }

    *outU = u;
    *outV = t;
}

// MiniGL draws a glBegin..glEnd block from a fixed vertex buffer and silently
// drops the rest of a larger batch. Splitting at a whole primitive is enough
// for lists. Strips and fans are expanded to independent primitives first so a
// split cannot change the winding or the shared fan centre.
GLenum FlattenedMode(GLenum mode)
{
    if (mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN)
        return GL_TRIANGLES;
    if (mode == GL_LINE_STRIP)
        return GL_LINES;
    return mode;
}

uint32_t FlattenedCount(GLenum mode, uint32_t count)
{
    if (mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN)
        return count >= 3u ? (count - 2u) * 3u : 0u;
    if (mode == GL_LINE_STRIP)
        return count >= 2u ? (count - 1u) * 2u : 0u;
    return count;
}

// Source vertex for output vertex i of the flattened primitive list. A strip
// reverses every odd triangle, which is what its winding rule does.
uint32_t FlattenedSource(GLenum mode, uint32_t i)
{
    const uint32_t tri = i / 3u;
    const uint32_t corner = i % 3u;
    switch (mode) {
    case GL_TRIANGLE_STRIP:
        if ((tri & 1u) == 0)
            return tri + corner;
        return corner == 0u ? tri + 1u : (corner == 1u ? tri : tri + 2u);
    case GL_TRIANGLE_FAN:
        return corner == 0u ? 0u : tri + corner;
    case GL_LINE_STRIP:
        return (i / 2u) + (i % 2u);
    default:
        return i;
    }
}

uint32_t BlockVertexCount(GLenum flattened)
{
    if (flattened == GL_TRIANGLES)
        return ((uint32_t)kMaxBlockVertices / 3u) * 3u;
    if (flattened == GL_LINES)
        return ((uint32_t)kMaxBlockVertices / 2u) * 2u;
    return (uint32_t)kMaxBlockVertices;
}

struct EmitPlan {
    bool emitColor;       // glColor feeds the prelit path or GL_COLOR_MATERIAL
    bool overrideColor;   // stage argument selects the texture factor
    bool overrideAlpha;
    uint32_t color;
};

// Draw-invariant stage-zero colour selection, computed once per draw rather
// than per vertex.
EmitPlan BuildEmitPlan(bool prelit)
{
    const StageState &s0 = s_stages[0];
    const bool colorFactor =
        (s0.colorOp == GFX_TOP_SELECTARG1 && (s0.colorArg1 & 0xf) == GFX_TA_TFACTOR) ||
        (s0.colorOp == GFX_TOP_SELECTARG2 && (s0.colorArg2 & 0xf) == GFX_TA_TFACTOR);
    const bool alphaFactor =
        (s0.alphaOp == GFX_TOP_SELECTARG1 && (s0.alphaArg1 & 0xf) == GFX_TA_TFACTOR) ||
        (s0.alphaOp == GFX_TOP_SELECTARG2 && (s0.alphaArg2 & 0xf) == GFX_TA_TFACTOR) ||
        (s0.alphaOp == GFX_TOP_MODULATE &&
         (((s0.alphaArg1 & 0xf) == GFX_TA_TFACTOR) || ((s0.alphaArg2 & 0xf) == GFX_TA_TFACTOR)));

    EmitPlan plan;
    // The vertex colour is the emitted colour in the prelit path and the
    // material source when GL_COLOR_MATERIAL tracks it.
    plan.emitColor = prelit || s_glCache.colorMaterial != 0;
    plan.overrideColor = prelit && colorFactor;
    plan.overrideAlpha = prelit && alphaFactor;
    plan.color = s_textureFactor;
    return plan;
}

void Emit3DVertex(const GfxVertex *v, bool lit, const EmitPlan &plan, int activeStages,
                  const GfxMatrix &worldView)
{
    if (activeStages >= 1) {
        float u0, v0;
        ComputeStageTexCoord(0, v, worldView, &u0, &v0);
        glTexCoord2f(u0, v0);
    }
    if (activeStages >= 2) {
        float u1, v1;
        ComputeStageTexCoord(1, v, worldView, &u1, &v1);
        glMultiTexCoord2fARB(GL_TEXTURE1_ARB, u1, v1);
    }
    if (plan.emitColor) {
        const uint32_t rgb = plan.overrideColor ? plan.color : v->diffuse;
        const uint32_t alpha = plan.overrideAlpha ? plan.color : v->diffuse;
        glColor4ub((GLubyte)((rgb >> 16) & 0xffu), (GLubyte)((rgb >> 8) & 0xffu),
                   (GLubyte)(rgb & 0xffu), (GLubyte)((alpha >> 24) & 0xffu));
    }
    if (lit)
        glNormal3f(v->nx, v->ny, v->nz);
    glVertex3f(v->x, v->y, v->z);
}

void EmitTLVertex(const GfxTLVertex *v)
{
    glColor4ub((GLubyte)((v->color >> 16) & 0xffu), (GLubyte)((v->color >> 8) & 0xffu),
               (GLubyte)(v->color & 0xffu), (GLubyte)((v->color >> 24) & 0xffu));
    glTexCoord2f(v->tu, v->tv);
    glVertex2f(v->sx, v->sy);
}

void Submit3DBlocks(GLenum mode, const GfxVertex *vertices, const uint16_t *indices,
                    uint32_t sourceCount, const EmitPlan &plan, bool lit, int activeStages,
                    const GfxMatrix &worldView)
{
    const GLenum flattened = FlattenedMode(mode);
    const uint32_t total = FlattenedCount(mode, sourceCount);
    const uint32_t block = BlockVertexCount(flattened);
    for (uint32_t first = 0; first < total; first += block) {
        const uint32_t end = (total - first) > block ? first + block : total;
        glBegin(flattened);
        for (uint32_t i = first; i < end; ++i) {
            const uint32_t src = FlattenedSource(mode, i);
            Emit3DVertex(indices != NULL ? &vertices[indices[src]] : &vertices[src],
                         lit, plan, activeStages, worldView);
        }
        glEnd();
    }
}

void SubmitTLBlocks(GLenum mode, const GfxTLVertex *vertices, const uint16_t *indices,
                    uint32_t sourceCount)
{
    const GLenum flattened = FlattenedMode(mode);
    const uint32_t total = FlattenedCount(mode, sourceCount);
    const uint32_t block = BlockVertexCount(flattened);
    for (uint32_t first = 0; first < total; first += block) {
        const uint32_t end = (total - first) > block ? first + block : total;
        glBegin(flattened);
        for (uint32_t i = first; i < end; ++i) {
            const uint32_t src = FlattenedSource(mode, i);
            EmitTLVertex(indices != NULL ? &vertices[indices[src]] : &vertices[src]);
        }
        glEnd();
    }
}

bool ValidateIndices(const uint16_t *indices, uint32_t indexCount, uint32_t vertexCount)
{
    if (indices == NULL)
        return true;
    for (uint32_t i = 0; i < indexCount; ++i) {
        if (indices[i] >= vertexCount) {
            LogDegradationOnce(DEGRADE_LOG_BAD_INDEX,
                               "AMIGA: mesh index out of range; draw skipped");
            return false;
        }
    }
    return true;
}

uint32_t CountTriangles(int primitive, uint32_t count)
{
    switch (primitive) {
    case GFX_TRIANGLELIST: return count / 3u;
    case GFX_TRIANGLESTRIP:
    case GFX_TRIANGLEFAN: return count >= 3u ? count - 2u : 0u;
    default: return 0u;
    }
}

void Submit3DDraw(int primitive, const GfxVertex *vertices, uint32_t vertexCount,
                  const uint16_t *indices, uint32_t indexCount)
{
    if (vertices == NULL || vertexCount == 0)
        return;
    if (indices != NULL && indexCount == 0)
        return;
    if (s_renderTarget != NULL) {
        s_diag.suppressedRttDraws++;
        return;
    }
    GLenum mode;
    if (!MapPrimitiveMode(primitive, &mode))
        return;
    if (!ValidateIndices(indices, indexCount, vertexCount))
        return;
    if (!EnsureLocked())
        return;
    if (!s_caps.probed)
        ProbeCapabilities();

    s_diag.inStage = 1;
    if (s_stageEntryPending) {
        s_stageEntryPending = 0;
        Sys_Log("AMIGA: stage: first 3D draw primitive=%d vertices=%u indices=%u",
                primitive, (unsigned)vertexCount, (unsigned)indexCount);
    }
    s_diag.drawCalls3D++;
    s_diag.triangles3D += CountTriangles(primitive, indices != NULL ? indexCount : vertexCount);

    if (s_tlScreenActive) {
        s_tlScreenActive = 0;
        ApplyViewport();
    }
    GfxMatrix worldView;
    Apply3DTransforms(&worldView);
    Apply3DRasterState();
    const bool prelit = Apply3DLightingAndFog();
    const int activeStages = Apply3DTextureStages();
    const EmitPlan plan = BuildEmitPlan(prelit);
    Submit3DBlocks(mode, vertices, indices, indices != NULL ? indexCount : vertexCount,
                   plan, !prelit, activeStages, worldView);
}

// Matches stage zero's GL state to s_boundTexture before a 2D draw. A 3D pass
// can leave unit 0 with texturing disabled or with a texture environment that
// is not the modulate the TL path expects.
void SyncStage0ForTL(void)
{
    GfxTexture *texture = s_boundTexture;
    unsigned int texId = 0;
    if (texture != NULL) {
        if ((texture->flags & (GFX_TEXTURE_CUBE | GFX_TEXTURE_RENDER_TARGET)) != 0) {
            EnsureNeutralReflectionTexture();
            SelectActiveUnit(0);
            texId = s_neutralReflectionTex;
        } else {
            if (texture->dirty)
                UploadTexture(texture);
            texId = texture->glId;
        }
    }
    if (texId == 0) {
        if (s_glCache.texEnabled[0] != 0) {
            glDisable(GL_TEXTURE_2D);
            s_glCache.texEnabled[0] = 0;
        }
        s_glCache.boundTex[0] = 0;
    } else {
        if (s_glCache.texEnabled[0] != 1) {
            glEnable(GL_TEXTURE_2D);
            s_glCache.texEnabled[0] = 1;
        }
        if (s_glCache.boundTex[0] != texId) {
            glBindTexture(GL_TEXTURE_2D, texId);
            s_glCache.boundTex[0] = texId;
        }
        ApplyStageSampler(0, texture, texId);
    }
    if (s_glCache.texEnvMode[0] != GL_MODULATE) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        s_glCache.texEnvMode[0] = GL_MODULATE;
    }
}

// The 2D/TL path is unlit, untextured by stage state and draws over the whole
// screen; the game's viewport still clips it through the scissor test.
void BeginTLDraw(void)
{
    if (s_glCache.lighting != 0) {
        glDisable(GL_LIGHTING);
        s_glCache.lighting = 0;
    }
    if (s_glCache.colorMaterial != 0) {
        glDisable(GL_COLOR_MATERIAL);
        s_glCache.colorMaterial = 0;
    }
    if (s_glCache.fog != 0) {
        glDisable(GL_FOG);
        s_glCache.fog = 0;
    }
    if (s_glCache.depthTest != 0) {
        glDisable(GL_DEPTH_TEST);
        s_glCache.depthTest = 0;
    }
    if (s_glCache.cullFace != 0) {
        glDisable(GL_CULL_FACE);
        s_glCache.cullFace = 0;
    }
    ApplyBlendAndAlphaTest();
    ResetPost3DStages();
    SelectActiveUnit(0);
    SyncStage0ForTL();
    if (!s_tlScreenActive) {
        BeginScreenDraw();
        s_tlScreenActive = 1;
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

} // namespace

extern "C" void Gfx_DrawPrimitiveVB(int primitive, GfxVertexBuffer *buffer, uint32_t firstVertex, uint32_t vertexCount)
{
    if (buffer == NULL || buffer->vertices == NULL || vertexCount == 0)
        return;
    if (firstVertex >= buffer->count)
        return;
    const uint32_t avail = buffer->count - firstVertex;
    const uint32_t clampedCount = vertexCount > avail ? avail : vertexCount;
    Submit3DDraw(primitive, buffer->vertices + firstVertex, clampedCount, NULL, 0);
}

extern "C" void Gfx_DrawIndexedPrimitiveVB(int primitive, GfxVertexBuffer *buffer, uint32_t firstVertex,
                                           uint32_t vertexCount, const uint16_t *indices, uint32_t indexCount)
{
    if (buffer == NULL || buffer->vertices == NULL || indices == NULL || indexCount == 0)
        return;
    if (firstVertex >= buffer->count)
        return;
    const uint32_t avail = buffer->count - firstVertex;
    const uint32_t clampedCount = (vertexCount == 0 || vertexCount > avail) ? avail : vertexCount;
    Submit3DDraw(primitive, buffer->vertices + firstVertex, clampedCount, indices, indexCount);
}

extern "C" void Gfx_DrawPrimitive(int primitive, const GfxVertex *vertices, uint32_t vertexCount)
{
    Submit3DDraw(primitive, vertices, vertexCount, NULL, 0);
}

extern "C" void Gfx_DrawIndexedPrimitive(int primitive, const GfxVertex *vertices, uint32_t vertexCount,
                                         const uint16_t *indices, uint32_t indexCount)
{
    if (indices == NULL || indexCount == 0)
        return;
    Submit3DDraw(primitive, vertices, vertexCount, indices, indexCount);
}

extern "C" void Gfx_DrawPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount)
{
    if (vertices == NULL || vertexCount == 0)
        return;
    if (s_renderTarget != NULL) {
        s_diag.suppressedRttDraws++;
        return;
    }
    GLenum mode;
    if (!MapPrimitiveMode(primitive, &mode))
        return;
    if (!EnsureLocked())
        return;
    if (!s_caps.probed)
        ProbeCapabilities();
    if (s_diag.inStage)
        s_diag.drawCallsTL++;

    BeginTLDraw();
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
        s_glCache.texEnabled[0] = 0;
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
        s_glCache.texEnabled[0] = 1;
        return;
    }
    SubmitTLBlocks(mode, vertices, NULL, vertexCount);
}

extern "C" void Gfx_DrawIndexedPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount,
                                           const uint16_t *indices, uint32_t indexCount)
{
    if (vertices == NULL || indices == NULL || indexCount == 0)
        return;
    if (s_renderTarget != NULL) {
        s_diag.suppressedRttDraws++;
        return;
    }
    GLenum mode;
    if (!MapPrimitiveMode(primitive, &mode))
        return;
    if (!ValidateIndices(indices, indexCount, vertexCount))
        return;
    if (!EnsureLocked())
        return;
    if (!s_caps.probed)
        ProbeCapabilities();
    if (s_diag.inStage)
        s_diag.drawCallsTL++;

    BeginTLDraw();
    SubmitTLBlocks(mode, vertices, indices, indexCount);
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
    const int unit = s_glCache.activeUnit >= 0 && s_glCache.activeUnit < 2 ? s_glCache.activeUnit : 0;
    glBindTexture(GL_TEXTURE_2D, texture->glId);
    s_glCache.boundTex[unit] = texture->glId;
    ApplyStageSampler(unit, texture, texture->glId);
    unsigned char *rgba = TextureToRGBA(texture);
    if (rgba != NULL) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        s_diag.textureUploads++;
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
    const GfxTexture *rep = texture->replacement;
    const bool wasBound0 = s_boundTexture == texture || (rep != NULL && s_boundTexture == rep) ||
                           s_stages[0].texture == texture || (rep != NULL && s_stages[0].texture == rep);
    const bool wasBound1 = s_stages[1].texture == texture || (rep != NULL && s_stages[1].texture == rep);
    if (wasBound0) {
        s_boundTexture = NULL;
        s_stages[0].texture = NULL;
    }
    if (wasBound1)
        s_stages[1].texture = NULL;
    if (texture->parent == NULL) {
        if (texture->replacement != NULL) {
            Gfx_DestroyTexture(texture->replacement);
            texture->replacement = NULL;
        }
        if (texture->glId != 0 && s_context != NULL) {
            for (int u = 0; u < 2; ++u) {
                if (s_glCache.boundTex[u] == texture->glId)
                    s_glCache.boundTex[u] = ~0u;
            }
            const int wasLocked = s_locked;
            if (EnsureLocked()) {
                if (wasBound0) {
                    SelectActiveUnit(0);
                    glDisable(GL_TEXTURE_2D);
                    s_glCache.texEnabled[0] = 0;
                }
                if (wasBound1 && s_caps.maxTexUnits > 1) {
                    SelectActiveUnit(1);
                    glDisable(GL_TEXTURE_2D);
                    s_glCache.texEnabled[1] = 0;
                    SelectActiveUnit(0);
                }
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
