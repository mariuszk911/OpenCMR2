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

#include <stdio.h>

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

// [video] frame_stats = 1: the same frame timing for either renderer, logged
// every 480 frames: the period between presents, the time inside Present, and
// the rest (the game, state and draw calls). Frames with 3D draws are summed
// apart. frame_stats = 2 also times every draw call (MiniGL submits there,
// gfx_core records), but an E-clock read costs about 22 us on the PiStorm:
// two per draw add about 30 ms to a 3D frame, so 2 is for the split, not the
// total. Done by replacing the Present and Draw entries of a copy of the
// backend's table, so it costs nothing when off.
GfxBackend s_timed;
const GfxBackend *s_inner;

struct FrameStats {
    double lastPresent = 0, period = 0, present = 0, draw = 0;
    double period3d = 0, present3d = 0, draw3d = 0;
    double frameDraw = 0;
    long frames = 0, frames3d = 0, draws = 0, draws3d = 0, frameDraws3d = 0;
} s_fs;

bool s_timeDraws;

struct DrawTimer {
    double t0;
    DrawTimer(bool is3d)
    {
        s_fs.draws++;
        if (is3d) {
            s_fs.draws3d++;
            s_fs.frameDraws3d++;
        }
        t0 = s_timeDraws ? Sys_GetTicksPrecise() : 0.0;
    }
    ~DrawTimer()
    {
        if (s_timeDraws)
            s_fs.frameDraw += Sys_GetTicksPrecise() - t0;
    }
};

void LogFrameStats()
{
    const double t0 = Sys_GetTicksPrecise();
    for (int i = 0; i < 1000; i++)
        Sys_GetTicksPrecise();
    const double clockUs = (Sys_GetTicksPrecise() - t0);   // ms per 1000 = us per call
    // Integer microseconds: %f is not usable in Sys_Log on the Amiga targets.
    const double n = (double)s_fs.frames, n3 = s_fs.frames3d > 0 ? (double)s_fs.frames3d : 1.0;
#define FS_US(ms) ((long)((ms) * 1000.0 + 0.5))
    Sys_Log("gfx: frame stats %s, %ld frames: us/frame period %ld = present %ld + draw calls %ld + other %ld; "
            "%ld 3D frames: period %ld = present %ld + draw calls %ld + other %ld, 3D draws %ld; "
            "draws/frame %ld; clock read %ld ns",
            s_inner->name, s_fs.frames, FS_US(s_fs.period / n), FS_US(s_fs.present / n), FS_US(s_fs.draw / n),
            FS_US((s_fs.period - s_fs.present - s_fs.draw) / n), s_fs.frames3d, FS_US(s_fs.period3d / n3),
            FS_US(s_fs.present3d / n3), FS_US(s_fs.draw3d / n3),
            FS_US((s_fs.period3d - s_fs.present3d - s_fs.draw3d) / n3), (long)(s_fs.draws3d / n3 + 0.5),
            (long)(s_fs.draws / n + 0.5), FS_US(clockUs));
#undef FS_US
    s_fs = FrameStats();
    s_fs.lastPresent = Sys_GetTicksPrecise();
}

void TimedPresent(void (*present)(int), int vsync)
{
    const double p0 = Sys_GetTicksPrecise();
    present(vsync);
    const double now = Sys_GetTicksPrecise();
    if (s_fs.lastPresent != 0.0) {
        const double period = now - s_fs.lastPresent, inPresent = now - p0;
        s_fs.frames++;
        s_fs.period += period;
        s_fs.present += inPresent;
        s_fs.draw += s_fs.frameDraw;
        if (s_fs.frameDraws3d > 0) {
            s_fs.frames3d++;
            s_fs.period3d += period;
            s_fs.present3d += inPresent;
            s_fs.draw3d += s_fs.frameDraw;
        }
    }
    s_fs.lastPresent = now;
    s_fs.frameDraw = 0;
    s_fs.frameDraws3d = 0;
    if (s_fs.frames > 0 && s_fs.frames % 480 == 0)
        LogFrameStats();
}

