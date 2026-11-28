/*
 * Draw batching for the GLES2 executor: merges consecutive 2D/TL draws,
 * opaque 3D draws sharing state/textures within each opaque run (including
 * CPU world-matrix baking for small world-transformed objects), and
 * consecutive non-opaque 3D draws into single indexed GL_TRIANGLES calls.
 *
 * Pure, GL-free helper tested on the host (tests/gles2_batch.cpp).
 *
 * Batching modes ([video] gles2_batch):
 *   0: disabled (pass through unchanged)
 *   1: 2D/TL consecutive + 3D opaque bucket merging (sorted for state/world
 *      locality) + CPU world-baking for small 3D draws + 3D consecutive (default)
 *   2: consecutive only (2D/TL + 3D, zero reordering)
 */
#ifndef OPENCMR2_RENDER_GLES2_GLES2_BATCH_H
#define OPENCMR2_RENDER_GLES2_GLES2_BATCH_H

#include "render/gfx_internal.h"
#include "render/gles2/gles2_atlas.h"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <vector>

struct Gles2BatchInput {
    // 0: dynamic stream buffer (frame.vertices); non-zero: static GL buffer
    // (1 for the shared arena, or distinct per-VB ID).
    uint32_t bufferId;
    // First vertex index of this draw in bufferId.
    uint32_t baseVertex;
};

struct Gles2BatchedCmd {
    GfxCommand cmd;
    uint32_t bufferId;
    uint32_t baseVertex;
    uint16_t minVertex;
    uint16_t maxVertex;
    bool hasRange;
};

struct Gles2BatchStats {
    uint32_t bakedDraws = 0;        // raw 3D draws merged via CPU world-baking
    uint32_t bakedGroups = 0;       // emitted world-baked draws
    uint32_t bakedVerts = 0;        // vertices written to outBakedVerts
    uint32_t atlasRemappedDraws = 0; // raw 3D draws merged via the texture atlas
};

namespace Gles2BatchDetail {

static const uint32_t kBakeMaxVerts = 64u;
static const uint32_t kBakeMaxTotalVerts = 16384u;
static const uint32_t kBakedWorldKey = ~1u;
static const uint32_t kBakedBufKey = ~1u;
static const size_t kMaxAlphaBuckets = 32u;

struct ScreenRect {
    float minX, maxX, minY, maxY;
};

// Columns 0 (X), 1 (Y) and 3 (W) of an MVP matrix: column 2 (Z) is unused by
// screen-space NDC projection.
struct MvpXYW {
    float x1, x2, x3, x4;
    float y1, y2, y3, y4;
    float w1, w2, w3, w4;
};

inline bool RectsOverlap(const ScreenRect &a, const ScreenRect &b)
{
    return !(a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY || b.maxY < a.minY);
}

inline void UnionRect(ScreenRect &dst, const ScreenRect &src)
{
    if (src.minX < dst.minX) dst.minX = src.minX;
    if (src.maxX > dst.maxX) dst.maxX = src.maxX;
    if (src.minY < dst.minY) dst.minY = src.minY;
    if (src.maxY > dst.maxY) dst.maxY = src.maxY;
}

inline void MulMat4(const GfxMatrix &a, const GfxMatrix &b, GfxMatrix &out)
{
    const float *pa = &a._11, *pb = &b._11;
    float *po = &out._11;
    for (int r = 0; r < 4; r++) {
        const float a0 = pa[r * 4 + 0], a1 = pa[r * 4 + 1], a2 = pa[r * 4 + 2], a3 = pa[r * 4 + 3];
        po[r * 4 + 0] = a0 * pb[0] + a1 * pb[4] + a2 * pb[8] + a3 * pb[12];
        po[r * 4 + 1] = a0 * pb[1] + a1 * pb[5] + a2 * pb[9] + a3 * pb[13];
        po[r * 4 + 2] = a0 * pb[2] + a1 * pb[6] + a2 * pb[10] + a3 * pb[14];
        po[r * 4 + 3] = a0 * pb[3] + a1 * pb[7] + a2 * pb[11] + a3 * pb[15];
    }
}

inline void MvpFromVP(const GfxMatrix &vp, MvpXYW &out)
{
    out.x1 = vp._11; out.x2 = vp._21; out.x3 = vp._31; out.x4 = vp._41;
    out.y1 = vp._12; out.y2 = vp._22; out.y3 = vp._32; out.y4 = vp._42;
    out.w1 = vp._14; out.w2 = vp._24; out.w3 = vp._34; out.w4 = vp._44;
}

// Affine world * VP for columns X, Y, W only (36 muls instead of 64).
inline void MulAffineVP(const GfxMatrix &w, const GfxMatrix &vp, MvpXYW &out)
{
    const float vp11 = vp._11, vp21 = vp._21, vp31 = vp._31, vp41 = vp._41;
    const float vp12 = vp._12, vp22 = vp._22, vp32 = vp._32, vp42 = vp._42;
    const float vp14 = vp._14, vp24 = vp._24, vp34 = vp._34, vp44 = vp._44;

    out.x1 = w._11 * vp11 + w._12 * vp21 + w._13 * vp31;
    out.x2 = w._21 * vp11 + w._22 * vp21 + w._23 * vp31;
    out.x3 = w._31 * vp11 + w._32 * vp21 + w._33 * vp31;
    out.x4 = w._41 * vp11 + w._42 * vp21 + w._43 * vp31 + vp41;

    out.y1 = w._11 * vp12 + w._12 * vp22 + w._13 * vp32;
    out.y2 = w._21 * vp12 + w._22 * vp22 + w._23 * vp32;
    out.y3 = w._31 * vp12 + w._32 * vp22 + w._33 * vp32;
    out.y4 = w._41 * vp12 + w._42 * vp22 + w._43 * vp32 + vp42;

    out.w1 = w._11 * vp14 + w._12 * vp24 + w._13 * vp34;
    out.w2 = w._21 * vp14 + w._22 * vp24 + w._23 * vp34;
    out.w3 = w._31 * vp14 + w._32 * vp24 + w._33 * vp34;
    out.w4 = w._41 * vp14 + w._42 * vp24 + w._43 * vp34 + vp44;
}

// Conservative screen-space NDC bounding box of `src[0..count-1]` under `mvp`.
// Any corner with clip.w <= 1e-4f (crossing or behind the near plane) falls
// back to a full-screen sentinel rect so painter's order is never violated.
inline ScreenRect ProjectBoundsNDC(const GfxVertex *src, uint32_t count, const MvpXYW &mvp)
{
    static const ScreenRect kFull = { -1e9f, 1e9f, -1e9f, 1e9f };
    if (count == 0)
        return kFull;
    ScreenRect r = { 1e9f, -1e9f, 1e9f, -1e9f };
    if (count <= 8u) {
        for (uint32_t i = 0; i < count; i++) {
            const float x = src[i].x, y = src[i].y, z = src[i].z;
            const float cw = x * mvp.w1 + y * mvp.w2 + z * mvp.w3 + mvp.w4;
            if (cw <= 1e-4f)
                return kFull;
            const float invW = 1.0f / cw;
            const float nx = (x * mvp.x1 + y * mvp.x2 + z * mvp.x3 + mvp.x4) * invW;
            const float ny = (x * mvp.y1 + y * mvp.y2 + z * mvp.y3 + mvp.y4) * invW;
            if (nx < r.minX) r.minX = nx;
            if (nx > r.maxX) r.maxX = nx;
            if (ny < r.minY) r.minY = ny;
            if (ny > r.maxY) r.maxY = ny;
        }
        return r;
    }
    float lx0 = src[0].x, lx1 = lx0;
    float ly0 = src[0].y, ly1 = ly0;
    float lz0 = src[0].z, lz1 = lz0;
    for (uint32_t i = 1; i < count; i++) {
        const float x = src[i].x, y = src[i].y, z = src[i].z;
        if (x < lx0) lx0 = x; else if (x > lx1) lx1 = x;
        if (y < ly0) ly0 = y; else if (y > ly1) ly1 = y;
        if (z < lz0) lz0 = z; else if (z > lz1) lz1 = z;
    }
    // Incremental AABB corner projection: 18 muls instead of 72, plus a single
    // minimum-W check before any division.
    const float dx = lx1 - lx0, dy = ly1 - ly0, dz = lz1 - lz0;
    const float bw = lx0 * mvp.w1 + ly0 * mvp.w2 + lz0 * mvp.w3 + mvp.w4;
    const float dxw = dx * mvp.w1, dyw = dy * mvp.w2, dzw = dz * mvp.w3;
    const float minW = bw + (dxw < 0.0f ? dxw : 0.0f) + (dyw < 0.0f ? dyw : 0.0f) + (dzw < 0.0f ? dzw : 0.0f);
    if (minW <= 1e-4f)
        return kFull;
    const float bx = lx0 * mvp.x1 + ly0 * mvp.x2 + lz0 * mvp.x3 + mvp.x4;
    const float by = lx0 * mvp.y1 + ly0 * mvp.y2 + lz0 * mvp.y3 + mvp.y4;
    const float dxx = dx * mvp.x1, dyx = dy * mvp.x2, dzx = dz * mvp.x3;
    const float dxy = dx * mvp.y1, dyy = dy * mvp.y2, dzy = dz * mvp.y3;

    const float cx[8] = {
        bx, bx + dzx, bx + dyx, bx + dyx + dzx,
        bx + dxx, bx + dxx + dzx, bx + dxx + dyx, bx + dxx + dyx + dzx
    };
    const float cy[8] = {
        by, by + dzy, by + dyy, by + dyy + dzy,
        by + dxy, by + dxy + dzy, by + dxy + dyy, by + dxy + dyy + dzy
    };
    const float cw[8] = {
        bw, bw + dzw, bw + dyw, bw + dyw + dzw,
        bw + dxw, bw + dxw + dzw, bw + dxw + dyw, bw + dxw + dyw + dzw
    };
    for (int i = 0; i < 8; i++) {
        const float invW = 1.0f / cw[i];
        const float nx = cx[i] * invW;
        const float ny = cy[i] * invW;
        if (nx < r.minX) r.minX = nx;
        if (nx > r.maxX) r.maxX = nx;
        if (ny < r.minY) r.minY = ny;
        if (ny > r.maxY) r.maxY = ny;
    }
    return r;
}

inline bool IsCommutativeBlend(const GfxDrawState &s)
{
    if (!s.alphaBlend || s.depthWrite)
        return false;
    // Additive (ONE/SRC_ALPHA, ONE) or multiplicative (ZERO, SRCCOLOR).
    if (s.destBlend == GFX_BLEND_ONE && (s.srcBlend == GFX_BLEND_ONE || s.srcBlend == GFX_BLEND_SRCALPHA))
        return true;
    if (s.srcBlend == GFX_BLEND_ZERO && s.destBlend == GFX_BLEND_SRCCOLOR)
        return true;
    return false;
}

// Bebbo's GCC 6.5 backend optimiser ("bbb") can fold a load of a vector
// parameter's _M_start into `move.l ([bd,a5],0),4(aN)` while dropping the load
// of `aN`, leaving the vector uncleared and corrupting memory through the stale
// address register (docs/AMIGA_PORT.md, GCC 6.5 gotchas). Pinning the vector
// pointer in an address register prevents that fold.
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

inline bool IsIdentityMatrix(const GfxMatrix &m)
{
    return m._41 == 0.0f && m._42 == 0.0f && m._43 == 0.0f &&
           m._11 == 1.0f && m._22 == 1.0f && m._33 == 1.0f && m._44 == 1.0f &&
           m._12 == 0.0f && m._13 == 0.0f && m._14 == 0.0f &&
           m._21 == 0.0f && m._23 == 0.0f && m._24 == 0.0f &&
           m._31 == 0.0f && m._32 == 0.0f && m._34 == 0.0f;
}

inline bool IsAffineMatrix(const GfxMatrix &m)
{
    return m._14 == 0.0f && m._24 == 0.0f && m._34 == 0.0f && m._44 == 1.0f;
}

inline bool NeedsNormalTransform(const GfxDrawState &s)
{
    return s.lighting || (s.stages[0].texCoordIndex & 0xffff0000u) == 0x30000u ||
           (s.stages[1].texCoordIndex & 0xffff0000u) == 0x30000u;
}

inline const GfxVertex *SourceVertices3D(const GfxFrame &frame, const GfxCommand &c)
{
    if (c.transformed || c.vertexCount == 0)
        return NULL;
    if (c.buffer != NULL) {
        const size_t first = c.firstVertexByte / sizeof(GfxVertex);
        if (first + c.vertexCount > c.buffer->vertices.size())
            return NULL;
        return &c.buffer->vertices[first];
    }
    if ((size_t)c.firstVertexByte + (size_t)c.vertexCount * sizeof(GfxVertex) > frame.vertices.size())
        return NULL;
    return (const GfxVertex *)(frame.vertices.data() + c.firstVertexByte);
}

inline bool CanWorldBake(const GfxFrame &frame, const GfxCommand &c)
{
    if (c.transformed || c.vertexCount == 0 || c.vertexCount > kBakeMaxVerts)
        return false;
    if (c.world >= frame.worlds.size() || !IsAffineMatrix(frame.worlds[c.world]))
        return false;
    return SourceVertices3D(frame, c) != NULL;
}

inline bool UvsInUnitSquare(const GfxVertex *src, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        const float u = src[i].u0;
        const float v = src[i].v0;
        if (!(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f))
            return false;
    }
    return true;
}

