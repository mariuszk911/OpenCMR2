// The GLES2 renderer's uniform packing (render/gles2/gles2_uniforms.h).
#include "render/gles2/gles2_uniforms.h"

#include <cmath>
#include <cstdio>

namespace {
int failures;
void Check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
void Identity(GfxMatrix &m)
{
    memset(&m, 0, sizeof(m));
    m._11 = m._22 = m._33 = m._44 = 1.0f;
}
}

int main()
{
    GfxDrawState s;
    Identity(s.world);
    Identity(s.view);
    Identity(s.projection);
    Identity(s.texture[0]);
    Identity(s.texture[1]);
    s.world._41 = 5.0f;   // translation in row 4
    s.view._42 = 7.0f;

    static float u[GLES2_VERTEX_VEC4 * 4];
    GfxViewport viewport = { 0, 0, 640, 480, 0.0f, 1.0f };
    Gles2U_FillVertex(s, 640, 480, viewport, false, u);
    Check(Near(u[GLES2_SLOT(0) + 0], 1.0f) && Near(u[GLES2_SLOT(3) + 0], 5.0f) && Near(u[GLES2_SLOT(3) + 1], 7.0f),
          "world*view rows: translation in slot 3");
    Check(Near(u[GLES2_SLOT(4) + 0], 1.0f) && Near(u[GLES2_SLOT(7) + 3], 1.0f), "projection rows");

    s.material.diffuse = { 0.1f, 0.2f, 0.3f, 0.4f };
    s.material.power = 12.0f;
    s.ambient = 0xff204060;
    s.lights[2].type = GFX_LIGHT_SPOT;
    s.lights[2].range = 0.0f;
    s.lights[2].position = { 1, 2, 3 };
    s.lights[2].theta = 1.0f;
    s.lights[2].phi = 2.0f;
    s.lights[2].falloff = 1.5f;
    s.lightEnabled[2] = true;
    s.lighting = true;
    s.stages[0].texCoordIndex = GFX_TCI_CAMERASPACEREFLECTIONVECTOR;
    s.stages[1].texCoordIndex = 1;
    s.stages[1].textureTransform = GFX_TTFF_COUNT2;
    Gles2U_FillVertex(s, 640, 480, viewport, false, u);
    Check(Near(u[GLES2_SLOT(16) + 2], 0.3f), "material diffuse");
    Check(Near(u[GLES2_SLOT(20) + 3], 12.0f) && Near(u[GLES2_SLOT(20) + 0], 0x20 / 255.0f), "ambient and power");
    const float *light = u + GLES2_SLOT(21 + 7 * 2);
    Check(Near(light[12], 1.0f) && Near(light[15], (float)GFX_LIGHT_SPOT), "light position and type");
    Check(light[19] > 1e29f, "no range means a huge one");
    Check(Near(light[24], std::cos(0.5f)) && Near(light[25], std::cos(1.0f)) && light[26] == 1.0f, "spot cones and enable");
    Check(Near(light[23], 1.5f), "falloff");
    Check(u[GLES2_SLOT(21 + 7 * 3) + 26] == 0.0f, "other lights are off");
    Check(u[GLES2_SLOT(77)] == 1.0f, "lighting on");
    Check(u[GLES2_SLOT(81) + 0] == 2.0f && u[GLES2_SLOT(81) + 1] == 1.0f, "texture coordinate modes");
    Check(u[GLES2_SLOT(81) + 2] == 0.0f && u[GLES2_SLOT(81) + 3] == 1.0f, "texture transform flags");
    Check(Near(u[GLES2_SLOT(82) + 2], 1.0f / 640) && Near(u[GLES2_SLOT(82) + 3], -1.0f / 480), "half-pixel offset");

    s.screenW = 640;
    s.screenH = 480;
    Gles2U_FillVertex(s, 1024, 768, viewport, true, u);
    Check(u[GLES2_SLOT(82) + 0] == 640.0f && u[GLES2_SLOT(82) + 1] == 480.0f, "virtual screen size when scaled");
    Gles2U_FillVertex(s, 1024, 768, viewport, false, u);
    Check(u[GLES2_SLOT(82) + 0] == 1024.0f, "target size when not scaled");

    s.fog = true;
    s.fogTableMode = GFX_FOG_NONE;
    s.fogVertexMode = GFX_FOG_NONE;
    Gles2U_FillVertex(s, 640, 480, viewport, false, u);
    Check(u[GLES2_SLOT(79) + 0] == 0.0f && u[GLES2_SLOT(79) + 2] == 1.0f, "fog from specular alpha");
    s.fogVertexMode = GFX_FOG_LINEAR;
    Gles2U_FillVertex(s, 640, 480, viewport, false, u);
    Check(u[GLES2_SLOT(79) + 0] == 3.0f && u[GLES2_SLOT(79) + 2] == 0.0f, "linear vertex fog");

    static float f[GLES2_FRAGMENT_VEC4 * 4];
    s.stages[0].colorOp = GFX_TOP_BUMPENVMAP;
    s.stages[0].bumpMat[3] = 0.25f;
    s.alphaTest = true;
    s.alphaFunc = GFX_CMP_GREATEREQUAL;
    s.alphaRef = 0x80;
    s.textureFactor = 0x80ff0000;
    Gles2U_FillFragment(s, GLES2_KIND_2D, GLES2_KIND_CUBE, f);
    Check(f[GLES2_SLOT(0)] == 22.0f && f[GLES2_SLOT(1) + 2] == 1.0f && f[GLES2_SLOT(3) + 2] == 2.0f, "stage ops and kinds");
    Check(f[GLES2_SLOT(5)] == 1.0f && f[GLES2_SLOT(5) + 1] == 7.0f && Near(f[GLES2_SLOT(6)], 128 / 255.0f), "alpha test");
    Check(Near(f[GLES2_SLOT(4) + 0], 1.0f) && Near(f[GLES2_SLOT(4) + 3], 128 / 255.0f), "texture factor RGBA");
    Check(Near(f[GLES2_SLOT(10) + 3], 0.25f), "bump matrix");

    GfxDrawState t = s;
    Check(Gles2U_SameVertex(s, t), "identical states match");
    t.world._41 = 6.0f;
    Check(!Gles2U_SameVertex(s, t), "a matrix change differs");
    t = s;
    t.stages[1].texCoordIndex = 0;
    Check(!Gles2U_SameVertex(s, t), "a texture coordinate source change differs");

    // The split parts give the same slots as the whole block, and the static part
    // ignores the world matrix.
    {
        static float whole[GLES2_VERTEX_VEC4 * 4], split[GLES2_VERTEX_VEC4 * 4];
        Gles2U_FillVertex(s, 640, 480, viewport, false, whole);
        memset(split, 0x55, sizeof(split));
        Gles2U_FillWorldView(s, split);
        Gles2U_FillStatic(s, 640, 480, viewport, false, split);
        Check(memcmp(whole, split, sizeof(whole)) == 0, "world and static parts make the whole block");
        GfxDrawState moved = s;
        moved.world._41 = 99.0f;
        Check(Gles2U_SameStatic(s, moved) && !Gles2U_SameVertex(s, moved), "a moved object keeps the static part");
        moved = s;
        moved.view._42 = 3.0f;
        Check(!Gles2U_SameStatic(s, moved), "a moved view changes it");
    }

    float tl[4];
    Gles2U_FillTL(s, 1024, 768, true, tl);
    Check(tl[0] == 640.0f && tl[1] == 480.0f, "TL target size");
    if (failures == 0)
        std::puts("gles2_uniforms: ok");
    return failures != 0;
}
