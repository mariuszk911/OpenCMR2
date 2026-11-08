/*
 * Texture stages that leave the colour unchanged.
 *
 * A stage whose colour operation selects CURRENT passes the previous stage's
 * colour through; only its alpha can read its texture. GL 1.1 texture
 * environments (MiniGL has no GL_COMBINE) apply one operation to colour and
 * alpha, so such a stage cannot be expressed: mapped to GL_MODULATE it
 * multiplies its texture into the colour. The car's Standard environment pass
 * is one (stage 0 the reflection, stage 1 the car's own texture selecting
 * CURRENT), and MODULATE turned its sun flares into flickering squares on the
 * Amiga. Leaving the stage off draws the colour exactly; the texture's alpha
 * is lost, which the caller reports.
 */
#ifndef OPENCMR2_PORT_STAGE_PASSTHROUGH_H
#define OPENCMR2_PORT_STAGE_PASSTHROUGH_H

#include "port/gfx.h"

/* Whether a stage's colour is exactly the previous stage's colour. */
static inline int GfxStage_ColourPassesCurrent(int colorOp, int colorArg1, int colorArg2)
{
    int arg;
    if (colorOp == GFX_TOP_SELECTARG1)
        arg = colorArg1;
    else if (colorOp == GFX_TOP_SELECTARG2)
        arg = colorArg2;
    else
        return 0;
    /* A complemented or alpha-replicated CURRENT is not the same colour. */
    return arg == GFX_TA_CURRENT;
}

/* Whether leaving such a stage off loses its texture's alpha. */
static inline int GfxStage_AlphaReadsTexture(int alphaOp, int alphaArg1, int alphaArg2)
{
    switch (alphaOp) {
    case GFX_TOP_DISABLE:
        return 0;
    case GFX_TOP_SELECTARG1:
        return (alphaArg1 & 0xf) == GFX_TA_TEXTURE;
    case GFX_TOP_SELECTARG2:
        return (alphaArg2 & 0xf) == GFX_TA_TEXTURE;
    default:
        return (alphaArg1 & 0xf) == GFX_TA_TEXTURE || (alphaArg2 & 0xf) == GFX_TA_TEXTURE;
    }
}

#endif