inline bool IsAtlasEligibleState(const GfxDrawState &s)
{
    return s.alphaBlend && s.stages[0].colorOp != GFX_TOP_DISABLE && s.stages[1].colorOp == GFX_TOP_DISABLE &&
           s.stages[0].address == GFX_ADDRESS_CLAMP && s.stages[0].mipFilter == GFX_MIPFILTER_NONE &&
           s.stages[0].textureTransform == GFX_TTFF_DISABLE && s.stages[0].texCoordIndex == 0u;
}

inline void AppendBakedVertices(const GfxVertex *src, uint32_t count, const GfxMatrix &m, bool isIdentity,
                                bool transformNormal, std::vector<GfxVertex> &out,
                                const Gles2AtlasTile *atlasTile = NULL)
{
    if (isIdentity && atlasTile == NULL) {
        out.insert(out.end(), src, src + count);
        return;
    }
    const size_t base = out.size();
    const size_t needed = base + count;
    if (needed > out.capacity()) {
        size_t cap = out.capacity() ? out.capacity() * 2u : 512u;
        while (cap < needed)
            cap *= 2u;
        out.reserve(cap);
    }
    out.resize(needed);
    GfxVertex *dst = &out[base];
    if (isIdentity) {
        const float u0 = atlasTile->u0, v0 = atlasTile->v0;
        const float uScale = atlasTile->uScale, vScale = atlasTile->vScale;
        for (uint32_t i = 0; i < count; i++) {
            GfxVertex o = src[i];
            o.u0 = u0 + o.u0 * uScale;
            o.v0 = v0 + o.v0 * vScale;
            dst[i] = o;
        }
        return;
    }
    const float m11 = m._11, m12 = m._12, m13 = m._13;
    const float m21 = m._21, m22 = m._22, m23 = m._23;
    const float m31 = m._31, m32 = m._32, m33 = m._33;
    const float m41 = m._41, m42 = m._42, m43 = m._43;
    if (atlasTile != NULL) {
        const float u0 = atlasTile->u0, v0 = atlasTile->v0;
        const float uScale = atlasTile->uScale, vScale = atlasTile->vScale;
        if (transformNormal) {
            for (uint32_t i = 0; i < count; i++) {
                const GfxVertex &v = src[i];
                GfxVertex &o = dst[i];
                o.x = v.x * m11 + v.y * m21 + v.z * m31 + m41;
                o.y = v.x * m12 + v.y * m22 + v.z * m32 + m42;
                o.z = v.x * m13 + v.y * m23 + v.z * m33 + m43;
                o.nx = v.nx * m11 + v.ny * m21 + v.nz * m31;
                o.ny = v.nx * m12 + v.ny * m22 + v.nz * m32;
                o.nz = v.nx * m13 + v.ny * m23 + v.nz * m33;
                o.diffuse = v.diffuse;
                o.specular = v.specular;
                o.u0 = u0 + v.u0 * uScale;
                o.v0 = v0 + v.v0 * vScale;
                o.u1 = v.u1;
                o.v1 = v.v1;
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                const GfxVertex &v = src[i];
                GfxVertex &o = dst[i];
                o.x = v.x * m11 + v.y * m21 + v.z * m31 + m41;
                o.y = v.x * m12 + v.y * m22 + v.z * m32 + m42;
                o.z = v.x * m13 + v.y * m23 + v.z * m33 + m43;
                o.nx = v.nx;
                o.ny = v.ny;
                o.nz = v.nz;
                o.diffuse = v.diffuse;
                o.specular = v.specular;
                o.u0 = u0 + v.u0 * uScale;
                o.v0 = v0 + v.v0 * vScale;
                o.u1 = v.u1;
                o.v1 = v.v1;
            }
        }
        return;
    }
    if (transformNormal) {
        for (uint32_t i = 0; i < count; i++) {
            const GfxVertex &v = src[i];
            GfxVertex &o = dst[i];
            o.x = v.x * m11 + v.y * m21 + v.z * m31 + m41;
            o.y = v.x * m12 + v.y * m22 + v.z * m32 + m42;
            o.z = v.x * m13 + v.y * m23 + v.z * m33 + m43;
            o.nx = v.nx * m11 + v.ny * m21 + v.nz * m31;
            o.ny = v.nx * m12 + v.ny * m22 + v.nz * m32;
            o.nz = v.nx * m13 + v.ny * m23 + v.nz * m33;
            o.diffuse = v.diffuse;
            o.specular = v.specular;
            o.u0 = v.u0;
            o.v0 = v.v0;
            o.u1 = v.u1;
            o.v1 = v.v1;
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            const GfxVertex &v = src[i];
            GfxVertex &o = dst[i];
            o.x = v.x * m11 + v.y * m21 + v.z * m31 + m41;
            o.y = v.x * m12 + v.y * m22 + v.z * m32 + m42;
            o.z = v.x * m13 + v.y * m23 + v.z * m33 + m43;
            o.nx = v.nx;
            o.ny = v.ny;
            o.nz = v.nz;
            o.diffuse = v.diffuse;
            o.specular = v.specular;
            o.u0 = v.u0;
            o.v0 = v.v0;
            o.u1 = v.u1;
            o.v1 = v.v1;
        }
    }
}

