/*
 * Texture atlas packer for the GLES2 executor: packs small power-of-two 2D
 * textures (such as alpha-blended tree cards) into a single RGBA8 page with
 * edge-replicated gutters so the batcher can merge interleaved draws without
 * changing painter's order.
 *
 * Pure, GL-free helper tested on the host (tests/gles2_atlas.cpp).
 */
#ifndef OPENCMR2_RENDER_GLES2_GLES2_ATLAS_H
#define OPENCMR2_RENDER_GLES2_GLES2_ATLAS_H

#include "render/gfx_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <vector>

struct Gles2AtlasTile {
    GfxTexture *page;
    float u0;
    float v0;
    float uScale;
    float vScale;
};

namespace Gles2AtlasDetail {

// Same Bebbo GCC 6.5 workaround as Gles2BatchDetail::ClearVector: pin the
// vector pointer in an address register so the backend does not drop its load.
template <typename T>
inline void ClearVector(std::vector<T> &v)
{
#if defined(__m68k__) && defined(__GNUC__) && !defined(__clang__)
    std::vector<T> *p = &v;
    __asm__ volatile("" : "+a"(p));
    p->clear();
#else
    v.clear();
#endif
}

inline bool IsPowerOfTwo(int v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

inline uint32_t HashPtr(const GfxTexture *p)
{
    uintptr_t v = (uintptr_t)p;
    uint32_t h = (uint32_t)(v >> 3) ^ (uint32_t)(v >> 11);
    return h * 2654435761u;
}

} // namespace Gles2AtlasDetail

struct Gles2Atlas {
    static const int kDefaultPageWidth = 1024;
    static const int kDefaultPageHeight = 1024;
    static const int kDefaultGutter = 1;
    static const int kMaxTileSize = 256;
    static const int kMaxTileTexels = 128 * 256; // excludes 256x256 so tree cards (128x256) fit one page
    static const size_t kHashSize = 128u; // power of two; caps live tiles at kHashSize - 1

    struct Slot {
        GfxTexture *source;
        int x;
        int y;
        int w;
        int h;
        int paddedW;
        int paddedH;
        Gles2AtlasTile tile;
        bool inUse;
        bool dirty;
    };

    struct Shelf {
        int y;
        int height;
        int nextX;
    };

    explicit Gles2Atlas(int pageWidth = kDefaultPageWidth, int pageHeight = kDefaultPageHeight,
                        int gutter = kDefaultGutter)
        : pageW(pageWidth), pageH(pageHeight), gutterPx(gutter < 0 ? 0 : gutter), pageTex(NULL), usedY(0),
          liveCount(0), anyDirty(false)
    {
        ClearHash();
    }

    int PageWidth() const { return pageW; }
    int PageHeight() const { return pageH; }
    int Gutter() const { return gutterPx; }
    size_t LiveCount() const { return liveCount; }
    bool HasDirtySlots() const { return anyDirty; }
    GfxTexture *PageTexture() const { return pageTex; }
    std::vector<Slot> &Slots() { return slots; }
    const std::vector<Slot> &Slots() const { return slots; }

    void ClearDirtyFlag() { anyDirty = false; }

    void SetPageTexture(GfxTexture *page)
    {
        pageTex = page;
        for (size_t i = 0; i < slots.size(); i++)
            slots[i].tile.page = page;
    }

    void Reset()
    {
        Gles2AtlasDetail::ClearVector(slots);
        Gles2AtlasDetail::ClearVector(shelves);
        usedY = 0;
        liveCount = 0;
        anyDirty = false;
        ClearHash();
    }

    static bool IsEligibleTexture(const GfxTexture *t)
    {
        if (t == NULL)
            return false;
        if (t->cube != NULL || t->replacement != NULL)
            return false;
        if ((t->flags & (GFX_TEXTURE_CUBE | GFX_TEXTURE_RENDER_TARGET | GFX_TEXTURE_STREAM)) != 0)
            return false;
        if (t->format == GFX_FORMAT_BUMP_DUDVL)
            return false;
        if (t->width <= 0 || t->height <= 0 || t->width > kMaxTileSize || t->height > kMaxTileSize)
            return false;
        if (t->width * t->height > kMaxTileTexels)
            return false;
        if (!Gles2AtlasDetail::IsPowerOfTwo(t->width) || !Gles2AtlasDetail::IsPowerOfTwo(t->height))
            return false;
        if (t->levels[0][0].empty())
            return false;
        return true;
    }

