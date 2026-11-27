// MiniGL renderer executor.
//
// Owns the MiniGL context/window, frame lifecycle, 2D TL rendering, and the
// capability-tiered 3D fixed-function pipeline.
// The Amiga binary links this backend and the GLES2 one behind
// render/gfx_dispatch_amiga.cpp: the definitions below are MiniGL_Gfx_*.
#define OPENCMR2_GFX_PREFIX_MINIGL
#include "render/gfx_api_prefix.h"

#include "port/backdrop_fog.h"
#include "port/bc_decode.h"
#include "port/bump_luminance.h"
#include "port/stage_passthrough.h"
#include "port/light_range.h"
#include "port/d3d_light.h"
#include "port/gfx.h"
#include "render/minigl/gfx_minigl_movie.h"
#include "port/sys.h"
#include "platform/amiga/amiga_sys.h"
#if defined(OPENCMR2_WARPOS)
#include "platform/warpos/wos_pcprof.h"
#endif
#include "platform/amiga/amiga_modes.h"
#include "platform/amiga/amiga_minigl_notice.h"
#include "port/display_modes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <new>

#include "platform/amiga/amiga_os_begin.h"
#include <exec/types.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include "platform/amiga/amiga_os_end.h"
// The PPC MiniGL's own structures keep the compiler's layout: outside the block.
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
    // Stream textures only (GfxMiniGL_CreateStreamTexture): the size of the GL
    // texture, the next powers of two above width and height.
    int glWidth;
    int glHeight;
    // Set until the first GfxMiniGL_UploadRows, which then stores the whole
    // frame (the GL name may be a reused one holding an older frame).
    int streamFresh;
    // The grey luminance texture a BUMPENVMAPLUMINANCE stage draws in this
    // texture's place (port/bump_luminance.h), and the scale and offset it was
    // built with; rebuilt after an upload of this texture.
    unsigned int lumGlId;
    int lumValid;
    float lumScale;
    float lumOffset;
    // PORT: AMIGA: every top-level texture is on this list so a context
    // rebuild (windowed resolution change, window/fullscreen switch) can drop
    // the old context's GL names and re-upload from the CPU copy.
    GfxTexture *liveNext;
    GfxTexture *livePrev;
    int live;
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
// Client arrays. GLDrawElements chunks internally at
// min(VertexBufferSize, MGL_MAXVERTS=1024) (gl/src/vertexelements.c), so the
// renderer submits one call per draw and does not chunk itself. Every absolute
// index must stay below 16384 because the array pipeline transforms by
// absolute index.
enum { kArrayMaxIndex = 16384, kArrayFastVertexLimit = 1024 };

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
    // Client arrays: probed once, and only when requested through
    // video.client_arrays. `clientArrayUnits` is how many texcoord arrays the
    // library accepts (unit 1 is unproven on MiniGL 29.x).
    int clientArrays;
    int clientArrayUnits;
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
    float bumpLumScale;
    float bumpLumOffset;
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
    // Matrix uploads are keyed on generation counters rather than on comparing
    // the matrices: the game rewrites WORLD per object, so a value compare
    // would cost more than the load it saves. A ~0u generation means "not
    // loaded by us" (another path wrote the GL matrix).
    GfxMatrix worldView;
    uint32_t loadedWorldViewGen;
    uint32_t loadedProjectionGen;
    GLfloat glProjection[16];
#if defined(OPENCMR2_WARPOS)
    // world*view*projection for the batched path's view-volume test, keyed on
    // both generations (clipValid is cleared with the rest of the cache).
    GfxMatrix clip;
    uint32_t clipWorldViewGen;
    uint32_t clipProjectionGen;
    int clipValid;
#endif
    // glEnableClientState bits kept across draws so a draw that needs the same
    // arrays costs no dispatch calls (bit values: kClientArray* below).
    uint32_t clientArrayMask;
    // GL_LIGHT0..7 enabled for the last lit draw; ~0 forces every light to be set.
    uint32_t lightMask;
};

// Lighting state is uploaded only when a writer invalidates it. Every setter
// that changes light, material or view state calls InvalidateLightingCache()
// (the view matters because GL transforms light positions to eye space when
// glLight* is called), so the upload itself needs no copy of the inputs.
struct LightingCache {
    int valid;
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
    // Plain increments only (no clock or logging in the draw path).
    uint32_t modelviewLoads;
    uint32_t modelviewSkips;
    uint32_t matrixModeSwitches;
    uint32_t projectionLoads;
    uint32_t lightingUploads;
    uint32_t lightingSkips;
    uint32_t tlMatrixLoads;
    uint32_t tlMatrixSkips;
    uint32_t stageSetterCalls;
    uint32_t lightingSetterCalls;
    uint32_t indicesValidated;
#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
    // Section totals in whole microseconds (integers only; the 68k target
    // does not print floats). Accumulated per call, printed once per flush.
    uint32_t timingSubmit3DCalls;
    uint64_t timingSubmit3DUs;
    uint32_t timingTLCalls;
    uint64_t timingTLUs;
    uint32_t timingPresentCalls;
    uint64_t timingPresentUs;
#endif
    // Draw-size histogram for the client-array decision: draws
    // bucketed by index count (or vertex count for non-indexed draws), plus how
    // many are plain GL_TRIANGLES, which is the only primitive MiniGL accepts
    // through the array pipeline.
    uint32_t drawSizeHistogram[7];
    uint32_t triangleIndexedDraws;
    uint32_t trianglePlainDraws;
    uint32_t otherPrimitiveDraws;
    // Eligibility: plain indexed triangles at or above librw's
    // 12-index threshold, split by whether the direct-texcoord fast path also
    // allows a client-array submission (no reflection texgen, no texture
    // transform). Draws the array path could not take are counted separately.
    uint32_t arrayEligibleDraws;
    uint32_t arrayEligibleIndices;
    uint32_t arrayRejectedDraws;
    // Draws actually submitted through glDrawElements, and the indices they
    // carried, so the A/B can prove the path was taken.
    uint32_t arrayDraws;
    uint32_t arrayIndices;
    // Draws whose referenced vertex span exceeds the library's 1024-vertex
    // fast-path limit, i.e. the ones compaction would help.
    uint32_t arraySpanOverLimitDraws;
    // Bumped by every FlushStageDiagnostics() reset. The benchmark window takes
    // a delta of this struct, so a flush inside the window is detectable.
    uint32_t flushSerial;
};

#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
// Converts Sys_GetTicksPrecise() milliseconds to whole microseconds. The
// double-to-integer conversion truncates; resolution is one microsecond.
uint64_t SectionUsFromMs(double ms)
{
    return ms <= 0.0 ? 0u : (uint64_t)(ms * 1000.0);
}
#endif

void *s_context;
int s_miniglOpen;
// video.client_arrays is read once when the context is created; it must also
// size the array scratch before MiniGL allocates it.
int s_clientArraysRequested;
CapabilityPolicy s_caps;
GlStateCache s_glCache;
LightingCache s_lightingCache;
StageDiagnostics s_diag;

// The clamping wrap mode. The R200 PPC stack knows GL_REPEAT and GL_CLAMP only
// and answers GL_CLAMP_TO_EDGE with GL_INVALID_ENUM, leaving the texture
// repeating (its porting guide maps one to the other).
#if defined(OPENCMR2_WARPOS)
const GLint kClampWrap = GL_CLAMP;
#else
const GLint kClampWrap = GL_CLAMP_TO_EDGE;
#endif

#if defined(OPENCMR2_WARPOS)
// ---- Draw batching (WarpOS) -------------------------------------------------
// On the R200 PPC stack a small draw costs about 17 us, any GL state change
// between two draws about 10 us more, and the game issues 500-700 draws a
// frame, two thirds of them four triangles or fewer (measured with
// drawbench on the reference A4000). So consecutive triangle draws stay inside
// one glBegin/glEnd, which is closed only when a GL call other than vertex
// emission is made. To keep a new object matrix from being such a call, 3D
// vertices and normals are transformed by world*view here and the GL
// modelview stays the identity. [video] batch = 0 turns this off.
//
// The emission calls of the batch itself are the Raw* functions, defined
// before the GL names are wrapped below.
int s_batching;
int s_modelviewIdentity;
struct OpenBatch {
    int open;
    GLenum mode;
    int format;
    uint32_t count;
} s_batch;
struct BatchStats {
    uint32_t draws, batches, vertices, presents, litDraws, litVertices, litByLights;
    uint32_t directionalLights, positionalLights, spotLights, powCalls, inRange, sortedDraws;
    uint32_t whyNot[6], bucketFlushes, culledTriangles, clippedTriangles;
} s_batchStats;
enum { kBatchMaxVertices = 3072 };  // whole triangles, below the 4096-vertex buffer

inline void RawBegin(GLenum mode) { glBegin(mode); }
inline void RawEnd(void) { glEnd(); }
inline void RawVertex3f(float x, float y, float z) { glVertex3f(x, y, z); }
inline void RawVertex2f(float x, float y) { glVertex2f(x, y); }
inline void RawNormal3f(float x, float y, float z) { glNormal3f(x, y, z); }
inline void RawTexCoord2f(float u, float v) { glTexCoord2f(u, v); }
inline void RawTexCoord1_2f(float u, float v) { glMultiTexCoord2fARB(GL_TEXTURE1_ARB, u, v); }
inline void RawColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4ub(r, g, b, a); }

// [video] profile = 1: where a frame's 3D draw time goes, from the PPC time
// base (low word; sections are far shorter than its 170 s wrap).
int s_profile;
enum { kProfApply, kProfLight, kProfEmit, kProfEnd, kProfTL, kProfLightLoop, kProfCount };
uint32_t s_profTicks[kProfCount];
inline uint32_t ProfNow(void)
{
    uint32_t lo;
    __asm__ volatile("mftb %0" : "=r"(lo));
    return lo;
}
struct ProfScope {
    int bucket;
    uint32_t t0;
    explicit ProfScope(int b) : bucket(b), t0(s_profile ? ProfNow() : 0) {}
    ~ProfScope()
    {
        if (s_profile)
            s_profTicks[bucket] += ProfNow() - t0;
    }
};

inline void BatchEnd(void)
{
    if (s_profile) {
        const uint32_t t0 = ProfNow();
        RawEnd();
        s_profTicks[kProfEnd] += ProfNow() - t0;
    } else {
        RawEnd();
    }
    s_batch.open = 0;
}

// Texture-sorted buckets ([video] batch_sort, default 1). The game changes
// texture on almost every draw, so batches kept in submission order hold one
// draw each. Opaque, depth-tested, depth-writing triangle draws that are
// already lit do not depend on their order: their finished vertices are
// stored per texture pair and sent as one batch per pair, in the order the
// pairs first appeared, when any other GL call or an order-dependent draw
// (blended, 2D) comes. s_texCapture makes the texture-state calls of
// Apply3DTextureStages no-ops so the wanted textures can be read from the GL
// cache without touching GL (CaptureTextureStages).
int s_batchSort;
int s_batchSortBlended;
int s_batchArrays;        // [video] batch_arrays: buckets go out as glDrawElements
int s_batchArraysChecked;
int s_cpuCull;            // [video] cpu_cull: back-face culling before the buckets
int s_cpuClip;            // [video] cpu_clip: drop triangles outside the view
int s_texCapture;
int s_inBucketFlush;
uint32_t s_bucketVertexTotal;
void FlushBuckets(void);

inline void BatchFlush(void)
{
    if (s_bucketVertexTotal != 0 && !s_inBucketFlush)
        FlushBuckets();
    if (s_batch.open)
        BatchEnd();
}

// [video] batch_log = 1: which source lines close batches (the GL call that
// follows them), to find state that is re-sent for every draw.
int s_batchLog;
int s_cpuLightAll;
enum { kBatchLogLines = 6144 };
uint16_t s_batchClosedAt[kBatchLogLines];
inline void BatchFlushAt(int line)
{
    if (s_bucketVertexTotal != 0 && !s_inBucketFlush)
        FlushBuckets();
    if (s_batch.open) {
        if (s_batchLog && line < (int)kBatchLogLines && s_batchClosedAt[line] != 0xffffu)
            s_batchClosedAt[line]++;
        BatchEnd();
    }
}

// Opens or continues a triangle batch of this vertex format; returns how many
// more vertices it takes (a multiple of three).
inline uint32_t BatchBegin(int format)
{
    if (s_bucketVertexTotal != 0 && !s_inBucketFlush)
        FlushBuckets();
    if (s_batch.open && s_batch.format == format && s_batch.count + 3u <= (uint32_t)kBatchMaxVertices)
        return (uint32_t)kBatchMaxVertices - s_batch.count;
    BatchFlush();
    RawBegin(GL_TRIANGLES);
    s_batch.open = 1;
    s_batch.mode = GL_TRIANGLES;
    s_batch.format = format;
    s_batch.count = 0;
    s_batchStats.batches++;
    return (uint32_t)kBatchMaxVertices;
}

// Every other GL entry point the renderer uses closes the batch first. The
// dispatch header maps each gl*/mgl* name to an MGLD_* function.
#undef glActiveTextureARB
#define glActiveTextureARB(...) (s_texCapture ? (void)0 : (void)(BatchFlushAt(__LINE__), MGLD_glActiveTextureARB(__VA_ARGS__)))
#undef glAlphaFunc
#define glAlphaFunc(...) (BatchFlushAt(__LINE__), MGLD_glAlphaFunc(__VA_ARGS__))
#undef glBegin
#define glBegin(...) (BatchFlushAt(__LINE__), MGLD_glBegin(__VA_ARGS__))
#undef glBindTexture
#define glBindTexture(...) (s_texCapture ? (void)0 : (void)(BatchFlushAt(__LINE__), MGLD_glBindTexture(__VA_ARGS__)))
#undef glBlendEquation
#define glBlendEquation(...) (BatchFlushAt(__LINE__), MGLD_glBlendEquation(__VA_ARGS__))
#undef glBlendFunc
#define glBlendFunc(...) (BatchFlushAt(__LINE__), MGLD_glBlendFunc(__VA_ARGS__))
#undef glClear
#define glClear(...) (BatchFlushAt(__LINE__), MGLD_glClear(__VA_ARGS__))
#undef glClearColor
#define glClearColor(...) (BatchFlushAt(__LINE__), MGLD_glClearColor(__VA_ARGS__))
#undef glClientActiveTextureARB
#define glClientActiveTextureARB(...) (BatchFlushAt(__LINE__), MGLD_glClientActiveTextureARB(__VA_ARGS__))
#undef glColorMaterial
#define glColorMaterial(...) (BatchFlushAt(__LINE__), MGLD_glColorMaterial(__VA_ARGS__))
#undef glColorPointer
#define glColorPointer(...) (BatchFlushAt(__LINE__), MGLD_glColorPointer(__VA_ARGS__))
#undef glCullFace
#define glCullFace(...) (BatchFlushAt(__LINE__), MGLD_glCullFace(__VA_ARGS__))
#undef glDeleteTextures
#define glDeleteTextures(...) (BatchFlushAt(__LINE__), MGLD_glDeleteTextures(__VA_ARGS__))
#undef glDepthFunc
#define glDepthFunc(...) (BatchFlushAt(__LINE__), MGLD_glDepthFunc(__VA_ARGS__))
#undef glDepthMask
#define glDepthMask(...) (BatchFlushAt(__LINE__), MGLD_glDepthMask(__VA_ARGS__))
#undef glDepthRange
#define glDepthRange(...) (BatchFlushAt(__LINE__), MGLD_glDepthRange(__VA_ARGS__))
#undef glDisable
#define glDisable(...) (s_texCapture ? (void)0 : (void)(BatchFlushAt(__LINE__), MGLD_glDisable(__VA_ARGS__)))
#undef glDisableClientState
#define glDisableClientState(...) (BatchFlushAt(__LINE__), MGLD_glDisableClientState(__VA_ARGS__))
#undef glDrawElements
#define glDrawElements(...) (BatchFlushAt(__LINE__), MGLD_glDrawElements(__VA_ARGS__))
#undef glEnable
#define glEnable(...) (s_texCapture ? (void)0 : (void)(BatchFlushAt(__LINE__), MGLD_glEnable(__VA_ARGS__)))
#undef glEnableClientState
#define glEnableClientState(...) (BatchFlushAt(__LINE__), MGLD_glEnableClientState(__VA_ARGS__))
#undef glFinish
#define glFinish(...) (BatchFlushAt(__LINE__), MGLD_glFinish(__VA_ARGS__))
#undef glFogf
#define glFogf(...) (BatchFlushAt(__LINE__), MGLD_glFogf(__VA_ARGS__))
#undef glFogfv
#define glFogfv(...) (BatchFlushAt(__LINE__), MGLD_glFogfv(__VA_ARGS__))
#undef glFogi
#define glFogi(...) (BatchFlushAt(__LINE__), MGLD_glFogi(__VA_ARGS__))
#undef glFrontFace
#define glFrontFace(...) (BatchFlushAt(__LINE__), MGLD_glFrontFace(__VA_ARGS__))
#undef glGenTextures
#define glGenTextures(...) (BatchFlushAt(__LINE__), MGLD_glGenTextures(__VA_ARGS__))
#undef glGetError
#define glGetError(...) (BatchFlushAt(__LINE__), MGLD_glGetError(__VA_ARGS__))
#undef glGetIntegerv
#define glGetIntegerv(...) (BatchFlushAt(__LINE__), MGLD_glGetIntegerv(__VA_ARGS__))
#undef glGetString
#define glGetString(...) (BatchFlushAt(__LINE__), MGLD_glGetString(__VA_ARGS__))
#undef glLightModelf
#define glLightModelf(...) (BatchFlushAt(__LINE__), MGLD_glLightModelf(__VA_ARGS__))
#undef glLightModelfv
#define glLightModelfv(...) (BatchFlushAt(__LINE__), MGLD_glLightModelfv(__VA_ARGS__))
#undef glLightf
#define glLightf(...) (BatchFlushAt(__LINE__), MGLD_glLightf(__VA_ARGS__))
#undef glLightfv
#define glLightfv(...) (BatchFlushAt(__LINE__), MGLD_glLightfv(__VA_ARGS__))
#undef glLoadIdentity
#define glLoadIdentity(...) (BatchFlushAt(__LINE__), MGLD_glLoadIdentity(__VA_ARGS__))
#undef glLoadMatrixf
#define glLoadMatrixf(...) (BatchFlushAt(__LINE__), MGLD_glLoadMatrixf(__VA_ARGS__))
#undef glMaterialf
#define glMaterialf(...) (BatchFlushAt(__LINE__), MGLD_glMaterialf(__VA_ARGS__))
#undef glMaterialfv
#define glMaterialfv(...) (BatchFlushAt(__LINE__), MGLD_glMaterialfv(__VA_ARGS__))
#undef glMatrixMode
#define glMatrixMode(...) (BatchFlushAt(__LINE__), MGLD_glMatrixMode(__VA_ARGS__))
#undef glNormalPointer
#define glNormalPointer(...) (BatchFlushAt(__LINE__), MGLD_glNormalPointer(__VA_ARGS__))
#undef glOrtho
#define glOrtho(...) (BatchFlushAt(__LINE__), MGLD_glOrtho(__VA_ARGS__))
#undef glPixelStorei
#define glPixelStorei(...) (BatchFlushAt(__LINE__), MGLD_glPixelStorei(__VA_ARGS__))
#undef glPopMatrix
#define glPopMatrix(...) (BatchFlushAt(__LINE__), MGLD_glPopMatrix(__VA_ARGS__))
#undef glPushMatrix
#define glPushMatrix(...) (BatchFlushAt(__LINE__), MGLD_glPushMatrix(__VA_ARGS__))
#undef glScissor
#define glScissor(...) (BatchFlushAt(__LINE__), MGLD_glScissor(__VA_ARGS__))
#undef glShadeModel
#define glShadeModel(...) (BatchFlushAt(__LINE__), MGLD_glShadeModel(__VA_ARGS__))
#undef glTexCoordPointer
#define glTexCoordPointer(...) (BatchFlushAt(__LINE__), MGLD_glTexCoordPointer(__VA_ARGS__))
#undef glTexEnvi
#define glTexEnvi(...) (s_texCapture ? (void)0 : (void)(BatchFlushAt(__LINE__), MGLD_glTexEnvi(__VA_ARGS__)))
#undef glTexImage2D
#define glTexImage2D(...) (BatchFlushAt(__LINE__), MGLD_glTexImage2D(__VA_ARGS__))
#undef glTexParameteri
#define glTexParameteri(...) (BatchFlushAt(__LINE__), MGLD_glTexParameteri(__VA_ARGS__))
#undef glTexSubImage2D
#define glTexSubImage2D(...) (BatchFlushAt(__LINE__), MGLD_glTexSubImage2D(__VA_ARGS__))
#undef glVertexPointer
#define glVertexPointer(...) (BatchFlushAt(__LINE__), MGLD_glVertexPointer(__VA_ARGS__))
#undef glViewport
#define glViewport(...) (BatchFlushAt(__LINE__), MGLD_glViewport(__VA_ARGS__))
#undef mglDeleteContext
#define mglDeleteContext(...) (BatchFlushAt(__LINE__), MGLD_mglDeleteContext(__VA_ARGS__))
#undef mglEnableSync
#define mglEnableSync(...) (BatchFlushAt(__LINE__), MGLD_mglEnableSync(__VA_ARGS__))
#undef mglLockDisplay
#define mglLockDisplay(...) (BatchFlushAt(__LINE__), MGLD_mglLockDisplay(__VA_ARGS__))
#undef mglLockMode
#define mglLockMode(...) (BatchFlushAt(__LINE__), MGLD_mglLockMode(__VA_ARGS__))
#undef mglResizeContext
#define mglResizeContext(...) (BatchFlushAt(__LINE__), MGLD_mglResizeContext(__VA_ARGS__))
#undef mglSwitchDisplay
#define mglSwitchDisplay(...) (BatchFlushAt(__LINE__), MGLD_mglSwitchDisplay(__VA_ARGS__))
#undef mglUnlockDisplay
#define mglUnlockDisplay(...) (BatchFlushAt(__LINE__), MGLD_mglUnlockDisplay(__VA_ARGS__))
#undef mglWriteShotPPM
#define mglWriteShotPPM(...) (BatchFlushAt(__LINE__), MGLD_mglWriteShotPPM(__VA_ARGS__))
#endif
// Set when the game leaves the frontend's virtual screen for gameplay. The
// first 3D draw consumes it with one log line, so a crash log can tell scene
// construction apart from rendering.
int s_stageEntryPending;
unsigned int s_neutralReflectionTex;
// GL names of destroyed stream textures, kept for the next ones of the same
// size: MiniGL 29.1 moves a deleted texture's memory into a pool that only its
// texture renames draw from, so deleting the two 1024x512 movie textures after
// every movie kept 4 MB per movie until the context closed. Deleted at shutdown.
struct SpareStreamTexture {
    unsigned int glId;
    int glWidth, glHeight;
};
enum { kSpareStreamTextures = 2 };
SpareStreamTexture s_spareStreams[kSpareStreamTextures];
int s_spareStreamCount;