inline uint32_t HashWord(uint32_t h, uint32_t w)
{
    return (h ^ w) * 16777619u;
}

inline uint32_t HashMatrix(const GfxMatrix &m)
{
    uint32_t words[16];
    memcpy(words, &m, sizeof(words));
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++)
        h = (h ^ words[i]) * 16777619u;
    return h;
}

inline bool SameStageNoTexture(const GfxStageState &a, const GfxStageState &b)
{
    return a.colorOp == b.colorOp && a.colorArg1 == b.colorArg1 && a.colorArg2 == b.colorArg2 &&
           a.alphaOp == b.alphaOp && a.alphaArg1 == b.alphaArg1 && a.alphaArg2 == b.alphaArg2 &&
           a.address == b.address && a.minFilter == b.minFilter && a.magFilter == b.magFilter &&
           a.mipFilter == b.mipFilter && a.maxMipLevel == b.maxMipLevel &&
           a.texCoordIndex == b.texCoordIndex && a.textureTransform == b.textureTransform &&
           a.bumpLumScale == b.bumpLumScale && a.bumpLumOffset == b.bumpLumOffset &&
           memcmp(a.bumpMat, b.bumpMat, sizeof(a.bumpMat)) == 0;
}

// Equality of two recorded GfxDrawStates ignoring the fields carried per draw
// (world, stages[0..1].texture) and vertexSerial.
inline bool SameBatchState(const GfxDrawState &a, const GfxDrawState &b)
{
    if (!SameStageNoTexture(a.stages[0], b.stages[0]) || !SameStageNoTexture(a.stages[1], b.stages[1]))
        return false;
    if (a.lighting != b.lighting || a.ambient != b.ambient || a.specular != b.specular ||
        a.localViewer != b.localViewer || a.normalizeNormals != b.normalizeNormals ||
        a.colorVertex != b.colorVertex || a.diffuseSource != b.diffuseSource ||
        a.specularSource != b.specularSource || a.ambientSource != b.ambientSource ||
        a.emissiveSource != b.emissiveSource || a.fog != b.fog || a.fogColor != b.fogColor ||
        a.fogVertexMode != b.fogVertexMode || a.fogTableMode != b.fogTableMode ||
        a.fogStart != b.fogStart || a.fogEnd != b.fogEnd || a.fadeStart != b.fadeStart ||
        a.fadeEnd != b.fadeEnd || a.depthTest != b.depthTest || a.depthWrite != b.depthWrite ||
        a.depthFunc != b.depthFunc || a.cull != b.cull || a.alphaBlend != b.alphaBlend ||
        a.srcBlend != b.srcBlend || a.destBlend != b.destBlend || a.alphaTest != b.alphaTest ||
        a.alphaFunc != b.alphaFunc || a.alphaRef != b.alphaRef || a.textureFactor != b.textureFactor ||
        a.screenW != b.screenW || a.screenH != b.screenH ||
        memcmp(&a.viewport, &b.viewport, sizeof(a.viewport)) != 0 ||
        memcmp(&a.view, &b.view, sizeof(a.view)) != 0 ||
        memcmp(&a.projection, &b.projection, sizeof(a.projection)) != 0 ||
        memcmp(a.texture, b.texture, sizeof(a.texture)) != 0 ||
        memcmp(&a.material, &b.material, sizeof(a.material)) != 0 ||
        memcmp(a.lightEnabled, b.lightEnabled, sizeof(a.lightEnabled)) != 0)
        return false;
    if (a.lighting) {
        for (int i = 0; i < 8; i++)
            if (a.lightEnabled[i] && memcmp(&a.lights[i], &b.lights[i], sizeof(a.lights[i])) != 0)
                return false;
    }
    return true;
}

inline uint32_t HashBatchState(const GfxDrawState &s)
{
    uint32_t h = 2166136261u;
    const uint32_t flags = (uint32_t)(s.lighting ? 1u : 0u) | (uint32_t)(s.specular ? 2u : 0u) |
                           (uint32_t)(s.fog ? 4u : 0u) | (uint32_t)(s.depthTest ? 8u : 0u) |
                           (uint32_t)(s.depthWrite ? 16u : 0u) | (uint32_t)(s.alphaBlend ? 32u : 0u) |
                           (uint32_t)(s.alphaTest ? 64u : 0u);
    h = HashWord(h, flags);
    h = HashWord(h, (uint32_t)s.stages[0].colorOp);
    h = HashWord(h, (uint32_t)s.stages[1].colorOp);
    h = HashWord(h, (uint32_t)s.cull);
    h = HashWord(h, (uint32_t)s.depthFunc);
    h = HashWord(h, (uint32_t)s.srcBlend);
    h = HashWord(h, (uint32_t)s.destBlend);
    h = HashWord(h, (uint32_t)s.alphaRef);
    h = HashWord(h, (uint32_t)s.fogColor);
    h = HashWord(h, (uint32_t)s.textureFactor);
    h = HashWord(h, (uint32_t)s.ambient);
    uint32_t vp[6];
    static_assert(sizeof(s.viewport) == sizeof(vp), "GfxViewport must be 6 words");
    memcpy(vp, &s.viewport, sizeof(vp));
    for (int i = 0; i < 6; i++)
        h = HashWord(h, vp[i]);
    return h;
}

inline bool IsMergeableTriangle(int primitive)
{
    return primitive == GFX_TRIANGLELIST || primitive == GFX_TRIANGLESTRIP;
}

inline bool IsOpaque3D(const GfxCommand &c, const GfxDrawState &s)
{
    return !c.transformed && IsMergeableTriangle(c.primitive) && s.depthTest && s.depthWrite && !s.alphaBlend &&
           (s.depthFunc == GFX_CMP_LESSEQUAL || s.depthFunc == GFX_CMP_LESS);
}

inline void EffectiveTextures(const GfxDrawState &s, const GfxCommand &c, GfxTexture **t0, GfxTexture **t1)
{
    if (s.stages[0].colorOp == GFX_TOP_DISABLE) {
        *t0 = NULL;
        *t1 = NULL;
        return;
    }
    *t0 = Gfx_DrawnTexture(c.textures[0]);
    *t1 = s.stages[1].colorOp == GFX_TOP_DISABLE ? NULL : Gfx_DrawnTexture(c.textures[1]);
}

inline bool CanFitSpan(uint32_t minBase, uint32_t maxEnd, uint32_t base, uint32_t vertexCount)
{
    if (vertexCount == 0 || vertexCount > 65536u)
        return false;
    const uint32_t newMin = base < minBase ? base : minBase;
    const uint32_t end = base + vertexCount;
    if (end < base)
        return false;
    const uint32_t newMax = end > maxEnd ? end : maxEnd;
    return (newMax - newMin) <= 65536u;
}

