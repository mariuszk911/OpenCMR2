// Pass-through texture stages (port/stage_passthrough.h). The MiniGL renderer
// leaves a stage off when its colour selects CURRENT: mapped to GL_MODULATE it
// multiplied the car's texture into the Standard environment pass, which
// showed as flickering squares instead of sun flares on the Amiga.
#include "port/stage_passthrough.h"

#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    // The game's blend mode 5 (Graphics.cpp, ConfigureTextureStageBlendMode):
    // the car texture on stage 1 of the Standard environment pass.
    REQUIRE(GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG2, GFX_TA_TEXTURE, GFX_TA_CURRENT));
    REQUIRE(GfxStage_AlphaReadsTexture(GFX_TOP_SELECTARG1, GFX_TA_TEXTURE, GFX_TA_CURRENT));

    REQUIRE(GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG1, GFX_TA_CURRENT, GFX_TA_TEXTURE));

    // Stages that change the colour stay on.
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG1, GFX_TA_TEXTURE, GFX_TA_CURRENT));
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG2, GFX_TA_CURRENT, GFX_TA_TEXTURE));
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_MODULATE, GFX_TA_TEXTURE, GFX_TA_CURRENT));
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_ADD, GFX_TA_CURRENT, GFX_TA_CURRENT));
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_DISABLE, GFX_TA_CURRENT, GFX_TA_CURRENT));
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG1, GFX_TA_DIFFUSE, GFX_TA_CURRENT));
    // A modified CURRENT is another colour.
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG1, GFX_TA_CURRENT | GFX_TA_COMPLEMENT, 0));
    REQUIRE(!GfxStage_ColourPassesCurrent(GFX_TOP_SELECTARG2, 0, GFX_TA_CURRENT | GFX_TA_ALPHAREPLICATE));

    REQUIRE(!GfxStage_AlphaReadsTexture(GFX_TOP_DISABLE, GFX_TA_TEXTURE, GFX_TA_TEXTURE));
    REQUIRE(!GfxStage_AlphaReadsTexture(GFX_TOP_SELECTARG2, GFX_TA_TEXTURE, GFX_TA_CURRENT));
    REQUIRE(GfxStage_AlphaReadsTexture(GFX_TOP_MODULATE, GFX_TA_CURRENT, GFX_TA_TEXTURE));
    REQUIRE(!GfxStage_AlphaReadsTexture(GFX_TOP_MODULATE, GFX_TA_CURRENT, GFX_TA_DIFFUSE));

    // Two units: the Bump environment pass (bump luminance, then the flare
    // modulating CURRENT) on; the Standard pass's pass-through stage 1 off.
    REQUIRE(GfxStage_DrawUsesTwoUnits(GFX_TOP_BUMPENVMAPLUMINANCE, 1, GFX_TOP_MODULATE, GFX_TA_TEXTURE,
                                      GFX_TA_CURRENT, 1));
    REQUIRE(!GfxStage_DrawUsesTwoUnits(GFX_TOP_MODULATE, 1, GFX_TOP_SELECTARG2, GFX_TA_TEXTURE,
                                       GFX_TA_CURRENT, 1));
    REQUIRE(!GfxStage_DrawUsesTwoUnits(GFX_TOP_MODULATE, 1, GFX_TOP_DISABLE, GFX_TA_TEXTURE, GFX_TA_CURRENT, 1));
    REQUIRE(!GfxStage_DrawUsesTwoUnits(GFX_TOP_DISABLE, 1, GFX_TOP_MODULATE, GFX_TA_TEXTURE, GFX_TA_CURRENT, 1));
    REQUIRE(!GfxStage_DrawUsesTwoUnits(GFX_TOP_MODULATE, 0, GFX_TOP_MODULATE, GFX_TA_TEXTURE, GFX_TA_CURRENT, 1));
    REQUIRE(!GfxStage_DrawUsesTwoUnits(GFX_TOP_MODULATE, 1, GFX_TOP_MODULATE, GFX_TA_TEXTURE, GFX_TA_CURRENT, 0));

    printf("stage_passthrough: ok\n");
    return 0;
}
