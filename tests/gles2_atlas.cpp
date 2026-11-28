// Texture atlas packer for the GLES2 executor (render/gles2/gles2_atlas.h).
#include "render/gles2/gles2_atlas.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int failures;
void Check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

GfxTexture MakeTex(int w, int h, int format = GFX_FORMAT_BC3, DWORD flags = 0)
{
    GfxTexture t = {};
    t.width = w;
    t.height = h;
    t.format = format;
    t.mipLevels = 1;
    t.flags = flags;
    t.levels[0][0].resize(16, 0xaa);
    return t;
}

} // namespace

int main()
{
    // 1. Tile placement, UV maths, and capacity for 128x256 tree cards in a 1024x1024 page (gutter 1).
    {
        GfxTexture page = {};
        Gles2Atlas atlas(1024, 1024, 1);
        atlas.SetPageTexture(&page);

        GfxTexture cards[22];
        int placed = 0;
        for (int i = 0; i < 22; i++) {
            cards[i] = MakeTex(128, 256);
            bool added = false;
            Gles2Atlas::Slot *s = atlas.Add(&cards[i], &added);
            if (s != NULL) {
                placed++;
                Check(added, "first Add marks addedNew = true");
                Check(s->paddedW == 130 && s->paddedH == 258, "128x256 tile padded to 130x258");
                Check(s->tile.page == &page, "tile points at atlas page");
                Check(Near(s->tile.u0, (float)(s->x + 1) / 1024.0f), "tile u0 accounts for gutter");
                Check(Near(s->tile.v0, (float)(s->y + 1) / 1024.0f), "tile v0 accounts for gutter");
                Check(Near(s->tile.uScale, 128.0f / 1024.0f), "tile uScale is w / pageW");
                Check(Near(s->tile.vScale, 256.0f / 1024.0f), "tile vScale is h / pageH");
            }
        }
        // 1024 / 130 = 7 columns, 1024 / 258 = 3 rows -> 21 tiles fit, 22nd returns NULL.
        Check(placed == 21, "1024x1024 page fits 21 tiles of 128x256 with 1 px gutter");
        Check(atlas.LiveCount() == 21u, "live count matches placed tiles");
        Check(atlas.Find(&cards[0]) != NULL && atlas.Find(&cards[20]) != NULL, "placed tiles found via Find");
        Check(atlas.Find(&cards[21]) == NULL, "unplaced tile not in atlas");

        // Re-adding an already-placed texture returns the same slot with addedNew = false.
        bool addedAgain = true;
        Gles2Atlas::Slot *dup = atlas.Add(&cards[3], &addedAgain);
        Check(dup != NULL && !addedAgain && dup->x == 3 * 130 && dup->y == 0,
              "re-adding existing texture returns existing slot without duplicating");

        // Free a slot and reuse it for a new same-sized texture.
        const int freedX = dup->x;
        const int freedY = dup->y;
        Check(atlas.Remove(&cards[3]), "Remove returns true for present texture");
        Check(atlas.Find(&cards[3]) == NULL, "removed texture no longer found");
        Check(atlas.LiveCount() == 20u, "live count decremented on Remove");

        bool reusedAdded = false;
        Gles2Atlas::Slot *reused = atlas.Add(&cards[21], &reusedAdded);
        Check(reused != NULL && reusedAdded && reused->x == freedX && reused->y == freedY && reused->dirty,
              "freed slot is reused at exact coordinates and marked dirty");
        Check(atlas.LiveCount() == 21u, "live count restored after reuse");

        // Removing all textures resets the page shelves completely.
        for (int i = 0; i < 22; i++)
            atlas.Remove(&cards[i]);
        Check(atlas.LiveCount() == 0u && atlas.Slots().empty() && !atlas.HasDirtySlots(),
              "removing last live texture resets atlas shelves and slots");
    }

    // 2. Dirty marking on texture update.
    {
        GfxTexture page = {};
        GfxTexture t = MakeTex(64, 64);
        Gles2Atlas atlas(256, 256, 1);
        atlas.SetPageTexture(&page);
        Gles2Atlas::Slot *s = atlas.Add(&t);
        Check(s != NULL && s->dirty && atlas.HasDirtySlots(), "new slot starts dirty");
        s->dirty = false;
        atlas.ClearDirtyFlag();
        Check(!atlas.HasDirtySlots(), "ClearDirtyFlag clears dirty state");
        atlas.MarkDirty(&t);
        Check(s->dirty && atlas.HasDirtySlots(), "MarkDirty marks existing slot dirty again");
    }

    // 3. Gutter fill (edge replication) on RGBA8.
    {
        // 2x2 source image with 4 distinct RGBA pixels:
        //   (0,0)=A  (1,0)=B
        //   (0,1)=C  (1,1)=D
        const uint8_t src[2 * 2 * 4] = {
            10, 20, 30, 40,     50, 60, 70, 80,
            90, 100, 110, 120,  130, 140, 150, 160
        };
        std::vector<uint8_t> dst;
        Gles2Atlas::FillPaddedRgba(src, 2, 2, 1, dst);
        // Expected 4x4 padded image:
        //   A A B B
        //   A A B B
        //   C C D D
        //   C C D D
        Check(dst.size() == 4u * 4u * 4u, "2x2 with 1 px gutter produces 4x4 RGBA buffer");
        auto pxAt = [&](int x, int y) { return &dst[(size_t)(y * 4 + x) * 4u]; };
        const uint8_t *A = &src[0], *B = &src[4], *C = &src[8], *D = &src[12];
        Check(memcmp(pxAt(0, 0), A, 4) == 0 && memcmp(pxAt(1, 0), A, 4) == 0 &&
                  memcmp(pxAt(2, 0), B, 4) == 0 && memcmp(pxAt(3, 0), B, 4) == 0,
              "top gutter row replicates top edge and corners");
        Check(memcmp(pxAt(0, 1), A, 4) == 0 && memcmp(pxAt(1, 1), A, 4) == 0 &&
                  memcmp(pxAt(2, 1), B, 4) == 0 && memcmp(pxAt(3, 1), B, 4) == 0,
              "first interior row replicates left and right edges");
        Check(memcmp(pxAt(0, 2), C, 4) == 0 && memcmp(pxAt(1, 2), C, 4) == 0 &&
                  memcmp(pxAt(2, 2), D, 4) == 0 && memcmp(pxAt(3, 2), D, 4) == 0,
              "second interior row replicates left and right edges");
        Check(memcmp(pxAt(0, 3), C, 4) == 0 && memcmp(pxAt(1, 3), C, 4) == 0 &&
                  memcmp(pxAt(2, 3), D, 4) == 0 && memcmp(pxAt(3, 3), D, 4) == 0,
              "bottom gutter row replicates bottom edge and corners");
    }

    // 4. Eligibility checks.
    {
        GfxTexture ok = MakeTex(128, 256, GFX_FORMAT_BC3, 0);
        GfxTexture cube = MakeTex(128, 128, GFX_FORMAT_BC3, GFX_TEXTURE_CUBE);
        GfxTexture rt = MakeTex(128, 128, GFX_FORMAT_BGRA8, GFX_TEXTURE_RENDER_TARGET);
        GfxTexture stream = MakeTex(128, 128, GFX_FORMAT_BGRA8, GFX_TEXTURE_STREAM);
        GfxTexture big = MakeTex(512, 256, GFX_FORMAT_BC3, 0);
        GfxTexture sq256 = MakeTex(256, 256, GFX_FORMAT_BC3, 0);
        GfxTexture npot = MakeTex(100, 128, GFX_FORMAT_BC3, 0);
        GfxTexture bump = MakeTex(128, 128, GFX_FORMAT_BUMP_DUDVL, 0);
        GfxTexture empty = MakeTex(128, 128, GFX_FORMAT_BC3, 0);
        empty.levels[0][0].clear();
        GfxTexture replaced = MakeTex(128, 128, GFX_FORMAT_BC3, 0);
        replaced.replacement = &ok;

        Check(Gles2Atlas::IsEligibleTexture(&ok), "128x256 BC3 2D texture is eligible");
        Check(!Gles2Atlas::IsEligibleTexture(&cube), "cube map rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&rt), "render target rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&stream), "stream texture rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&big), "texture > 256 rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&sq256), "256x256 texture rejected (> 128x256 texel budget)");
        Check(!Gles2Atlas::IsEligibleTexture(&npot), "non-power-of-two texture rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&bump), "bump texture rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&empty), "texture without CPU level 0 rejected");
        Check(!Gles2Atlas::IsEligibleTexture(&replaced), "un-resolved replacement owner rejected");
    }

    // 5. Hash table cap (kHashSize - 1) is enforced even across freed-slot reuse
    //    and mixed sizes (regression guard against RebuildHash overflow).
    {
        GfxTexture page = {};
        Gles2Atlas atlas(1024, 1024, 1);
        atlas.SetPageTexture(&page);

        GfxTexture smallA[127];
        for (int i = 0; i < 127; i++) {
            smallA[i] = MakeTex(32, 32);
            Check(atlas.Add(&smallA[i]) != NULL, "32x32 tile fits up to kHashSize - 1");
        }
        Check(atlas.LiveCount() == 127u, "atlas holds kHashSize - 1 live tiles");
        GfxTexture extra = MakeTex(32, 32);
        Check(atlas.Add(&extra) == NULL, "kHashSize-th tile rejected");

        // Remove 2 tiles of 32x32, add 2 tiles of 64x64 (leaving 2 freed 32x32 slots
        // while back at 127 live), then verify adding 32x32 is still rejected.
        Check(atlas.Remove(&smallA[0]) && atlas.Remove(&smallA[1]), "remove 2 smallA tiles");
        GfxTexture medB[2] = { MakeTex(64, 64), MakeTex(64, 64) };
        Check(atlas.Add(&medB[0]) != NULL && atlas.Add(&medB[1]) != NULL, "add 2 64x64 tiles");
        Check(atlas.LiveCount() == 127u, "live count back at kHashSize - 1");
        Check(atlas.Add(&extra) == NULL, "freed-slot reuse respects kHashSize - 1 cap");
    }

    // 6. Mixed-size shelf packing picks the tightest shelf rather than opening a new one.
    {
        GfxTexture page = {};
        Gles2Atlas atlas(512, 512, 1);
        atlas.SetPageTexture(&page);
        GfxTexture tall = MakeTex(64, 128);
        GfxTexture short1 = MakeTex(64, 64);
        GfxTexture short2 = MakeTex(64, 64);
        Check(atlas.Add(&tall) != NULL, "place 64x128");
        Check(atlas.Add(&short1) != NULL, "place first 64x64 in tall shelf");
        // Both sit on shelf 0 (y=0, height=130): short1 at x=66, short2 at x=132.
        Gles2Atlas::Slot *s2 = atlas.Add(&short2);
        Check(s2 != NULL && s2->y == 0 && s2->x == 132, "second 64x64 packs into existing shelf");
    }

    if (failures == 0)
        std::puts("gles2_atlas: ok");
    return failures != 0;
}