StageState s_stages[2];
GfxTexture *s_boundTexture;
// MiniGL has no render-to-texture. While the game targets a texture (the car
// reflection cube faces) its draws and clears are dropped so they cannot land
// on the back buffer; the texture keeps its contents and consumers bind a
// neutral reflection texture.
GfxTexture *s_renderTarget;
int s_width = 640;
int s_height = 480;
int s_locked;
int s_tlScreenActive;

int s_fullscreen;
// Windowed mode: the application owns this window and MiniGL renders into it.
void *s_gameWindow;
GfxTexture *s_liveTextures;

void LinkLiveTexture(GfxTexture *texture)
{
    texture->livePrev = NULL;
    texture->liveNext = s_liveTextures;
    if (s_liveTextures != NULL)
        s_liveTextures->livePrev = texture;
    s_liveTextures = texture;
    texture->live = 1;
}

void UnlinkLiveTexture(GfxTexture *texture)
{
    if (!texture->live)
        return;
    if (texture->livePrev != NULL)
        texture->livePrev->liveNext = texture->liveNext;
    else
        s_liveTextures = texture->liveNext;
    if (texture->liveNext != NULL)
        texture->liveNext->livePrev = texture->livePrev;
    texture->live = 0;
}

// After the context that owned them was deleted: no GL name is valid any
// more. Textures with a CPU copy re-upload on their next bind; stream textures
// get a new name on their next frame.
void ForgetLiveTextureNames(void)
{
    int count = 0;
    for (GfxTexture *t = s_liveTextures; t != NULL; t = t->liveNext) {
        t->glId = 0;
        t->lumGlId = 0;
        t->lumValid = 0;
        t->samplerValid = 0;
        if (t->data != NULL)
            t->dirty = 1;
        else
            t->streamFresh = 1;
        ++count;
    }
    Sys_Log("gfx: %d textures will be uploaded again", count);
}
void UploadTexture(GfxTexture *texture);
unsigned int EnsureBumpLuminanceTexture(GfxTexture *texture, float scale, float offset);
void ApplyViewport(void);
int s_virtualWidth;
int s_virtualHeight;

GfxViewport s_viewport = { 0, 0, 640, 480, 0.0f, 1.0f };
GfxMatrix s_transforms[18];
int s_transformSet[18];
// Bumped whenever Gfx_SetTransform rewrites a matrix that feeds the GL
// modelview or projection. Gfx_PopState restores s_transforms wholesale and
// goes through InvalidateGlCache, which resets the loaded generations instead.
uint32_t s_worldViewGeneration;
uint32_t s_projectionGeneration;

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

int s_colourLogEnabled;     // video.colour_log, see ColourLogEndFrame
// Set while a draw comes from a GfxVertexBuffer, whose positions are stable
// across frames, so its bounding sphere can be cached (see DrawWorldSphere).
int s_drawFromVertexBuffer;
// Model-space bounding spheres of vertex-buffer draws, direct mapped.
enum { kBoundsCacheSize = 1024 };
struct BoundsCacheEntry {
    const GfxVertex *vertices;
    const uint16_t *indices;
    uint32_t count;
    int valid;
    LightRangeSphere model;
};
BoundsCacheEntry s_boundsCache[kBoundsCacheSize];
// video.cpu_lights (default 1): light draws that a point or spot light
// reaches on the CPU. 0 drops point and spot lights instead (GL keeps only the
// directional lights), which saves the CPU time.
int s_cpuLightsEnabled = 1;
// video.mt_cpu_light (default 1): a lit draw with both texture units is lit
// on the CPU and drawn unlit. MiniGL's lit two-unit path drew the car's Bump
// environment pass with flickering squares; its unlit two-unit path does not.
int s_multiTextureCpuLight = 1;
// Set while a draw is drawn with CPU-lit vertex colours (see CpuLightDraw).
int s_cpuLitDraw;
// Grow-only scratch vertices for CPU-lit draws.
GfxVertex *s_cpuLitVertices;
uint32_t s_cpuLitCapacity;
// Which scratch vertices the current draw has lit already (see CpuLightDraw).
uint32_t *s_cpuLitStamp;
uint32_t s_cpuLitStampGen;
// video.light_mask: diagnostic, GL lights allowed on (bit i = GL_LIGHT0 + i).
uint32_t s_debugLightMask = 0xffu;
void ColourLogEndFrame(void);

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
    DEGRADE_LOG_BACKDROP_FOG = 14,
    DEGRADE_LOG_PASSTHROUGH_ALPHA = 15,
    DEGRADE_LOG_NO_ADD = 16,
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
        s_stages[i].bumpLumScale = 1.0f;
        s_stages[i].bumpLumOffset = 0.0f;
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
    s_glCache.loadedWorldViewGen = ~0u;
    s_glCache.loadedProjectionGen = ~0u;
#if defined(OPENCMR2_WARPOS)
    s_modelviewIdentity = 0;
#endif
    s_glCache.clientArrayMask = 0;
    s_glCache.lightMask = ~0u;
    InvalidateLightingCache();
}

// One place owns the diagnostic field list, so the stage flush and the
// benchmark window cannot report different fields. The stage flush passes a
// zeroed base; the window passes the snapshot taken by Gfx_BenchmarkBegin().
const StageDiagnostics kNoDiagnosticBase = {};

void LogDiagnosticLines(const char *summaryLabel, const char *countersLabel,
                        const char *timingLabel, const char *qualifier,
                        const StageDiagnostics &now, const StageDiagnostics &base)
{
    const char *q = qualifier != NULL ? qualifier : "";
    const unsigned frames = (unsigned)(now.frames - base.frames);
    const unsigned draws3D = (unsigned)(now.drawCalls3D - base.drawCalls3D);
    const unsigned drawsTL = (unsigned)(now.drawCallsTL - base.drawCallsTL);
    const unsigned triangles3D = (unsigned)(now.triangles3D - base.triangles3D);
    const unsigned uploads = (unsigned)(now.textureUploads - base.textureUploads);
    const unsigned rttDraws = (unsigned)(now.suppressedRttDraws - base.suppressedRttDraws);
    const unsigned rttClears = (unsigned)(now.suppressedRttClears - base.suppressedRttClears);
    Sys_Log("AMIGA: %s%s: frames=%u draws3d=%u drawsTL=%u tris3d=%u uploads=%u rttDropped=%u/%u",
            summaryLabel, q, frames, draws3D, drawsTL, triangles3D, uploads, rttDraws, rttClears);
    Sys_Log("AMIGA: %s%s: mvLoads=%u mvSkips=%u modeSwitches=%u projLoads=%u "
            "lightUploads=%u lightSkips=%u tlMatLoads=%u tlMatSkips=%u stageSetters=%u lightSetters=%u idxValidated=%u",
            countersLabel, q,
            (unsigned)(now.modelviewLoads - base.modelviewLoads),
            (unsigned)(now.modelviewSkips - base.modelviewSkips),
            (unsigned)(now.matrixModeSwitches - base.matrixModeSwitches),
            (unsigned)(now.projectionLoads - base.projectionLoads),
            (unsigned)(now.lightingUploads - base.lightingUploads),
            (unsigned)(now.lightingSkips - base.lightingSkips),
            (unsigned)(now.tlMatrixLoads - base.tlMatrixLoads),
            (unsigned)(now.tlMatrixSkips - base.tlMatrixSkips),
            (unsigned)(now.stageSetterCalls - base.stageSetterCalls),
            (unsigned)(now.lightingSetterCalls - base.lightingSetterCalls),
            (unsigned)(now.indicesValidated - base.indicesValidated));
    // Buckets: <=12, <=32, <=64, <=128, <=256, <=1024, >1024.
    Sys_Log("AMIGA: %s%s: <=12=%u <=32=%u <=64=%u <=128=%u <=256=%u <=1024=%u >1024=%u; "
            "triIndexed=%u triPlain=%u other=%u",
            countersLabel, q,
            (unsigned)(now.drawSizeHistogram[0] - base.drawSizeHistogram[0]),
            (unsigned)(now.drawSizeHistogram[1] - base.drawSizeHistogram[1]),
            (unsigned)(now.drawSizeHistogram[2] - base.drawSizeHistogram[2]),
            (unsigned)(now.drawSizeHistogram[3] - base.drawSizeHistogram[3]),
            (unsigned)(now.drawSizeHistogram[4] - base.drawSizeHistogram[4]),
            (unsigned)(now.drawSizeHistogram[5] - base.drawSizeHistogram[5]),
            (unsigned)(now.drawSizeHistogram[6] - base.drawSizeHistogram[6]),
            (unsigned)(now.triangleIndexedDraws - base.triangleIndexedDraws),
            (unsigned)(now.trianglePlainDraws - base.trianglePlainDraws),
            (unsigned)(now.otherPrimitiveDraws - base.otherPrimitiveDraws));
    Sys_Log("AMIGA: %s%s: arrayEligibleDraws=%u arrayEligibleIndices=%u arrayRejectedDraws=%u "
            "arrayDraws=%u arrayIndices=%u arraySpanOverLimit=%u",
            countersLabel, q,
            (unsigned)(now.arrayEligibleDraws - base.arrayEligibleDraws),
            (unsigned)(now.arrayEligibleIndices - base.arrayEligibleIndices),
            (unsigned)(now.arrayRejectedDraws - base.arrayRejectedDraws),
            (unsigned)(now.arrayDraws - base.arrayDraws),
            (unsigned)(now.arrayIndices - base.arrayIndices),
            (unsigned)(now.arraySpanOverLimitDraws - base.arraySpanOverLimitDraws));
#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
    // Totals are 64-bit microsecond counts, printed as hi/lo 32-bit halves so a
    // long session cannot wrap. Averages are derived by the reader, not here.
    const uint64_t submitUs = now.timingSubmit3DUs - base.timingSubmit3DUs;
    const uint64_t tlUs = now.timingTLUs - base.timingTLUs;
    const uint64_t presentUs = now.timingPresentUs - base.timingPresentUs;
    Sys_Log("AMIGA: %s%s: submit3d calls=%u us_hi=%u us_lo=%u; tl calls=%u us_hi=%u us_lo=%u; present calls=%u us_hi=%u us_lo=%u",
            timingLabel, q,
            (unsigned)(now.timingSubmit3DCalls - base.timingSubmit3DCalls),
            (unsigned)(submitUs >> 32), (unsigned)(submitUs & 0xffffffffu),
            (unsigned)(now.timingTLCalls - base.timingTLCalls),
            (unsigned)(tlUs >> 32), (unsigned)(tlUs & 0xffffffffu),
            (unsigned)(now.timingPresentCalls - base.timingPresentCalls),
            (unsigned)(presentUs >> 32), (unsigned)(presentUs & 0xffffffffu));
#endif
}

void FlushStageDiagnostics(const char *reason)
{
    if (!s_diag.inStage)
        return;
    char qualifier[48];
    snprintf(qualifier, sizeof(qualifier), " (%s)", reason != NULL ? reason : "end");
    LogDiagnosticLines("M3.2 stage summary", "M3.2 stage counters", "M3.2 section timing",
                       qualifier, s_diag, kNoDiagnosticBase);
    // The reset must not lose the serial: the benchmark window compares it to
    // detect a flush inside the window.
    const uint32_t serial = s_diag.flushSerial;
    memset(&s_diag, 0, sizeof(s_diag));
    s_diag.flushSerial = serial + 1;
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampWrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampWrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, kNeutralTexels);
}

// Client-array enable state is cached; only a change issues a GL call. A draw
// that needs the same arrays as the previous one costs nothing here.
enum {
    kClientArrayVertex = 1u << 0,
    kClientArrayNormal = 1u << 1,
    kClientArrayColor = 1u << 2,
    kClientArrayTex0 = 1u << 3,
    kClientArrayTex1 = 1u << 4
};

void SetClientArray(uint32_t bit, GLenum cap, int on)
{
    const bool want = on != 0;
    if (((s_glCache.clientArrayMask & bit) != 0) == want)
        return;
    if (want)
        glEnableClientState(cap);
    else
        glDisableClientState(cap);
    if (want)
        s_glCache.clientArrayMask |= bit;
    else
        s_glCache.clientArrayMask &= ~bit;
}