    // Fast O(1) lookup for the batcher. Returns NULL if `tex` is not in the atlas.
    const Gles2AtlasTile *Find(const GfxTexture *tex) const
    {
        const int32_t idx = FindSlotIndex(tex);
        return idx >= 0 ? &slots[(size_t)idx].tile : NULL;
    }

    Slot *FindSlot(const GfxTexture *tex)
    {
        const int32_t idx = FindSlotIndex(tex);
        return idx >= 0 ? &slots[(size_t)idx] : NULL;
    }

    // Adds `tex` (by its width/height) to the atlas, or returns its existing slot.
    // Returns NULL if the page has no room or the dimensions are invalid.
    Slot *Add(GfxTexture *tex, bool *addedNew = NULL)
    {
        if (addedNew != NULL)
            *addedNew = false;
        if (tex == NULL)
            return NULL;
        const int32_t existing = FindSlotIndex(tex);
        if (existing >= 0)
            return &slots[(size_t)existing];

        const int w = tex->width;
        const int h = tex->height;
        if (w <= 0 || h <= 0 || w > kMaxTileSize || h > kMaxTileSize || w * h > kMaxTileTexels ||
            !Gles2AtlasDetail::IsPowerOfTwo(w) || !Gles2AtlasDetail::IsPowerOfTwo(h))
            return NULL;

        const int paddedW = w + 2 * gutterPx;
        const int paddedH = h + 2 * gutterPx;
        if (paddedW > pageW || paddedH > pageH)
            return NULL;

        // Keep hash table load factor bounded (at least one empty entry) before
        // reusing a freed slot or allocating a new one.
        if (liveCount + 1u >= kHashSize)
            return NULL;

        // 1. Reuse an exact-size freed slot if available.
        for (size_t i = 0; i < slots.size(); i++) {
            Slot &s = slots[i];
            if (!s.inUse && s.w == w && s.h == h) {
                s.source = tex;
                s.tile.page = pageTex;
                s.inUse = true;
                s.dirty = true;
                liveCount++;
                anyDirty = true;
                RebuildHash();
                if (addedNew != NULL)
                    *addedNew = true;
                return &s;
            }
        }

        // 2. Fit into the tightest existing shelf that has room.
        int bestShelf = -1;
        int bestWaste = pageH + 1;
        for (size_t i = 0; i < shelves.size(); i++) {
            const Shelf &sh = shelves[i];
            if (sh.height >= paddedH && sh.nextX + paddedW <= pageW) {
                const int waste = sh.height - paddedH;
                if (waste < bestWaste) {
                    bestWaste = waste;
                    bestShelf = (int)i;
                    if (waste == 0)
                        break;
                }
            }
        }

        int placeX = 0;
        int placeY = 0;
        if (bestShelf >= 0) {
            Shelf &sh = shelves[(size_t)bestShelf];
            placeX = sh.nextX;
            placeY = sh.y;
            sh.nextX += paddedW;
        } else {
            // 3. Open a new shelf.
            if (usedY + paddedH > pageH)
                return NULL;
            Shelf sh;
            sh.y = usedY;
            sh.height = paddedH;
            sh.nextX = paddedW;
            shelves.push_back(sh);
            placeX = 0;
            placeY = usedY;
            usedY += paddedH;
        }

        Slot s;
        s.source = tex;
        s.x = placeX;
        s.y = placeY;
        s.w = w;
        s.h = h;
        s.paddedW = paddedW;
        s.paddedH = paddedH;
        s.tile.page = pageTex;
        s.tile.u0 = (float)(placeX + gutterPx) / (float)pageW;
        s.tile.v0 = (float)(placeY + gutterPx) / (float)pageH;
        s.tile.uScale = (float)w / (float)pageW;
        s.tile.vScale = (float)h / (float)pageH;
        s.inUse = true;
        s.dirty = true;
        slots.push_back(s);
        liveCount++;
        anyDirty = true;
        RebuildHash();
        if (addedNew != NULL)
            *addedNew = true;
        return &slots.back();
    }

