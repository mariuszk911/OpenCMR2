/*
 * Packs a recorded draw's state into the vec4 arrays the GLES2 shaders read
 * (gfx_gles2_shaders.h): u_v[83] for the 3D vertex shader, u_v[1] for the
 * pre-transformed one and u_f[13] for the fragment shader. GL ES 2.0 has no
 * uniform blocks and no integer uniforms worth using, so every integer is a
 * float. Pure functions of the recorded state, tested on the host
 * (tests/gles2_uniforms.cpp).
 *
 * Slots of u_v (3D): 0-3 world*view rows, 4-7 projection rows, 8-11 and 12-15
 * texture matrices, 16-19 material diffuse/ambient/specular/emissive, 20
 * global ambient (w: material power), 21 + 7 * light: diffuse, specular,
 * ambient, position (w: type), direction (w: range), attenuation (w:
 * falloff), spot (cos theta/2, cos phi/2, enabled), 77 lighting/colour
 * vertex/specular/local viewer, 78 material sources, 79 vertex fog mode,
 * normalise, fog from specular alpha, 80 fog start/end, 81 texture coordinate
 * mode of stage 0, 1 (0 uv0, 1 uv1, 2 reflection) and texture transform of
 * stage 0, 1, 82 target size and half-pixel offset.
 *
 * Slots of u_f: 0, 1 stage 0 (colour op, arg1, arg2, alpha op; alpha arg1,
 * arg2, texture kind), 2, 3 stage 1, 4 texture factor, 5 alpha test (on,
 * function), 6 alpha reference/255, 7 fog colour, 8 fog on, table mode,
 * specular on, 9 fog start/end, 10 bump matrix, 11 bump luminance scale and
 * offset, 12 unused (the distance fade).
 */
#ifndef OPENCMR2_RENDER_GLES2_GLES2_UNIFORMS_H
#define OPENCMR2_RENDER_GLES2_GLES2_UNIFORMS_H

#include "render/gfx_internal.h"

#include <math.h>
#include <string.h>

enum {
    GLES2_VERTEX_VEC4 = 83, GLES2_TL_VEC4 = 1, GLES2_FRAGMENT_VEC4 = 13,
    // Slots 0-3 (world*view) change with every object; the rest (slots 4-82)
    // only when the view, lights, material or render state do. They are given
    // to the program separately.
    GLES2_WORLD_VEC4 = 4, GLES2_STATIC_VEC4 = 79
};

enum Gles2TextureKind { GLES2_KIND_NONE = 0, GLES2_KIND_2D = 1, GLES2_KIND_CUBE = 2 };