// The 2D/TL path never reads a client array. Unbind every array the 3D array
// path may have left armed before the menu draws, so a later draw cannot
// inherit a stale normal/colour pointer (the optimised MiniGL fast gather now
// accepts a bound normal array instead of declining it). SetClientArray keeps
// the shadow mask truthful, so the next array draw re-enables exactly what it
// needs. Unit 1 is selected around its disable the same way Submit3DArrays
// does.
void ClearClientArrays(void)
{
    if ((s_glCache.clientArrayMask & kClientArrayTex1) != 0) {
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        SetClientArray(kClientArrayTex1, GL_TEXTURE_COORD_ARRAY, 0);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }
    SetClientArray(kClientArrayVertex, GL_VERTEX_ARRAY, 0);
    SetClientArray(kClientArrayNormal, GL_NORMAL_ARRAY, 0);
    SetClientArray(kClientArrayColor, GL_COLOR_ARRAY, 0);
    SetClientArray(kClientArrayTex0, GL_TEXTURE_COORD_ARRAY, 0);
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

    // Client arrays. The vertex/colour/texcoord array entry points
    // are part of the documented PiStorm3D pipeline; the part that is not
    // proven on 29.x is a second *client* texcoord unit, so probe that without
    // drawing anything. The first real array draw additionally checks
    // glGetError and falls back permanently if the library rejects it.
    if (s_clientArraysRequested) {
        s_caps.clientArrays = 1;
        s_caps.clientArrayUnits = 1;
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        if (glGetError() == GL_NO_ERROR) {
            glTexCoordPointer(2, GL_FLOAT, (GLsizei)sizeof(GfxVertex), NULL);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            if (glGetError() == GL_NO_ERROR)
                s_caps.clientArrayUnits = 2;
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
        while (glGetError() != GL_NO_ERROR) {}
        Sys_Log("AMIGA: client arrays requested=1 accepted=1 texcoordUnits=%d",
                s_caps.clientArrayUnits);
    }

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

namespace {

// The size, transforms and GL state of a display that was just created or
// resized, then one clear. Returns 0 when the display cannot be locked.
int StartDisplay(int width, int height)
{
    s_width = width;
    s_height = height;
    // A windowed request that fell back to MiniGL's screen is fullscreen.
    s_fullscreen = s_gameWindow == NULL;
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
    if (!EnsureLocked())
        return 0;
    ProbeCapabilities();
    ApplyViewport();
    glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    mglUnlockDisplay();
    s_locked = 0;
    return 1;
}

// Deletes the context, its input path and its window; MiniGL stays open.
// displayOpen = 0 after a failed screen reopen: no GL call is made then (the
// context's own delete frees its textures).
void DestroyContext(int displayOpen)
{
    if (s_context != NULL) {
        s_boundTexture = NULL;
        s_renderTarget = NULL;
        s_stages[0].texture = NULL;
        s_stages[1].texture = NULL;
        if (displayOpen && s_neutralReflectionTex != 0 && EnsureLocked()) {
            glDeleteTextures(1, &s_neutralReflectionTex);
            s_neutralReflectionTex = 0;
        }
        for (int i = 0; displayOpen && i < s_spareStreamCount; ++i) {
            if (EnsureLocked())
                glDeleteTextures(1, &s_spareStreams[i].glId);
        }
        if (s_locked) {
            mglUnlockDisplay();
            s_locked = 0;
        }
        if (s_gameWindow == NULL) {
            // Fullscreen: our input window is on MiniGL's screen, which goes
            // away with the context. Close it before deleting.
            AmigaSys_SetWindow(NULL);
        }
        mglDeleteContext();
        s_context = NULL;
    }
    s_neutralReflectionTex = 0;
    s_spareStreamCount = 0;
    s_caps.probed = 0;
    InvalidateGlCache();
    if (s_gameWindow != NULL) {
        // The adopted window outlives the context; close it before MiniGLClose
        // (proto/intuition.h stubs need the library's IntuitionBase).
        AmigaSys_SetWindow(NULL);
        s_gameWindow = NULL;
    }
}

// Creates the context at width x height (MiniGL is open) and its input path.
int CreateContext(int width, int height, int fullscreen)
{
    // PORT: AMIGA: MiniGL's immediate-mode vertex buffer defaults to 256
    // entries and glVertex has no bounds check. The renderer submits up to 256
    // vertices per glBegin block, so enlarge the buffer before the context
    // allocates it. reVC's MiniGL backend uses 4096 for the immediate path.
    // The client-array pipeline transforms by absolute index and clips from
    // size/2, so it needs 2 * the largest index; the immediate
    // path keeps librw's 4096.
    s_clientArraysRequested = Sys_GetOption("video.client_arrays", 0) != 0;
#if defined(OPENCMR2_WARPOS)
    s_batching = Sys_GetOption("video.batch", 1) != 0;
    s_batchLog = Sys_GetOption("video.batch_log", 0) != 0;
    s_profile = Sys_GetOption("video.profile", 0) != 0;
    s_batchSort = s_batching && Sys_GetOption("video.batch_sort", 1) != 0;
    s_batchSortBlended = Sys_GetOption("video.batch_sort_blended", 1) != 0;
    // Off by default: on the reference A4000 the stack is no faster with
    // indexed arrays than with immediate vertices (34.0 against 32.3 ms a
    // frame at the Finland 1 start line).
    s_batchArrays = s_batchSort && Sys_GetOption("video.batch_arrays", 0) != 0;
    s_batchArraysChecked = 0;
    s_cpuCull = Sys_GetOption("video.cpu_cull", 1) != 0;
    s_cpuClip = Sys_GetOption("video.cpu_clip", 1) != 0;
    s_cpuLightAll = Sys_GetOption("video.cpu_light_all", 1) != 0;
    Sys_Log("gfx: CPU lighting for every lit draw %s", s_cpuLightAll ? "on" : "off");
    Sys_Log("gfx: draw batching %s", s_batching ? "on" : "off");
#endif
    s_colourLogEnabled = Sys_GetOption("video.colour_log", 0) != 0;
    s_debugLightMask = (uint32_t)Sys_GetOption("video.light_mask", 0xff) & 0xffu;
    s_cpuLightsEnabled = Sys_GetOption("video.cpu_lights", 1) != 0;
    s_multiTextureCpuLight = Sys_GetOption("video.mt_cpu_light", 1) != 0;
    if (!s_cpuLightsEnabled)
        Sys_Log("AMIGA: video.cpu_lights=0: point and spot lights are not drawn");
    if (s_debugLightMask != 0xffu)
        Sys_Log("AMIGA: diagnostic: video.light_mask=%02lx", (unsigned long)s_debugLightMask);
#if defined(OPENCMR2_WARPOS)
    // Array buckets index up to kBucketVertexLimit vertices; the array
    // pipeline wants twice the largest index (see above).
    mglChooseVertexBufferSize((s_clientArraysRequested || s_batchArrays) ? 32768 : 4096);
#else
    mglChooseVertexBufferSize(s_clientArraysRequested ? 32768 : 4096);
#endif
    // PORT: AMIGA: the default texture-object table is 2048 entries; the game
    // reuses one table across stage reloads, so keep headroom and avoid
    // glGenTextures returning 0 (which presents as a bad texture later).
    mglChooseTextureBufferSize(4096);
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
#if defined(OPENCMR2_WARPOS)
        // The R200 stack's fullscreen setup: a 16-bit screen (its fast path)
        // and two buffers, presented by blit (sync off, below).
        mglChoosePixelDepth(Sys_GetOption("video.depth", 16));
        mglChooseNumberOfBuffers(2);
#endif
        s_context = mglCreateContext(0, 0, width, height);
        if (s_context == NULL) {
            Sys_Log("gfx: mglCreateContext %dx%d failed", width, height);
            return 0;
        }
        // Fullscreen: v3dWindow is NULL and inputWindow is MiniGL's borderless
        // backdrop; it has no UserPort, so AmigaSys_SetWindow owns a 1x1 window.
        void *inputWindow = mglGetInputWindowHandle();
        AmigaSys_SetWindow(inputWindow);
        Sys_Log("gfx: MiniGL %dx%d context ready (screen), input window %lx",
                width, height, (unsigned long)inputWindow);
#if defined(OPENCMR2_WARPOS)
        // Present by blit. With sync on (vertical-blank flips) the stack
        // fails mglSwitchDisplay with GL_INVALID_OPERATION once a frame is
        // presented while the previous flip is still pending (from about
        // the 12th frame of a light scene, on the reference A4000 with the
        // 2026-11-05 minigl_ppc.dll), and the screen stays empty. The blit
        // path has no such limit.
        mglEnableSync(GL_FALSE);
        mglLockMode(MGL_LOCK_SMART);
#endif
    } else {
        Sys_Log("gfx: MiniGL %dx%d context ready (window)", width, height);
    }
    if (!StartDisplay(width, height)) {
        DestroyContext(0);
        return 0;
    }
    return 1;
}

// PORT: AMIGA: MiniGL reopens its own screen at the new size and keeps the
// textures and GL state (MGLResizeContext, fullscreen only).
int ResizeFullscreen(int width, int height)
{
    if (s_locked) {
        mglUnlockDisplay();
        s_locked = 0;
    }
    // Our input window is on that screen, which cannot close under a window.
    AmigaSys_SetWindow(NULL);
    if (!mglResizeContext(width, height)) {
        Sys_Log("gfx: MiniGL could not reopen its screen at %dx%d", width, height);
        return 0;
    }
    AmigaSys_SetWindow(mglGetInputWindowHandle());
    return StartDisplay(width, height);
}

const char *ModeKind(int fullscreen)
{
    return fullscreen ? "screen" : "window";
}

// PORT: AMIGA: a new resolution or a switch between window and screen with a
// context open. A screen-to-screen change resizes in place; anything with a
// window rebuilds the context (a window context is sized once, by its window)
// and the textures upload again from their CPU copies. A mode that fails
// falls back to the previous one.
int ChangeMode(int width, int height, int fullscreen)
{
    const int oldWidth = s_width;
    const int oldHeight = s_height;
    const int oldFullscreen = s_fullscreen;
    // The menus set their virtual screen and viewport again every frame, so
    // at worst the rest of a switching frame draws unscaled.
    int displayOpen = 1;

    Sys_Log("gfx: mode %dx%d %s -> %dx%d %s", oldWidth, oldHeight, ModeKind(oldFullscreen), width, height,
            ModeKind(fullscreen));
    if (fullscreen && oldFullscreen) {
        if (ResizeFullscreen(width, height))
            return 1;
        displayOpen = 0;
    }
    DestroyContext(displayOpen);
    ForgetLiveTextureNames();
    if (CreateContext(width, height, fullscreen))
        return 1;
    Sys_Log("gfx: %dx%d %s failed; back to %dx%d %s", width, height, ModeKind(fullscreen), oldWidth, oldHeight,
            ModeKind(oldFullscreen));
    if (CreateContext(oldWidth, oldHeight, oldFullscreen))
        return 1;
    Sys_Log("gfx: the previous mode failed too; no display");
    return 0;
}

// The game's resolution list: the 32-bit RTG modes MiniGL can open a screen
// in, or for a window the sizes that fit on the Workbench screen.
DisplayModeSize s_modeList[DISPLAY_MODES_COLLECT_MAX];
int s_modeCount = -1;

void BuildModeList(void)
{
    static const int classic[3][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 } };
    const int windowed = Sys_GetOption("video.fullscreen", 1) == 0;
    int n = AmigaModes_ListBGRA32(s_modeList, DISPLAY_MODES_COLLECT_MAX);

    if (windowed) {
        int desktopWidth = 0;
        int desktopHeight = 0;
        for (int i = 0; i < 3; ++i)
            n = DisplayModes_Add(s_modeList, n, DISPLAY_MODES_COLLECT_MAX, classic[i][0], classic[i][1]);
        // Room for the window's borders and title bar.
        Sys_GetDesktopSize(&desktopWidth, &desktopHeight);
        n = DisplayModes_Fit(s_modeList, n, desktopWidth - 8, desktopHeight - 24);
    }
    if (n == 0) {
        for (int i = 0; i < 3; ++i)
            n = DisplayModes_Add(s_modeList, n, DISPLAY_MODES_COLLECT_MAX, classic[i][0], classic[i][1]);
        if (windowed)
            n = 1;   // the default; the window is shrunk to fit
    }
    {
        // Keep the Workbench screen's size in the list, then spread the rest.
        int wbWidth = 0;
        int wbHeight = 0;
        Sys_GetDesktopSize(&wbWidth, &wbHeight);
        s_modeCount = DisplayModes_Select(s_modeList, n, DISPLAY_MODES_GAME_MAX, wbWidth, wbHeight);
    }
    for (int i = 0; i < s_modeCount; ++i)
        Sys_Log("gfx: %s mode %d: %dx%d", ModeKind(!windowed), i, s_modeList[i].width, s_modeList[i].height);
}

// MiniGLOpen() fails silently both when minigl.library is missing and when it
// is too old for this client (the release does not ship it). Tell the player
// which, once per run, from the library's own version.
void ReportMiniGLOpenFailure(void)
{
    static int s_reported;
    if (s_reported)
        return;
    s_reported = 1;

    int present = 0;
    unsigned version = 0;
    unsigned revision = 0;
    struct Library *library = OpenLibrary((CONST_STRPTR)"minigl.library", 0);
    if (library != NULL) {
        present = 1;
        version = library->lib_Version;
        revision = library->lib_Revision;
        CloseLibrary(library);
    }
    if (!present)
        Sys_Log("gfx: MiniGLOpen failed: minigl.library not found");
    else
        Sys_Log("gfx: MiniGLOpen failed: minigl.library %u.%u (29.1 or newer required)",
                version, revision);

    char text[256];
    AmigaMiniGLNotice_Text(text, sizeof(text), present, version, revision);
    Sys_MessageBox("OpenCMR2", text, SYS_MESSAGEBOX_OK);
}

} // namespace

extern "C" int Gfx_SetVideoMode(int width, int height, int fullscreen)
{
    if (width <= 0 || height <= 0)
        return 0;
    fullscreen = fullscreen != 0;
    if (s_context != NULL) {
        if (width == s_width && height == s_height && fullscreen == s_fullscreen)
            return 1;
        return ChangeMode(width, height, fullscreen);
    }
    if (!MiniGLOpen()) {
        ReportMiniGLOpenFailure();
        return 0;
    }
    s_miniglOpen = 1;
    if (!CreateContext(width, height, fullscreen)) {
        MiniGLClose();
        s_miniglOpen = 0;
        return 0;
    }
    return 1;
}

extern "C" void Gfx_SetFullscreen(int fullscreen)
{
    // PORT: AMIGA: the options menu's switch (saved as video.fullscreen by
    // the caller) applies now, at the current resolution.
    fullscreen = fullscreen != 0;
    if (s_context == NULL) {
        s_fullscreen = fullscreen;
        return;
    }
    if (fullscreen != s_fullscreen)
        ChangeMode(s_width, s_height, fullscreen);
}

extern "C" void Gfx_Shutdown(void)
{
    FlushStageDiagnostics("shutdown");
    DestroyContext(1);
    if (s_miniglOpen) {
        MiniGLClose();
        s_miniglOpen = 0;
    }
}

extern "C" int Gfx_GetDisplayModeCount(void)
{
    BuildModeList();
    return s_modeCount;
}

extern "C" void Gfx_GetDisplayMode(int index, int *width, int *height)
{
    if (width == NULL || height == NULL)
        return;
    if (s_modeCount < 0)
        BuildModeList();
    if (index < 0 || index >= s_modeCount)
        index = 0;
    *width = s_modeList[index].width;
    *height = s_modeList[index].height;
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

// Benchmark window. The counters are cumulative until a stage flush, so the
// window is the difference between two snapshots: starting and ending the
// window costs no per-draw work and cannot drift from the real totals. A flush
// inside the window resets the counters underneath it, so that case is refused
// rather than reported with a wrong value.
namespace {
int s_benchActive = 0;
StageDiagnostics s_benchBase;
uint32_t s_benchFlushSerial = 0;
}

extern "C" void Gfx_BenchmarkBegin(void)
{
    s_benchBase = s_diag;
    s_benchFlushSerial = s_diag.flushSerial;
    s_benchActive = 1;
    Sys_Log("AMIGA: bench: begin");
}

extern "C" void Gfx_BenchmarkEnd(void)
{
    if (!s_benchActive)
        return;
    s_benchActive = 0;
    if (s_diag.flushSerial != s_benchFlushSerial) {
        Sys_Log("AMIGA: bench: window invalid, stage counters flushed inside it");
        return;
    }
    LogDiagnosticLines("bench summary", "bench counters", "bench timing", "",
                       s_diag, s_benchBase);
}

extern "C" void Gfx_EndScene(void)
{
}

void Gfx_PresentBody(int vsync)
{
    (void)vsync;
    if (s_context == NULL)
        return;
    if (s_diag.inStage)
        s_diag.frames++;
    if (s_colourLogEnabled)
        ColourLogEndFrame();
    s_tlScreenActive = 0;
#if !defined(OPENCMR2_WARPOS)
    if (s_locked) {
        mglUnlockDisplay();
        s_locked = 0;
    }
#endif
#if defined(OPENCMR2_WARPOS)
    if (s_batching && ++s_batchStats.presents >= 480) {
        Sys_Log("gfx: batching, 480 frames: per frame %lu draws in %lu batches, %lu vertices, "
                "%lu lit on the CPU (%lu by lights) in %lu draws with %lu directional and %lu point/spot lights%s",
                (unsigned long)(s_batchStats.draws / 480u), (unsigned long)(s_batchStats.batches / 480u),
                (unsigned long)(s_batchStats.vertices / 480u), (unsigned long)(s_batchStats.litVertices / 480u),
                (unsigned long)(s_batchStats.litByLights / 480u), (unsigned long)(s_batchStats.litDraws / 480u),
                (unsigned long)(s_batchStats.directionalLights / 480u),
                (unsigned long)(s_batchStats.positionalLights / 480u),
                s_normalizeNormals ? ", normals normalised" : "");
        Sys_Log("gfx: culling, per frame: %lu back-facing and %lu outside the view dropped on the CPU",
                (unsigned long)(s_batchStats.culledTriangles / 480u),
                (unsigned long)(s_batchStats.clippedTriangles / 480u));
        Sys_Log("gfx: sorting, per frame: %lu draws through texture buckets, %lu bucket flushes; not sorted: "
                "%lu GL-lit, %lu blended, %lu no depth test, %lu no depth write, %lu not triangles, %lu texture not ready",
                (unsigned long)(s_batchStats.sortedDraws / 480u), (unsigned long)(s_batchStats.bucketFlushes / 480u),
                (unsigned long)(s_batchStats.whyNot[0] / 480u), (unsigned long)(s_batchStats.whyNot[1] / 480u),
                (unsigned long)(s_batchStats.whyNot[2] / 480u), (unsigned long)(s_batchStats.whyNot[3] / 480u),
                (unsigned long)(s_batchStats.whyNot[4] / 480u), (unsigned long)(s_batchStats.whyNot[5] / 480u));

        memset(&s_batchStats, 0, sizeof(s_batchStats));
        if (s_profile) {
            // Time base ticks to microseconds per frame; 24.74 MHz is the
            // reference board's rate (the game clock measures the real one).
            const uint32_t perUs480 = 24740u * 480u / 1000u;
            {
                unsigned long fileCalls = 0, fileBytes = 0, audioCalls = 0;
                const unsigned long feed = AmigaSys_TakePumpMicros();
                const unsigned long file = AmigaSys_TakeFileMicros(&fileCalls, &fileBytes);
                const unsigned long audio = AmigaSys_TakeAudioMicros(&audioCalls);
                Sys_Log("gfx: profile, us/frame: audio feed %lu, file I/O %lu (%lu calls, %lu bytes), "
                        "audio API %lu (%lu calls)",
                        feed / 480u, file / 480u, fileCalls / 480u, fileBytes / 480u, audio / 480u, audioCalls / 480u);
            }
            Sys_Log("gfx: profile, us/frame: state apply %lu, closing batches %lu, CPU light %lu (vertex loop %lu), "
                    "3D emit %lu, 2D emit %lu",
                    (unsigned long)(s_profTicks[kProfApply] / perUs480),
                    (unsigned long)(s_profTicks[kProfEnd] / perUs480),
                    (unsigned long)(s_profTicks[kProfLight] / perUs480),
                    (unsigned long)(s_profTicks[kProfLightLoop] / perUs480),
                    (unsigned long)(s_profTicks[kProfEmit] / perUs480),
                    (unsigned long)(s_profTicks[kProfTL] / perUs480));
            memset(s_profTicks, 0, sizeof(s_profTicks));
        }
        if (s_batchLog) {
            // The eight lines that closed the most batches in this window.
            for (int rank = 0; rank < 8; rank++) {
                int best = 0;
                for (int line = 1; line < (int)kBatchLogLines; line++) {
                    if (s_batchClosedAt[line] > s_batchClosedAt[best])
                        best = line;
                }
                if (s_batchClosedAt[best] == 0)
                    break;
                Sys_Log("gfx: batches closed at line %d: %lu per frame", best,
                        (unsigned long)(s_batchClosedAt[best] / 480u));
                s_batchClosedAt[best] = 0;
            }
            memset(s_batchClosedAt, 0, sizeof(s_batchClosedAt));
        }
    }
    // SetEnv OPENCMR2_SHOT 1: the rendered frame goes to PROGDIR:shot.ppm, a
    // picture of what the card drew that neither a remote screenshot nor a
    // camera gives. Checked once a second (the check is a 68k call).
    {
        static uint32_t s_lastShotCheck;
        const uint32_t now = Sys_GetTicks();
        if (now - s_lastShotCheck >= 1000) {
            s_lastShotCheck = now;
            if (AmigaSys_TakeEnvFlag("OPENCMR2_PCPROF"))
                WosPcProf_Start();
            if (AmigaSys_TakeEnvFlag("OPENCMR2_SHOT")) {
                mglWriteShotPPM((char *)"PROGDIR:shot.ppm");
                Sys_Log("gfx: frame written to PROGDIR:shot.ppm");
            }
        }
    }
#endif
#if defined(OPENCMR2_WARPOS)
    // The R200 stack presents only from a locked display: its own host
    // switches and then unlocks, as does its gears demo. Switching after
    // the unlock left the screen showing its empty bitmap.
    if (!EnsureLocked())
        return;
    mglSwitchDisplay();
    mglUnlockDisplay();
    s_locked = 0;
    WosPcProf_Poll();
#else
    mglSwitchDisplay();
#endif
}

#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
extern "C" void Gfx_Present(int vsync)
{
    const double start = Sys_GetTicksPrecise();
    Gfx_PresentBody(vsync);
    s_diag.timingPresentCalls++;
    s_diag.timingPresentUs += SectionUsFromMs(Sys_GetTicksPrecise() - start);
}
#else
extern "C" void Gfx_Present(int vsync)
{
    Gfx_PresentBody(vsync);
}
#endif

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
    // crashed on the target; clearing to
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
    s_glCache.loadedProjectionGen = ~0u;
    s_diag.matrixModeSwitches++;
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
    // The frontend leaves the menus' screen at the end of every menu frame
    // (MenuScreen_End), so this only arms the log: the first 3D draw on the
    // real screen reports the stage entry.
    if (wasMenus && width == 0)
        s_stageEntryPending = 1;
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
        // ~0u is the "not loaded by us" sentinel, so a live generation must
        // never take that value or a wrapped counter would look like a hit.
        if (type == GFX_TRANSFORM_WORLD || type == GFX_TRANSFORM_VIEW) {
            if (++s_worldViewGeneration == ~0u)
                ++s_worldViewGeneration;
            if (type == GFX_TRANSFORM_VIEW)
                InvalidateLightingCache();
        } else if (type == GFX_TRANSFORM_PROJECTION) {
            if (++s_projectionGeneration == ~0u)
                ++s_projectionGeneration;
        }
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
    s_diag.lightingSetterCalls++;
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
    s_diag.lightingSetterCalls++;
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
    s_diag.lightingSetterCalls++;
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
    s_diag.lightingSetterCalls++;
    s_ambient = color;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetSpecular(int enable)
{
    s_diag.lightingSetterCalls++;
    s_specularEnabled = enable != 0;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetLocalViewer(int enable)
{
    s_diag.lightingSetterCalls++;
    s_localViewer = enable != 0;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetNormalizeNormals(int enable) { s_normalizeNormals = enable != 0; }
extern "C" void Gfx_SetColorVertex(int enable) { s_colorVertex = enable != 0; }
extern "C" void Gfx_SetDiffuseMaterialSource(int source)
{
    s_diag.lightingSetterCalls++;
    s_diffuseSource = source;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetSpecularMaterialSource(int source)
{
    s_diag.lightingSetterCalls++;
    s_specularSource = source;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetAmbientMaterialSource(int source)
{
    s_diag.lightingSetterCalls++;
    s_ambientSource = source;
    InvalidateLightingCache();
}
extern "C" void Gfx_SetEmissiveMaterialSource(int source)
{
    s_diag.lightingSetterCalls++;
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
    s_diag.stageSetterCalls++;
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
#if defined(OPENCMR2_WARPOS)
    // With texture-sorted batching the bind waits for the draw: every draw
    // path binds what it needs from s_stages / s_boundTexture itself, and a
    // bind here would close the buckets on every texture change.
    if (s_batchSort)
        return;
#endif
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
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].colorOp = op;
}
extern "C" void Gfx_SetStageColorArg1(int stage, int arg)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].colorArg1 = arg;
}
extern "C" void Gfx_SetStageColorArg2(int stage, int arg)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].colorArg2 = arg;
}
extern "C" void Gfx_SetStageAlphaOp(int stage, int op)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].alphaOp = op;
}
extern "C" void Gfx_SetStageAlphaArg1(int stage, int arg)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].alphaArg1 = arg;
}
extern "C" void Gfx_SetStageAlphaArg2(int stage, int arg)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].alphaArg2 = arg;
}

