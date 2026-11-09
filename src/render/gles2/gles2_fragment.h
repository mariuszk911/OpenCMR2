/*
 * Fragment shaders specialised per draw state (GLES2 renderer).
 *
 * The general fragment shader (gfx_gles2_shaders.h) picks the texture-stage
 * operations at run time from uniforms; on the V3D that costs 60-90 ms a frame
 * at 1024x768 (measured: a plain texture x colour shader brings the GPU wait
 * from 65-103 ms down to 10-13 ms). This builds a shader with the stage
 * operations, texture kinds, alpha test function, fog and specular add baked
 * in as straight-line code, from a key taken from the draw's state. Pure
 * functions of the key, tested on the host (tests/gles2_fragment.cpp).
 *
 * ogles2 finds a compiled shader by a hash of the exact text, so the text for
 * a key is always byte for byte the same (no counters, no addresses): the
 * binaries compiled once (OGLES2Cache:) can be shipped in PROGDIR:shaders/.
 * Uniform slots are those of gles2_uniforms.h (u_f).
 */
#ifndef OPENCMR2_RENDER_GLES2_GLES2_FRAGMENT_H
#define OPENCMR2_RENDER_GLES2_GLES2_FRAGMENT_H

#include "render/gfx_internal.h"
#include "render/gles2/gles2_uniforms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

struct Gles2FragmentKey {
    int kind[2];        // Gles2TextureKind of each stage's texture
    int colorOp[2], colorArg1[2], colorArg2[2];
    int alphaOp[2], alphaArg1[2], alphaArg2[2];
    int alphaTest;      // 0 off, else the compare function (GFX_CMP_*, not ALWAYS)
    int fog;            // 0 off, 1 the vertex's fog factor, 2 linear table fog
    int specular;       // add the specular colour
};

// The key of a draw. Whatever cannot change the picture is zeroed, so that
// draws that look the same share a shader: nothing after a disabled stage 0,
// no stage 1 when it is disabled, no alpha function when the test is off.
inline void Gles2Fragment_Key(const GfxDrawState &s, int kind0, int kind1, Gles2FragmentKey *key)
{
    memset(key, 0, sizeof(*key));
    const int kinds[2] = { kind0, kind1 };
    for (int i = 0; i < 2; i++) {
        const GfxStageState &st = s.stages[i];
        if (i == 1 && key->colorOp[0] == 0)
            break;
        if (st.colorOp == GFX_TOP_DISABLE) {
            if (i == 0)
                break;
            continue;
        }
        const bool bump = i == 0 && st.colorOp >= GFX_TOP_BUMPENVMAP;
        key->colorOp[i] = st.colorOp;
        // A bump stage reads its own 2D texture, whatever kind the draw has.
        key->kind[i] = bump ? GLES2_KIND_2D : kinds[i];
        if (!bump) {
            key->colorArg1[i] = st.colorArg1;
            key->colorArg2[i] = st.colorArg2;
            key->alphaOp[i] = st.alphaOp;
            if (st.alphaOp != GFX_TOP_DISABLE) {
                key->alphaArg1[i] = st.alphaArg1;
                key->alphaArg2[i] = st.alphaArg2;
            }
        }
    }
    if (key->colorOp[0] == 0)
        memset(key, 0, sizeof(*key));
    if (s.alphaTest && s.alphaFunc != GFX_CMP_ALWAYS)
        key->alphaTest = s.alphaFunc;
    if (s.fog)
        key->fog = s.fogTableMode == GFX_FOG_LINEAR ? 2 : 1;
    key->specular = s.specular ? 1 : 0;
}