inline void Gles2U_Multiply(const GfxMatrix &a, const GfxMatrix &b, float *out)
{
    const float *pa = &a._11, *pb = &b._11;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += pa[r * 4 + k] * pb[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

inline void Gles2U_TransformPoint(const GfxVector &v, float w, const GfxMatrix &m, float out[3])
{
    const float *p = &m._11;
    for (int c = 0; c < 3; c++)
        out[c] = v.x * p[c] + v.y * p[4 + c] + v.z * p[8 + c] + w * p[12 + c];
}

inline void Gles2U_Color(DWORD c, float out[4])
{
    out[0] = ((c >> 16) & 0xff) / 255.0f;
    out[1] = ((c >> 8) & 0xff) / 255.0f;
    out[2] = (c & 0xff) / 255.0f;
    out[3] = ((c >> 24) & 0xff) / 255.0f;
}

inline void Gles2U_Copy4(const GfxColorValue &c, float out[4])
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

// 0: the vertex's first texture coordinate set, 1: its second, 2: the
// camera-space reflection vector (GFX_TCI_CAMERASPACEREFLECTIONVECTOR).
inline float Gles2U_TexCoordMode(DWORD index)
{
    if ((index & 0xffff0000u) == 0x30000u)
        return 2.0f;
    return (index & 0xffffu) == 1u ? 1.0f : 0.0f;
}

// Float offset of vec4 slot n.
#define GLES2_SLOT(n) ((n) * 4)

// The 3D vertex uniforms of a draw, in two parts. viewport is the draw's viewport in target
// pixels (for the half-pixel offset); scaled: the target is the back buffer
// and the game's virtual screen applies.
inline void Gles2U_FillWorldView(const GfxMatrix &world, const GfxMatrix &view, float *u16)
{
    Gles2U_Multiply(world, view, u16);
}

inline void Gles2U_FillWorldView(const GfxDrawState &s, float *u16)
{
    Gles2U_FillWorldView(s.world, s.view, u16);
}

// Slots 4-82 of a full 83-slot buffer (slots 0-3 are left alone).
// Skips fields the 3D vertex shader does not read: texture matrices whose stage
// transform is disabled, all material/light slots when lighting is off, disabled
// lights, and spot-light cosf() for non-spot lights.
inline void Gles2U_FillStatic(const GfxDrawState &s, int targetW, int targetH, const GfxViewport &viewport,
                              bool scaled, float *u)
{
    memset(u + GLES2_SLOT(GLES2_WORLD_VEC4), 0, sizeof(float) * 4 * GLES2_STATIC_VEC4);
    memcpy(u + GLES2_SLOT(4), &s.projection, 64);
    if (s.stages[0].textureTransform != GFX_TTFF_DISABLE)
        memcpy(u + GLES2_SLOT(8), &s.texture[0], 64);
    if (s.stages[1].textureTransform != GFX_TTFF_DISABLE)
        memcpy(u + GLES2_SLOT(12), &s.texture[1], 64);
    if (s.lighting) {
        Gles2U_Copy4(s.material.diffuse, u + GLES2_SLOT(16));
        Gles2U_Copy4(s.material.ambient, u + GLES2_SLOT(17));
        Gles2U_Copy4(s.material.specular, u + GLES2_SLOT(18));
        Gles2U_Copy4(s.material.emissive, u + GLES2_SLOT(19));
        float *ambient = u + GLES2_SLOT(20);
        Gles2U_Color(s.ambient, ambient);
        ambient[3] = s.material.power;
        for (int i = 0; i < 8; i++) {
            if (!s.lightEnabled[i])
                continue;
            const GfxLight &l = s.lights[i];
            float *lu = u + GLES2_SLOT(21 + 7 * i);
            Gles2U_Copy4(l.diffuse, lu);
            Gles2U_Copy4(l.specular, lu + 4);
            Gles2U_Copy4(l.ambient, lu + 8);
            lu[15] = (float)l.type;
            if (l.type == GFX_LIGHT_DIRECTIONAL) {
                Gles2U_TransformPoint(l.direction, 0.0f, s.view, lu + 16);
            } else {
                Gles2U_TransformPoint(l.position, 1.0f, s.view, lu + 12);
                lu[19] = l.range > 0.0f ? l.range : 1e30f;
                lu[20] = l.attenuation0;
                lu[21] = l.attenuation1;
                lu[22] = l.attenuation2;
                if (l.type == GFX_LIGHT_SPOT) {
                    Gles2U_TransformPoint(l.direction, 0.0f, s.view, lu + 16);
                    lu[23] = l.falloff;
                    lu[24] = cosf(l.theta * 0.5f);
                    lu[25] = cosf(l.phi * 0.5f);
                }
            }
            lu[26] = 1.0f;
        }
        float *flags = u + GLES2_SLOT(77);
        flags[0] = 1.0f;
        flags[1] = s.colorVertex ? 1.0f : 0.0f;
        flags[2] = s.specular ? 1.0f : 0.0f;
        flags[3] = s.localViewer ? 1.0f : 0.0f;
        float *sources = u + GLES2_SLOT(78);
        sources[0] = (float)s.diffuseSource;
        sources[1] = (float)s.specularSource;
        sources[2] = (float)s.ambientSource;
        sources[3] = (float)s.emissiveSource;
    }
    float *fog = u + GLES2_SLOT(79);
    const bool vertexFogLinear = s.fog && s.fogTableMode == GFX_FOG_NONE && s.fogVertexMode == GFX_FOG_LINEAR;
    fog[0] = (s.fog && s.fogTableMode == GFX_FOG_NONE) ? (float)s.fogVertexMode : 0.0f;
    fog[1] = s.normalizeNormals ? 1.0f : 0.0f;
    // Direct3D takes the fog factor from the vertex's specular alpha when fog
    // is on with neither vertex nor table fog (a foggy stage's backdrop).
    fog[2] = (s.fog && s.fogTableMode == GFX_FOG_NONE && s.fogVertexMode == GFX_FOG_NONE) ? 1.0f : 0.0f;
    if (vertexFogLinear) {
        float *fogRange = u + GLES2_SLOT(80);
        fogRange[0] = s.fogStart;
        fogRange[1] = s.fogEnd;
    }
    float *tex = u + GLES2_SLOT(81);
    tex[0] = Gles2U_TexCoordMode(s.stages[0].texCoordIndex);
    tex[1] = Gles2U_TexCoordMode(s.stages[1].texCoordIndex);
    tex[2] = s.stages[0].textureTransform != GFX_TTFF_DISABLE ? 1.0f : 0.0f;
    tex[3] = s.stages[1].textureTransform != GFX_TTFF_DISABLE ? 1.0f : 0.0f;
    float *target = u + GLES2_SLOT(82);
    target[0] = (float)(scaled ? s.screenW : targetW);
    target[1] = (float)(scaled ? s.screenH : targetH);
    target[2] = viewport.width ? 1.0f / viewport.width : 0.0f;
    target[3] = viewport.height ? -1.0f / viewport.height : 0.0f;
}

// Single-struct convenience wrapper for unit tests and non-batched callers
// where s.world is populated. Recorded frames carry per-command worlds in
// frame.worlds[c.world] and call Gles2U_FillWorldView(world, s.view, u) +
// Gles2U_FillStatic(...) directly.
inline void Gles2U_FillVertex(const GfxDrawState &s, int targetW, int targetH, const GfxViewport &viewport,
                              bool scaled, float *u)
{
    Gles2U_FillWorldView(s, u);
    Gles2U_FillStatic(s, targetW, targetH, viewport, scaled, u);
}

// The pre-transformed vertex shader's one slot: the target size positions are
// given in.
inline void Gles2U_FillTL(const GfxDrawState &s, int targetW, int targetH, bool scaled, float *u)
{
    u[0] = (float)(scaled ? s.screenW : targetW);
    u[1] = (float)(scaled ? s.screenH : targetH);
    u[2] = 0.0f;
    u[3] = 0.0f;
}

// The fragment uniforms. kind0/kind1: Gles2TextureKind of each stage's
// drawn texture.
inline void Gles2U_FillFragment(const GfxDrawState &s, int kind0, int kind1, float *u)
{
    memset(u, 0, sizeof(float) * 4 * GLES2_FRAGMENT_VEC4);
    const GfxStageState &a = s.stages[0], &b = s.stages[1];
    u[GLES2_SLOT(0) + 0] = (float)a.colorOp;
    u[GLES2_SLOT(0) + 1] = (float)a.colorArg1;
    u[GLES2_SLOT(0) + 2] = (float)a.colorArg2;
    u[GLES2_SLOT(0) + 3] = (float)a.alphaOp;
    u[GLES2_SLOT(1) + 0] = (float)a.alphaArg1;
    u[GLES2_SLOT(1) + 1] = (float)a.alphaArg2;
    u[GLES2_SLOT(1) + 2] = (float)kind0;
    u[GLES2_SLOT(2) + 0] = (float)b.colorOp;
    u[GLES2_SLOT(2) + 1] = (float)b.colorArg1;
    u[GLES2_SLOT(2) + 2] = (float)b.colorArg2;
    u[GLES2_SLOT(2) + 3] = (float)b.alphaOp;
    u[GLES2_SLOT(3) + 0] = (float)b.alphaArg1;
    u[GLES2_SLOT(3) + 1] = (float)b.alphaArg2;
    u[GLES2_SLOT(3) + 2] = (float)kind1;
    Gles2U_Color(s.textureFactor, u + GLES2_SLOT(4));
    u[GLES2_SLOT(5) + 0] = s.alphaTest ? 1.0f : 0.0f;
    u[GLES2_SLOT(5) + 1] = (float)s.alphaFunc;
    u[GLES2_SLOT(6) + 0] = (s.alphaRef & 0xff) / 255.0f;
    Gles2U_Color(s.fogColor, u + GLES2_SLOT(7));
    u[GLES2_SLOT(8) + 0] = s.fog ? 1.0f : 0.0f;
    u[GLES2_SLOT(8) + 1] = (float)s.fogTableMode;
    u[GLES2_SLOT(8) + 2] = s.specular ? 1.0f : 0.0f;
    u[GLES2_SLOT(9) + 0] = s.fogStart;
    u[GLES2_SLOT(9) + 1] = s.fogEnd;
    memcpy(u + GLES2_SLOT(10), a.bumpMat, sizeof(a.bumpMat));
    u[GLES2_SLOT(11) + 0] = a.bumpLumScale;
    u[GLES2_SLOT(11) + 1] = a.bumpLumOffset;
}

// Whether two draws need the same 3D vertex uniforms (the state they are
// computed from compares equal).
inline bool Gles2U_SameStatic(const GfxDrawState &a, const GfxDrawState &b)
{
    for (int i = 0; i < 2; i++) {
        if (a.stages[i].texCoordIndex != b.stages[i].texCoordIndex ||
            a.stages[i].textureTransform != b.stages[i].textureTransform)
            return false;
        if (a.stages[i].textureTransform != GFX_TTFF_DISABLE &&
            memcmp(&a.texture[i], &b.texture[i], sizeof(a.texture[i])) != 0)
            return false;
    }
    if (a.viewport.width != b.viewport.width || a.viewport.height != b.viewport.height ||
        a.screenW != b.screenW || a.screenH != b.screenH || a.lighting != b.lighting ||
        a.normalizeNormals != b.normalizeNormals || a.fog != b.fog ||
        memcmp(&a.view, &b.view, sizeof(a.view)) != 0 ||
        memcmp(&a.projection, &b.projection, sizeof(a.projection)) != 0)
        return false;
    if (a.fog) {
        const bool aNoTable = a.fogTableMode == GFX_FOG_NONE;
        const bool bNoTable = b.fogTableMode == GFX_FOG_NONE;
        if (aNoTable != bNoTable)
            return false;
        if (aNoTable) {
            if (a.fogVertexMode != b.fogVertexMode)
                return false;
            if (a.fogVertexMode == GFX_FOG_LINEAR && (a.fogStart != b.fogStart || a.fogEnd != b.fogEnd))
                return false;
        }
    }
    if (a.lighting) {
        if (a.colorVertex != b.colorVertex || a.specular != b.specular || a.localViewer != b.localViewer ||
            a.diffuseSource != b.diffuseSource || a.specularSource != b.specularSource ||
            a.ambientSource != b.ambientSource || a.emissiveSource != b.emissiveSource ||
            a.ambient != b.ambient || memcmp(&a.material, &b.material, sizeof(a.material)) != 0 ||
            memcmp(a.lightEnabled, b.lightEnabled, sizeof(a.lightEnabled)) != 0)
            return false;
        for (int i = 0; i < 8; i++)
            if (a.lightEnabled[i] && memcmp(&a.lights[i], &b.lights[i], sizeof(a.lights[i])) != 0)
                return false;
    }
    return true;
}

inline bool Gles2U_SameVertex(const GfxDrawState &a, const GfxDrawState &b)
{
    return memcmp(&a.world, &b.world, sizeof(a.world)) == 0 && Gles2U_SameStatic(a, b);
}

#endif