// Appends triangle-list indices for draw `c` with `delta` added to every local
// vertex index, preserving OpenGL ES 2.0 triangle-strip winding order.
inline void EmitTriangleIndices(const GfxFrame &frame, const GfxCommand &c, uint32_t delta,
                                std::vector<uint16_t> &out)
{
    const bool indexed = c.firstIndex != ~0u;
    const uint16_t *src = indexed ? &frame.indices[c.firstIndex] : NULL;
    const uint32_t count = indexed ? c.indexCount : c.vertexCount;
    if (c.primitive == GFX_TRIANGLELIST) {
        const uint32_t n = count - (count % 3u);
        if (indexed && delta == 0u) {
            out.insert(out.end(), src, src + n);
            return;
        }
        const size_t base = out.size();
        const size_t needed = base + n;
        if (needed > out.capacity()) {
            size_t cap = out.capacity() ? out.capacity() * 2u : 256u;
            while (cap < needed)
                cap *= 2u;
            out.reserve(cap);
        }
        out.resize(needed);
        uint16_t *dst = &out[base];
        if (indexed) {
            for (uint32_t k = 0; k < n; k++)
                dst[k] = (uint16_t)(src[k] + delta);
        } else {
            for (uint32_t k = 0; k < n; k++)
                dst[k] = (uint16_t)(k + delta);
        }
    } else if (c.primitive == GFX_TRIANGLESTRIP && count >= 3u) {
        const uint32_t tris = count - 2u;
        const size_t base = out.size();
        const size_t needed = base + (size_t)tris * 3u;
        if (needed > out.capacity()) {
            size_t cap = out.capacity() ? out.capacity() * 2u : 256u;
            while (cap < needed)
                cap *= 2u;
            out.reserve(cap);
        }
        out.resize(needed);
        uint16_t *dst = &out[base];
        uint16_t *p = dst;
        for (uint32_t k = 0; k < tris; k++) {
            const uint32_t i0 = indexed ? src[k] : k;
            const uint32_t i1 = indexed ? src[k + 1u] : (k + 1u);
            const uint32_t i2 = indexed ? src[k + 2u] : (k + 2u);
            if (i0 == i1 || i1 == i2 || i0 == i2)
                continue;
            if ((k & 1u) == 0u) {
                *p++ = (uint16_t)(i0 + delta);
                *p++ = (uint16_t)(i1 + delta);
                *p++ = (uint16_t)(i2 + delta);
            } else {
                *p++ = (uint16_t)(i1 + delta);
                *p++ = (uint16_t)(i0 + delta);
                *p++ = (uint16_t)(i2 + delta);
            }
        }
        out.resize(base + (size_t)(p - dst));
    }
}

} // namespace Gles2BatchDetail

struct Gles2Batcher {
    struct BucketKey {
        uint32_t state;
        uint32_t world;
        uint32_t bufferId;
        GfxTexture *tex0;
        GfxTexture *tex1;
        bool operator==(const BucketKey &o) const
        {
            return state == o.state && world == o.world && bufferId == o.bufferId && tex0 == o.tex0 && tex1 == o.tex1;
        }
    };

    struct Bucket {
        BucketKey key;
        uint32_t minBase;
        uint32_t maxEnd;
        uint32_t firstNode;
        uint32_t lastNode;
        uint32_t count;
        uint32_t totalVerts;
        uint32_t commonWorld;
        uint32_t commonBufferId;
        bool sameWorldAndBuf;
        bool sameBufSpanOk;
        int32_t nextInHash;
    };

    struct DrawNode {
        uint32_t cmdIndex;
        uint32_t next;
    };

    struct BucketSortKey {
        uint32_t bucketIndex;
        uint32_t bufferId;
        uint32_t state;
        uint32_t world;
        uintptr_t tex0;
        uintptr_t tex1;
    };

    std::vector<uint32_t> canonState;
    std::vector<uint32_t> stateHash;
    std::vector<int32_t> stateTable;
    std::vector<int32_t> stateNext;
    std::vector<uint32_t> canonWorld;
    std::vector<uint32_t> worldHash;
    std::vector<int32_t> worldTable;
    std::vector<int32_t> worldNext;
    int32_t identityWorldIdx = -1;

    struct AlphaBucket {
        uint32_t state;
        GfxTexture *tex0;
        GfxTexture *tex1;
        bool usesAtlas;
        Gles2BatchDetail::ScreenRect rect;
        bool commutative;
        uint32_t firstNode;
        uint32_t lastNode;
        uint32_t count;
        uint32_t totalVerts;
        uint32_t minBase;
        uint32_t maxEnd;
        uint32_t commonWorld;
        uint32_t commonBufferId;
        bool sameWorldAndBuf;
        bool sameBufSpanOk;
    };

    std::vector<Bucket> buckets;
    std::vector<DrawNode> nodes;
    std::vector<BucketSortKey> bucketOrder;
    int32_t bucketHeads[256];

    std::vector<AlphaBucket> alphaBuckets;
    std::vector<DrawNode> alphaNodes;
    uint32_t cachedVPState = ~0u;
    GfxMatrix cachedVP = {};
    Gles2BatchDetail::MvpXYW cachedVPXYW = {};
    uint32_t lastAlphaWorld = ~0u;
    Gles2BatchDetail::MvpXYW lastAlphaMvp = {};

    std::vector<uint32_t> runCmds;

    static uint32_t HashBucket(const BucketKey &k)
    {
        uint32_t h = k.state * 2654435761u ^ k.world * 2246822519u ^ k.bufferId * 3266489917u;
        const uintptr_t p0 = (uintptr_t)k.tex0, p1 = (uintptr_t)k.tex1;
        h ^= (uint32_t)(p0 >> 3) * 668265263u ^ (uint32_t)(p1 >> 3) * 374761393u;
        return (h ^ (h >> 8) ^ (h >> 16)) & 255u;
    }

    static bool LessBucketSort(const BucketSortKey &a, const BucketSortKey &b)
    {
        if (a.state != b.state)
            return a.state < b.state;
        if (a.world != b.world)
            return a.world < b.world;
        if (a.bufferId != b.bufferId)
            return a.bufferId < b.bufferId;
        if (a.tex0 != b.tex0)
            return a.tex0 < b.tex0;
        if (a.tex1 != b.tex1)
            return a.tex1 < b.tex1;
        return a.bucketIndex < b.bucketIndex;
    }

    static void EnsureBatchedIndices(const GfxFrame &frame, std::vector<uint16_t> &outIndices, bool *copied)
    {
        if (*copied)
            return;
        const size_t n = frame.indices.size();
        const size_t cap = n + 256u;
        if (cap > outIndices.capacity())
            outIndices.reserve(cap);
        if (n > 0)
            outIndices.assign(frame.indices.data(), frame.indices.data() + n);
        else
            Gles2BatchDetail::ClearVector(outIndices);
        *copied = true;
    }

    void BuildCanonicalTables(const GfxFrame &frame)
    {
        const size_t ns = frame.states.size();
        canonState.resize(ns);
        stateHash.resize(ns);
        size_t sTableSize = 32;
        while (sTableSize < ns * 2)
            sTableSize *= 2;
        stateTable.assign(sTableSize, -1);
        stateNext.assign(ns, -1);
        const size_t sMask = sTableSize - 1;
        for (size_t i = 0; i < ns; i++) {
            const uint32_t h = Gles2BatchDetail::HashBatchState(frame.states[i]);
            stateHash[i] = h;
            const size_t slot = h & sMask;
            int32_t found = -1;
            for (int32_t j = stateTable[slot]; j >= 0; j = stateNext[(size_t)j]) {
                if (stateHash[(size_t)j] == h &&
                    Gles2BatchDetail::SameBatchState(frame.states[(size_t)j], frame.states[i])) {
                    found = j;
                    break;
                }
            }
            if (found >= 0) {
                canonState[i] = (uint32_t)found;
            } else {
                canonState[i] = (uint32_t)i;
                stateNext[i] = stateTable[slot];
                stateTable[slot] = (int32_t)i;
            }
        }

        const size_t nw = frame.worlds.size();
        canonWorld.resize(nw);
        worldHash.resize(nw);
        size_t tableSize = 64;
        while (tableSize < nw * 2)
            tableSize *= 2;
        worldTable.assign(tableSize, -1);
        worldNext.assign(nw, -1);
        identityWorldIdx = -1;
        const size_t mask = tableSize - 1;
        for (size_t i = 0; i < nw; i++) {
            const GfxMatrix &m = frame.worlds[i];
            if (Gles2BatchDetail::IsIdentityMatrix(m)) {
                if (identityWorldIdx < 0)
                    identityWorldIdx = (int32_t)i;
                canonWorld[i] = (uint32_t)identityWorldIdx;
                continue;
            }
            const uint32_t h = Gles2BatchDetail::HashMatrix(m);
            worldHash[i] = h;
            const size_t slot = h & mask;
            int32_t found = -1;
            for (int32_t j = worldTable[slot]; j >= 0; j = worldNext[(size_t)j]) {
                if (worldHash[(size_t)j] == h && memcmp(&frame.worlds[(size_t)j], &m, sizeof(m)) == 0) {
                    found = j;
                    break;
                }
            }
            if (found >= 0) {
                canonWorld[i] = (uint32_t)found;
            } else {
                canonWorld[i] = (uint32_t)i;
                worldNext[i] = worldTable[slot];
                worldTable[slot] = (int32_t)i;
            }
        }
    }

    uint32_t IdentityWorld() const
    {
        return identityWorldIdx >= 0 ? (uint32_t)identityWorldIdx : ~0u;
    }

    // Emits an unmerged draw without touching outIndices: its firstIndex (if
    // indexed) already addresses the prefix copy of frame.indices.
    static void EmitSingle(const GfxCommand &src, uint32_t state, uint32_t world, const Gles2BatchInput &in,
                           std::vector<Gles2BatchedCmd> &outCommands)
    {
        Gles2BatchedCmd out;
        out.cmd = src;
        out.cmd.state = state;
        out.cmd.world = world;
        out.bufferId = in.bufferId;
        out.baseVertex = in.baseVertex;
        out.minVertex = 0;
        out.maxVertex = 0;
        out.hasRange = false;
        outCommands.push_back(out);
    }