static GLint StageWrapMode(int stage)
{
    switch (s_stages[stage].address) {
    case GFX_ADDRESS_CLAMP: return kClampWrap;
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
// no glTexParameteri calls. Mipmaps are off by default.
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
    s_diag.stageSetterCalls++;
    if (stage < 0 || stage >= 2 || address == s_stages[stage].address)
        return;
    s_stages[stage].address = address;
    ReapplyStageSampler(stage);
}
extern "C" void Gfx_SetStageMinFilter(int stage, int filter)
{
    s_diag.stageSetterCalls++;
    if (stage < 0 || stage >= 2 || filter == s_stages[stage].minFilter)
        return;
    s_stages[stage].minFilter = filter;
    ReapplyStageSampler(stage);
}
extern "C" void Gfx_SetStageMagFilter(int stage, int filter)
{
    s_diag.stageSetterCalls++;
    if (stage < 0 || stage >= 2 || filter == s_stages[stage].magFilter)
        return;
    s_stages[stage].magFilter = filter;
    ReapplyStageSampler(stage);
}
// Mipmaps and bump-environment mapping are not supported: textures
// upload one level and BUMPENVMAP degrades to MODULATE. The entry points still
// accept the calls so the game's state cache stays consistent.
extern "C" void Gfx_SetStageMipFilter(int stage, int filter)
{
    s_diag.stageSetterCalls++;
    (void)stage;
    (void)filter;
}
extern "C" void Gfx_SetStageMaxMipLevel(int stage, int level)
{
    s_diag.stageSetterCalls++;
    (void)stage;
    (void)level;
}
extern "C" void Gfx_SetStageTexCoordIndex(int stage, uint32_t index)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].texCoordIndex = index;
}
extern "C" void Gfx_SetStageTextureTransform(int stage, int flags)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].textureTransform = flags;
}
extern "C" void Gfx_SetStageBumpEnvMat(int stage, int index, float value)
{
    s_diag.stageSetterCalls++;
    (void)stage;
    (void)index;
    (void)value;
}
extern "C" void Gfx_SetStageBumpEnvLScale(int stage, float value)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].bumpLumScale = value;
}
extern "C" void Gfx_SetStageBumpEnvLOffset(int stage, float value)
{
    s_diag.stageSetterCalls++;
    if (stage >= 0 && stage < 2)
        s_stages[stage].bumpLumOffset = value;
}