void Timed_Present(int vsync) { TimedPresent(s_inner->Gfx_Present, vsync); }
void Timed_PresentStretched(int vsync) { TimedPresent(s_inner->Gfx_PresentStretched, vsync); }
void Timed_DrawPrimitiveVB(int p, GfxVertexBuffer *b, uint32_t f, uint32_t n)
{
    DrawTimer t(true);
    s_inner->Gfx_DrawPrimitiveVB(p, b, f, n);
}
void Timed_DrawIndexedPrimitiveVB(int p, GfxVertexBuffer *b, uint32_t f, uint32_t n, const uint16_t *i, uint32_t ni)
{
    DrawTimer t(true);
    s_inner->Gfx_DrawIndexedPrimitiveVB(p, b, f, n, i, ni);
}
void Timed_DrawPrimitive(int p, const GfxVertex *v, uint32_t n)
{
    DrawTimer t(true);
    s_inner->Gfx_DrawPrimitive(p, v, n);
}
void Timed_DrawIndexedPrimitive(int p, const GfxVertex *v, uint32_t n, const uint16_t *i, uint32_t ni)
{
    DrawTimer t(true);
    s_inner->Gfx_DrawIndexedPrimitive(p, v, n, i, ni);
}
void Timed_DrawPrimitiveTL(int p, const GfxTLVertex *v, uint32_t n)
{
    DrawTimer t(false);
    s_inner->Gfx_DrawPrimitiveTL(p, v, n);
}
void Timed_DrawIndexedPrimitiveTL(int p, const GfxTLVertex *v, uint32_t n, const uint16_t *i, uint32_t ni)
{
    DrawTimer t(false);
    s_inner->Gfx_DrawIndexedPrimitiveTL(p, v, n, i, ni);
}

const GfxBackend *WithFrameStats(const GfxBackend *backend)
{
    const int mode = Sys_GetOption("video.frame_stats", 0);
    if (mode == 0)
        return backend;
    s_timeDraws = mode >= 2;
    s_inner = backend;
    s_timed = *backend;
    s_timed.Gfx_Present = &Timed_Present;
    s_timed.Gfx_PresentStretched = &Timed_PresentStretched;
    s_timed.Gfx_DrawPrimitiveVB = &Timed_DrawPrimitiveVB;
    s_timed.Gfx_DrawIndexedPrimitiveVB = &Timed_DrawIndexedPrimitiveVB;
    s_timed.Gfx_DrawPrimitive = &Timed_DrawPrimitive;
    s_timed.Gfx_DrawIndexedPrimitive = &Timed_DrawIndexedPrimitive;
    s_timed.Gfx_DrawPrimitiveTL = &Timed_DrawPrimitiveTL;
    s_timed.Gfx_DrawIndexedPrimitiveTL = &Timed_DrawIndexedPrimitiveTL;
    Sys_Log("gfx: frame stats on (%s)", backend->name);
    return &s_timed;
}

// The bench window (Gfx_BenchmarkBegin/End, around a replay of player 1's
// car: the attract's or the player's own) timed the same way for either
// renderer: the whole window and every 200 frames, in wall time, with the 3D
// draws per frame. Installed only with [debug] attract_benchmark = 1 and
// attract_stage set (diagnostics/bench_replay.h), like frame_stats by
// replacing entries of a copy of the backend's table. Integers only: the 68k
// target does not print floats.
GfxBackend s_bench;
const GfxBackend *s_benchInner;

struct BenchTiming {
    int active = 0;
    uint32_t startMs = 0, segmentMs = 0;
    uint32_t frames = 0, segmentFrames = 0;
    uint32_t draws3d = 0, segmentDraws3d = 0;
} s_bt;
const uint32_t kBenchSegmentFrames = 200;

// "12.34" from a numerator and a denominator, two decimals.
void BenchRatio(char *out, size_t size, uint64_t num, uint64_t den)
{
    const uint64_t hundredths = den != 0 ? (num * 100 + den / 2) / den : 0;
    snprintf(out, size, "%lu.%02lu", (unsigned long)(hundredths / 100), (unsigned long)(hundredths % 100));
}

void BenchSegmentStart(uint32_t now)
{
    s_bt.segmentMs = now;
    s_bt.segmentFrames = s_bt.frames;
    s_bt.segmentDraws3d = s_bt.draws3d;
}