    void EmitBakedSlice(const GfxFrame &frame, const uint32_t *cmdIndices, size_t count, uint32_t state,
                        uint32_t bakedVertexBase, std::vector<Gles2BatchedCmd> &outCommands,
                        std::vector<uint16_t> &outIndices, bool *usedBatchedIndices,
                        std::vector<GfxVertex> &outBakedVerts, Gles2BatchStats *statsOut,
                        const Gles2Atlas *atlas = NULL, GfxTexture *atlasPage = NULL) const
    {
        if (count == 0)
            return;
        const GfxDrawState &s = frame.states[state];
        const bool transformNormal = Gles2BatchDetail::NeedsNormalTransform(s);
        const bool useAtlas = (atlas != NULL && atlasPage != NULL);
        const uint32_t idWorld = IdentityWorld();
        EnsureBatchedIndices(frame, outIndices, usedBatchedIndices);

        size_t pos = 0;
        while (pos < count) {
            const uint32_t firstCi = cmdIndices[pos];
            const uint32_t groupBakedStart = (uint32_t)outBakedVerts.size();
            const uint32_t absBase = bakedVertexBase + groupBakedStart;
            const bool inlineBase = (absBase < 65536u);
            const uint32_t indexOffset = inlineBase ? absBase : 0u;
            const uint32_t startIndex = (uint32_t)outIndices.size();
            uint32_t localVerts = 0;
            uint32_t mergedCount = 0;
            while (pos < count) {
                const uint32_t ci = cmdIndices[pos];
                const GfxCommand &c = frame.commands[ci];
                if (mergedCount > 0 && indexOffset + localVerts + c.vertexCount > 65536u)
                    break;
                if (mergedCount == 0 && inlineBase && absBase + c.vertexCount > 65536u)
                    break;
                const GfxVertex *src = Gles2BatchDetail::SourceVertices3D(frame, c);
                if (src == NULL) {
                    pos++;
                    continue;
                }
                const bool isId = identityWorldIdx >= 0 && canonWorld[c.world] == (uint32_t)identityWorldIdx;
                const Gles2AtlasTile *tile = useAtlas ? atlas->Find(Gfx_DrawnTexture(c.textures[0])) : NULL;
                Gles2BatchDetail::AppendBakedVertices(src, c.vertexCount, frame.worlds[c.world], isId,
                                                      transformNormal, outBakedVerts, tile);
                Gles2BatchDetail::EmitTriangleIndices(frame, c, indexOffset + localVerts, outIndices);
                localVerts += c.vertexCount;
                mergedCount++;
                pos++;
            }
            if (mergedCount == 0 && inlineBase && pos < count) {
                // Single draw straddled 65536: retry with non-zero baseVertex.
                const uint32_t ci = cmdIndices[pos++];
                const GfxCommand &c = frame.commands[ci];
                const GfxVertex *src = Gles2BatchDetail::SourceVertices3D(frame, c);
                if (src != NULL) {
                    const bool isId = identityWorldIdx >= 0 && canonWorld[c.world] == (uint32_t)identityWorldIdx;
                    const Gles2AtlasTile *tile = useAtlas ? atlas->Find(Gfx_DrawnTexture(c.textures[0])) : NULL;
                    Gles2BatchDetail::AppendBakedVertices(src, c.vertexCount, frame.worlds[c.world], isId,
                                                          transformNormal, outBakedVerts, tile);
                    Gles2BatchDetail::EmitTriangleIndices(frame, c, 0u, outIndices);
                    localVerts = c.vertexCount;
                    mergedCount = 1u;
                }
                const uint32_t indexCount = (uint32_t)outIndices.size() - startIndex;
                if (indexCount == 0)
                    continue;
                Gles2BatchedCmd out;
                out.cmd = frame.commands[firstCi];
                if (useAtlas)
                    out.cmd.textures[0] = atlasPage;
                out.cmd.primitive = GFX_TRIANGLELIST;
                out.cmd.state = state;
                out.cmd.world = idWorld;
                out.cmd.buffer = NULL;
                out.cmd.firstIndex = startIndex;
                out.cmd.indexCount = indexCount;
                out.cmd.vertexCount = localVerts;
                out.bufferId = 0u;
                out.baseVertex = absBase;
                out.minVertex = 0;
                out.maxVertex = 0;
                out.hasRange = false;
                outCommands.push_back(out);
                if (statsOut != NULL) {
                    statsOut->bakedDraws += mergedCount;
                    statsOut->bakedGroups++;
                    statsOut->bakedVerts += localVerts;
                    if (useAtlas)
                        statsOut->atlasRemappedDraws += mergedCount;
                }
                continue;
            }
            const uint32_t indexCount = (uint32_t)outIndices.size() - startIndex;
            if (indexCount == 0)
                continue;
            Gles2BatchedCmd out;
            out.cmd = frame.commands[firstCi];
            if (useAtlas)
                out.cmd.textures[0] = atlasPage;
            out.cmd.primitive = GFX_TRIANGLELIST;
            out.cmd.state = state;
            out.cmd.world = idWorld;
            out.cmd.buffer = NULL;
            out.cmd.firstIndex = startIndex;
            out.cmd.indexCount = indexCount;
            out.cmd.vertexCount = localVerts;
            out.bufferId = 0u;
            if (inlineBase) {
                out.baseVertex = 0u;
                out.minVertex = (uint16_t)absBase;
                out.maxVertex = (uint16_t)(absBase + localVerts - 1u);
                out.hasRange = localVerts > 0u;
            } else {
                out.baseVertex = absBase;
                out.minVertex = 0;
                out.maxVertex = 0;
                out.hasRange = false;
            }
            outCommands.push_back(out);
            if (statsOut != NULL) {
                statsOut->bakedDraws += mergedCount;
                statsOut->bakedGroups++;
                statsOut->bakedVerts += localVerts;
                if (useAtlas)
                    statsOut->atlasRemappedDraws += mergedCount;
            }
        }
    }