extern "C" GfxVertexBuffer *Gfx_CreateVertexBuffer(uint32_t vertexCount)
{
    // A new buffer can reuse a freed one's memory: forget cached bounds.
    memset(s_boundsCache, 0, sizeof(s_boundsCache));
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
    memset(s_boundsCache, 0, sizeof(s_boundsCache));
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

// The specular alpha of a draw that Direct3D would fog by it (fog on with
// neither vertex nor table fog), or -1 for any other draw. See port/backdrop_fog.h.
int BackdropFogAlpha(const GfxVertex *vertices, uint32_t vertexCount)
{
    if (!s_fogEnabled || s_fogVertexMode != GFX_FOG_NONE || s_fogTableMode != GFX_FOG_NONE ||
        !s_caps.hasFogLinear)
        return -1;
    const uint32_t alpha = vertices[0].specular >> 24;
    for (uint32_t i = 1; i < vertexCount; ++i) {
        if ((vertices[i].specular >> 24) != alpha) {
            LogDegradationOnce(DEGRADE_LOG_BACKDROP_FOG,
                               "AMIGA: backdrop draw with mixed specular alpha; fogged by its first vertex");
            break;
        }
    }
    return (int)alpha;
}

// video.colour_log: one line per frame in which the colours reaching the
// renderer change, to tell a colour the game asked for from one added after
// it. Off by default: it logs from the frame path. Sampled here rather than in
// game/ because the backend sees the same values and needs no divergence.
enum {
    kColourLogBackdropDraws = 6,
    kColourLogMaxLines = 4000,  // caps the file
    // Weather and time-of-day blends change the colours on every frame, so a
    // line is written at most this often; one stage then stays well under the
    // cap. A change shorter than this (one lightning frame) can be missed.
    kColourLogMinFrames = 10
};

// Compared with memcmp, so uint32_t only and no padding.
struct ColourLogState {
    uint32_t backdropDiffuse[kColourLogBackdropDraws];
    uint32_t backdropSpecular[kColourLogBackdropDraws];
    uint32_t backdropDraws;
    uint32_t fogColor;
    uint32_t fogEnabled;
    uint32_t fogVertexMode;
    uint32_t ambient;
    uint32_t light0;            // scene light, 0xRRGGBB
};

int s_colourLogLines;
// Lights of the first lit draw of the frame, logged when they change.
struct ColourLogLights {
    uint32_t sampled;
    uint32_t glMask;            // lights switched on for that draw
    int32_t value[8][12];       // type, range, ambient, diffuse, att0..2 x1000, phi x1000, x, y, z, enabled
};
ColourLogLights s_colourLogLightsNow;
ColourLogLights s_colourLogLightsLast;
uint32_t s_colourLogLightsFrame;
uint32_t s_colourLogFrame;
uint32_t s_colourLogLastLineFrame;
int s_colourLogSampled;
uint32_t s_colourLogWorldDiffuse;   // baked world colour; varies with the mesh, so not compared
ColourLogState s_colourLogNow;
ColourLogState s_colourLogLast;

uint32_t ColourLogChannel(float value)
{
    return value <= 0.0f ? 0u : (value >= 1.0f ? 255u : (uint32_t)(value * 255.0f));
}

int32_t ColourLogFixed(float value)
{
    return (int32_t)(value * 1000.0f);
}

int32_t ColourLogRgb(const GfxColorValue &c)
{
    return (int32_t)(ColourLogChannel(c.r) << 16 | ColourLogChannel(c.g) << 8 | ColourLogChannel(c.b));
}

void ColourLogSampleLights(void)
{
    ColourLogLights &n = s_colourLogLightsNow;
    n.sampled = 1;
    n.glMask = s_glCache.lightMask;
    for (int i = 0; i < 8; ++i) {
        const GfxLight &l = s_lights[i];
        int32_t *v = n.value[i];
        v[0] = l.type;
        v[1] = ColourLogFixed(l.range);
        v[2] = ColourLogRgb(l.ambient);
        v[3] = ColourLogRgb(l.diffuse);
        v[4] = ColourLogFixed(l.attenuation0);
        v[5] = ColourLogFixed(l.attenuation1);
        v[6] = ColourLogFixed(l.attenuation2);
        v[7] = ColourLogFixed(l.phi);
        v[8] = (int32_t)l.position.x;
        v[9] = (int32_t)l.position.y;
        v[10] = (int32_t)l.position.z;
        v[11] = (s_lightEnabled[i] && s_lightSet[i]) ? 1 : 0;
    }
}

void ColourLogSample(const GfxVertex *vertices, int backdropFogAlpha, bool prelit)
{
    if (!prelit && !s_colourLogLightsNow.sampled)
        ColourLogSampleLights();
    if (!s_colourLogSampled) {
        s_colourLogSampled = 1;
        const GfxLight &light = s_lights[0];
        s_colourLogNow.fogColor = s_fogColor;
        s_colourLogNow.fogEnabled = (uint32_t)s_fogEnabled;
        s_colourLogNow.fogVertexMode = (uint32_t)s_fogVertexMode;
        s_colourLogNow.ambient = s_ambient;
        s_colourLogNow.light0 = ColourLogChannel(light.diffuse.r) << 16 |
                                ColourLogChannel(light.diffuse.g) << 8 |
                                ColourLogChannel(light.diffuse.b);
    }
    if (backdropFogAlpha >= 0) {
        if (s_colourLogNow.backdropDraws < kColourLogBackdropDraws) {
            s_colourLogNow.backdropDiffuse[s_colourLogNow.backdropDraws] = vertices[0].diffuse;
            s_colourLogNow.backdropSpecular[s_colourLogNow.backdropDraws] = vertices[0].specular;
            s_colourLogNow.backdropDraws++;
        }
    } else if (s_colourLogWorldDiffuse == 0 && !prelit) {
        s_colourLogWorldDiffuse = vertices[0].diffuse;
    }
}

void ColourLogEndFrame(void)
{
    s_colourLogFrame++;
    if (s_colourLogSampled && s_colourLogLines < kColourLogMaxLines &&
        (s_colourLogLines == 0 || s_colourLogFrame - s_colourLogLastLineFrame >= kColourLogMinFrames) &&
        memcmp(&s_colourLogNow, &s_colourLogLast, sizeof(s_colourLogNow)) != 0) {
        const ColourLogState &c = s_colourLogNow;
        s_colourLogLast = c;
        s_colourLogLastLineFrame = s_colourLogFrame;
        if (++s_colourLogLines == kColourLogMaxLines)
            Sys_Log("AMIGA: colour log: line limit reached");
        Sys_Log("AMIGA: colour f=%lu fog=%lu/%lu/%08lx amb=%08lx light=%06lx world=%08lx "
                "n=%lu sky=%08lx/%02lx %08lx/%02lx %08lx/%02lx %08lx/%02lx %08lx/%02lx %08lx/%02lx",
                (unsigned long)s_colourLogFrame, (unsigned long)c.fogEnabled,
                (unsigned long)c.fogVertexMode, (unsigned long)c.fogColor,
                (unsigned long)c.ambient, (unsigned long)c.light0,
                (unsigned long)s_colourLogWorldDiffuse, (unsigned long)c.backdropDraws,
                (unsigned long)c.backdropDiffuse[0], (unsigned long)(c.backdropSpecular[0] >> 24),
                (unsigned long)c.backdropDiffuse[1], (unsigned long)(c.backdropSpecular[1] >> 24),
                (unsigned long)c.backdropDiffuse[2], (unsigned long)(c.backdropSpecular[2] >> 24),
                (unsigned long)c.backdropDiffuse[3], (unsigned long)(c.backdropSpecular[3] >> 24),
                (unsigned long)c.backdropDiffuse[4], (unsigned long)(c.backdropSpecular[4] >> 24),
                (unsigned long)c.backdropDiffuse[5], (unsigned long)(c.backdropSpecular[5] >> 24));
    }
    if (s_colourLogLightsNow.sampled && s_colourLogLines < kColourLogMaxLines &&
        s_colourLogFrame - s_colourLogLightsFrame >= kColourLogMinFrames &&
        memcmp(&s_colourLogLightsNow, &s_colourLogLightsLast, sizeof(s_colourLogLightsNow)) != 0) {
        s_colourLogLightsLast = s_colourLogLightsNow;
        s_colourLogLightsFrame = s_colourLogFrame;
        s_colourLogLines++;
        Sys_Log("AMIGA: lights f=%lu on=%02lx global_amb=%08lx", (unsigned long)s_colourLogFrame,
                (unsigned long)s_colourLogLightsNow.glMask, (unsigned long)s_ambient);
        for (int i = 0; i < 8; ++i) {
            const int32_t *v = s_colourLogLightsNow.value[i];
            if (v[11] == 0)
                continue;
            s_colourLogLines++;
            Sys_Log("AMIGA: light%d type=%ld range=%ld amb=%06lx dif=%06lx att=%ld/%ld/%ld phi=%ld pos=%ld,%ld,%ld",
                    i, (long)v[0], (long)v[1], (unsigned long)v[2], (unsigned long)v[3], (long)v[4],
                    (long)v[5], (long)v[6], (long)v[7], (long)v[8], (long)v[9], (long)v[10]);
        }
    }
    memset(&s_colourLogLightsNow, 0, sizeof(s_colourLogLightsNow));
    memset(&s_colourLogNow, 0, sizeof(s_colourLogNow));
    s_colourLogSampled = 0;
    s_colourLogWorldDiffuse = 0;
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
    // The out-parameter is read by the reflection texcoord path even when the
    // cached product is reused, so every return path fills it.
    const bool worldViewHit = s_glCache.loadedWorldViewGen == s_worldViewGeneration;
    if (worldViewHit) {
        s_diag.modelviewSkips++;
    } else {
        GfxMatrix world, view;
        if (s_transformSet[GFX_TRANSFORM_WORLD])
            world = s_transforms[GFX_TRANSFORM_WORLD];
        else
            Identity(&world);
        if (s_transformSet[GFX_TRANSFORM_VIEW])
            view = s_transforms[GFX_TRANSFORM_VIEW];
        else
            Identity(&view);
        MultiplyRowMatrices(world, view, &s_glCache.worldView);
        s_glCache.loadedWorldViewGen = s_worldViewGeneration;
    }
    *outWorldView = s_glCache.worldView;

    // The projection changes per camera/pass, not per draw. It is also
    // replaced by the 2D ortho projection, so BeginScreenDraw clears the
    // loaded generation. D3D vertex submission always runs in the modelview
    // mode; GL leaves the mode wherever the last load put it, so this function
    // must restore it even when the modelview matrix itself is reused.
    bool modeIsModelView = true;
    if (s_glCache.loadedProjectionGen != s_projectionGeneration) {
        GfxMatrix proj;
        if (s_transformSet[GFX_TRANSFORM_PROJECTION])
            proj = s_transforms[GFX_TRANSFORM_PROJECTION];
        else
            Identity(&proj);
        BuildGlProjectionMatrix(proj, s_glCache.glProjection);
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(s_glCache.glProjection);
        s_glCache.loadedProjectionGen = s_projectionGeneration;
        s_diag.projectionLoads++;
        s_diag.matrixModeSwitches++;
        modeIsModelView = false;
    }

#if defined(OPENCMR2_WARPOS)
    if (s_batching) {
        // Vertices are transformed by world*view on the CPU (BatchEmit3D).
        if (!s_modelviewIdentity) {
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            s_modelviewIdentity = 1;
            s_diag.matrixModeSwitches++;
        } else if (!modeIsModelView) {
            glMatrixMode(GL_MODELVIEW);
            s_diag.matrixModeSwitches++;
        }
        return;
    }
#endif
    if (!worldViewHit) {
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(&s_glCache.worldView._11);
        s_diag.modelviewLoads++;
        s_diag.matrixModeSwitches++;
    } else if (!modeIsModelView) {
        glMatrixMode(GL_MODELVIEW);
        s_diag.matrixModeSwitches++;
    }
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

    // The MiniGL viewport maps the game's top-left rows onto GL's bottom-left
    // origin, which reverses the winding: a Direct3D clockwise front face
    // arrives clockwise in OpenGL here. The host GL path keeps the D3D rows
    // and flips only at presentation, which is why it can use GL_CCW.
    if (s_glCache.frontFace != GL_CW) {
        glFrontFace(GL_CW);
        s_glCache.frontFace = GL_CW;
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

// Lighting policy:
// - lighting off (modes 4/5): prelit, GL lighting is disabled and the vertex
//   colour goes straight to glColor.
// - lighting on: GL fixed-function lighting with up to 8 lights. D3D material
//   sources that name a vertex colour are expressed through GL_COLOR_MATERIAL.
//   The game uses one such source at a time: emissive=COLOR1 for the baked
//   world meshes (modes 0/1) and diffuse=COLOR1 for mode 3.
//
// backdropFogAlpha is BackdropFogAlpha()'s result for the draw.
//
// Positional lights: minigl.library 29.x applies only one directional light
// and treats a point or spot light as a direction, so GL is given directional
// lights only. A draw that a point or spot light reaches (its bounding sphere
// within the light's range) is lit on the CPU instead (CpuLightDraw,
// port/d3d_light.h) and drawn unlit. The upload below sets the light
// parameters; which lights are on is decided per draw after it.
struct DrawGeometry {
    const GfxVertex *vertices;
    const uint16_t *indices;    // NULL: the first `count` vertices
    uint32_t count;
    int state;                  // 0 not computed, 1 world sphere ready, -1 empty
    LightRangeSphere world;
};

// 1/sqrt(x) for normalising a normal. newlib's sqrtf is software on the PPC
// targets (about a microsecond); frsqrte gives 5 bits and each Newton step
// doubles them, so three steps exceed single precision.
inline float InverseSqrt(float x)
{
#if defined(OPENCMR2_WARPOS)
    double estimate;
    const double value = x;
    __asm__("frsqrte %0,%1" : "=f"(estimate) : "f"(value));
    estimate = estimate * (1.5 - 0.5 * value * estimate * estimate);
    estimate = estimate * (1.5 - 0.5 * value * estimate * estimate);
    estimate = estimate * (1.5 - 0.5 * value * estimate * estimate);
    return (float)estimate;
#else
    return 1.0f / sqrtf(x);
#endif
}

const LightRangeSphere *DrawWorldSphere(DrawGeometry *geometry)
{
    if (geometry->state == 0) {
        LightRangeSphere model;
        int haveModel;
        geometry->state = -1;
        if (s_drawFromVertexBuffer) {
            // Vertex-buffer positions are written when a mesh is uploaded and
            // only move with damage deformation, so the sphere is cached.
            const uint32_t hash = (uint32_t)(((uintptr_t)geometry->vertices >> 4) ^
                                             ((uintptr_t)geometry->indices >> 1) ^
                                             (geometry->count * 2654435761u));
            BoundsCacheEntry &e = s_boundsCache[(hash ^ (hash >> 16)) & (kBoundsCacheSize - 1)];
            if (!e.valid || e.vertices != geometry->vertices || e.indices != geometry->indices ||
                e.count != geometry->count) {
                e.vertices = geometry->vertices;
                e.indices = geometry->indices;
                e.count = geometry->count;
                e.valid = LightRange_Bounds(&geometry->vertices[0].x, sizeof(GfxVertex), geometry->indices,
                                            geometry->count, &e.model);
            }
            haveModel = e.valid;
            model = e.model;
        } else {
            haveModel = LightRange_Bounds(&geometry->vertices[0].x, sizeof(GfxVertex), geometry->indices,
                                          geometry->count, &model);
        }
        if (haveModel) {
            GfxMatrix world;
            if (s_transformSet[GFX_TRANSFORM_WORLD])
                world = s_transforms[GFX_TRANSFORM_WORLD];
            else
                Identity(&world);
#if defined(OPENCMR2_WARPOS)
            // LightRange_ToWorld with the largest axis scale from one
            // reciprocal square root instead of three newlib sqrtf calls
            // (software on the PPC targets, about 0.45 us each).
            const float *m = &world._11;
            float largest = 0.0f;
            for (int r = 0; r < 3; ++r) {
                const float axis = m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2];
                if (axis > largest)
                    largest = axis;
            }
            geometry->world.x = model.x * m[0] + model.y * m[4] + model.z * m[8] + m[12];
            geometry->world.y = model.x * m[1] + model.y * m[5] + model.z * m[9] + m[13];
            geometry->world.z = model.x * m[2] + model.y * m[6] + model.z * m[10] + m[14];
            geometry->world.radius = largest > 0.0f ? model.radius * largest * InverseSqrt(largest) : 0.0f;
#else
            geometry->world = LightRange_ToWorld(model, &world._11);
#endif
            geometry->state = 1;
        }
    }
    return geometry->state == 1 ? &geometry->world : NULL;
}

bool LightIsPositional(const GfxLight &light)
{
    return light.type == GFX_LIGHT_POINT || light.type == GFX_LIGHT_SPOT;
}

bool LightOn(int i)
{
    return s_lightEnabled[i] && s_lightSet[i] && (s_debugLightMask & (1u << i)) != 0;
}

// Whether any enabled point or spot light can reach the draw.
bool PositionalLightReachesDraw(DrawGeometry *geometry)
{
    for (int i = 0; i < 8; ++i) {
        if (!LightOn(i) || !LightIsPositional(s_lights[i]))
            continue;
        const GfxLight &l = s_lights[i];
        if (l.range <= 0.0f)
            return true;
        const LightRangeSphere *sphere = DrawWorldSphere(geometry);
        if (sphere == NULL)
            return true;
#if defined(OPENCMR2_WARPOS)
        // LightRange_NearestDistance(...) <= range, without the square root:
        // |centre - light| - radius <= range  <=>  |centre - light|^2 <=
        // (range + radius)^2, both sides being non-negative.
        const float dx = sphere->x - l.position.x, dy = sphere->y - l.position.y, dz = sphere->z - l.position.z;
        const float reach = l.range + sphere->radius;
        if (dx * dx + dy * dy + dz * dz <= reach * reach)
            return true;
#else
        if (LightRange_NearestDistance(l.position.x, l.position.y, l.position.z, *sphere) <= l.range)
            return true;
#endif
    }
    return false;
}

// Byte to [0,1] without an integer-to-float conversion (slow on a 32-bit
// PowerPC: it goes through memory).
const float *ByteToUnitTable(void)
{
    static float table[256];
    static int built;
    if (!built) {
        for (int i = 0; i < 256; ++i)
            table[i] = (float)i / 255.0f;
        built = 1;
    }
    return table;
}

GfxColorValue UnpackColorValue(uint32_t argb)
{
#if defined(OPENCMR2_WARPOS)
    const float *unit = ByteToUnitTable();
    const GfxColorValue fast = { unit[(argb >> 16) & 0xffu], unit[(argb >> 8) & 0xffu], unit[argb & 0xffu],
                                 unit[argb >> 24] };
    return fast;
#endif
    float f[4];
    UnpackColorFloats(argb, f);
    const GfxColorValue c = { f[0], f[1], f[2], f[3] };
    return c;
}

// The material colour a D3D material source selects for this vertex.
GfxColorValue MaterialSourceColor(int source, const GfxColorValue &material, const GfxVertex &v)
{
    if (s_colorVertex) {
        if (source == GFX_MCS_COLOR1)
            return UnpackColorValue(v.diffuse);
        if (source == GFX_MCS_COLOR2)
            return UnpackColorValue(v.specular);
    }
    return material;
}

uint32_t PackColorValue(const GfxColorValue &c)
{
    return (uint32_t)(c.a * 255.0f + 0.5f) << 24 | (uint32_t)(c.r * 255.0f + 0.5f) << 16 |
           (uint32_t)(c.g * 255.0f + 0.5f) << 8 | (uint32_t)(c.b * 255.0f + 0.5f);
}


// Lights the vertices a draw uses with Direct3D's vertex lighting and returns
// a copy (same indexing) whose diffuse colour is the lit colour, for the
// unlit path; NULL when the scratch buffer cannot grow.
// Everything about a draw's lighting that does not depend on the vertex,
// computed once: the unit vector of a directional light, a spot light's axis
// and cone cosines, the constant ambient sum. D3DLight_Vertex
// (port/d3d_light.h) is the reference for the arithmetic; it recomputes these
// per vertex with newlib's software sqrtf and cosf, about a microsecond each
// on the PPC targets.
struct DrawLight {
    const GfxLight *light;
    int type;
    float vector[3];   // directional: unit vector to the light
    float axis[3];     // spot: unit direction
    float inner, outer;
    int hasAxis;
};
struct LightSetup {
    DrawLight lights[8];
    int count, positional;
    float ambientSum[3];
    GfxMatrix world;
    // Without any light: colour = emissive + ambient sum * ambient, in integers.
    bool vertexDiffuse, vertexAmbient, vertexEmissive;
    uint32_t materialDiffuse, materialAmbient, materialEmissive;
    int32_t factor[3];
};

// positionalReach: whether a point or spot light can reach the draw at all
// (PositionalLightReachesDraw); when not, those lights are left out, and a
// draw lit only by them takes the integer no-light path.
void BuildLightSetup(LightSetup *setup, bool positionalReach)
{
    if (s_specularEnabled) {
        LogDegradationOnce(DEGRADE_LOG_COLOR_SOURCE,
                           "AMIGA: CPU vertex lighting does not light specular");
    }
    if (s_transformSet[GFX_TRANSFORM_WORLD])
        setup->world = s_transforms[GFX_TRANSFORM_WORLD];
    else
        Identity(&setup->world);
    const GfxColorValue globalAmbient = UnpackColorValue(s_ambient);
    setup->ambientSum[0] = globalAmbient.r;
    setup->ambientSum[1] = globalAmbient.g;
    setup->ambientSum[2] = globalAmbient.b;
    setup->count = 0;
    setup->positional = 0;
    for (int i = 0; i < 8; ++i) {
        if (!LightOn(i))
            continue;
        const GfxLight &l = s_lights[i];
        if (l.type != GFX_LIGHT_DIRECTIONAL && !positionalReach)
            continue;
        DrawLight &d = setup->lights[setup->count];
        d.light = &l;
        d.type = l.type;
        d.hasAxis = 0;
        const float squared = l.direction.x * l.direction.x + l.direction.y * l.direction.y +
                              l.direction.z * l.direction.z;
        const float inverse = squared > 0.0f ? InverseSqrt(squared) : 0.0f;
        if (l.type == GFX_LIGHT_DIRECTIONAL) {
            if (squared <= 0.0f)
                continue;
            d.vector[0] = -l.direction.x * inverse;
            d.vector[1] = -l.direction.y * inverse;
            d.vector[2] = -l.direction.z * inverse;
            // A directional light's ambient term is the same for every vertex.
            setup->ambientSum[0] += l.ambient.r;
            setup->ambientSum[1] += l.ambient.g;
            setup->ambientSum[2] += l.ambient.b;
        } else {
            if (l.type == GFX_LIGHT_SPOT) {
                // The cone angles rarely change; newlib's cosf is software.
                static float cachedTheta[8], cachedPhi[8], cachedInner[8], cachedOuter[8];
                static unsigned char cachedValid[8];
                if (!cachedValid[i] || cachedTheta[i] != l.theta || cachedPhi[i] != l.phi) {
                    cachedTheta[i] = l.theta;
                    cachedPhi[i] = l.phi;
                    cachedInner[i] = cosf(l.theta * 0.5f);
                    cachedOuter[i] = cosf(l.phi * 0.5f);
                    cachedValid[i] = 1;
                }
                d.inner = cachedInner[i];
                d.outer = cachedOuter[i];
                if (squared > 0.0f) {
                    d.axis[0] = l.direction.x * inverse;
                    d.axis[1] = l.direction.y * inverse;
                    d.axis[2] = l.direction.z * inverse;
                    d.hasAxis = 1;
                }
            }
            setup->positional++;
        }
        setup->count++;
    }
    setup->vertexDiffuse = s_colorVertex && (s_diffuseSource == GFX_MCS_COLOR1 || s_diffuseSource == GFX_MCS_COLOR2);
    setup->vertexAmbient = s_colorVertex && (s_ambientSource == GFX_MCS_COLOR1 || s_ambientSource == GFX_MCS_COLOR2);
    setup->vertexEmissive = s_colorVertex && (s_emissiveSource == GFX_MCS_COLOR1 || s_emissiveSource == GFX_MCS_COLOR2);
    setup->materialDiffuse = PackColorValue(s_material.diffuse);
    setup->materialAmbient = PackColorValue(s_material.ambient);
    setup->materialEmissive = PackColorValue(s_material.emissive);
    for (int c = 0; c < 3; ++c) {
        const float f = setup->ambientSum[c] < 0.0f ? 0.0f : (setup->ambientSum[c] > 255.0f ? 255.0f : setup->ambientSum[c]);
        setup->factor[c] = (int32_t)(f * 65536.0f + 0.5f);
    }
#if defined(OPENCMR2_WARPOS)
    s_batchStats.litDraws++;
    s_batchStats.directionalLights += (uint32_t)(setup->count - setup->positional);
    s_batchStats.positionalLights += (uint32_t)setup->positional;
#endif
}

// The lit colour (0xAARRGGBB) of one vertex: Direct3D's vertex lighting,
// alpha = the diffuse alpha.
inline uint32_t LightVertex(const LightSetup &setup, const GfxVertex &v)
{
#if defined(OPENCMR2_WARPOS)
    s_batchStats.litVertices++;
#endif
    if (setup.count == 0) {
        // No light is on: integers only (16.16 ambient factors, byte
        // colours). Converting a vertex colour to floats and back is the
        // expensive part on a 32-bit PowerPC, and most of a stage's lit draws
        // are of this kind.
        const uint32_t diffuse = !setup.vertexDiffuse ? setup.materialDiffuse
            : (s_diffuseSource == GFX_MCS_COLOR1 ? v.diffuse : v.specular);
        const uint32_t ambient = !setup.vertexAmbient ? setup.materialAmbient
            : (s_ambientSource == GFX_MCS_COLOR1 ? v.diffuse : v.specular);
        const uint32_t emissive = !setup.vertexEmissive ? setup.materialEmissive
            : (s_emissiveSource == GFX_MCS_COLOR1 ? v.diffuse : v.specular);
        uint32_t lit = diffuse & 0xff000000u;
        for (int c = 0; c < 3; ++c) {
            const int shift = 16 - 8 * c;   // r, g, b
            const int64_t scaled = (int64_t)setup.factor[c] * (int32_t)((ambient >> shift) & 0xffu);
            int32_t value = (int32_t)((emissive >> shift) & 0xffu) + (int32_t)((scaled + 32768) >> 16);
            if (value > 255)
                value = 255;
            lit |= (uint32_t)value << shift;
        }
        return lit;
    }
#if defined(OPENCMR2_WARPOS)
    s_batchStats.litByLights++;
#endif
    const GfxMatrix &world = setup.world;
    float n[3] = {
        v.nx * world._11 + v.ny * world._21 + v.nz * world._31,
        v.nx * world._12 + v.ny * world._22 + v.nz * world._32,
        v.nx * world._13 + v.ny * world._23 + v.nz * world._33
    };
    if (s_normalizeNormals) {
        const float squared = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
        if (squared > 0.0f) {
            const float scale = InverseSqrt(squared);
            n[0] *= scale;
            n[1] *= scale;
            n[2] *= scale;
        }
    }
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    if (setup.positional != 0) {
        pos[0] = v.x * world._11 + v.y * world._21 + v.z * world._31 + world._41;
        pos[1] = v.x * world._12 + v.y * world._22 + v.z * world._32 + world._42;
        pos[2] = v.x * world._13 + v.y * world._23 + v.z * world._33 + world._43;
    }
    float amb[3] = { setup.ambientSum[0], setup.ambientSum[1], setup.ambientSum[2] };
    float dif[3] = { 0.0f, 0.0f, 0.0f };
    for (int d = 0; d < setup.count; ++d) {
        const DrawLight &dl = setup.lights[d];
        const GfxLight &l = *dl.light;
        float scale = 1.0f;
        float nDotL;
        if (dl.type == GFX_LIGHT_DIRECTIONAL) {
            nDotL = n[0] * dl.vector[0] + n[1] * dl.vector[1] + n[2] * dl.vector[2];
        } else {
            const float tx = l.position.x - pos[0];
            const float ty = l.position.y - pos[1];
            const float tz = l.position.z - pos[2];
            const float squared = tx * tx + ty * ty + tz * tz;
            if (l.range > 0.0f && squared > l.range * l.range)
                continue;
            float L[3] = { 0.0f, 0.0f, 1.0f };
            float distance = 0.0f;
            if (squared > 0.0f) {
                const float inverse = InverseSqrt(squared);
                distance = squared * inverse;
                L[0] = tx * inverse;
                L[1] = ty * inverse;
                L[2] = tz * inverse;
            }
            const float denom = l.attenuation0 + l.attenuation1 * distance + l.attenuation2 * squared;
            scale = denom > 0.0f ? 1.0f / denom : 1.0f;
            if (dl.type == GFX_LIGHT_SPOT) {
                const float rho = dl.hasAxis
                    ? -(L[0] * dl.axis[0] + L[1] * dl.axis[1] + L[2] * dl.axis[2]) : 1.0f;
                if (rho <= dl.outer) {
                    scale = 0.0f;
                } else if (rho < dl.inner) {
                    const float span = dl.inner - dl.outer > 1e-6f ? dl.inner - dl.outer : 1e-6f;
                    scale *= powf((rho - dl.outer) / span, l.falloff);
                }
            }
            amb[0] += l.ambient.r * scale;
            amb[1] += l.ambient.g * scale;
            amb[2] += l.ambient.b * scale;
            nDotL = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        }
        if (nDotL > 0.0f) {
            dif[0] += l.diffuse.r * nDotL * scale;
            dif[1] += l.diffuse.g * nDotL * scale;
            dif[2] += l.diffuse.b * nDotL * scale;
        }
    }
    const GfxColorValue diffuse = MaterialSourceColor(s_diffuseSource, s_material.diffuse, v);
    const GfxColorValue ambient = MaterialSourceColor(s_ambientSource, s_material.ambient, v);
    const GfxColorValue emissive = MaterialSourceColor(s_emissiveSource, s_material.emissive, v);
    // Clamped to [0,1], then to bytes with signed conversions (an unsigned
    // float-to-integer conversion is a branchy sequence on PPC).
    const float r = D3DLight_Clamp01(emissive.r + amb[0] * ambient.r + dif[0] * diffuse.r);
    const float g = D3DLight_Clamp01(emissive.g + amb[1] * ambient.g + dif[1] * diffuse.g);
    const float b = D3DLight_Clamp01(emissive.b + amb[2] * ambient.b + dif[2] * diffuse.b);
    const float alpha = D3DLight_Clamp01(diffuse.a);
    return (uint32_t)(int32_t)(alpha * 255.0f + 0.5f) << 24 | (uint32_t)(int32_t)(r * 255.0f + 0.5f) << 16 |
           (uint32_t)(int32_t)(g * 255.0f + 0.5f) << 8 | (uint32_t)(int32_t)(b * 255.0f + 0.5f);
}

// Lights the vertices a draw uses and returns a copy (same indexing) whose
// diffuse colour is the lit colour, for the unlit path; NULL when the scratch
// buffer cannot grow. An indexed draw lights the vertices it references, each
// once: a small draw out of a large vertex buffer spans far more than it uses.
const GfxVertex *CpuLightDraw(const LightSetup &setup, const GfxVertex *vertices, uint32_t vertexCount,
                              const uint16_t *indices, uint32_t indexCount)
{
    if (vertexCount > s_cpuLitCapacity) {
        GfxVertex *grown = new (std::nothrow) GfxVertex[vertexCount];
        uint32_t *stamps = new (std::nothrow) uint32_t[vertexCount];
        if (grown == NULL || stamps == NULL) {
            delete[] grown;
            delete[] stamps;
            Sys_Log("AMIGA: CPU lighting: cannot grow scratch to %lu vertices", (unsigned long)vertexCount);
            return NULL;
        }
        memset(stamps, 0, vertexCount * sizeof(uint32_t));
        delete[] s_cpuLitVertices;
        delete[] s_cpuLitStamp;
        s_cpuLitVertices = grown;
        s_cpuLitStamp = stamps;
        s_cpuLitCapacity = vertexCount;
        s_cpuLitStampGen = 0;
    }
    if (++s_cpuLitStampGen == 0) {
        memset(s_cpuLitStamp, 0, s_cpuLitCapacity * sizeof(uint32_t));
        s_cpuLitStampGen = 1;
    }
    const uint32_t count = indices != NULL ? indexCount : vertexCount;
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t i = indices != NULL ? indices[k] : k;
        if (s_cpuLitStamp[i] == s_cpuLitStampGen)
            continue;
        s_cpuLitStamp[i] = s_cpuLitStampGen;
        GfxVertex &out = s_cpuLitVertices[i];
        out = vertices[i];
        out.diffuse = LightVertex(setup, vertices[i]);
    }
    return s_cpuLitVertices;
}

bool Apply3DLightingAndFog(int backdropFogAlpha)
{
    const bool usePrelitPath = !s_lightingEnabled || s_cpuLitDraw;
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

        if (!s_lightingCache.valid) {
            GfxMatrix view;
            if (s_transformSet[GFX_TRANSFORM_VIEW])
                view = s_transforms[GFX_TRANSFORM_VIEW];
            else
                Identity(&view);
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
                if (!s_lightEnabled[i] || !s_lightSet[i])
                    continue;
                const GfxLight &l = s_lights[i];
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

            s_diag.lightingUploads++;
            s_lightingCache.valid = 1;
        } else {
            s_diag.lightingSkips++;
        }

        // GL gets the directional lights only. A draw a positional light
        // reaches is CPU-lit and drawn on the prelit path; one it does not
        // reach gets nothing from that light.
        uint32_t lightMask = 0;
        for (int i = 0; i < 8; ++i) {
            if (LightOn(i) && !LightIsPositional(s_lights[i]))
                lightMask |= 1u << i;
        }
        if (lightMask != s_glCache.lightMask) {
            for (int i = 0; i < 8; ++i) {
                const uint32_t bit = 1u << i;
                if ((lightMask & bit) == (s_glCache.lightMask & bit) && s_glCache.lightMask != ~0u)
                    continue;
                if ((lightMask & bit) != 0)
                    glEnable((GLenum)(GL_LIGHT0 + i));
                else
                    glDisable((GLenum)(GL_LIGHT0 + i));
            }
            s_glCache.lightMask = lightMask;
        }
    }

    const bool activeFogMode = s_fogTableMode == GFX_FOG_LINEAR || s_fogVertexMode == GFX_FOG_LINEAR;
    // Alpha 255 is no fog at all; it needs no fog state.
    const bool backdropFog = backdropFogAlpha >= 0 && backdropFogAlpha < 255;
    const int wantFog = (backdropFog ||
                         (s_fogEnabled && activeFogMode && s_caps.hasFogLinear && s_fogEnd > s_fogStart)) ? 1 : 0;
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
        float fogStart = s_fogStart;
        float fogEnd = s_fogEnd;
        if (backdropFog)
            BackdropFog_Limits((unsigned int)backdropFogAlpha, &fogStart, &fogEnd);
        if (s_glCache.fogStart != fogStart || s_glCache.fogEnd != fogEnd) {
            glFogi(GL_FOG_MODE, GL_LINEAR);
            glFogf(GL_FOG_START, fogStart);
            glFogf(GL_FOG_END, fogEnd);
            s_glCache.fogStart = fogStart;
            s_glCache.fogEnd = fogEnd;
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
                           "AMIGA: BUMPENVMAP perturbation disabled; the stage modulates by its luminance");
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

        // A second stage that passes the colour through is left off: GL_MODULATE
        // would multiply its texture in (port/stage_passthrough.h).
        const bool passThrough = stage == 1 &&
            GfxStage_ColourPassesCurrent(st.colorOp, st.colorArg1, st.colorArg2);
        if (passThrough && GfxStage_AlphaReadsTexture(st.alphaOp, st.alphaArg1, st.alphaArg2))
            LogDegradationOnce(DEGRADE_LOG_PASSTHROUGH_ALPHA,
                               "AMIGA: pass-through stage left off; its texture alpha is not applied");
        // An additive second stage (the car's reflection, glows) on a library
        // without GL_ADD is left off: modulating by its texture instead
        // darkens the whole surface (the car turned almost black on the R200
        // PPC stack). Without the stage the surface keeps its own colours.
        const bool additiveWithoutAdd = stage == 1 && !s_caps.hasAdd &&
            (st.colorOp == GFX_TOP_ADD || st.colorOp == GFX_TOP_ADDSIGNED || st.colorOp == GFX_TOP_ADDSIGNED2X);
        if (additiveWithoutAdd && st.texture != NULL)
            LogDegradationOnce(DEGRADE_LOG_NO_ADD,
                               "AMIGA: no GL_ADD; additive second stages (reflections) are left off");
        const bool stageDisabled = (stage == 0 && st.colorOp == GFX_TOP_DISABLE) ||
                                   (stage == 1 && (s_stages[0].colorOp == GFX_TOP_DISABLE || st.colorOp == GFX_TOP_DISABLE)) ||
                                   passThrough || additiveWithoutAdd || st.texture == NULL;
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
            // A bump stage only scales the next stage by its luminance: draw
            // that, not the bump texel (port/bump_luminance.h).
            if (st.colorOp == GFX_TOP_BUMPENVMAPLUMINANCE || st.colorOp == GFX_TOP_BUMPENVMAP) {
                const unsigned int lumId = st.colorOp == GFX_TOP_BUMPENVMAP
                    ? EnsureBumpLuminanceTexture(drawn, 0.0f, 1.0f)
                    : EnsureBumpLuminanceTexture(drawn, st.bumpLumScale, st.bumpLumOffset);
                if (lumId != 0)
                    texId = lumId;
            }
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
    // Texcoord half, computed once per draw. `plainList` means the primitive
    // needs no flatten mapping; `directTexCoord` means every active stage takes
    // its coordinates straight from the vertex (no reflection texgen and no
    // texture transform), which is what allows the inlined emit loop.
    bool plainList;
    bool directTexCoord;
    int activeStages;
    bool stageUsesU1[2];
};

