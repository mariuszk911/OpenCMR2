// gfx_core's recorded states (render/gfx_core.cpp): a setter that changes
// nothing records no new state, and GfxDrawState::vertexSerial changes with
// everything the GLES2 3D vertex uniforms (render/gles2/gles2_uniforms.h) are
// built from, so that the executor may skip comparing two states with the same
// serial.
#include "port/sys.h"
#include "render/gfx_internal.h"
#include "render/gles2/gles2_uniforms.h"

#include <stdio.h>
#include <string.h>

#include <functional>
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
int Platform_GetSettingInt(const char *, int value) { return value; }
const char *Platform_GetSetting(const char *, const char *value) { return value; }

static std::vector<GfxDrawState> drawn;   // the state of every draw of the last frame

struct CaptureExecutor : GfxExecutor {
    bool SetVideoMode(int, int, bool) override { return true; }
    void SetFullscreen(bool) override {}
    void Execute(GfxFrame &frame, std::vector<GfxTexture *> &, GfxReadback &, bool, bool, bool) override
    {
        drawn.clear();
        for (const GfxCommand &c : frame.commands)
            if (c.type == GFX_CMD_DRAW)
                drawn.push_back(frame.states[c.state]);
    }
    void DestroyTexture(GfxTexture *) override {}
    void ReadTexture(GfxTexture *) override {}
};
GfxExecutor *Gfx_CreateGpuExecutor() { return new CaptureExecutor; }

static const GfxVertex kTriangle[3] = {};

static void Draw()
{
    Gfx_DrawPrimitive(GFX_TRIANGLELIST, kTriangle, 3);
}

static std::vector<float> Static(const GfxDrawState &s)
{
    std::vector<float> u(GLES2_VERTEX_VEC4 * 4);
    Gles2U_FillStatic(s, 640, 480, s.viewport, false, u.data());
    return u;
}

static GfxMatrix Matrix(float k)
{
    GfxMatrix m;
    memset(&m, 0, sizeof(m));
    m._11 = m._22 = m._33 = m._44 = k;
    m._41 = k * 3;
    return m;
}