    void FlushOpaqueBuckets(const GfxFrame &frame, const std::vector<Gles2BatchInput> &inputs, uint32_t bakedVertexBase,
                            std::vector<Gles2BatchedCmd> &outCommands, std::vector<uint16_t> &outIndices,
                            bool *usedBatchedIndices, std::vector<GfxVertex> &outBakedVerts,
                            Gles2BatchStats *statsOut)
    {
        if (buckets.empty())
            return;

        // Sort buckets by (bufferId, state, world, tex0, tex1) so flushing
        // preserves VBO, shader and world-matrix locality instead of thrashing
        // uWorld/VB binds across interleaved objects.
        const size_t nb = buckets.size();
        bucketOrder.resize(nb);
        const uint32_t idWorld = IdentityWorld();
        for (size_t b = 0; b < nb; b++) {
            const Bucket &bk = buckets[b];
            const bool isBakeBucket = bk.key.world == Gles2BatchDetail::kBakedWorldKey;
            const bool willBake = isBakeBucket && bk.count >= 2u && !(bk.sameWorldAndBuf && bk.sameBufSpanOk) &&
                                  (outBakedVerts.size() + bk.totalVerts <= Gles2BatchDetail::kBakeMaxTotalVerts);
            BucketSortKey sk;
            sk.bucketIndex = (uint32_t)b;
            sk.state = bk.key.state;
            if (willBake) {
                sk.bufferId = 0u;
                sk.world = idWorld;
            } else if (bk.count == 1u || bk.sameWorldAndBuf) {
                sk.bufferId = bk.commonBufferId;
                sk.world = bk.commonWorld;
            } else {
                sk.bufferId = bk.key.bufferId;
                sk.world = bk.key.world;
            }
            sk.tex0 = (uintptr_t)bk.key.tex0;
            sk.tex1 = (uintptr_t)bk.key.tex1;
            bucketOrder[b] = sk;
        }
        if (nb <= 16u) {
            for (size_t i = 1; i < nb; i++) {
                const BucketSortKey cur = bucketOrder[i];
                size_t j = i;
                while (j > 0 && LessBucketSort(cur, bucketOrder[j - 1])) {
                    bucketOrder[j] = bucketOrder[j - 1];
                    j--;
                }
                bucketOrder[j] = cur;
            }
        } else {
            std::sort(bucketOrder.begin(), bucketOrder.end(), LessBucketSort);
        }

        for (size_t oi = 0; oi < nb; oi++) {
            const Bucket &bk = buckets[bucketOrder[oi].bucketIndex];
            const uint32_t firstIdx = nodes[bk.firstNode].cmdIndex;
            const GfxCommand &firstCmd = frame.commands[firstIdx];
            if (bk.count == 1u) {
                EmitSingle(firstCmd, bk.key.state, bk.commonWorld, inputs[firstIdx], outCommands);
                continue;
            }
            if (bk.key.world == Gles2BatchDetail::kBakedWorldKey) {
                if (bk.sameWorldAndBuf && bk.sameBufSpanOk) {
                    EnsureBatchedIndices(frame, outIndices, usedBatchedIndices);
                    const bool inlineBase = (bk.maxEnd <= 65536u);
                    const uint32_t baseOff = inlineBase ? 0u : bk.minBase;
                    const uint32_t startIndex = (uint32_t)outIndices.size();
                    for (uint32_t n = bk.firstNode; n != ~0u; n = nodes[n].next) {
                        const uint32_t ci = nodes[n].cmdIndex;
                        const uint32_t delta = inputs[ci].baseVertex - baseOff;
                        Gles2BatchDetail::EmitTriangleIndices(frame, frame.commands[ci], delta, outIndices);
                    }
                    const uint32_t indexCount = (uint32_t)outIndices.size() - startIndex;
                    if (indexCount == 0)
                        continue;
                    Gles2BatchedCmd out;
                    out.cmd = firstCmd;
                    out.cmd.primitive = GFX_TRIANGLELIST;
                    out.cmd.state = bk.key.state;
                    out.cmd.world = bk.commonWorld;
                    out.cmd.firstIndex = startIndex;
                    out.cmd.indexCount = indexCount;
                    out.cmd.vertexCount = bk.maxEnd - bk.minBase;
                    out.bufferId = bk.commonBufferId;
                    out.baseVertex = baseOff;
                    out.minVertex = inlineBase ? (uint16_t)bk.minBase : 0u;
                    out.maxVertex = inlineBase && bk.maxEnd > bk.minBase ? (uint16_t)(bk.maxEnd - 1u) : 0u;
                    out.hasRange = inlineBase && bk.maxEnd > bk.minBase;
                    outCommands.push_back(out);
                    continue;
                }
                if (outBakedVerts.size() + bk.totalVerts <= Gles2BatchDetail::kBakeMaxTotalVerts) {
                    Gles2BatchDetail::ClearVector(runCmds);
                    for (uint32_t n = bk.firstNode; n != ~0u; n = nodes[n].next)
                        runCmds.push_back(nodes[n].cmdIndex);
                    EmitBakedSlice(frame, runCmds.data(), runCmds.size(), bk.key.state, bakedVertexBase, outCommands,
                                   outIndices, usedBatchedIndices, outBakedVerts, statsOut);
                    Gles2BatchDetail::ClearVector(runCmds);
                } else {
                    for (uint32_t n = bk.firstNode; n != ~0u; n = nodes[n].next) {
                        const uint32_t ci = nodes[n].cmdIndex;
                        const GfxCommand &c = frame.commands[ci];
                        const uint32_t cw = c.world < canonWorld.size() ? canonWorld[c.world] : c.world;
                        EmitSingle(c, bk.key.state, cw, inputs[ci], outCommands);
                    }
                }
                continue;
            }
            EnsureBatchedIndices(frame, outIndices, usedBatchedIndices);
            const bool inlineBase = (bk.maxEnd <= 65536u);
            const uint32_t baseOff = inlineBase ? 0u : bk.minBase;
            const uint32_t startIndex = (uint32_t)outIndices.size();
            for (uint32_t n = bk.firstNode; n != ~0u; n = nodes[n].next) {
                const uint32_t ci = nodes[n].cmdIndex;
                const uint32_t delta = inputs[ci].baseVertex - baseOff;
                Gles2BatchDetail::EmitTriangleIndices(frame, frame.commands[ci], delta, outIndices);
            }
            const uint32_t indexCount = (uint32_t)outIndices.size() - startIndex;
            if (indexCount == 0)
                continue;
            Gles2BatchedCmd out;
            out.cmd = firstCmd;
            out.cmd.primitive = GFX_TRIANGLELIST;
            out.cmd.state = bk.key.state;
            out.cmd.world = bk.key.world;
            out.cmd.firstIndex = startIndex;
            out.cmd.indexCount = indexCount;
            out.cmd.vertexCount = bk.maxEnd - bk.minBase;
            out.bufferId = bk.key.bufferId;
            out.baseVertex = baseOff;
            out.minVertex = inlineBase ? (uint16_t)bk.minBase : 0u;
            out.maxVertex = inlineBase && bk.maxEnd > bk.minBase ? (uint16_t)(bk.maxEnd - 1u) : 0u;
            out.hasRange = inlineBase && bk.maxEnd > bk.minBase;
            outCommands.push_back(out);
        }
        Gles2BatchDetail::ClearVector(buckets);
        Gles2BatchDetail::ClearVector(nodes);
        memset(bucketHeads, 0xff, sizeof(bucketHeads));
    }

    // Flushes open alpha-blended buckets in strict creation order so every
    // overlapping screen pixel is drawn in exact painter's order.
    void FlushAlphaBuckets(const GfxFrame &frame, const std::vector<Gles2BatchInput> &inputs, uint32_t bakedVertexBase,
                           std::vector<Gles2BatchedCmd> &outCommands, std::vector<uint16_t> &outIndices,
                           bool *usedBatchedIndices, std::vector<GfxVertex> &outBakedVerts,
                           Gles2BatchStats *statsOut, const Gles2Atlas *atlas)
    {
        if (alphaBuckets.empty())
            return;
        for (size_t b = 0; b < alphaBuckets.size(); b++) {
            const AlphaBucket &ab = alphaBuckets[b];
            const uint32_t firstIdx = alphaNodes[ab.firstNode].cmdIndex;
            const GfxCommand &firstCmd = frame.commands[firstIdx];
            if (ab.count == 1u) {
                EmitSingle(firstCmd, ab.state, ab.commonWorld, inputs[firstIdx], outCommands);
                continue;
            }
            if (!ab.usesAtlas && ab.sameWorldAndBuf && ab.sameBufSpanOk) {
                EnsureBatchedIndices(frame, outIndices, usedBatchedIndices);
                const bool inlineBase = (ab.maxEnd <= 65536u);
                const uint32_t baseOff = inlineBase ? 0u : ab.minBase;
                const uint32_t startIndex = (uint32_t)outIndices.size();
                for (uint32_t n = ab.firstNode; n != ~0u; n = alphaNodes[n].next) {
                    const uint32_t ci = alphaNodes[n].cmdIndex;
                    const uint32_t delta = inputs[ci].baseVertex - baseOff;
                    Gles2BatchDetail::EmitTriangleIndices(frame, frame.commands[ci], delta, outIndices);
                }
                const uint32_t indexCount = (uint32_t)outIndices.size() - startIndex;
                if (indexCount == 0)
                    continue;
                Gles2BatchedCmd out;
                out.cmd = firstCmd;
                out.cmd.primitive = GFX_TRIANGLELIST;
                out.cmd.state = ab.state;
                out.cmd.world = ab.commonWorld;
                out.cmd.firstIndex = startIndex;
                out.cmd.indexCount = indexCount;
                out.cmd.vertexCount = ab.maxEnd - ab.minBase;
                out.bufferId = ab.commonBufferId;
                out.baseVertex = baseOff;
                out.minVertex = inlineBase ? (uint16_t)ab.minBase : 0u;
                out.maxVertex = inlineBase && ab.maxEnd > ab.minBase ? (uint16_t)(ab.maxEnd - 1u) : 0u;
                out.hasRange = inlineBase && ab.maxEnd > ab.minBase;
                outCommands.push_back(out);
                continue;
            }
            if (outBakedVerts.size() + ab.totalVerts <= Gles2BatchDetail::kBakeMaxTotalVerts) {
                Gles2BatchDetail::ClearVector(runCmds);
                for (uint32_t n = ab.firstNode; n != ~0u; n = alphaNodes[n].next)
                    runCmds.push_back(alphaNodes[n].cmdIndex);
                EmitBakedSlice(frame, runCmds.data(), runCmds.size(), ab.state, bakedVertexBase, outCommands,
                               outIndices, usedBatchedIndices, outBakedVerts, statsOut,
                               ab.usesAtlas ? atlas : NULL, ab.usesAtlas ? ab.tex0 : NULL);
                Gles2BatchDetail::ClearVector(runCmds);
            } else {
                for (uint32_t n = ab.firstNode; n != ~0u; n = alphaNodes[n].next) {
                    const uint32_t ci = alphaNodes[n].cmdIndex;
                    const GfxCommand &c = frame.commands[ci];
                    const uint32_t cw = c.world < canonWorld.size() ? canonWorld[c.world] : c.world;
                    EmitSingle(c, ab.state, cw, inputs[ci], outCommands);
                }
            }
        }
        Gles2BatchDetail::ClearVector(alphaBuckets);
        Gles2BatchDetail::ClearVector(alphaNodes);
    }

