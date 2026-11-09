// [video] cube_faces_per_frame (render/gfx_core.cpp): with 1, each pass of the
// game over a cube render target's six faces records one face, in turn; the
// other faces' targets, clears and draws are not recorded. Flushes and other
// targets in between do not start a new pass.
#include "port/sys.h"
#include "render/gfx_internal.h"

#include <stdio.h>
#include <string.h>

#include <vector>

static int failures;
static void Check(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

extern "C" DWORD Sys_GetTicks() { return 0; }
extern "C" void Sys_GetDesktopSize(int *w, int *h) { *w = 640; *h = 480; }
extern "C" void Sys_Log(const char *, ...) {}
extern "C" int Sys_GetOption(const char *, int fallback) { return fallback; }
int Platform_GetSettingInt(const char *key, int value)
{
    return strcmp(key, "video.cube_faces_per_frame") == 0 ? 1 : value;
}
const char *Platform_GetSetting(const char *, const char *value) { return value; }

// What the executor saw: per command, the target (draws: the target they go to).
struct Seen {
    GfxCommandType type;
    GfxTexture *target;
};
static std::vector<Seen> seen;
static GfxTexture *executorTarget;

struct CaptureExecutor : GfxExecutor {
    bool SetVideoMode(int, int, bool) override { return true; }
    void SetFullscreen(bool) override {}
    void Execute(GfxFrame &frame, std::vector<GfxTexture *> &, GfxReadback &, bool present, bool, bool) override
    {
        for (const GfxCommand &c : frame.commands) {
            if (c.type == GFX_CMD_TARGET)
                executorTarget = c.target;
            seen.push_back({ c.type, c.type == GFX_CMD_TARGET ? c.target : executorTarget });
        }
        if (present)
            executorTarget = NULL;
    }
    void DestroyTexture(GfxTexture *) override {}
    void ReadTexture(GfxTexture *) override {}
};
GfxExecutor *Gfx_CreateGpuExecutor() { return new CaptureExecutor; }

static const GfxVertex kTriangle[3] = {};

static int DrawsInto(GfxTexture *target)
{
    int n = 0;
    for (const Seen &s : seen)
        if (s.type == GFX_CMD_DRAW && s.target == target)
            n++;
    return n;
}

int main()
{
    Check(Gfx_SetVideoMode(640, 480, FALSE), "renderer init");
    GfxTexture *cube = Gfx_CreateTexture(64, 64, GFX_FORMAT_BGRX8, 1, GFX_TEXTURE_RENDER_TARGET | GFX_TEXTURE_CUBE);
    GfxTexture *faces[6];
    for (int f = 0; f < 6; f++)
        faces[f] = Gfx_GetCubeFace(cube, f);
    GfxVertexBuffer *buffer = Gfx_CreateVertexBuffer(3);

    for (int pass = 0; pass < 8; pass++) {
        seen.clear();
        for (int f = 0; f < 6; f++) {
            Gfx_SetRenderTarget(faces[f]);
            Gfx_Clear(GFX_CLEAR_ZBUFFER, 0, 1.0f);
            Gfx_DrawPrimitive(GFX_TRIANGLELIST, kTriangle, 3);
            if (f == 2 && pass == 3) {
                // A lock of a buffer drawn by reference flushes mid-pass.
                Gfx_LockVertexBuffer(buffer);
                Gfx_UnlockVertexBuffer(buffer);
                Gfx_Finish();
            }
            Gfx_DrawPrimitive(GFX_TRIANGLELIST, kTriangle, 3);
        }
        Gfx_SetRenderTarget(NULL);
        Gfx_DrawPrimitive(GFX_TRIANGLELIST, kTriangle, 3);
        Gfx_Present(FALSE);
        const int drawn = (pass + 1) % 6;   // the first pass begins at 1: passes are counted from 1
        char what[96];
        for (int f = 0; f < 6; f++) {
            snprintf(what, sizeof(what), "pass %d face %d", pass, f);
            Check(DrawsInto(faces[f]) == (f == drawn ? 2 : 0), what);
        }
        snprintf(what, sizeof(what), "pass %d back buffer", pass);
        Check(DrawsInto(NULL) == 1, what);
        int clears = 0, targets = 0;
        for (const Seen &s : seen) {
            clears += s.type == GFX_CMD_CLEAR;
            targets += s.type == GFX_CMD_TARGET && s.target != NULL;
        }
        snprintf(what, sizeof(what), "pass %d one clear", pass);
        Check(clears == 1, what);
        snprintf(what, sizeof(what), "pass %d one face target", pass);
        Check(targets >= 1, what);
    }

    Gfx_DestroyVertexBuffer(buffer);
    Gfx_DestroyTexture(cube);
    Gfx_Shutdown();
    if (failures == 0)
        printf("gfx_core_cube: ok\n");
    return failures == 0 ? 0 : 1;
}