inline bool Gles2Fragment_SameKey(const Gles2FragmentKey &a, const Gles2FragmentKey &b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

// A key as one text line, to remember the shaders a game used and compile them
// at the next start instead of in the middle of a loading screen (a
// two-stage 3D shader takes 13 s to compile the first time). "v1" is the
// version of the line; a shader text change needs no new version, the
// compiled programs are found by the text's hash.
// The key's fields in the order of a line, with the largest value each may have.
inline int Gles2Fragment_Fields(Gles2FragmentKey &k, int **field, int *limit)
{
    int *const f[17] = { &k.kind[0], &k.kind[1], &k.colorOp[0], &k.colorOp[1], &k.colorArg1[0], &k.colorArg1[1],
                         &k.colorArg2[0], &k.colorArg2[1], &k.alphaOp[0], &k.alphaOp[1], &k.alphaArg1[0],
                         &k.alphaArg1[1], &k.alphaArg2[0], &k.alphaArg2[1], &k.alphaTest, &k.fog, &k.specular };
    static const int l[17] = { 2, 2, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 8, 2, 1 };
    for (int i = 0; i < 17; i++) {
        field[i] = f[i];
        limit[i] = l[i];
    }
    return 17;
}

inline std::string Gles2Fragment_KeyText(bool transformed, const Gles2FragmentKey &key)
{
    Gles2FragmentKey k = key;
    int *field[17], limit[17];
    const int n = Gles2Fragment_Fields(k, field, limit);
    std::string line = transformed ? "v1 1" : "v1 0";
    char number[16];
    for (int i = 0; i < n; i++) {
        snprintf(number, sizeof(number), " %d", *field[i]);
        line += number;
    }
    return line;
}

// Parses a line of Gles2Fragment_KeyText; false for anything else or values
// outside what a key can hold.
inline bool Gles2Fragment_ParseKey(const char *line, bool *transformed, Gles2FragmentKey *key)
{
    Gles2FragmentKey k;
    memset(&k, 0, sizeof(k));
    int *field[17], limit[17];
    const int n = Gles2Fragment_Fields(k, field, limit);
    if (strncmp(line, "v1 ", 3) != 0 || (line[3] != '0' && line[3] != '1'))
        return false;
    const bool t = line[3] == '1';
    const char *p = line + 4;
    for (int i = 0; i < n; i++) {
        char *end = NULL;
        const long value = strtol(p, &end, 10);
        if (end == p || value < 0 || value > limit[i])
            return false;
        *field[i] = (int)value;
        p = end;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != '\0')
        return false;
    *key = k;
    *transformed = t;
    return true;
}

// The fragment uniforms a program built from key reads, and nothing else: the
// operations, kinds, alpha function, fog and specular flags come from the key
// itself, and values the key does not use (the texture factor without a
// TFACTOR argument, the alpha reference without an alpha test, the fog colour
// without fog, the bump matrix without a bump stage) are left zero. Draws that
// look the same then have the same block, which is uploaded once.
inline void Gles2Fragment_Uniforms(const GfxDrawState &s, const Gles2FragmentKey &k, float *u)
{
    memset(u, 0, sizeof(float) * 4 * GLES2_FRAGMENT_VEC4);
    for (int i = 0; i < 2; i++) {
        float *a = u + GLES2_SLOT(2 * i);
        float *b = u + GLES2_SLOT(2 * i + 1);
        a[0] = (float)k.colorOp[i];
        a[1] = (float)k.colorArg1[i];
        a[2] = (float)k.colorArg2[i];
        a[3] = (float)k.alphaOp[i];
        b[0] = (float)k.alphaArg1[i];
        b[1] = (float)k.alphaArg2[i];
        b[2] = (float)k.kind[i];
    }
    bool factor = false;
    for (int i = 0; i < 2; i++) {
        if (k.colorOp[i] == 0)
            continue;
        const int args[4] = { k.colorArg1[i], k.colorArg2[i], k.alphaArg1[i], k.alphaArg2[i] };
        for (int a = 0; a < 4; a++)
            factor = factor || (args[a] & 0xf) == GFX_TA_TFACTOR;
    }
    if (factor)
        Gles2U_Color(s.textureFactor, u + GLES2_SLOT(4));
    if (k.alphaTest) {
        u[GLES2_SLOT(5) + 0] = 1.0f;
        u[GLES2_SLOT(5) + 1] = (float)k.alphaTest;
        u[GLES2_SLOT(6) + 0] = (s.alphaRef & 0xff) / 255.0f;
    }
    if (k.fog) {
        Gles2U_Color(s.fogColor, u + GLES2_SLOT(7));
        u[GLES2_SLOT(8) + 0] = 1.0f;
        u[GLES2_SLOT(8) + 1] = k.fog == 2 ? 3.0f : 0.0f;
        u[GLES2_SLOT(9) + 0] = s.fogStart;
        u[GLES2_SLOT(9) + 1] = s.fogEnd;
    }
    u[GLES2_SLOT(8) + 2] = k.specular ? 1.0f : 0.0f;
    if (k.colorOp[0] >= GFX_TOP_BUMPENVMAP) {
        memcpy(u + GLES2_SLOT(10), s.stages[0].bumpMat, sizeof(s.stages[0].bumpMat));
        u[GLES2_SLOT(11) + 0] = s.stages[0].bumpLumScale;
        u[GLES2_SLOT(11) + 1] = s.stages[0].bumpLumOffset;
    }
}

namespace Gles2Fragment {

inline std::string Arg(int arg, const char *current, const char *texel)
{
    std::string v;
    switch (arg & 0xf) {
    case 0: v = "vDiffuse"; break;
    case 1: v = current; break;
    case 2: v = texel; break;
    case 3: v = "u_f[4]"; break;
    case 4: v = "vSpecular"; break;
    default: v = current; break;
    }
    if (arg & 0x10)
        v = "(vec4(1.0) - " + v + ")";
    if (arg & 0x20)
        v = "(" + v + ").aaaa";
    return v;
}

inline std::string Op(int op, const std::string &a, const std::string &b)
{
    switch (op) {
    case GFX_TOP_SELECTARG2: return b;
    case GFX_TOP_MODULATE: return "(" + a + " * " + b + ")";
    case GFX_TOP_MODULATE2X: return "clamp(" + a + " * " + b + " * 2.0, 0.0, 1.0)";
    case GFX_TOP_MODULATE4X: return "clamp(" + a + " * " + b + " * 4.0, 0.0, 1.0)";
    case GFX_TOP_ADD: return "clamp(" + a + " + " + b + ", 0.0, 1.0)";
    case GFX_TOP_ADDSIGNED: return "clamp(" + a + " + " + b + " - 0.5, 0.0, 1.0)";
    case GFX_TOP_ADDSIGNED2X: return "clamp((" + a + " + " + b + " - 0.5) * 2.0, 0.0, 1.0)";
    case GFX_TOP_SUBTRACT: return "clamp(" + a + " - " + b + ", 0.0, 1.0)";
    default: return a;   // SELECTARG1 and anything unknown
    }
}

inline std::string Texel(int kind, int stage, const char *bumpOffset)
{
    const char *coord = stage == 0 ? "vTex0" : "vTex1";
    const char *tex = stage == 0 ? "tex0" : "tex1";
    const char *cube = stage == 0 ? "cube0" : "cube1";
    std::string c = coord;
    if (kind == GLES2_KIND_CUBE)
        return std::string("textureCube(") + cube + ", " + c + ".xyz" +
               (bumpOffset ? std::string(" + vec3(") + bumpOffset + ", 0.0)" : "") + ")";
    if (kind == GLES2_KIND_2D)
        return std::string("texture2D(") + tex + ", " + c + ".xy" +
               (bumpOffset ? std::string(" + ") + bumpOffset : "") + ")";
    return "vec4(1.0)";
}

// One stage's lines (not a bump stage): the stage's output is `current`.
inline void Stage(std::string &out, const Gles2FragmentKey &k, int i, const char *bumpOffset, bool luminance)
{
    out += "    {\n";
    out += "        vec4 texel = " + Texel(k.kind[i], i, i == 1 ? bumpOffset : NULL) + ";\n";
    if (i == 1 && luminance)
        out += "        texel.rgb *= luminance;\n";
    out += "        vec4 color = " + Op(k.colorOp[i], Arg(k.colorArg1[i], "current", "texel"),
                                      Arg(k.colorArg2[i], "current", "texel")) + ";\n";
    if (k.alphaOp[i] == GFX_TOP_DISABLE || k.alphaOp[i] == 0)
        out += "        current = vec4(color.rgb, current.a);\n";
    else
        out += "        current = vec4(color.rgb, " +
               Op(k.alphaOp[i], Arg(k.alphaArg1[i], "current", "texel"), Arg(k.alphaArg2[i], "current", "texel")) +
               ".a);\n";
    out += "    }\n";
}

inline std::string Source(const Gles2FragmentKey &k)
{
    const bool bump = k.colorOp[0] >= GFX_TOP_BUMPENVMAP;
    const bool luminance = k.colorOp[0] == GFX_TOP_BUMPENVMAPLUMINANCE;
    std::string s;
    s += "\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n\n";
    s += "varying vec4 vDiffuse;\nvarying vec4 vSpecular;\nvarying vec4 vTex0;\nvarying vec4 vTex1;\nvarying vec2 vFog;\n\n";
    s += "uniform sampler2D tex0;\nuniform samplerCube cube0;\nuniform sampler2D tex1;\nuniform samplerCube cube1;\n\n";
    s += "uniform vec4 u_f[13];\n\n";
    if (bump)
        s += "float SignedByte(float v)\n{\n    float b = floor(v * 255.0 + 0.5);\n"
             "    return (b >= 128.0 ? b - 256.0 : b) / 128.0;\n}\n\n";
    s += "void main()\n{\n    vec4 current = vDiffuse;\n";
    if (k.colorOp[0] != 0) {
        if (bump) {
            s += "    vec4 bump = texture2D(tex0, vTex0.xy);\n";
            s += "    float du = SignedByte(bump.r);\n    float dv = SignedByte(bump.g);\n";
            s += "    vec2 bumpOffset = vec2(du * u_f[10].x + dv * u_f[10].z, du * u_f[10].y + dv * u_f[10].w);\n";
            if (luminance)
                s += "    float luminance = clamp(bump.b * u_f[11].x + u_f[11].y, 0.0, 1.0);\n";
        } else {
            Stage(s, k, 0, NULL, false);
        }
        if (k.colorOp[1] != 0)
            Stage(s, k, 1, bump ? "bumpOffset" : NULL, luminance);
    }
    if (k.specular)
        s += "    current.rgb = clamp(current.rgb + vSpecular.rgb, 0.0, 1.0);\n";
    if (k.alphaTest == GFX_CMP_NEVER) {
        s += "    discard;\n";
    } else if (k.alphaTest != 0) {
        const char *op = k.alphaTest == GFX_CMP_LESS         ? "<"
                         : k.alphaTest == GFX_CMP_EQUAL      ? "=="
                         : k.alphaTest == GFX_CMP_LESSEQUAL  ? "<="
                         : k.alphaTest == GFX_CMP_GREATER    ? ">"
                         : k.alphaTest == GFX_CMP_NOTEQUAL   ? "!="
                                                             : ">=";
        s += "    if (!(floor(current.a * 255.0 + 0.5) ";
        s += op;
        s += " floor(u_f[6].x * 255.0 + 0.5)))\n        discard;\n";
    }
    if (k.fog == 1) {
        s += "    current.rgb = mix(u_f[7].rgb, current.rgb, vFog.x);\n";
    } else if (k.fog == 2) {
        s += "    float range = u_f[9].y - u_f[9].x;\n";
        s += "    float fogFactor = range != 0.0 ? clamp((u_f[9].y - vFog.y) / range, 0.0, 1.0) : 1.0;\n";
        s += "    current.rgb = mix(u_f[7].rgb, current.rgb, fogFactor);\n";
    }
    s += "    gl_FragColor = current;\n}\n";
    return s;
}

} // namespace Gles2Fragment

#endif