    void Run(const GfxFrame &frame, const std::vector<Gles2BatchInput> &inputs, int mode,
             std::vector<Gles2BatchedCmd> &outCommands, std::vector<uint16_t> &outIndices, bool *usedBatchedIndices,
             std::vector<GfxVertex> *outBakedVerts = NULL, uint32_t bakedVertexBase = 0,
             Gles2BatchStats *statsOut = NULL, const Gles2Atlas *atlas = NULL)
    {
        Gles2BatchDetail::ClearVector(outCommands);
        Gles2BatchDetail::ClearVector(outIndices);
        if (outBakedVerts != NULL)
            Gles2BatchDetail::ClearVector(*outBakedVerts);
        if (statsOut != NULL)
            *statsOut = Gles2BatchStats();
        *usedBatchedIndices = false;
        const size_t nc = frame.commands.size();
        if (nc == 0)
            return;
        if (mode == 0) {
            outCommands.resize(nc);
            for (size_t i = 0; i < nc; i++) {
                outCommands[i].cmd = frame.commands[i];
                outCommands[i].bufferId = inputs[i].bufferId;
                outCommands[i].baseVertex = inputs[i].baseVertex;
                outCommands[i].minVertex = 0;
                outCommands[i].maxVertex = 0;
                outCommands[i].hasRange = false;
            }
            return;
        }

        BuildCanonicalTables(frame);
        outCommands.reserve(nc);
        Gles2BatchDetail::ClearVector(buckets);
        Gles2BatchDetail::ClearVector(nodes);
        memset(bucketHeads, 0xff, sizeof(bucketHeads));
        Gles2BatchDetail::ClearVector(alphaBuckets);
        Gles2BatchDetail::ClearVector(alphaNodes);
        cachedVPState = ~0u;
        lastAlphaWorld = ~0u;
        Gles2BatchDetail::ClearVector(runCmds);

        const bool allowBake = (mode == 1 && outBakedVerts != NULL);
        bool runActive = false;
        bool runTransformed = false;
        bool runBakeMode = false;
        bool runSameWorldAndBuf = true;
        bool runSameBufSpanOk = true;
        uint32_t runCommonWorld = 0, runCommonBuf = 0;
        uint32_t runTotalVerts = 0;
        BucketKey runKey = { 0, 0, 0, NULL, NULL };
        uint32_t runMinBase = 0, runMaxEnd = 0;

        auto flushRun = [&]() {
            if (!runActive)
                return;
            if (runCmds.size() == 1u) {
                const uint32_t ci = runCmds[0];
                EmitSingle(frame.commands[ci], runKey.state, runCommonWorld, inputs[ci], outCommands);
            } else if (runBakeMode && !(runSameWorldAndBuf && runSameBufSpanOk)) {
                if (outBakedVerts->size() + runTotalVerts <= Gles2BatchDetail::kBakeMaxTotalVerts) {
                    EmitBakedSlice(frame, runCmds.data(), runCmds.size(), runKey.state, bakedVertexBase, outCommands,
                                   outIndices, usedBatchedIndices, *outBakedVerts, statsOut);
                } else {
                    for (size_t k = 0; k < runCmds.size(); k++) {
                        const uint32_t ci = runCmds[k];
                        const GfxCommand &c = frame.commands[ci];
                        const uint32_t cw = c.world < canonWorld.size() ? canonWorld[c.world] : c.world;
                        EmitSingle(c, runKey.state, cw, inputs[ci], outCommands);
                    }
                }
            } else {
                const uint32_t firstCi = runCmds[0];
                EnsureBatchedIndices(frame, outIndices, usedBatchedIndices);
                const bool inlineBase = (runMaxEnd <= 65536u);
                const uint32_t baseOff = inlineBase ? 0u : runMinBase;
                const uint32_t startIndex = (uint32_t)outIndices.size();
                for (size_t k = 0; k < runCmds.size(); k++) {
                    const uint32_t ci = runCmds[k];
                    const uint32_t delta = inputs[ci].baseVertex - baseOff;
                    Gles2BatchDetail::EmitTriangleIndices(frame, frame.commands[ci], delta, outIndices);
                }
                const uint32_t indexCount = (uint32_t)outIndices.size() - startIndex;
                if (indexCount > 0) {
                    Gles2BatchedCmd out;
                    out.cmd = frame.commands[firstCi];
                    out.cmd.primitive = GFX_TRIANGLELIST;
                    out.cmd.state = runKey.state;
                    out.cmd.world = runCommonWorld;
                    out.cmd.firstIndex = startIndex;
                    out.cmd.indexCount = indexCount;
                    out.cmd.vertexCount = runMaxEnd - runMinBase;
                    out.bufferId = runCommonBuf;
                    out.baseVertex = baseOff;
                    out.minVertex = inlineBase ? (uint16_t)runMinBase : 0u;
                    out.maxVertex = inlineBase && runMaxEnd > runMinBase ? (uint16_t)(runMaxEnd - 1u) : 0u;
                    out.hasRange = inlineBase && runMaxEnd > runMinBase;
                    outCommands.push_back(out);
                }
            }
            Gles2BatchDetail::ClearVector(runCmds);
            runActive = false;
        };

        std::vector<GfxVertex> dummyBaked;
        std::vector<GfxVertex> &bakedRef = outBakedVerts != NULL ? *outBakedVerts : dummyBaked;

        for (size_t i = 0; i < nc; i++) {
            const GfxCommand &c = frame.commands[i];
            if (c.type != GFX_CMD_DRAW) {
                flushRun();
                FlushOpaqueBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices,
                                   bakedRef, statsOut);
                FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices,
                                  bakedRef, statsOut, atlas);
                Gles2BatchedCmd out;
                out.cmd = c;
                out.bufferId = 0;
                out.baseVertex = 0;
                out.minVertex = 0;
                out.maxVertex = 0;
                out.hasRange = false;
                outCommands.push_back(out);
                continue;
            }
            const GfxDrawState &s = frame.states[c.state];
            const uint32_t cs = canonState[c.state];
            const uint32_t cw = c.transformed ? 0u : (c.world < canonWorld.size() ? canonWorld[c.world] : c.world);
            GfxTexture *t0 = NULL, *t1 = NULL;
            Gles2BatchDetail::EffectiveTextures(s, c, &t0, &t1);
            const Gles2BatchInput &in = inputs[i];