// Draw-invariant stage-zero colour selection, computed once per draw rather
// than per vertex.
EmitPlan BuildEmitPlan(bool prelit, GLenum mode, int activeStages)
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

    plan.plainList = mode == GL_TRIANGLES || mode == GL_LINES;
    plan.activeStages = activeStages;
    plan.directTexCoord = plan.plainList;
    for (int i = 0; i < 2; ++i) {
        plan.stageUsesU1[i] = false;
        if (i >= activeStages)
            continue;
        const StageState &st = s_stages[i];
        if ((st.texCoordIndex & 0xffff0000u) == GFX_TCI_CAMERASPACEREFLECTIONVECTOR)
            plan.directTexCoord = false;
        const int xformType = i == 0 ? GFX_TRANSFORM_TEXTURE0 : GFX_TRANSFORM_TEXTURE1;
        if (st.textureTransform != GFX_TTFF_DISABLE && s_transformSet[xformType])
            plan.directTexCoord = false;
        plan.stageUsesU1[i] = (st.texCoordIndex & 0xffffu) == 1u;
    }
    return plan;
}

#if defined(OPENCMR2_WARPOS)
// One 3D vertex of an open batch: Emit3DVertex with the position and normal
// taken through world*view (row vectors, as the D3D matrices are stored).
void BatchEmit3D(const GfxVertex *v, bool normals, const EmitPlan &plan, int activeStages,
                 const GfxMatrix &m)
{
    if (activeStages >= 1) {
        float u0, v0;
        ComputeStageTexCoord(0, v, m, &u0, &v0);
        RawTexCoord2f(u0, v0);
    }
    if (activeStages >= 2) {
        float u1, v1;
        ComputeStageTexCoord(1, v, m, &u1, &v1);
        RawTexCoord1_2f(u1, v1);
    }
    if (plan.emitColor) {
        const uint32_t rgb = plan.overrideColor ? plan.color : v->diffuse;
        const uint32_t alpha = plan.overrideAlpha ? plan.color : v->diffuse;
        RawColor4ub((GLubyte)((rgb >> 16) & 0xffu), (GLubyte)((rgb >> 8) & 0xffu),
                    (GLubyte)(rgb & 0xffu), (GLubyte)((alpha >> 24) & 0xffu));
    }
    if (normals)
        RawNormal3f(v->nx * m._11 + v->ny * m._21 + v->nz * m._31,
                    v->nx * m._12 + v->ny * m._22 + v->nz * m._32,
                    v->nx * m._13 + v->ny * m._23 + v->nz * m._33);
    RawVertex3f(v->x * m._11 + v->y * m._21 + v->z * m._31 + m._41,
                v->x * m._12 + v->y * m._22 + v->z * m._32 + m._42,
                v->x * m._13 + v->y * m._23 + v->z * m._33 + m._43);
}
#endif

#if defined(OPENCMR2_WARPOS)
struct BucketVertex {
    float x, y, z;
    uint32_t argb;
    float u0, v0, u1, v1;
};
// The texture state a draw wants, as Apply3DTextureStages leaves the GL cache.
struct TexKey {
    unsigned int tex[2];
    int enabled[2];
    GLint env[2];
};
// The game's raster state a bucket is drawn with: what Apply3DRasterState
// reads.
struct RasterKey {
    int depthTest, depthWrite, depthFunc, cullMode;
    int alphaBlend, srcBlend, dstBlend;
    int alphaTest, alphaFunc;
    uint32_t alphaRef;
};

RasterKey CurrentRasterKey(void)
{
    const RasterKey key = { s_depthTest, s_depthWrite, s_depthFunc, s_cullMode, s_alphaBlend, s_srcBlend,
                            s_dstBlend, s_alphaTest, s_alphaFunc, s_alphaRef };
    return key;
}

void SetRasterGlobals(const RasterKey &key)
{
    s_depthTest = key.depthTest;
    s_depthWrite = key.depthWrite;
    s_depthFunc = key.depthFunc;
    s_cullMode = key.cullMode;
    s_alphaBlend = key.alphaBlend;
    s_srcBlend = key.srcBlend;
    s_dstBlend = key.dstBlend;
    s_alphaTest = key.alphaTest;
    s_alphaFunc = key.alphaFunc;
    s_alphaRef = key.alphaRef;
}

bool SameRasterKey(const RasterKey &a, const RasterKey &b)
{
    return a.depthTest == b.depthTest && a.depthWrite == b.depthWrite && a.depthFunc == b.depthFunc &&
           a.cullMode == b.cullMode && a.alphaBlend == b.alphaBlend && a.srcBlend == b.srcBlend &&
           a.dstBlend == b.dstBlend && a.alphaTest == b.alphaTest && a.alphaFunc == b.alphaFunc &&
           a.alphaRef == b.alphaRef;
}

// A bucket holds each vertex of a draw once, with 16-bit indices: the stack
// sends shared vertices to the card once (the Zorro bus is the bottleneck,
// about 0.3 us per vertex), and nothing is copied twice on the way.
struct Bucket {
    TexKey key;
    RasterKey raster;
    int format;
    BucketVertex *vertices;
    uint16_t *indices;
    uint32_t count, capacity;             // vertices
    uint32_t indexCount, indexCapacity;
};
enum { kMaxBuckets = 256, kBucketVertexLimit = 4096, kBucketIndexLimit = 16384 };
Bucket s_buckets[kMaxBuckets];
int s_bucketCount;
// Per draw and source vertex: its clip outcode (computed when a triangle that
// survived back-face culling first references it) and, once a kept triangle
// uses it, its place in the bucket.
struct SourceVertex {
    uint32_t codeStamp, slotStamp;
    uint16_t slot;
    uint8_t outcode;
};
SourceVertex *s_source;
uint32_t s_sourceCapacity, s_sourceGen;
// Direct-mapped lookup from a draw's state to its bucket (index + 1).
unsigned char s_bucketLookup[256];

// Whether the stages' textures can be read without GL work: nothing to
// upload, no fallback texture to create, the sampler state already on the
// texture object.
bool StagesCapturable(void)
{
    for (int stage = 0; stage < 2; ++stage) {
        const StageState &st = s_stages[stage];
        GfxTexture *t = st.texture;
        if (t == NULL)
            continue;
        if (st.colorOp == GFX_TOP_BUMPENVMAP || st.colorOp == GFX_TOP_BUMPENVMAPLUMINANCE)
            return false;
        // The neutral stand-in of a cube or render-target texture has its
        // sampler set on every draw (it is not tracked): not capturable.
        if ((t->flags & (GFX_TEXTURE_CUBE | GFX_TEXTURE_RENDER_TARGET)) != 0)
            return false;
        if (t->dirty || t->glId == 0 || !t->samplerValid || t->samplerWrap != StageWrapMode(stage) ||
            t->samplerMinFilter != FilterMode(st.minFilter) || t->samplerMagFilter != FilterMode(st.magFilter))
            return false;
    }
    return true;
}

// Runs Apply3DTextureStages with its GL calls suppressed and returns the
// texture state it would have set; the GL cache is put back to what GL holds.
int CaptureTextureStages(TexKey *key)
{
    const unsigned int tex0 = s_glCache.boundTex[0], tex1 = s_glCache.boundTex[1];
    const int enabled0 = s_glCache.texEnabled[0], enabled1 = s_glCache.texEnabled[1];
    const GLint env0 = s_glCache.texEnvMode[0], env1 = s_glCache.texEnvMode[1];
    const int unit = s_glCache.activeUnit;
    s_texCapture = 1;
    const int stages = Apply3DTextureStages();
    s_texCapture = 0;
    for (int i = 0; i < 2; ++i) {
        key->enabled[i] = s_glCache.texEnabled[i] == 1 ? 1 : 0;
        key->tex[i] = key->enabled[i] ? s_glCache.boundTex[i] : 0;
        key->env[i] = key->enabled[i] ? s_glCache.texEnvMode[i] : 0;
    }
    s_glCache.boundTex[0] = tex0;
    s_glCache.boundTex[1] = tex1;
    s_glCache.texEnabled[0] = enabled0;
    s_glCache.texEnabled[1] = enabled1;
    s_glCache.texEnvMode[0] = env0;
    s_glCache.texEnvMode[1] = env1;
    s_glCache.activeUnit = unit;
    return stages;
}

// Sets GL's texture state to a bucket's key, through the cache.
void ApplyTexKey(const TexKey &key)
{
    const int units = s_caps.maxTexUnits > 1 ? 2 : 1;
    for (int stage = units - 1; stage >= 0; --stage) {
        const int want = key.enabled[stage];
        if (s_glCache.texEnabled[stage] == want &&
            (!want || (s_glCache.boundTex[stage] == key.tex[stage] && s_glCache.texEnvMode[stage] == key.env[stage])))
            continue;
        SelectActiveUnit(stage);
        if (s_glCache.texEnabled[stage] != want) {
            if (want)
                glEnable(GL_TEXTURE_2D);
            else
                glDisable(GL_TEXTURE_2D);
            s_glCache.texEnabled[stage] = want;
        }
        if (!want)
            continue;
        if (s_glCache.boundTex[stage] != key.tex[stage]) {
            glBindTexture(GL_TEXTURE_2D, key.tex[stage]);
            s_glCache.boundTex[stage] = key.tex[stage];
        }
        if (s_glCache.texEnvMode[stage] != key.env[stage]) {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, key.env[stage]);
            s_glCache.texEnvMode[stage] = key.env[stage];
        }
    }
    if (units > 1)
        SelectActiveUnit(0);
}

void FlushBuckets(void)
{
    if (s_bucketVertexTotal == 0 || s_inBucketFlush)
        return;
    s_inBucketFlush = 1;
    s_batchStats.bucketFlushes++;
    // Each bucket is drawn with the raster state its draws had: the game's
    // state variables are set to it for Apply3DRasterState and put back at
    // the end. Opaque buckets go first, then the blended ones.
    const RasterKey current = CurrentRasterKey();
    bool usedArrays = false;
    if (s_batchArrays && !s_batchArraysChecked) {
        // Errors left by earlier calls must not be read as the arrays' own.
        while (glGetError() != GL_NO_ERROR) {}
    }
    for (int pass = 0; pass < 2; ++pass)
    for (int b = 0; b < s_bucketCount; ++b) {
        Bucket &bucket = s_buckets[b];
        if (bucket.indexCount == 0 || (bucket.raster.alphaBlend != 0) != (pass == 1))
            continue;
        SetRasterGlobals(bucket.raster);
        Apply3DRasterState();
        ApplyTexKey(bucket.key);
        const int stages = (bucket.format >> 1) & 3;
        if (s_batchArrays) {
            const GLsizei stride = (GLsizei)sizeof(BucketVertex);
            glVertexPointer(3, GL_FLOAT, stride, &bucket.vertices[0].x);
            SetClientArray(kClientArrayVertex, GL_VERTEX_ARRAY, 1);
            SetClientArray(kClientArrayNormal, GL_NORMAL_ARRAY, 0);
            // 0xAARRGGBB is A,R,G,B in memory on this big-endian target.
            glColorPointer(4, MGL_UBYTE_ARGB, stride, &bucket.vertices[0].argb);
            SetClientArray(kClientArrayColor, GL_COLOR_ARRAY, 1);
            if (stages >= 1)
                glTexCoordPointer(2, GL_FLOAT, stride, &bucket.vertices[0].u0);
            SetClientArray(kClientArrayTex0, GL_TEXTURE_COORD_ARRAY, stages >= 1 ? 1 : 0);
            if (stages >= 2) {
                glClientActiveTextureARB(GL_TEXTURE1_ARB);
                glTexCoordPointer(2, GL_FLOAT, stride, &bucket.vertices[0].u1);
                SetClientArray(kClientArrayTex1, GL_TEXTURE_COORD_ARRAY, 1);
                glClientActiveTextureARB(GL_TEXTURE0_ARB);
            } else if ((s_glCache.clientArrayMask & kClientArrayTex1) != 0) {
                glClientActiveTextureARB(GL_TEXTURE1_ARB);
                SetClientArray(kClientArrayTex1, GL_TEXTURE_COORD_ARRAY, 0);
                glClientActiveTextureARB(GL_TEXTURE0_ARB);
            }
            glDrawElements(GL_TRIANGLES, (GLsizei)bucket.indexCount, GL_UNSIGNED_SHORT, bucket.indices);
            s_batchStats.batches++;
            usedArrays = true;
        } else {
            for (uint32_t i = 0; i < bucket.indexCount;) {
                const uint32_t room = BatchBegin(bucket.format);
                const uint32_t end = bucket.indexCount - i > room ? i + room : bucket.indexCount;
                const uint32_t emitted = end - i;
                for (; i < end; ++i) {
                    const BucketVertex &v = bucket.vertices[bucket.indices[i]];
                    if (stages >= 1)
                        RawTexCoord2f(v.u0, v.v0);
                    if (stages >= 2)
                        RawTexCoord1_2f(v.u1, v.v1);
                    RawColor4ub((GLubyte)((v.argb >> 16) & 0xffu), (GLubyte)((v.argb >> 8) & 0xffu),
                                (GLubyte)(v.argb & 0xffu), (GLubyte)(v.argb >> 24));
                    RawVertex3f(v.x, v.y, v.z);
                }
                s_batch.count += emitted;
            }
        }
        bucket.count = 0;
        bucket.indexCount = 0;
    }
    if (usedArrays) {
        // The immediate paths never read a client array; leave none armed.
        ClearClientArrays();
        if (!s_batchArraysChecked) {
            // The first array flush decides whether the library takes them.
            s_batchArraysChecked = 1;
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                s_batchArrays = 0;
                Sys_Log("gfx: array buckets rejected by MiniGL (error %x); using immediate mode", (unsigned)error);
            }
        }
    }
    SetRasterGlobals(current);
    memset(s_bucketLookup, 0, sizeof(s_bucketLookup));
    // The last batch stays open; the next GL call closes it. GL is left with
    // the last bucket's raster and texture state, as the cache records.
    s_bucketCount = 0;
    s_bucketVertexTotal = 0;
    s_inBucketFlush = 0;
}

