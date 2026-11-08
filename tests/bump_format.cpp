// Bump maps get the renderer's bump format (game/Graphics.cpp,
// SelectTextureFormats). The renderers offer one bump format and
// CreateTextureSurface takes it only when a 16 and a 32-bit one exist, as
// Direct3D drivers offered; bump maps fell back to BGRX8 and the Bump
// environment pass read its luminance from the dU byte, so the car showed
// almost no reflection on the Linux host and the Amiga.
#include "port/types.h"
#include "Graphics.h"

#include <cstdio>
#include <cstdlib>

// The texture manager CGraphics::m_pTextureManager points at.
extern D3DTextureManager g_textureManager;

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "bump format: line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void Check(unsigned int depth)
{
    Graphics graphics{};
    graphics.depth = depth;
    graphics.field913_0x3bc = 0x18; // environment map on, Bump
    g_pGraphics = &graphics;
    D3DTextureManager &manager = g_textureManager;
    manager = D3DTextureManager{};

    CGraphics::SelectTextureFormats();
    REQUIRE(manager.textureInfo5 != NULL);
    REQUIRE(manager.textureInfo5->gfxFormat == GFX_FORMAT_BUMP_DUDVL);

    Texture texture{};
    CGraphics::CreateTextureSurface(&texture, 4, 4, 0x20);
    REQUIRE(texture.pSurface != NULL);
    GfxLockedRect desc{};
    REQUIRE(Gfx_LockTexture(texture.pSurface, 0, 0, &desc));
    REQUIRE((desc.format.flags & GFX_PF_BUMP) != 0);
    REQUIRE(desc.format.bitsPerPixel == 24);
    Gfx_UnlockTexture(texture.pSurface, 0, 0);
    Gfx_DestroyTexture(texture.pSurface);
}

int main()
{
    Check(32);
    Check(16);
    printf("bump_format: ok\n");
    return 0;
}