    bool Remove(const GfxTexture *tex)
    {
        const int32_t idx = FindSlotIndex(tex);
        if (idx < 0)
            return false;
        Slot &s = slots[(size_t)idx];
        s.source = NULL;
        s.inUse = false;
        s.dirty = false;
        liveCount--;
        if (liveCount == 0) {
            Reset();
        } else {
            RebuildHash();
        }
        return true;
    }

    void MarkDirty(const GfxTexture *tex)
    {
        const int32_t idx = FindSlotIndex(tex);
        if (idx >= 0) {
            slots[(size_t)idx].dirty = true;
            anyDirty = true;
        }
    }

    // Copies a w*h RGBA8 image into a (w + 2*gutter) * (h + 2*gutter) buffer,
    // replicating border texels into the gutter so bilinear sampling at [0, 1]
    // matches GL_CLAMP_TO_EDGE.
    static void FillPaddedRgba(const uint8_t *srcRgba, int w, int h, int gutter, std::vector<uint8_t> &dst)
    {
        if (w <= 0 || h <= 0 || gutter < 0 || srcRgba == NULL) {
            Gles2AtlasDetail::ClearVector(dst);
            return;
        }
        const int dstW = w + 2 * gutter;
        const int dstH = h + 2 * gutter;
        const size_t rowBytes = (size_t)dstW * 4u;
        dst.resize(rowBytes * (size_t)dstH);
        uint8_t *out = dst.data();

        for (int y = 0; y < h; y++) {
            const uint8_t *srcRow = srcRgba + (size_t)y * (size_t)w * 4u;
            uint8_t *dstRow = out + (size_t)(y + gutter) * rowBytes;
            for (int g = 0; g < gutter; g++)
                memcpy(dstRow + (size_t)g * 4u, srcRow, 4u);
            memcpy(dstRow + (size_t)gutter * 4u, srcRow, (size_t)w * 4u);
            const uint8_t *rightPx = srcRow + (size_t)(w - 1) * 4u;
            for (int g = 0; g < gutter; g++)
                memcpy(dstRow + (size_t)(gutter + w + g) * 4u, rightPx, 4u);
        }

        const uint8_t *firstRow = out + (size_t)gutter * rowBytes;
        for (int g = 0; g < gutter; g++)
            memcpy(out + (size_t)g * rowBytes, firstRow, rowBytes);

        const uint8_t *lastRow = out + (size_t)(gutter + h - 1) * rowBytes;
        for (int g = 0; g < gutter; g++)
            memcpy(out + (size_t)(gutter + h + g) * rowBytes, lastRow, rowBytes);
    }

private:
    int pageW;
    int pageH;
    int gutterPx;
    GfxTexture *pageTex;
    int usedY;
    size_t liveCount;
    bool anyDirty;
    std::vector<Slot> slots;
    std::vector<Shelf> shelves;
    int32_t hashTable[kHashSize];

    void ClearHash()
    {
        for (size_t i = 0; i < kHashSize; i++)
            hashTable[i] = -1;
    }

    void RebuildHash()
    {
        ClearHash();
        const size_t mask = kHashSize - 1u;
        for (size_t i = 0; i < slots.size(); i++) {
            if (!slots[i].inUse)
                continue;
            size_t pos = Gles2AtlasDetail::HashPtr(slots[i].source) & mask;
            while (hashTable[pos] >= 0)
                pos = (pos + 1u) & mask;
            hashTable[pos] = (int32_t)i;
        }
    }

    int32_t FindSlotIndex(const GfxTexture *tex) const
    {
        if (tex == NULL || liveCount == 0)
            return -1;
        const size_t mask = kHashSize - 1u;
        size_t pos = Gles2AtlasDetail::HashPtr(tex) & mask;
        for (size_t probe = 0; probe < kHashSize; probe++) {
            const int32_t idx = hashTable[pos];
            if (idx < 0)
                return -1;
            if (slots[(size_t)idx].source == tex)
                return idx;
            pos = (pos + 1u) & mask;
        }
        return -1;
    }
};

#endif