void BenchPresent(void (*present)(int), int vsync)
{
    present(vsync);
    if (!s_bt.active)
        return;
    s_bt.frames++;
    const uint32_t frames = s_bt.frames - s_bt.segmentFrames;
    if (frames < kBenchSegmentFrames)
        return;
    const uint32_t now = Sys_GetTicks();
    const uint32_t ms = now - s_bt.segmentMs;
    char msPerFrame[24], fps[24];
    BenchRatio(msPerFrame, sizeof(msPerFrame), ms, frames);
    BenchRatio(fps, sizeof(fps), (uint64_t)frames * 1000, ms);
    Sys_Log("gfx: bench segment: frames %lu-%lu %s ms/frame %s fps %lu draws3d/frame",
            (unsigned long)s_bt.segmentFrames, (unsigned long)s_bt.frames, msPerFrame, fps,
            (unsigned long)((s_bt.draws3d - s_bt.segmentDraws3d) / frames));
    BenchSegmentStart(now);
}

void Bench_Present(int vsync) { BenchPresent(s_benchInner->Gfx_Present, vsync); }
void Bench_PresentStretched(int vsync) { BenchPresent(s_benchInner->Gfx_PresentStretched, vsync); }
void Bench_DrawPrimitiveVB(int p, GfxVertexBuffer *b, uint32_t f, uint32_t n)
{
    s_bt.draws3d++;
    s_benchInner->Gfx_DrawPrimitiveVB(p, b, f, n);
}
void Bench_DrawIndexedPrimitiveVB(int p, GfxVertexBuffer *b, uint32_t f, uint32_t n, const uint16_t *i, uint32_t ni)
{
    s_bt.draws3d++;
    s_benchInner->Gfx_DrawIndexedPrimitiveVB(p, b, f, n, i, ni);
}
void Bench_DrawPrimitive(int p, const GfxVertex *v, uint32_t n)
{
    s_bt.draws3d++;
    s_benchInner->Gfx_DrawPrimitive(p, v, n);
}
void Bench_DrawIndexedPrimitive(int p, const GfxVertex *v, uint32_t n, const uint16_t *i, uint32_t ni)
{
    s_bt.draws3d++;
    s_benchInner->Gfx_DrawIndexedPrimitive(p, v, n, i, ni);
}

void Bench_Begin(void)
{
    s_bt = BenchTiming();
    s_bt.active = 1;
    s_bt.startMs = Sys_GetTicks();
    BenchSegmentStart(s_bt.startMs);
    Sys_Log("gfx: bench: begin (%s)", s_benchInner->name);
    s_benchInner->Gfx_BenchmarkBegin();
}

void Bench_End(void)
{
    if (s_bt.active) {
        s_bt.active = 0;
        const uint32_t ms = Sys_GetTicks() - s_bt.startMs;
        char msPerFrame[24], fps[24];
        BenchRatio(msPerFrame, sizeof(msPerFrame), ms, s_bt.frames);
        BenchRatio(fps, sizeof(fps), (uint64_t)s_bt.frames * 1000, ms);
        Sys_Log("gfx: bench time (%s): %lu frames in %lu ms: %s ms/frame, %s fps, %lu draws3d/frame",
                s_benchInner->name, (unsigned long)s_bt.frames, (unsigned long)ms, msPerFrame, fps,
                (unsigned long)(s_bt.frames != 0 ? s_bt.draws3d / s_bt.frames : 0));
    }
    s_benchInner->Gfx_BenchmarkEnd();
}

const GfxBackend *WithBenchTiming(const GfxBackend *backend)
{
    // The keys of diagnostics/bench_replay.h, read here directly: the renderer
    // tests link this file without the game. A key that names no playable run
    // only leaves the timing installed: the window opens with a replay.
    const char *stage = Sys_GetOptionText("debug.attract_stage", NULL);
    if (Sys_GetOption("debug.attract_benchmark", 0) == 0 || stage == NULL || *stage == 0)
        return backend;
    s_benchInner = backend;
    s_bench = *backend;
    s_bench.Gfx_Present = &Bench_Present;
    s_bench.Gfx_PresentStretched = &Bench_PresentStretched;
    s_bench.Gfx_DrawPrimitiveVB = &Bench_DrawPrimitiveVB;
    s_bench.Gfx_DrawIndexedPrimitiveVB = &Bench_DrawIndexedPrimitiveVB;
    s_bench.Gfx_DrawPrimitive = &Bench_DrawPrimitive;
    s_bench.Gfx_DrawIndexedPrimitive = &Bench_DrawIndexedPrimitive;
    s_bench.Gfx_BenchmarkBegin = &Bench_Begin;
    s_bench.Gfx_BenchmarkEnd = &Bench_End;
    return &s_bench;
}

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
    s_active = WithBenchTiming(WithFrameStats(s_active));
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