int main()
{
    Check(Gfx_SetVideoMode(640, 480, FALSE), "renderer init");
    GfxTexture *texture = Gfx_CreateTexture(8, 8, GFX_FORMAT_BGRA8, 1, 0);
    GfxLight light;
    memset(&light, 0, sizeof(light));
    light.type = 3;
    light.diffuse.r = 1.0f;
    light.direction.z = 1.0f;
    Gfx_SetLight(0, &light);
    Gfx_EnableLight(0, TRUE);
    const GfxMatrix view = Matrix(2.0f);
    Gfx_SetTransform(GFX_TRANSFORM_VIEW, &view);

    // Calls that may leave the serial alone: each must leave the static
    // vertex uniforms as they were.
    const GfxMatrix world = Matrix(5.0f);
    const std::vector<std::pair<const char *, std::function<void()>>> other = {
        { "world", [&] { Gfx_SetTransform(GFX_TRANSFORM_WORLD, &world); } },
        { "texture", [&] { Gfx_SetTexture(0, texture); } },
        { "stage 1 texture", [&] { Gfx_SetTexture(1, texture); } },
        { "color op", [] { Gfx_SetStageColorOp(0, GFX_TOP_ADD); } },
        { "color arg1", [] { Gfx_SetStageColorArg1(0, GFX_TA_DIFFUSE); } },
        { "color arg2", [] { Gfx_SetStageColorArg2(0, GFX_TA_TEXTURE); } },
        { "alpha op", [] { Gfx_SetStageAlphaOp(0, GFX_TOP_ADD); } },
        { "alpha arg1", [] { Gfx_SetStageAlphaArg1(0, GFX_TA_DIFFUSE); } },
        { "alpha arg2", [] { Gfx_SetStageAlphaArg2(0, GFX_TA_TEXTURE); } },
        { "address", [] { Gfx_SetStageAddress(0, GFX_ADDRESS_CLAMP); } },
        { "min filter", [] { Gfx_SetStageMinFilter(0, GFX_FILTER_LINEAR); } },
        { "mag filter", [] { Gfx_SetStageMagFilter(0, GFX_FILTER_LINEAR); } },
        { "mip filter", [] { Gfx_SetStageMipFilter(0, GFX_MIPFILTER_LINEAR); } },
        { "max mip", [] { Gfx_SetStageMaxMipLevel(0, 2); } },
        { "bump scale", [] { Gfx_SetStageBumpEnvLScale(0, 3.0f); } },
        { "bump offset", [] { Gfx_SetStageBumpEnvLOffset(0, 0.5f); } },
        { "bump matrix", [] { Gfx_SetStageBumpEnvMat(0, 1, 0.25f); } },
        { "fog colour", [] { Gfx_SetFogColor(0x123456); } },
        { "depth func", [] { Gfx_SetDepthFunc(GFX_CMP_LESS); } },
        { "cull", [] { Gfx_SetCullMode(GFX_CULL_NONE); } },
        { "src blend", [] { Gfx_SetSrcBlend(GFX_BLEND_SRCALPHA); } },
        { "dest blend", [] { Gfx_SetDestBlend(GFX_BLEND_INVSRCALPHA); } },
        { "alpha func", [] { Gfx_SetAlphaFunc(GFX_CMP_GREATER); } },
        { "alpha ref", [] { Gfx_SetAlphaRef(0x80); } },
        { "texture factor", [] { Gfx_SetTextureFactor(0x80808080); } },
        { "depth test", [] { Gfx_SetDepthTest(FALSE); } },
        { "depth write", [] { Gfx_SetDepthWrite(FALSE); } },
        { "alpha blend", [] { Gfx_SetAlphaBlend(TRUE); } },
        { "alpha test", [] { Gfx_SetAlphaTest(TRUE); } },
        { "distance fade", [] { Gfx_SetDistanceFade(10.0f, 20.0f); } },
    };
    for (const auto &call : other) {
        Draw();
        call.second();
        Draw();
        Gfx_Present(FALSE);
        char what[128];
        snprintf(what, sizeof(what), "%s: a new state is recorded", call.first);
        Check(drawn.size() == 2 && memcmp(&drawn[0], &drawn[1], sizeof(GfxDrawState)) != 0, what);
        if (drawn.size() != 2)
            continue;
        if (drawn[0].vertexSerial == drawn[1].vertexSerial) {
            snprintf(what, sizeof(what), "%s: same serial, same static vertex uniforms", call.first);
            Check(Static(drawn[0]) == Static(drawn[1]), what);
        }
    }

    // Calls that change the static vertex uniforms: each must change the serial.
    GfxLight light2 = light;
    light2.diffuse.g = 0.5f;
    GfxMaterial material;
    memset(&material, 0, sizeof(material));
    material.diffuse.r = 0.75f;
    const GfxMatrix view2 = Matrix(4.0f), projection = Matrix(0.5f), tex0 = Matrix(0.25f), tex1 = Matrix(0.125f);
    GfxViewport viewport;
    Gfx_GetViewport(&viewport);
    viewport.width = 320;
    const std::vector<std::pair<const char *, std::function<void()>>> vertex = {
        { "view", [&] { Gfx_SetTransform(GFX_TRANSFORM_VIEW, &view2); } },
        { "projection", [&] { Gfx_SetTransform(GFX_TRANSFORM_PROJECTION, &projection); } },
        { "texture matrix 0", [&] { Gfx_SetTransform(GFX_TRANSFORM_TEXTURE0, &tex0); } },
        { "texture matrix 1", [&] { Gfx_SetTransform(GFX_TRANSFORM_TEXTURE1, &tex1); } },
        { "light", [&] { Gfx_SetLight(0, &light2); } },
        { "light enable", [] { Gfx_EnableLight(1, TRUE); } },
        { "material", [&] { Gfx_SetMaterial(&material); } },
        { "ambient", [] { Gfx_SetAmbient(0x404040); } },
        { "diffuse source", [] { Gfx_SetDiffuseMaterialSource(GFX_MCS_MATERIAL); } },
        { "specular source", [] { Gfx_SetSpecularMaterialSource(GFX_MCS_MATERIAL); } },
        { "ambient source", [] { Gfx_SetAmbientMaterialSource(GFX_MCS_COLOR1); } },
        { "emissive source", [] { Gfx_SetEmissiveMaterialSource(GFX_MCS_COLOR1); } },
        { "fog vertex mode", [] { Gfx_SetFogVertexMode(GFX_FOG_LINEAR); } },
        { "fog table mode", [] { Gfx_SetFogTableMode(GFX_FOG_LINEAR); } },
        { "fog start", [] { Gfx_SetFogStart(5.0f); } },
        { "fog end", [] { Gfx_SetFogEnd(50.0f); } },
        { "fog", [] { Gfx_SetFog(TRUE); } },
        { "lighting", [] { Gfx_SetLighting(FALSE); } },
        { "specular", [] { Gfx_SetSpecular(TRUE); } },
        { "local viewer", [] { Gfx_SetLocalViewer(FALSE); } },
        { "normalize", [] { Gfx_SetNormalizeNormals(TRUE); } },
        { "colour vertex", [] { Gfx_SetColorVertex(FALSE); } },
        { "tex coord index", [] { Gfx_SetStageTexCoordIndex(0, 1); } },
        { "texture transform", [] { Gfx_SetStageTextureTransform(1, GFX_TTFF_COUNT2); } },
        { "viewport", [&] { Gfx_SetViewport(&viewport); } },
        { "virtual screen", [] { Gfx_SetVirtualScreen(800, 600); } },
    };
    for (const auto &call : vertex) {
        Draw();
        call.second();
        Draw();
        Gfx_Present(FALSE);
        char what[128];
        snprintf(what, sizeof(what), "%s: the serial changes", call.first);
        Check(drawn.size() == 2 && drawn[0].vertexSerial != drawn[1].vertexSerial, what);
    }

    // Push/pop gives a new serial (the restored state may differ).
    Draw();
    Gfx_PushState();
    Gfx_SetAmbient(0x010101);
    Gfx_PopState();
    Draw();
    Gfx_Present(FALSE);
    Check(drawn.size() == 2 && drawn[0].vertexSerial != drawn[1].vertexSerial, "pop: the serial changes");

    // Setting what is already set records no new state.
    Gfx_SetAmbient(0x202020);
    Gfx_SetTransform(GFX_TRANSFORM_WORLD, &world);
    Draw();
    Gfx_SetAmbient(0x202020);
    Gfx_SetTransform(GFX_TRANSFORM_WORLD, &world);
    Gfx_SetTexture(0, texture);
    Gfx_SetStageColorOp(0, GFX_TOP_ADD);
    Gfx_SetAlphaBlend(TRUE);
    Gfx_SetMaterial(&material);
    Gfx_SetLight(0, &light2);
    Gfx_SetViewport(&viewport);
    Draw();
    Gfx_Present(FALSE);
    Check(drawn.size() == 2 && memcmp(&drawn[0], &drawn[1], sizeof(GfxDrawState)) == 0, "no-op calls: same state");

    Gfx_DestroyTexture(texture);
    Gfx_Shutdown();
    if (failures == 0)
        printf("gfx_core_serial: ok\n");
    return failures == 0 ? 0 : 1;
}