            if (mode == 1 && Gles2BatchDetail::IsOpaque3D(c, s)) {
                flushRun();
                FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices,
                                  bakedRef, statsOut, atlas);
                const bool bakeable = allowBake && Gles2BatchDetail::CanWorldBake(frame, c);
                const BucketKey key = { cs, bakeable ? Gles2BatchDetail::kBakedWorldKey : cw,
                                        bakeable ? Gles2BatchDetail::kBakedBufKey : in.bufferId, t0, t1 };
                const uint32_t slot = HashBucket(key);
                int32_t found = -1;
                for (int32_t bi = bucketHeads[slot]; bi >= 0; bi = buckets[(size_t)bi].nextInHash) {
                    const Bucket &bk = buckets[(size_t)bi];
                    if (bk.key == key &&
                        (bakeable || Gles2BatchDetail::CanFitSpan(bk.minBase, bk.maxEnd, in.baseVertex, c.vertexCount))) {
                        found = bi;
                        break;
                    }
                }
                const uint32_t nodeIdx = (uint32_t)nodes.size();
                DrawNode node = { (uint32_t)i, ~0u };
                nodes.push_back(node);
                if (found >= 0) {
                    Bucket &bk = buckets[(size_t)found];
                    if (bk.sameWorldAndBuf) {
                        if (cw != bk.commonWorld || in.bufferId != bk.commonBufferId) {
                            bk.sameWorldAndBuf = false;
                            bk.sameBufSpanOk = false;
                        } else if (bk.sameBufSpanOk) {
                            if (Gles2BatchDetail::CanFitSpan(bk.minBase, bk.maxEnd, in.baseVertex, c.vertexCount)) {
                                if (in.baseVertex < bk.minBase)
                                    bk.minBase = in.baseVertex;
                                if (in.baseVertex + c.vertexCount > bk.maxEnd)
                                    bk.maxEnd = in.baseVertex + c.vertexCount;
                            } else {
                                bk.sameBufSpanOk = false;
                            }
                        }
                    }
                    bk.totalVerts += c.vertexCount;
                    nodes[bk.lastNode].next = nodeIdx;
                    bk.lastNode = nodeIdx;
                    bk.count++;
                } else {
                    Bucket bk;
                    bk.key = key;
                    bk.minBase = in.baseVertex;
                    bk.maxEnd = in.baseVertex + c.vertexCount;
                    bk.firstNode = nodeIdx;
                    bk.lastNode = nodeIdx;
                    bk.count = 1u;
                    bk.totalVerts = c.vertexCount;
                    bk.commonWorld = cw;
                    bk.commonBufferId = in.bufferId;
                    bk.sameWorldAndBuf = true;
                    bk.sameBufSpanOk = true;
                    bk.nextInHash = bucketHeads[slot];
                    bucketHeads[slot] = (int32_t)buckets.size();
                    buckets.push_back(bk);
                }
                continue;
            }

            FlushOpaqueBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices, bakedRef,
                               statsOut);

            if (!Gles2BatchDetail::IsMergeableTriangle(c.primitive) || c.vertexCount == 0 || c.vertexCount > 65536u) {
                flushRun();
                FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices,
                                  bakedRef, statsOut, atlas);
                EmitSingle(c, cs, cw, in, outCommands);
                continue;
            }

            // Order-preserving non-overlapping batching for alpha-blended 3D
            // draws (e.g. deferred roadside trees and bushes): projects each
            // draw's 3D AABB to screen-space NDC and merges into an open bucket
            // of the same (state, tex0, tex1) only when it does not overlap any
            // later open bucket, preserving exact painter's order per pixel.
            if (allowBake && !c.transformed && s.alphaBlend && c.vertexCount <= Gles2BatchDetail::kBakeMaxVerts &&
                c.world < frame.worlds.size() && Gles2BatchDetail::IsAffineMatrix(frame.worlds[c.world])) {
                const GfxVertex *srcV = Gles2BatchDetail::SourceVertices3D(frame, c);
                if (srcV != NULL) {
                    flushRun();
                    if (!alphaBuckets.empty()) {
                        const GfxDrawState &prevS = frame.states[alphaBuckets[0].state];
                        if (prevS.screenW != s.screenW || prevS.screenH != s.screenH ||
                            memcmp(&prevS.viewport, &s.viewport, sizeof(s.viewport)) != 0)
                            FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices,
                                              usedBatchedIndices, bakedRef, statsOut, atlas);
                    }
                    if (cachedVPState != cs) {
                        Gles2BatchDetail::MulMat4(s.view, s.projection, cachedVP);
                        Gles2BatchDetail::MvpFromVP(cachedVP, cachedVPXYW);
                        cachedVPState = cs;
                        lastAlphaWorld = ~0u;
                    }
                    const bool isId = identityWorldIdx >= 0 && cw == (uint32_t)identityWorldIdx;
                    const Gles2BatchDetail::MvpXYW *mvpPtr;
                    if (isId) {
                        mvpPtr = &cachedVPXYW;
                    } else {
                        if (lastAlphaWorld != cw) {
                            Gles2BatchDetail::MulAffineVP(frame.worlds[c.world], cachedVP, lastAlphaMvp);
                            lastAlphaWorld = cw;
                        }
                        mvpPtr = &lastAlphaMvp;
                    }
                    const Gles2BatchDetail::ScreenRect rect =
                        Gles2BatchDetail::ProjectBoundsNDC(srcV, c.vertexCount, *mvpPtr);
                    const bool comm = Gles2BatchDetail::IsCommutativeBlend(s);

                    GfxTexture *keyTex0 = t0;
                    bool usesAtlas = false;
                    if (atlas != NULL && t0 != NULL && t1 == NULL && Gles2BatchDetail::IsAtlasEligibleState(s) &&
                        Gles2BatchDetail::UvsInUnitSquare(srcV, c.vertexCount)) {
                        const Gles2AtlasTile *tile = atlas->Find(t0);
                        if (tile != NULL && tile->page != NULL) {
                            keyTex0 = tile->page;
                            usesAtlas = true;
                        }
                    }

                    int32_t targetBucket = -1;
                    for (int32_t bi = (int32_t)alphaBuckets.size() - 1; bi >= 0; bi--) {
                        const AlphaBucket &ab = alphaBuckets[(size_t)bi];
                        if (ab.state == cs && ab.tex0 == keyTex0 && ab.tex1 == t1 && ab.usesAtlas == usesAtlas) {
                            targetBucket = bi;
                            break;
                        }
                        if (!(comm && ab.commutative && ab.state == cs) &&
                            Gles2BatchDetail::RectsOverlap(rect, ab.rect)) {
                            break;
                        }
                    }
                    const uint32_t nodeIdx = (uint32_t)alphaNodes.size();
                    DrawNode node = { (uint32_t)i, ~0u };
                    alphaNodes.push_back(node);
                    if (targetBucket >= 0) {
                        AlphaBucket &ab = alphaBuckets[(size_t)targetBucket];
                        Gles2BatchDetail::UnionRect(ab.rect, rect);
                        if (ab.sameWorldAndBuf) {
                            if (cw != ab.commonWorld || in.bufferId != ab.commonBufferId) {
                                ab.sameWorldAndBuf = false;
                                ab.sameBufSpanOk = false;
                            } else if (ab.sameBufSpanOk) {
                                if (Gles2BatchDetail::CanFitSpan(ab.minBase, ab.maxEnd, in.baseVertex, c.vertexCount)) {
                                    if (in.baseVertex < ab.minBase)
                                        ab.minBase = in.baseVertex;
                                    if (in.baseVertex + c.vertexCount > ab.maxEnd)
                                        ab.maxEnd = in.baseVertex + c.vertexCount;
                                } else {
                                    ab.sameBufSpanOk = false;
                                }
                            }
                        }
                        ab.totalVerts += c.vertexCount;
                        alphaNodes[ab.lastNode].next = nodeIdx;
                        ab.lastNode = nodeIdx;
                        ab.count++;
                    } else {
                        if (alphaBuckets.size() >= Gles2BatchDetail::kMaxAlphaBuckets) {
                            alphaNodes.pop_back();
                            FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices,
                                              usedBatchedIndices, bakedRef, statsOut, atlas);
                            alphaNodes.push_back(node);
                        }
                        const uint32_t freshIdx = (uint32_t)alphaNodes.size() - 1u;
                        AlphaBucket ab;
                        ab.state = cs;
                        ab.tex0 = keyTex0;
                        ab.tex1 = t1;
                        ab.usesAtlas = usesAtlas;
                        ab.rect = rect;
                        ab.commutative = comm;
                        ab.firstNode = freshIdx;
                        ab.lastNode = freshIdx;
                        ab.count = 1u;
                        ab.totalVerts = c.vertexCount;
                        ab.minBase = in.baseVertex;
                        ab.maxEnd = in.baseVertex + c.vertexCount;
                        ab.commonWorld = cw;
                        ab.commonBufferId = in.bufferId;
                        ab.sameWorldAndBuf = !usesAtlas;
                        ab.sameBufSpanOk = !usesAtlas;
                        alphaBuckets.push_back(ab);
                    }
                    continue;
                }
            }

            FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices, bakedRef,
                              statsOut, atlas);

            const bool bakeable = allowBake && !c.transformed && Gles2BatchDetail::CanWorldBake(frame, c);
            const BucketKey key = { cs, bakeable ? Gles2BatchDetail::kBakedWorldKey : cw,
                                    bakeable ? Gles2BatchDetail::kBakedBufKey : in.bufferId, t0, t1 };
            const bool canJoin =
                runActive && runTransformed == c.transformed && runBakeMode == bakeable && runKey == key &&
                (bakeable || Gles2BatchDetail::CanFitSpan(runMinBase, runMaxEnd, in.baseVertex, c.vertexCount));
            if (canJoin) {
                if (runSameWorldAndBuf) {
                    if (cw != runCommonWorld || in.bufferId != runCommonBuf) {
                        runSameWorldAndBuf = false;
                        runSameBufSpanOk = false;
                    } else if (runSameBufSpanOk) {
                        if (Gles2BatchDetail::CanFitSpan(runMinBase, runMaxEnd, in.baseVertex, c.vertexCount)) {
                            if (in.baseVertex < runMinBase)
                                runMinBase = in.baseVertex;
                            if (in.baseVertex + c.vertexCount > runMaxEnd)
                                runMaxEnd = in.baseVertex + c.vertexCount;
                        } else {
                            runSameBufSpanOk = false;
                        }
                    }
                }
                runTotalVerts += c.vertexCount;
                runCmds.push_back((uint32_t)i);
            } else {
                flushRun();
                runActive = true;
                runTransformed = c.transformed;
                runBakeMode = bakeable;
                runSameWorldAndBuf = true;
                runSameBufSpanOk = true;
                runCommonWorld = cw;
                runCommonBuf = in.bufferId;
                runTotalVerts = c.vertexCount;
                runKey = key;
                runMinBase = in.baseVertex;
                runMaxEnd = in.baseVertex + c.vertexCount;
                runCmds.push_back((uint32_t)i);
            }
        }
        flushRun();
        FlushOpaqueBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices, bakedRef,
                           statsOut);
        FlushAlphaBuckets(frame, inputs, bakedVertexBase, outCommands, outIndices, usedBatchedIndices, bakedRef,
                          statsOut, atlas);
    }
};

#endif