// Adds a draw to the bucket of its texture and raster state. Triangles that
// face away are dropped here when the draw culls (cpu_cull): their vertices
// are then never lit, stored, sent over the bus or seen by the stack. Each
// vertex a kept triangle references is lit (when `light` is given),
// transformed by world*view and stored once; the triangles are indices into
// the bucket. Returns false when the draw must go the immediate way (too
// large, no memory).
bool BucketAppend(const TexKey &key, const RasterKey &raster, int format, GLenum mode, const GfxVertex *vertices,
                  uint32_t vertexCount, const uint16_t *indices, uint32_t sourceCount, const EmitPlan &plan,
                  int activeStages, const GfxMatrix &m, const LightSetup *light)
{
    const uint32_t total = FlattenedCount(mode, sourceCount);
    // The most vertices and indices this draw can add.
    const uint32_t mostVertices = total < vertexCount ? total : vertexCount;
    if (mostVertices > (uint32_t)kBucketVertexLimit || total > (uint32_t)kBucketIndexLimit)
        return false;
    if (vertexCount > s_sourceCapacity) {
        SourceVertex *grown = (SourceVertex *)realloc(s_source, vertexCount * sizeof(SourceVertex));
        if (grown == NULL)
            return false;
        s_source = grown;
        memset(s_source, 0, vertexCount * sizeof(SourceVertex));
        s_sourceCapacity = vertexCount;
        s_sourceGen = 0;
    }
    const unsigned hash = (key.tex[0] * 31u + key.tex[1] * 7u + (unsigned)format + (unsigned)raster.alphaBlend * 3u +
                           (unsigned)raster.cullMode * 5u + (unsigned)raster.alphaTest * 11u) & 255u;
    Bucket *bucket = NULL;
    if (s_bucketLookup[hash] != 0 && (int)s_bucketLookup[hash] <= s_bucketCount) {
        Bucket &c = s_buckets[s_bucketLookup[hash] - 1];
        if (c.format == format && SameRasterKey(c.raster, raster) && c.key.tex[0] == key.tex[0] &&
            c.key.tex[1] == key.tex[1] && c.key.enabled[0] == key.enabled[0] && c.key.enabled[1] == key.enabled[1] &&
            c.key.env[0] == key.env[0] && c.key.env[1] == key.env[1])
            bucket = &c;
    }
    if (bucket == NULL) {
        for (int b = 0; b < s_bucketCount; ++b) {
            Bucket &c = s_buckets[b];
            if (c.format == format && SameRasterKey(c.raster, raster) && c.key.tex[0] == key.tex[0] &&
                c.key.tex[1] == key.tex[1] &&
                c.key.enabled[0] == key.enabled[0] && c.key.enabled[1] == key.enabled[1] &&
                c.key.env[0] == key.env[0] && c.key.env[1] == key.env[1]) {
                bucket = &c;
                s_bucketLookup[hash] = (unsigned char)(b + 1);
                break;
            }
        }
    }
    if (bucket != NULL && (bucket->count + mostVertices > (uint32_t)kBucketVertexLimit ||
                           bucket->indexCount + total > (uint32_t)kBucketIndexLimit)) {
        // Full: send everything (the order of the buckets is kept) and start over.
        FlushBuckets();
        bucket = NULL;
    }
    if (bucket == NULL) {
        if (s_bucketCount == (int)kMaxBuckets)
            FlushBuckets();
        bucket = &s_buckets[s_bucketCount++];
        bucket->key = key;
        bucket->raster = raster;
        bucket->format = format;
        bucket->count = 0;
        bucket->indexCount = 0;
        s_bucketLookup[hash] = (unsigned char)s_bucketCount;
    }
    // Grown to the largest frame seen and kept: no steady-state allocation.
    if (bucket->count + mostVertices > bucket->capacity) {
        uint32_t capacity = (bucket->count + mostVertices) * 2u + 64u;
        if (capacity > (uint32_t)kBucketVertexLimit)
            capacity = (uint32_t)kBucketVertexLimit;
        BucketVertex *grown = (BucketVertex *)realloc(bucket->vertices, capacity * sizeof(BucketVertex));
        if (grown == NULL)
            return false;
        bucket->vertices = grown;
        bucket->capacity = capacity;
    }
    if (bucket->indexCount + total > bucket->indexCapacity) {
        uint32_t capacity = (bucket->indexCount + total) * 2u + 96u;
        if (capacity > (uint32_t)kBucketIndexLimit)
            capacity = (uint32_t)kBucketIndexLimit;
        uint16_t *grown = (uint16_t *)realloc(bucket->indices, capacity * sizeof(uint16_t));
        if (grown == NULL)
            return false;
        bucket->indices = grown;
        bucket->indexCapacity = capacity;
    }
    if (++s_sourceGen == 0) {
        memset(s_source, 0, s_sourceCapacity * sizeof(SourceVertex));
        s_sourceGen = 1;
    }
    const uint32_t gen = s_sourceGen;
    // Plain lists and plain texture coordinates skip the general mappings.
    const bool plainList = mode == GL_TRIANGLES;
    const bool directUV = plan.directTexCoord;
    const bool uv0FromU1 = plan.stageUsesU1[0], uv1FromU1 = plan.stageUsesU1[1];
    // Direct3D culls by winding on screen; in its left-handed eye space a
    // clockwise triangle has (b-a)x(c-a) . a < 0. GFX_CULL_CW drops those,
    // GFX_CULL_CCW the others. Degenerate triangles (0) are kept for GL.
    //
    // The test is made in object space, so a culled triangle's vertices are
    // never transformed: with p_eye = p R + T (row vectors), the same value
    // is det(R) * ((b-a)x(c-a) . (a - e)), where e = -T R^-1 is the eye in
    // object space. A matrix that cannot be inverted turns culling off for
    // the draw (GL then culls, as for the immediate path).
    int cullSign = !s_cpuCull ? 0 : raster.cullMode == GFX_CULL_CW ? -1 : raster.cullMode == GFX_CULL_CCW ? 1 : 0;
    float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
    if (cullSign != 0) {
        const float c11 = m._22 * m._33 - m._23 * m._32, c12 = m._23 * m._31 - m._21 * m._33,
                    c13 = m._21 * m._32 - m._22 * m._31;
        const float det = m._11 * c11 + m._12 * c12 + m._13 * c13;
        if (det > 1e-12f || det < -1e-12f) {
            const float inv = 1.0f / det;
            // R^-1 = adjugate / det; e = -T R^-1.
            const float i11 = c11 * inv, i12 = (m._13 * m._32 - m._12 * m._33) * inv,
                        i13 = (m._12 * m._23 - m._13 * m._22) * inv;
            const float i21 = c12 * inv, i22 = (m._11 * m._33 - m._13 * m._31) * inv,
                        i23 = (m._13 * m._21 - m._11 * m._23) * inv;
            const float i31 = c13 * inv, i32 = (m._12 * m._31 - m._11 * m._32) * inv,
                        i33 = (m._11 * m._22 - m._12 * m._21) * inv;
            eyeX = -(m._41 * i11 + m._42 * i21 + m._43 * i31);
            eyeY = -(m._41 * i12 + m._42 * i22 + m._43 * i32);
            eyeZ = -(m._41 * i13 + m._42 * i23 + m._43 * i33);
            if (det < 0.0f)
                cullSign = -cullSign;   // a mirroring matrix reverses the winding
        } else {
            cullSign = 0;
        }
    }
    // Triangles wholly outside one plane of the view volume are dropped too:
    // GL would clip them away, after they had cost the stack about a
    // microsecond per vertex. object -> clip is world*view*projection; in
    // Direct3D's clip space the view volume is -w<=x<=w, -w<=y<=w, 0<=z<=w.
    // m is the world*view of the current generation (Apply3DTransforms), so
    // the product is reused until WORLD, VIEW or PROJECTION is set again.
    const bool clipTest = s_cpuClip != 0;
    if (clipTest && (!s_glCache.clipValid || s_glCache.clipWorldViewGen != s_worldViewGeneration ||
                     s_glCache.clipProjectionGen != s_projectionGeneration)) {
        GfxMatrix projection;
        if (s_transformSet[GFX_TRANSFORM_PROJECTION])
            projection = s_transforms[GFX_TRANSFORM_PROJECTION];
        else
            Identity(&projection);
        MultiplyRowMatrices(m, projection, &s_glCache.clip);
        s_glCache.clipWorldViewGen = s_worldViewGeneration;
        s_glCache.clipProjectionGen = s_projectionGeneration;
        s_glCache.clipValid = 1;
    }
    const GfxMatrix &clip = s_glCache.clip;
    uint16_t *outIndex = bucket->indices + bucket->indexCount;
    uint32_t kept = 0;
    for (uint32_t i = 0; i + 3 <= total; i += 3) {
        uint32_t src[3];
        for (int k = 0; k < 3; ++k) {
            const uint32_t flat = plainList ? i + (uint32_t)k : FlattenedSource(mode, i + (uint32_t)k);
            src[k] = indices != NULL ? indices[flat] : flat;
        }
        if (cullSign != 0) {
            const GfxVertex *a = &vertices[src[0]], *b = &vertices[src[1]], *c = &vertices[src[2]];
            const float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
            const float wx = c->x - a->x, wy = c->y - a->y, wz = c->z - a->z;
            const float facing = (uy * wz - uz * wy) * (a->x - eyeX) + (uz * wx - ux * wz) * (a->y - eyeY) +
                                 (ux * wy - uy * wx) * (a->z - eyeZ);
            if ((cullSign < 0 && facing < 0.0f) || (cullSign > 0 && facing > 0.0f)) {
                s_batchStats.culledTriangles++;
                continue;
            }
        }
        if (clipTest) {
            unsigned outside = 0x3fu;
            for (int k = 0; k < 3; ++k) {
                SourceVertex *e = &s_source[src[k]];
                if (e->codeStamp != gen) {
                    e->codeStamp = gen;
                    const GfxVertex *v = &vertices[src[k]];
                    const float cx = v->x * clip._11 + v->y * clip._21 + v->z * clip._31 + clip._41;
                    const float cy = v->x * clip._12 + v->y * clip._22 + v->z * clip._32 + clip._42;
                    const float cz = v->x * clip._13 + v->y * clip._23 + v->z * clip._33 + clip._43;
                    const float cw = v->x * clip._14 + v->y * clip._24 + v->z * clip._34 + clip._44;
                    e->outcode = (uint8_t)((cx > cw ? 1u : 0u) | (cx < -cw ? 2u : 0u) | (cy > cw ? 4u : 0u) |
                                           (cy < -cw ? 8u : 0u) | (cz < 0.0f ? 16u : 0u) | (cz > cw ? 32u : 0u));
                }
                outside &= e->outcode;
            }
            if (outside != 0) {
                s_batchStats.clippedTriangles++;
                continue;
            }
        }
        for (int k = 0; k < 3; ++k) {
            SourceVertex *e = &s_source[src[k]];
            if (e->slotStamp != gen) {
                e->slotStamp = gen;
                e->slot = (uint16_t)bucket->count;
                const GfxVertex *v = &vertices[src[k]];
                BucketVertex *out = &bucket->vertices[bucket->count++];
                if (directUV) {
                    const float *uv0 = uv0FromU1 ? &v->u1 : &v->u0;
                    const float *uv1 = uv1FromU1 ? &v->u1 : &v->u0;
                    out->u0 = uv0[0];
                    out->v0 = uv0[1];
                    out->u1 = uv1[0];
                    out->v1 = uv1[1];
                } else {
                    out->u0 = out->v0 = out->u1 = out->v1 = 0.0f;
                    if (activeStages >= 1)
                        ComputeStageTexCoord(0, v, m, &out->u0, &out->v0);
                    if (activeStages >= 2)
                        ComputeStageTexCoord(1, v, m, &out->u1, &out->v1);
                }
                const uint32_t base = light != NULL ? LightVertex(*light, *v) : v->diffuse;
                const uint32_t rgb = plan.overrideColor ? plan.color : base;
                const uint32_t alpha = plan.overrideAlpha ? plan.color : base;
                out->argb = (rgb & 0x00ffffffu) | (alpha & 0xff000000u);
                out->x = v->x * m._11 + v->y * m._21 + v->z * m._31 + m._41;
                out->y = v->x * m._12 + v->y * m._22 + v->z * m._32 + m._42;
                out->z = v->x * m._13 + v->y * m._23 + v->z * m._33 + m._43;
            }
            *outIndex++ = e->slot;
        }
        kept += 3;
    }
    bucket->indexCount += kept;
    // Counts the draw even when every triangle was culled: the bucket list
    // must still be flushed in order.
    s_bucketVertexTotal += kept != 0 ? kept : 1u;
    s_batchStats.draws++;
    s_batchStats.vertices += kept;
    s_batchStats.sortedDraws++;
    return true;
}
#endif

