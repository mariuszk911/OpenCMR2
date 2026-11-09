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
