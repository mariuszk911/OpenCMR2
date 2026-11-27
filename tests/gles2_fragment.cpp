// The GLES2 renderer's specialised fragment shaders (render/gles2/gles2_fragment.h).
#include "render/gles2/gles2_fragment.h"

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
bool Near(float a, float b) { return a - b < 1e-5f && b - a < 1e-5f; }
bool Has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }
}

int main()
{
    GfxDrawState s;   // stage 0: MODULATE(TEXTURE, CURRENT), alpha SELECTARG1(TEXTURE); stage 1 as default too
    s.stages[1].colorOp = GFX_TOP_DISABLE;
    Gles2FragmentKey k;
    Gles2Fragment_Key(s, GLES2_KIND_2D, GLES2_KIND_NONE, &k);
    const std::string plain = Gles2Fragment::Source(k);
    Check(Has(plain, "texture2D(tex0, vTex0.xy)") && !Has(plain, "texture2D(tex1"), "plain draw samples tex0 only");
    Check(!Has(plain, "textureCube(cube0") && !Has(plain, "discard") && !Has(plain, "mix("), "plain draw has no cube, test or fog");
    Check(Has(plain, "(texel * current)"), "modulate texture by the current colour");
    Check(Has(plain, "current = vec4(color.rgb, texel.a);"), "alpha from the texture (SELECTARG1)");
    Check(plain == Gles2Fragment::Source(k), "the text is deterministic");

    // Things that cannot change the picture do not change the key.
    GfxDrawState t = s;
    t.fogColor = 0x12345678;
    t.textureFactor = 0x1;
    t.alphaRef = 77;                   // test off
    t.stages[1].colorArg1 = GFX_TA_DIFFUSE;   // stage 1 disabled
    Gles2FragmentKey k2;
    Gles2Fragment_Key(t, GLES2_KIND_2D, GLES2_KIND_2D, &k2);
    Check(Gles2Fragment_SameKey(k, k2), "unused state, even a texture on a disabled stage 1, does not change the key");

    // A disabled stage 0 drops stage 1 and the textures.
    t = s;
    t.stages[0].colorOp = GFX_TOP_DISABLE;
    t.stages[1].colorOp = GFX_TOP_ADD;
    Gles2Fragment_Key(t, GLES2_KIND_2D, GLES2_KIND_2D, &k2);
    Check(k2.colorOp[0] == 0 && k2.colorOp[1] == 0 && k2.kind[0] == 0, "stage 0 disabled: no stages");
    const std::string flat = Gles2Fragment::Source(k2);
    Check(!Has(flat, "texture2D"), "no stages, no sampling");

    // Alpha test, fog, specular.
    t = s;
    t.alphaTest = true;
    t.alphaFunc = GFX_CMP_GREATEREQUAL;
    t.fog = true;
    t.fogTableMode = GFX_FOG_LINEAR;
    t.specular = true;
    Gles2Fragment_Key(t, GLES2_KIND_2D, GLES2_KIND_NONE, &k2);
    const std::string full = Gles2Fragment::Source(k2);
    Check(Has(full, ">= floor(u_f[6].x") && Has(full, "discard") && Has(full, "fogFactor") && Has(full, "vSpecular.rgb"),
          "alpha test, linear fog and specular are baked in");
    t.alphaFunc = GFX_CMP_ALWAYS;
    t.fogTableMode = GFX_FOG_NONE;
    Gles2Fragment_Key(t, GLES2_KIND_2D, GLES2_KIND_NONE, &k2);
    Check(k2.alphaTest == 0 && k2.fog == 1, "ALWAYS is no test; vertex fog uses the vertex factor");
    Check(Has(Gles2Fragment::Source(k2), "vFog.x"), "vertex fog");

    // Two stages with a cube map and complement/alpha-replicate modifiers.
    t = s;
    t.stages[1].colorOp = GFX_TOP_MODULATE2X;
    t.stages[1].colorArg1 = GFX_TA_TEXTURE | GFX_TA_COMPLEMENT;
    t.stages[1].colorArg2 = GFX_TA_CURRENT | GFX_TA_ALPHAREPLICATE;
    t.stages[1].alphaOp = GFX_TOP_DISABLE;
    Gles2Fragment_Key(t, GLES2_KIND_2D, GLES2_KIND_CUBE, &k2);
    const std::string two = Gles2Fragment::Source(k2);
    Check(Has(two, "textureCube(cube1, vTex1.xyz)") && Has(two, "(vec4(1.0) - texel)") && Has(two, "(current).aaaa") &&
              Has(two, "clamp(") && Has(two, "* 2.0"),
          "stage 1 with a cube map and modifiers");

    // Bump: stage 0 reads its bump texel, stage 1 is shifted and scaled.
    t = s;
    t.stages[0].colorOp = GFX_TOP_BUMPENVMAPLUMINANCE;
    t.stages[1].colorOp = GFX_TOP_ADD;
    Gles2Fragment_Key(t, GLES2_KIND_NONE, GLES2_KIND_2D, &k2);
    const std::string bump = Gles2Fragment::Source(k2);
    Check(Has(bump, "SignedByte") && Has(bump, "bumpOffset") && Has(bump, "texel.rgb *= luminance;") &&
              Has(bump, "texture2D(tex1, vTex1.xy + bumpOffset)"),
          "bump stage");

    // The uniforms follow the key: unused values stay zero, so draws that look the same share them.
    {
        static float a[GLES2_FRAGMENT_VEC4 * 4], b[GLES2_FRAGMENT_VEC4 * 4];
        GfxDrawState u = s;
        u.textureFactor = 0x11223344;
        u.fogColor = 0x55667788;
        u.alphaRef = 99;
        u.stages[0].bumpMat[0] = 0.5f;
        Gles2Fragment_Uniforms(s, k, a);
        Gles2Fragment_Uniforms(u, k, b);
        Check(memcmp(a, b, sizeof(a)) == 0, "values the key does not use do not reach the uniforms");
        Check(a[GLES2_SLOT(0)] == (float)GFX_TOP_MODULATE && a[GLES2_SLOT(1) + 2] == 1.0f, "operations and kinds come from the key");
        GfxDrawState f = s;
        f.stages[0].colorArg2 = GFX_TA_TFACTOR;
        f.textureFactor = 0xff804020;
        Gles2FragmentKey fk;
        Gles2Fragment_Key(f, GLES2_KIND_2D, GLES2_KIND_NONE, &fk);
        Gles2Fragment_Uniforms(f, fk, a);
        Check(Near(a[GLES2_SLOT(4) + 0], 0x80 / 255.0f), "a TFACTOR argument brings the factor");
        f = s;
        f.alphaTest = true;
        f.alphaFunc = GFX_CMP_GREATER;
        f.alphaRef = 0x40;
        Gles2Fragment_Key(f, GLES2_KIND_2D, GLES2_KIND_NONE, &fk);
        Gles2Fragment_Uniforms(f, fk, a);
        Check(a[GLES2_SLOT(5)] == 1.0f && a[GLES2_SLOT(5) + 1] == 5.0f && Near(a[GLES2_SLOT(6)], 0x40 / 255.0f), "an alpha test brings its reference");
    }

    // A key survives its text line; bad lines are refused.
    {
        GfxDrawState b = s;
        b.stages[0].colorOp = GFX_TOP_BUMPENVMAPLUMINANCE;
        b.stages[1].colorOp = GFX_TOP_MODULATE;
        b.stages[1].colorArg2 = GFX_TA_TFACTOR | GFX_TA_COMPLEMENT;
        b.alphaTest = true;
        b.alphaFunc = GFX_CMP_GREATEREQUAL;
        b.fog = true;
        b.specular = true;
        Gles2FragmentKey original, back;
        Gles2Fragment_Key(b, GLES2_KIND_NONE, GLES2_KIND_CUBE, &original);
        const std::string line = Gles2Fragment_KeyText(true, original);
        bool transformed = false;
        Check(Gles2Fragment_ParseKey(line.c_str(), &transformed, &back) && transformed &&
                  Gles2Fragment_SameKey(original, back),
              "a key round-trips through its line");
        Check(Gles2Fragment::Source(original) == Gles2Fragment::Source(back), "and gives the same shader text");
        Check(!Gles2Fragment_ParseKey("v2 0 1", &transformed, &back), "another version is refused");
        Check(!Gles2Fragment_ParseKey((line + " 7").c_str(), &transformed, &back), "extra fields are refused");
        Check(!Gles2Fragment_ParseKey("v1 0 1 2", &transformed, &back), "a short line is refused");
        Check(!Gles2Fragment_ParseKey("v1 0 9 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", &transformed, &back), "a value out of range is refused");
        Check(!Gles2Fragment_ParseKey("", &transformed, &back), "an empty line is refused");
    }

    // The variations used to warm the shader binaries.
    {
        GfxDrawState d = s;
        Gles2FragmentKey base;
        Gles2Fragment_Key(d, GLES2_KIND_2D, GLES2_KIND_NONE, &base);   // no test, no fog
        std::vector<std::pair<bool, Gles2FragmentKey> > v3 = Gles2Fragment_Variations(false, base);
        Check(v3.size() == 4, "a 3D key: with and without the alpha test and with and without fog");
        std::vector<std::pair<bool, Gles2FragmentKey> > v2 = Gles2Fragment_Variations(true, base);
        Check(v2.size() == 2, "a 2D key: with and without the alpha test");
        bool hasBase = false;
        for (size_t i = 0; i < v3.size(); i++)
            hasBase = hasBase || Gles2Fragment_SameKey(v3[i].second, base);
        Check(hasBase, "the key itself is among its variations");
        d.alphaTest = true;
        d.alphaFunc = GFX_CMP_GREATER;
        d.fog = true;
        Gles2FragmentKey odd;
        Gles2Fragment_Key(d, GLES2_KIND_2D, GLES2_KIND_NONE, &odd);   // GREATER test, fog on
        std::vector<std::pair<bool, Gles2FragmentKey> > vo = Gles2Fragment_Variations(false, odd);
        Check(vo.size() == 4, "no duplicates when the key already has a test and fog");
        bool keepsOwnTest = false;
        for (size_t i = 0; i < vo.size(); i++)
            keepsOwnTest = keepsOwnTest || vo[i].second.alphaTest == GFX_CMP_GREATER;
        Check(keepsOwnTest, "the key's own alpha function is kept");
    }

    // Specialised fragment uniforms omit baked-in slots (0..3, 5, 8) and report
    // the exact highest vec4 slot read.
    {
        static float ua[GLES2_FRAGMENT_VEC4 * 4], ub[GLES2_FRAGMENT_VEC4 * 4];
        Check(Gles2Fragment_SpecialVec4Count(k) == 0, "plain specialised shader reads no fragment uniforms");
        Check(Gles2Fragment_SpecialUniforms(s, k, ua) == 0, "plain SpecialUniforms returns 0");

        GfxDrawState fa = s, fb = s;
        fa.fog = true;
        fa.fogColor = 0xff336699;
        fb.fog = true;
        fb.fogColor = 0xff336699;
        fb.stages[0].colorOp = GFX_TOP_ADD;   // different op, same runtime fog uniform
        Gles2FragmentKey kfa, kfb;
        Gles2Fragment_Key(fa, GLES2_KIND_2D, GLES2_KIND_NONE, &kfa);
        Gles2Fragment_Key(fb, GLES2_KIND_2D, GLES2_KIND_NONE, &kfb);
        const int ca = Gles2Fragment_SpecialUniforms(fa, kfa, ua);
        const int cb = Gles2Fragment_SpecialUniforms(fb, kfb, ub);
        Check(ca == 8 && cb == 8 && Gles2Fragment_SpecialVec4Count(kfa) == 8, "vertex fog uses 8 vec4 slots");
        Check(memcmp(ua, ub, sizeof(ua)) == 0, "different ops with same fog share specialised fragment uniforms");

        fa.stages[0].colorOp = GFX_TOP_BUMPENVMAPLUMINANCE;
        fa.stages[1].colorOp = GFX_TOP_ADD;
        Gles2Fragment_Key(fa, GLES2_KIND_2D, GLES2_KIND_2D, &kfa);
        Check(Gles2Fragment_SpecialVec4Count(kfa) == 12 && Gles2Fragment_SpecialUniforms(fa, kfa, ua) == 12,
              "bump luminance uses 12 vec4 slots");
    }

    // Distinct states, distinct keys.
    t = s;
    t.stages[0].colorOp = GFX_TOP_ADD;
    Gles2FragmentKey k3;
    Gles2Fragment_Key(t, GLES2_KIND_2D, GLES2_KIND_NONE, &k3);
    Check(!Gles2Fragment_SameKey(k, k3) && Gles2Fragment::Source(k) != Gles2Fragment::Source(k3), "other op, other key");
    if (failures == 0)
        std::puts("gles2_fragment: ok");
    return failures != 0;
}