void Emit3DVertex(const GfxVertex *v, bool normals, const EmitPlan &plan, int activeStages,
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
    if (normals)
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

// Client-array submission for eligible plain triangle draws.
// Returns true when the draw was consumed. The immediate path stays the
// fallback for every draw this cannot express (lines, strips, texgen, texture
// transforms, colour overrides, too few indices, indices beyond the library's
// absolute-index budget) and for the whole session if the library rejects the
// pipeline at runtime.
bool Submit3DArrays(const GfxVertex *vertices, const uint16_t *indices,
                    uint32_t indexCount, const EmitPlan &plan, bool normals)
{
    if (!s_caps.clientArrays || indexCount < 12u || !plan.directTexCoord ||
        plan.activeStages > s_caps.clientArrayUnits ||
        plan.overrideColor || plan.overrideAlpha)
        return false;

    uint16_t minIndex = 0xffffu;
    uint16_t maxIndex = 0;
    for (uint32_t i = 0; i < indexCount; ++i) {
        if (indices[i] >= kArrayMaxIndex) {
            s_diag.arrayRejectedDraws++;
            return false;
        }
        if (indices[i] < minIndex)
            minIndex = indices[i];
        if (indices[i] > maxIndex)
            maxIndex = indices[i];
    }
    // The library's fast path covers a bounded vertex window per submission; a
    // spread-out index list defeats it even when the index count is small.
    if (minIndex != 0xffffu && (uint32_t)(maxIndex - minIndex) + 1u > (uint32_t)kArrayFastVertexLimit)
        s_diag.arraySpanOverLimitDraws++;

    const GLsizei stride = (GLsizei)sizeof(GfxVertex);
    const void *uvSrc = plan.stageUsesU1[0] ? &vertices[0].u1 : &vertices[0].u0;
    const bool twoStages = plan.activeStages >= 2;

    // Drain first, then configure, so a failure in these calls is not confused
    // with an earlier error and is itself caught below.
    while (glGetError() != GL_NO_ERROR) {}
    glVertexPointer(3, GL_FLOAT, stride, &vertices[0].x);
    SetClientArray(kClientArrayVertex, GL_VERTEX_ARRAY, 1);
    if (normals) {
        glNormalPointer(GL_FLOAT, stride, &vertices[0].nx);
        SetClientArray(kClientArrayNormal, GL_NORMAL_ARRAY, 1);
    } else {
        // The draw is unlit, so the normal pointer is unused. Unbinding it also
        // spares the gather a per-vertex normal copy, which the fast path now
        // performs whenever a normal array is bound.
        SetClientArray(kClientArrayNormal, GL_NORMAL_ARRAY, 0);
    }
    if (plan.emitColor) {
        // GfxVertex packs 0xAARRGGBB, which is A,R,G,B in memory on this
        // big-endian target; MGL_UBYTE_ARGB is the matching array layout.
        glColorPointer(4, MGL_UBYTE_ARGB, stride, &vertices[0].diffuse);
    }
    SetClientArray(kClientArrayColor, GL_COLOR_ARRAY, plan.emitColor ? 1 : 0);
    if (plan.activeStages >= 1)
        glTexCoordPointer(2, GL_FLOAT, stride, uvSrc);
    SetClientArray(kClientArrayTex0, GL_TEXTURE_COORD_ARRAY, plan.activeStages >= 1 ? 1 : 0);
    if (twoStages) {
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glTexCoordPointer(2, GL_FLOAT, stride, plan.stageUsesU1[1] ? &vertices[0].u1 : &vertices[0].u0);
        SetClientArray(kClientArrayTex1, GL_TEXTURE_COORD_ARRAY, 1);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    } else if ((s_glCache.clientArrayMask & kClientArrayTex1) != 0) {
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        SetClientArray(kClientArrayTex1, GL_TEXTURE_COORD_ARRAY, 0);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
    }
    const bool setupFailed = glGetError() != GL_NO_ERROR;

    if (!setupFailed)
        glDrawElements(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_SHORT, indices);

    const bool drawFailed = glGetError() != GL_NO_ERROR;
    if (setupFailed || drawFailed) {
        // Hard rejection: keep every later draw on the immediate path instead
        // of producing wrong or missing geometry. A rejected setup drew
        // nothing, so the immediate path can still draw this one; a rejected
        // glDrawElements may have drawn part of it and is not re-submitted.
        s_caps.clientArrays = 0;
        Sys_Log("AMIGA: client arrays rejected by MiniGL, falling back to immediate mode");
        return !setupFailed;
    }

    s_diag.arrayDraws++;
    s_diag.arrayIndices += indexCount;
    return true;
}

void Submit3DBlocks(GLenum mode, const GfxVertex *vertices, const uint16_t *indices,
                    uint32_t sourceCount, const EmitPlan &plan, bool normals, int activeStages,
                    const GfxMatrix &worldView)
{
#if defined(OPENCMR2_WARPOS)
    if (s_batching && FlattenedMode(mode) == GL_TRIANGLES) {
        const uint32_t total = FlattenedCount(mode, sourceCount);
        const int format = 1 | (activeStages << 1) | (plan.emitColor ? 8 : 0) | (normals ? 16 : 0);
        s_batchStats.draws++;
        s_batchStats.vertices += total;
        for (uint32_t i = 0; i < total;) {
            const uint32_t room = BatchBegin(format);
            const uint32_t end = total - i > room ? i + room : total;
            const uint32_t emitted = end - i;
            for (; i < end; ++i) {
                const uint32_t src = FlattenedSource(mode, i);
                BatchEmit3D(indices != NULL ? &vertices[indices[src]] : &vertices[src],
                            normals, plan, activeStages, worldView);
            }
            s_batch.count += emitted;
        }
        return;
    }
#endif
    // Eligibility: the client-array path needs plain indexed
    // triangles above librw's threshold, plus texcoords that come straight
    // from the vertex (plan.directTexCoord).
    if (mode == GL_TRIANGLES && indices != NULL) {
        if (plan.directTexCoord && !plan.overrideColor && !plan.overrideAlpha) {
            if (sourceCount >= 12u) {
                s_diag.arrayEligibleDraws++;
                s_diag.arrayEligibleIndices += sourceCount;
            }
        } else {
            s_diag.arrayRejectedDraws++;
        }
    }
    if (mode == GL_TRIANGLES && indices != NULL &&
        Submit3DArrays(vertices, indices, sourceCount, plan, normals))
        return;

    if (plan.plainList && plan.directTexCoord) {
        // Plain list: the flatten mapping is the identity and each active stage
        // reads u0/v0 or u1/v1, so every per-vertex decision is hoisted out of
        // the loop. The block split still happens at whole primitives.
        const uint32_t block = BlockVertexCount(mode);
        const bool useU1Stage0 = plan.stageUsesU1[0];
        const bool useU1Stage1 = plan.stageUsesU1[1];
        const int stages = plan.activeStages;
        for (uint32_t first = 0; first < sourceCount; first += block) {
            const uint32_t end = (sourceCount - first) > block ? first + block : sourceCount;
            glBegin(mode);
            for (uint32_t i = first; i < end; ++i) {
                const GfxVertex *v = indices != NULL ? &vertices[indices[i]] : &vertices[i];
                if (stages >= 1) {
                    const float *uv = useU1Stage0 ? &v->u1 : &v->u0;
                    glTexCoord2f(uv[0], uv[1]);
                }
                if (stages >= 2) {
                    const float *uv = useU1Stage1 ? &v->u1 : &v->u0;
                    glMultiTexCoord2fARB(GL_TEXTURE1_ARB, uv[0], uv[1]);
                }
                if (plan.emitColor) {
                    const uint32_t rgb = plan.overrideColor ? plan.color : v->diffuse;
                    const uint32_t alpha = plan.overrideAlpha ? plan.color : v->diffuse;
                    glColor4ub((GLubyte)((rgb >> 16) & 0xffu), (GLubyte)((rgb >> 8) & 0xffu),
                               (GLubyte)(rgb & 0xffu), (GLubyte)((alpha >> 24) & 0xffu));
                }
                if (normals)
                    glNormal3f(v->nx, v->ny, v->nz);
                glVertex3f(v->x, v->y, v->z);
            }
            glEnd();
        }
        return;
    }

    const GLenum flattened = FlattenedMode(mode);
    const uint32_t total = FlattenedCount(mode, sourceCount);
    const uint32_t block = BlockVertexCount(flattened);
    for (uint32_t first = 0; first < total; first += block) {
        const uint32_t end = (total - first) > block ? first + block : total;
        glBegin(flattened);
        for (uint32_t i = first; i < end; ++i) {
            const uint32_t src = FlattenedSource(mode, i);
            Emit3DVertex(indices != NULL ? &vertices[indices[src]] : &vertices[src],
                         normals, plan, activeStages, worldView);
        }
        glEnd();
    }
}

void SubmitTLBlocks(GLenum mode, const GfxTLVertex *vertices, const uint16_t *indices,
                    uint32_t sourceCount)
{
    const GLenum flattened = FlattenedMode(mode);
    const uint32_t total = FlattenedCount(mode, sourceCount);
#if defined(OPENCMR2_WARPOS)
    if (s_batching && flattened == GL_TRIANGLES) {
        const ProfScope prof(kProfTL);
        s_batchStats.draws++;
        s_batchStats.vertices += total;
        for (uint32_t i = 0; i < total;) {
            const uint32_t room = BatchBegin(0x100);
            const uint32_t end = total - i > room ? i + room : total;
            const uint32_t emitted = end - i;
            for (; i < end; ++i) {
                const uint32_t src = FlattenedSource(mode, i);
                const GfxTLVertex *v = indices != NULL ? &vertices[indices[src]] : &vertices[src];
                RawColor4ub((GLubyte)((v->color >> 16) & 0xffu), (GLubyte)((v->color >> 8) & 0xffu),
                            (GLubyte)(v->color & 0xffu), (GLubyte)((v->color >> 24) & 0xffu));
                RawTexCoord2f(v->tu, v->tv);
                RawVertex2f(v->sx, v->sy);
            }
            s_batch.count += emitted;
        }
        return;
    }
#endif
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
    s_diag.indicesValidated += indexCount;
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

void Submit3DDrawBody(int primitive, const GfxVertex *vertices, uint32_t vertexCount,
                      const uint16_t *indices, uint32_t indexCount);

void Submit3DDraw(int primitive, const GfxVertex *vertices, uint32_t vertexCount,
                  const uint16_t *indices, uint32_t indexCount)
{
#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
    const double start = Sys_GetTicksPrecise();
    Submit3DDrawBody(primitive, vertices, vertexCount, indices, indexCount);
    s_diag.timingSubmit3DCalls++;
    s_diag.timingSubmit3DUs += SectionUsFromMs(Sys_GetTicksPrecise() - start);
#else
    Submit3DDrawBody(primitive, vertices, vertexCount, indices, indexCount);
#endif
}

void Submit3DDrawBody(int primitive, const GfxVertex *vertices, uint32_t vertexCount,
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

    // 3D drawn on the menus' screen (a frontend scene) is not a stage.
    if (s_virtualWidth == 0)
        s_diag.inStage = 1;
    if (s_stageEntryPending && s_virtualWidth == 0) {
        s_stageEntryPending = 0;
        Sys_Log("AMIGA: stage: gameplay screen entered, first 3D draw primitive=%d vertices=%u indices=%u",
                primitive, (unsigned)vertexCount, (unsigned)indexCount);
    }
    s_diag.drawCalls3D++;
    s_diag.triangles3D += CountTriangles(primitive, indices != NULL ? indexCount : vertexCount);
    {
        const uint32_t size = indices != NULL ? indexCount : vertexCount;
        const int bucket = size <= 12u ? 0 : size <= 32u ? 1 : size <= 64u ? 2 : size <= 128u ? 3
                                         : size <= 256u ? 4 : size <= 1024u ? 5 : 6;
        s_diag.drawSizeHistogram[bucket]++;
        if (mode == GL_TRIANGLES) {
            if (indices != NULL)
                s_diag.triangleIndexedDraws++;
            else
                s_diag.trianglePlainDraws++;
        } else {
            s_diag.otherPrimitiveDraws++;
        }
    }

    if (s_tlScreenActive) {
        s_tlScreenActive = 0;
        ApplyViewport();
    }
#if defined(OPENCMR2_WARPOS)
    uint32_t profT = s_profile ? ProfNow() : 0;
#define PROF_SECTION(bucket) \
    do { if (s_profile) { const uint32_t n_ = ProfNow(); s_profTicks[bucket] += n_ - profT; profT = n_; } } while (0)
#else
#define PROF_SECTION(bucket) do { } while (0)
#endif
#if defined(OPENCMR2_WARPOS)
    // A draw is a candidate for the texture-sorted buckets when its result
    // does not depend on the draws around it: depth-tested and depth-writing
    // triangles, opaque or (batch_sort_blended) blended. Anything else is
    // order-dependent, so the buckets are sent before any of its state is set.
    bool sortCandidate = s_batchSort && s_depthTest && s_depthWrite && FlattenedMode(mode) == GL_TRIANGLES &&
                         (!s_alphaBlend || s_batchSortBlended);
    if (s_batchSort && !sortCandidate) {
        s_batchStats.whyNot[s_alphaBlend ? 1 : !s_depthTest ? 2 : !s_depthWrite ? 3 : 4]++;
        FlushBuckets();
    }
#else
    const bool sortCandidate = false;
#endif
    GfxMatrix worldView;
    Apply3DTransforms(&worldView);
    if (!sortCandidate)
        Apply3DRasterState();
    const int backdropFogAlpha = BackdropFogAlpha(vertices, vertexCount);
    PROF_SECTION(kProfApply);
    // Draws a point or spot light reaches are lit on the CPU (see above).
    DrawGeometry geometry = { vertices, indices, indices != NULL ? indexCount : vertexCount, 0, { 0, 0, 0, 0 } };
    const GfxVertex *drawVertices = vertices;
    s_cpuLitDraw = 0;
    const bool twoStages = s_multiTextureCpuLight && s_caps.maxTexUnits > 1 &&
        GfxStage_DrawUsesTwoUnits(s_stages[0].colorOp, s_stages[0].texture != NULL, s_stages[1].colorOp,
                                  s_stages[1].colorArg1, s_stages[1].colorArg2, s_stages[1].texture != NULL);
#if defined(OPENCMR2_WARPOS)
    // [video] cpu_light_all (default 1): every lit draw is lit here. The R200
    // PPC stack spends about 2.4 us on a lit vertex against 0.7 us on a
    // pre-lit one (drawbench, reference A4000), and two thirds of a stage's
    // draws are lit.
    const bool lightAll = s_cpuLightAll != 0;
#else
    const bool lightAll = false;
#endif
    // Lit on the CPU: the light setup is built here; the vertices are lit
    // either straight into a sorted bucket or, for the immediate path, into
    // the scratch copy.
    LightSetup lightSetup;
    bool cpuLight = false;
    const bool positionalReach = s_lightingEnabled && PositionalLightReachesDraw(&geometry);
    if (s_lightingEnabled && (lightAll || twoStages || (s_cpuLightsEnabled && positionalReach))) {
        BuildLightSetup(&lightSetup, positionalReach);
        cpuLight = true;
        s_cpuLitDraw = 1;
        if (!sortCandidate) {
            const GfxVertex *lit = CpuLightDraw(lightSetup, vertices, vertexCount, indices, indexCount);
            if (lit != NULL) {
                drawVertices = lit;
            } else {
                cpuLight = false;
                s_cpuLitDraw = 0;
            }
        }
    }
    PROF_SECTION(kProfLight);
    const bool prelit = Apply3DLightingAndFog(backdropFogAlpha);
    if (s_colourLogEnabled)
        ColourLogSample(vertices, backdropFogAlpha, prelit);
#if defined(OPENCMR2_WARPOS)
    if (sortCandidate) {
        if (prelit && StagesCapturable()) {
            TexKey key;
            const int wantedStages = CaptureTextureStages(&key);
            const EmitPlan sortedPlan = BuildEmitPlan(prelit, mode, wantedStages);
            if (sortedPlan.emitColor) {
                PROF_SECTION(kProfApply);
                const int format = 1 | (wantedStages << 1) | 8;
                if (BucketAppend(key, CurrentRasterKey(), format, mode, vertices, vertexCount, indices,
                                 indices != NULL ? indexCount : vertexCount, sortedPlan, wantedStages, worldView,
                                 cpuLight ? &lightSetup : NULL)) {
                    PROF_SECTION(kProfEmit);
                    return;
                }
            }
        }
        // Not sortable after all (lit by GL, a texture to upload): the
        // immediate path, in order, with its raster state applied now.
        s_batchStats.whyNot[!prelit ? 0 : 5]++;
        FlushBuckets();
        Apply3DRasterState();
        if (cpuLight) {
            const GfxVertex *lit = CpuLightDraw(lightSetup, vertices, vertexCount, indices, indexCount);
            if (lit != NULL)
                drawVertices = lit;
        }
    }
#endif
    const int activeStages = Apply3DTextureStages();
    // The library renders a multitextured draw unlit when fog is on or unit 1
    // uses DECAL/REPLACE (gl/src/draw.c's `lit` predicate). Normals are then
    // irrelevant; leaving the array unbound still spares the gather a
    // per-vertex normal copy, since the fast path now accepts a bound array.
    bool emitNormals = !prelit;
    if (emitNormals && activeStages >= 2) {
        const GLint env1 = MapStageEnvMode(1);
        if (s_glCache.fog != 0 || env1 == GL_DECAL || env1 == GL_REPLACE)
            emitNormals = false;
    }

    const EmitPlan plan = BuildEmitPlan(prelit, mode, activeStages);
    PROF_SECTION(kProfApply);
    Submit3DBlocks(mode, drawVertices, indices, indices != NULL ? indexCount : vertexCount,
                   plan, emitNormals, activeStages, worldView);
    PROF_SECTION(kProfEmit);
#undef PROF_SECTION
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
#if defined(OPENCMR2_WARPOS)
    // 2D is order-dependent: send the sorted 3D buckets before any of its
    // state is set (a flush in the middle of the setup below would change
    // raster and texture state it has already checked).
    FlushBuckets();
#endif
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
        // Entering the 2D/TL pass: drop any client arrays the last 3D draw
        // left enabled. The TL path is immediate mode, but the arrays would
        // otherwise stay armed across the menu and be consumed by a later
        // draw, which is exactly the state leak the normal fast gather
        // exposed.
        if (s_glCache.clientArrayMask != 0) {
            static int logged;
            if (!logged) {
                logged = 1;
                Sys_Log("AMIGA: cleared stale client arrays before 2D (mask=%x)",
                        (unsigned)s_glCache.clientArrayMask);
            }
        }
        ClearClientArrays();
        BeginScreenDraw();
        s_tlScreenActive = 1;
#if defined(OPENCMR2_WARPOS)
        if (s_batching) {
            // BeginScreenDraw left the projection selected.
            glMatrixMode(GL_MODELVIEW);
            if (!s_modelviewIdentity) {
                glLoadIdentity();
                s_modelviewIdentity = 1;
            }
        }
#endif
    }
#if defined(OPENCMR2_WARPOS)
    if (s_batching) {
        // The modelview is the identity for 3D and 2D alike: nothing to load,
        // and the cached world*view product stays valid.
        if (!s_modelviewIdentity) {
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            s_modelviewIdentity = 1;
        }
        return;
    }
#endif
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // The modelview now holds the TL identity, not the cached product.
    s_glCache.loadedWorldViewGen = ~0u;
    s_diag.tlMatrixLoads++;
    s_diag.matrixModeSwitches++;
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
    s_drawFromVertexBuffer = 1;
    Submit3DDraw(primitive, buffer->vertices + firstVertex, clampedCount, NULL, 0);
    s_drawFromVertexBuffer = 0;
}

extern "C" void Gfx_DrawIndexedPrimitiveVB(int primitive, GfxVertexBuffer *buffer, uint32_t firstVertex,
                                           uint32_t vertexCount, const uint16_t *indices, uint32_t indexCount)
{
    if (buffer == NULL || buffer->vertices == NULL || indices == NULL || indexCount == 0)
        return;
    // PORT: AMIGA: index arrays are CPU-written too (Mesh_BuildParts); V3D
    // must see them before the draw, not a stale D-cache line.
    if (firstVertex >= buffer->count)
        return;
    const uint32_t avail = buffer->count - firstVertex;
    const uint32_t clampedCount = (vertexCount == 0 || vertexCount > avail) ? avail : vertexCount;
    s_drawFromVertexBuffer = 1;
    Submit3DDraw(primitive, buffer->vertices + firstVertex, clampedCount, indices, indexCount);
    s_drawFromVertexBuffer = 0;
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

void Gfx_DrawPrimitiveTLBody(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount)
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
    SubmitTLBlocks(mode, vertices, NULL, vertexCount);
}

#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
extern "C" void Gfx_DrawPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount)
{
    const double start = Sys_GetTicksPrecise();
    Gfx_DrawPrimitiveTLBody(primitive, vertices, vertexCount);
    s_diag.timingTLCalls++;
    s_diag.timingTLUs += SectionUsFromMs(Sys_GetTicksPrecise() - start);
}
#else
extern "C" void Gfx_DrawPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount)
{
    Gfx_DrawPrimitiveTLBody(primitive, vertices, vertexCount);
}
#endif

void Gfx_DrawIndexedPrimitiveTLBody(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount,
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

#if defined(OPENCMR2_MINIGL_SECTION_TIMING)
extern "C" void Gfx_DrawIndexedPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount,
                                           const uint16_t *indices, uint32_t indexCount)
{
    const double start = Sys_GetTicksPrecise();
    Gfx_DrawIndexedPrimitiveTLBody(primitive, vertices, vertexCount, indices, indexCount);
    s_diag.timingTLCalls++;
    s_diag.timingTLUs += SectionUsFromMs(Sys_GetTicksPrecise() - start);
}
#else
extern "C" void Gfx_DrawIndexedPrimitiveTL(int primitive, const GfxTLVertex *vertices, uint32_t vertexCount,
                                           const uint16_t *indices, uint32_t indexCount)
{
    Gfx_DrawIndexedPrimitiveTLBody(primitive, vertices, vertexCount, indices, indexCount);
}
#endif

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
                    BcDecode_BC1Block(block, pixels, 16);
                else
                    BcDecode_BC3Block(block, pixels, 16);
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
    texture->lumValid = 0;
}

// The luminance texture of a BUMPENVMAPLUMINANCE stage (port/bump_luminance.h),
// built on first use and after the texture changes. Returns 0 when it cannot
// be built; the caller then binds the texture itself.
unsigned int EnsureBumpLuminanceTexture(GfxTexture *texture, float scale, float offset)
{
    if (texture->data == NULL || BumpLuminance_TexelBytes(texture->format) == 0)
        return 0;
    if (texture->lumValid && texture->lumGlId != 0 && texture->lumScale == scale &&
        texture->lumOffset == offset)
        return texture->lumGlId;
    const size_t size = (size_t)texture->width * (size_t)texture->height * 4u;
    unsigned char *rgba = (unsigned char *)malloc(size);
    if (rgba == NULL)
        return 0;
    BumpLuminance_Build(texture->format, texture->data, (long)texture->pitch, texture->width,
                        texture->height, scale, offset, rgba);
    if (texture->lumGlId == 0)
        glGenTextures(1, &texture->lumGlId);
    if (texture->lumGlId == 0) {
        free(rgba);
        return 0;
    }
    const int unit = s_glCache.activeUnit >= 0 && s_glCache.activeUnit < 2 ? s_glCache.activeUnit : 0;
    glBindTexture(GL_TEXTURE_2D, texture->lumGlId);
    s_glCache.boundTex[unit] = texture->lumGlId;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    free(rgba);
    s_diag.textureUploads++;
    texture->lumValid = 1;
    texture->lumScale = scale;
    texture->lumOffset = offset;
    return texture->lumGlId;
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
    LinkLiveTexture(texture);
    return texture;
}

// ---- stream textures (movie frames) --------------------------------------
// See gfx_minigl_movie.h. A stream texture has data == NULL, so every path
// that reads texture->data (UploadTexture, Gfx_LockTexture, Gfx_CopyTexture,
// TextureToRGBA, destruction) already skips it, and dirty stays 0 so the
// bind paths never try to upload it; its GL name exists after the first
// GfxMiniGL_UploadRows.
//
// The GL texture is padded to powers of two and only its top-left
// width x height is written: MiniGL 29.1 on the PiStorm samples a 640x480
// texture with shifted rows (tests/amiga/minigl_movie_upload_test.c, "npot"),
// but a 1024x512 texture exactly.
static int NextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

extern "C" GfxTexture *GfxMiniGL_CreateStreamTexture(int width, int height)
{
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return NULL;
    GfxTexture *texture = new (std::nothrow) GfxTexture;
    if (texture == NULL)
        return NULL;
    memset(texture, 0, sizeof(*texture));
    texture->width = width;
    texture->height = height;
    texture->glWidth = NextPowerOfTwo(width);
    texture->glHeight = NextPowerOfTwo(height);
    texture->format = GFX_FORMAT_BGRX8; // opaque; the stored pixels are RGBA with alpha 255
    texture->mipLevels = 1;
    texture->streamFresh = 1;
    for (int i = 0; i < s_spareStreamCount; ++i) {
        if (s_spareStreams[i].glWidth == texture->glWidth && s_spareStreams[i].glHeight == texture->glHeight) {
            texture->glId = s_spareStreams[i].glId;
            s_spareStreams[i] = s_spareStreams[--s_spareStreamCount];
            break;
        }
    }
    LinkLiveTexture(texture);
    return texture;
}

extern "C" int GfxMiniGL_GetStreamTextureSize(const GfxTexture *texture, int *glWidth, int *glHeight)
{
    if (texture == NULL || texture->data != NULL || texture->glWidth <= 0 || glWidth == NULL || glHeight == NULL)
        return 0;
    *glWidth = texture->glWidth;
    *glHeight = texture->glHeight;
    return 1;
}

extern "C" int GfxMiniGL_UploadRows(GfxTexture *texture, const uint8_t *rgba, int y0, int rows)
{
    if (texture == NULL || rgba == NULL || texture->parent != NULL || texture->data != NULL ||
        texture->glWidth <= 0 || y0 < 0 || rows <= 0 ||
        rows > texture->height || y0 > texture->height - rows)
        return 0;
    const bool created = texture->glId == 0;
    unsigned char *blank = NULL;
    if (created) {
        // The padded GL texture starts blank (black, alpha 0); freed right after.
        blank = (unsigned char *)calloc((size_t)texture->glWidth * (size_t)texture->glHeight, 4);
        if (blank == NULL) {
            Sys_Log("gfx: no memory for the %dx%d stream texture", texture->glWidth, texture->glHeight);
            return 0;
        }
    }
    if (!EnsureLocked()) {
        free(blank);
        return 0;
    }
    const int unit = s_glCache.activeUnit >= 0 && s_glCache.activeUnit < 2 ? s_glCache.activeUnit : 0;
    if (created)
        glGenTextures(1, &texture->glId);
    if (texture->glId == 0) {
        Sys_Log("gfx: glGenTextures failed for the %dx%d stream texture", texture->glWidth, texture->glHeight);
        free(blank);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture->glId);
    s_glCache.boundTex[unit] = texture->glId;
    // Tightly packed rows: MiniGL then stores the caller's rows directly, with
    // no conversion buffer.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (created) {
        ApplyStageSampler(unit, texture, texture->glId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->glWidth, texture->glHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, blank);
        free(blank);
        // The first upload stores the whole frame, whatever the row range.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture->width, texture->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else if (texture->streamFresh) {
        // A reused GL name: replace the older frame it holds.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture->width, texture->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, texture->width, rows, GL_RGBA, GL_UNSIGNED_BYTE,
                        rgba + (size_t)y0 * (size_t)texture->width * 4);
    }
    texture->streamFresh = 0;
    return 1;
}

extern "C" void Gfx_Finish(void)
{
    if (s_context != NULL)
        glFinish();
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
                if (texture->lumGlId != 0) {
                    for (int u = 0; u < 2; ++u) {
                        if (s_glCache.boundTex[u] == texture->lumGlId)
                            s_glCache.boundTex[u] = ~0u;
                    }
                    glDeleteTextures(1, &texture->lumGlId);
                    texture->lumGlId = 0;
                }
                const bool stream = texture->data == NULL && texture->glWidth > 0;
                if (stream && s_spareStreamCount < kSpareStreamTextures) {
                    SpareStreamTexture &spare = s_spareStreams[s_spareStreamCount++];
                    spare.glId = texture->glId;
                    spare.glWidth = texture->glWidth;
                    spare.glHeight = texture->glHeight;
                } else {
                    glDeleteTextures(1, &texture->glId);
                }
                if (!wasLocked) {
                    mglUnlockDisplay();
                    s_locked = 0;
                }
            }
        }
        free(texture->data);
    }
    UnlinkLiveTexture(texture);
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
    // glyphs. The menu dot copy samples the original bitmap, as the GLES2
    // renderer does (Core_Gfx_GetSpriteTexelInset), and needs none.
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
    // A view is not on the live-texture list (its cube is).
    faceTexture->live = 0;
    faceTexture->liveNext = NULL;
    faceTexture->livePrev = NULL;
    return faceTexture;
}

extern "C" int Gfx_ReadBackBuffer(int x, int y, int width, int height, uint32_t *pixels)
{
    (void)x; (void)y; (void)width; (void)height; (void)pixels;
    return 0;
}
