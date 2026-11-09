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
