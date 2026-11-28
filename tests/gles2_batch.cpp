// Draw batching for the GLES2 executor (render/gles2/gles2_batch.h).
#include "render/gles2/gles2_batch.h"

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

GfxMatrix Identity()
{
    GfxMatrix m;
    memset(&m, 0, sizeof(m));
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    return m;
}

GfxMatrix Translate(float x)
{
    GfxMatrix m = Identity();
    m._41 = x;
    return m;
}

GfxDrawState BaseState()
{
    GfxDrawState s;
    s.world = s.view = s.projection = Identity();
    s.texture[0] = s.texture[1] = Identity();
    memset(s.lights, 0, sizeof(s.lights));
    memset(&s.material, 0, sizeof(s.material));
    s.stages[1].colorOp = GFX_TOP_DISABLE;
    s.stages[1].alphaOp = GFX_TOP_DISABLE;
    s.viewport = { 0, 0, 640, 480, 0.0f, 1.0f };
    return s;
}

GfxCommand MakeDraw(int primitive, bool transformed, uint32_t state, uint32_t world, GfxTexture *t0, GfxTexture *t1,
                    uint32_t vertexCount, uint32_t firstIndex, uint32_t indexCount)
{
    GfxCommand c = {};
    c.type = GFX_CMD_DRAW;
    c.primitive = primitive;
    c.transformed = transformed;
    c.state = state;
    c.world = world;
    c.textures[0] = t0;
    c.textures[1] = t1;
    c.vertexCount = vertexCount;
    c.firstIndex = firstIndex;
    c.indexCount = indexCount;
    return c;
}

GfxCommand MakeClear()
{
    GfxCommand c = {};
    c.type = GFX_CMD_CLEAR;
    c.clearFlags = GFX_CLEAR_ZBUFFER;
    c.clearZ = 1.0f;
    return c;
}

GfxCommand MakeTarget(GfxTexture *target)
{
    GfxCommand c = {};
    c.type = GFX_CMD_TARGET;
    c.target = target;
    return c;
}

} // namespace

int main()
{
    GfxTexture texA = {}, texB = {}, texStale = {};
    Gles2Batcher batcher;
    std::vector<Gles2BatchedCmd> outCmds;
    std::vector<uint16_t> outIdx;
    bool usedBatched = false;

    // 1. 2D/TL consecutive triangle-strip quads merge into one indexed
    //    triangle list with exact OpenGL strip winding, ignoring world matrix
    //    and disabled stage 1 textures.
    {
        GfxFrame frame;
        GfxDrawState tl = BaseState();
        tl.depthTest = false;
        tl.depthWrite = false;
        frame.states.push_back(tl);
        frame.worlds.push_back(Identity());
        frame.worlds.push_back(Translate(10.0f));

        std::vector<Gles2BatchInput> in;
        // Three consecutive 4-vertex TL strip quads with texA (different worlds and stale stage 1 tex).
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 0, &texA, &texStale, 4, ~0u, 0));
        in.push_back({ 0, 0 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 1, &texA, NULL, 4, ~0u, 0));
        in.push_back({ 0, 4 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 0, &texA, &texB, 4, ~0u, 0));
        in.push_back({ 0, 8 });
        // Followed by texB -> separate draw.
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 0, &texB, NULL, 4, ~0u, 0));
        in.push_back({ 0, 12 });

        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched);
        Check(usedBatched, "TL batch uses batched indices");
        Check(outCmds.size() == 2, "3 consecutive TL quads + 1 different texture -> 2 draws");
        Check(outCmds[0].cmd.primitive == GFX_TRIANGLELIST && outCmds[0].cmd.indexCount == 18 &&
                  outCmds[0].baseVertex == 0,
              "merged TL quad has 18 indices from baseVertex 0");
        // Verify strip winding: quad 0 = (0,1,2), (2,1,3); quad 1 = (4,5,6), (6,5,7); quad 2 = (8,9,10), (10,9,11).
        const uint16_t expected[18] = { 0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7, 8, 9, 10, 10, 9, 11 };
        Check(outIdx.size() == 18 && memcmp(outIdx.data(), expected, sizeof(expected)) == 0,
              "strip-to-list winding order matches OpenGL ES 2.0");
        Check(outCmds[1].cmd.primitive == GFX_TRIANGLESTRIP && outCmds[1].cmd.firstIndex == ~0u &&
                  outCmds[1].baseVertex == 12,
              "unmerged standalone TL quad stays non-indexed");

        // Mode 0 passes through untouched.
        batcher.Run(frame, in, 0, outCmds, outIdx, &usedBatched);
        Check(!usedBatched && outCmds.size() == 4, "mode 0 disables batching");
    }

    // 2. 2D/TL draws are NEVER reordered across a different texture (painter's order).
    {
        GfxFrame frame;
        GfxDrawState tl = BaseState();
        tl.depthTest = false;
        frame.states.push_back(tl);
        frame.worlds.push_back(Identity());
        std::vector<Gles2BatchInput> in;
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 0, &texA, NULL, 4, ~0u, 0));
        in.push_back({ 0, 0 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 0, &texB, NULL, 4, ~0u, 0));
        in.push_back({ 0, 4 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLESTRIP, true, 0, 0, &texA, NULL, 4, ~0u, 0));
        in.push_back({ 0, 8 });
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched);
        Check(outCmds.size() == 3, "interleaved TL draws preserve painter's order");
    }

    // 3. 3D opaque bucket merging collapses interleaved stage sectors (same
    //    canonical state + canonical Identity world + same arena buffer) while
    //    respecting barriers (alphaBlend, CLEAR, TARGET).
    {
        GfxFrame frame;
        GfxDrawState opaqueA = BaseState();
        opaqueA.lighting = false;
        opaqueA.vertexSerial = 1;
        GfxDrawState opaqueLit = BaseState();
        opaqueLit.lighting = true;
        opaqueLit.vertexSerial = 2;
        GfxDrawState opaqueA2 = opaqueA;
        opaqueA2.vertexSerial = 3;   // different serial, identical batch state to opaqueA
        GfxDrawState blended = opaqueA;
        blended.alphaBlend = true;
        blended.vertexSerial = 4;

        frame.states = { opaqueA, opaqueLit, opaqueA2, blended };
        frame.worlds = { Identity(), Translate(5.0f), Identity() };
        frame.indices = { 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2 };

        std::vector<Gles2BatchInput> in;
        // Sector 0: texA (state 0, world 0), texB (state 0, world 0)
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 0, 3));
        in.push_back({ 1, 100 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texB, NULL, 3, 3, 3));
        in.push_back({ 1, 110 });
        // Roadside lit object in between (state 1, world 1)
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 1, 1, &texA, NULL, 3, 6, 3));
        in.push_back({ 1, 200 });
        // Sector 1: texA (state 2 == opaqueA, world 2 == Identity), texB
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 2, 2, &texA, NULL, 3, 9, 3));
        in.push_back({ 1, 50 });   // smaller baseVertex than sector 0: minBase becomes 50!
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 2, 2, &texB, NULL, 3, 12, 3));
        in.push_back({ 1, 130 });
        // Translucent decal barrier (state 3: alphaBlend)
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 3, 0, &texA, NULL, 3, 15, 3));
        in.push_back({ 1, 300 });
        // Opaque draw after barrier must NOT merge across the translucent draw
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 0, 3));
        in.push_back({ 1, 310 });

        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched);
        // Expected:
        //   0: merged texA (sector 0 @100 + sector 1 @50) -> minBase 50, indices: 50,51,52, 0,1,2
        //   1: merged texB (sector 0 @110 + sector 1 @130) -> minBase 110, indices: 0,1,2, 20,21,22
        //   2: lit object @200
        //   3: blended barrier @300
        //   4: post-barrier opaque texA @310
        Check(outCmds.size() == 5, "opaque run collapses 5 interleaved draws into 3 + barrier + post-barrier");
        if (outCmds.size() == 5) {
            Check(outCmds[0].baseVertex == 0 && outCmds[0].hasRange && outCmds[0].minVertex == 50 &&
                      outCmds[0].maxVertex == 102 && outCmds[0].cmd.indexCount == 6 && outCmds[0].cmd.state == 0 &&
                      outCmds[0].cmd.world == 0,
                  "merged texA inlines baseVertex <= 65536 with DrawRangeElements bounds and canonical state/world");
            const uint16_t expA[6] = { 100, 101, 102, 50, 51, 52 };
            Check(memcmp(&outIdx[outCmds[0].cmd.firstIndex], expA, sizeof(expA)) == 0,
                  "merged texA indices use direct vertex offsets");
            Check(outCmds[1].baseVertex == 0 && outCmds[1].hasRange && outCmds[1].minVertex == 110 &&
                      outCmds[1].maxVertex == 132 && outCmds[1].cmd.indexCount == 6,
                  "merged texB inlines baseVertex with DrawRangeElements bounds");
            const uint16_t expB[6] = { 110, 111, 112, 130, 131, 132 };
            Check(memcmp(&outIdx[outCmds[1].cmd.firstIndex], expB, sizeof(expB)) == 0,
                  "merged texB indices use direct vertex offsets");
            Check(outCmds[2].baseVertex == 200 && outCmds[3].baseVertex == 300 && outCmds[4].baseVertex == 310,
                  "lit object, translucent barrier, and post-barrier draw stay separated");
        }

        // Mode 2 (consecutive only) must NOT reorder interleaved draws.
        batcher.Run(frame, in, 2, outCmds, outIdx, &usedBatched);
        Check(outCmds.size() == 7, "mode 2 does not reorder interleaved opaque draws");
    }

    // 4. 65536-vertex span limit splits batches cleanly so uint16 indices never wrap.
    {
        GfxFrame frame;
        GfxDrawState op = BaseState();
        frame.states.push_back(op);
        frame.worlds.push_back(Identity());
        frame.indices = { 0, 1, 2, 0, 1, 2 };
        std::vector<Gles2BatchInput> in;
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 10, 0, 3));
        in.push_back({ 1, 0 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 10, 3, 3));
        in.push_back({ 1, 70000 });
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched);
        Check(outCmds.size() == 2, "draws spanning >65536 vertices stay separate");
    }

    // 5. CLEAR and TARGET act as barriers.
    {
        GfxFrame frame;
        GfxDrawState op = BaseState();
        frame.states.push_back(op);
        frame.worlds.push_back(Identity());
        frame.indices = { 0, 1, 2, 0, 1, 2 };
        std::vector<Gles2BatchInput> in;
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 0, 3));
        in.push_back({ 1, 0 });
        frame.commands.push_back(MakeClear());
        in.push_back({ 0, 0 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 3, 3));
        in.push_back({ 1, 3 });
        frame.commands.push_back(MakeTarget(&texB));
        in.push_back({ 0, 0 });
        frame.commands.push_back(MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 0, 3));
        in.push_back({ 1, 6 });
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched);
        Check(outCmds.size() == 5, "CLEAR and TARGET split opaque batches");
        Check(!usedBatched, "no merges -> batched index buffer stays untouched");
    }

    // 6. CPU world-baking merges small opaque 3D draws with distinct world
    //    matrices and buffers into a single draw with Identity world,
    //    preserving exact positions and (when lit) normals.
    {
        GfxFrame frame;
        GfxDrawState litOp = BaseState();
        litOp.lighting = true;
        frame.states.push_back(litOp);
        frame.worlds.push_back(Identity());
        GfxMatrix rotTranslate = Identity();
        // 90 deg around Z: (x,y,z) -> (-y, x, z), plus translate (10, 20, 30)
        rotTranslate._11 = 0.0f;
        rotTranslate._12 = 1.0f;
        rotTranslate._21 = -1.0f;
        rotTranslate._22 = 0.0f;
        rotTranslate._41 = 10.0f;
        rotTranslate._42 = 20.0f;
        rotTranslate._43 = 30.0f;
        frame.worlds.push_back(rotTranslate);
        frame.worlds.push_back(Translate(-5.0f));

        GfxVertexBuffer vb;
        vb.vertices.resize(6);
        memset(vb.vertices.data(), 0, sizeof(GfxVertex) * 6);
        vb.vertices[0].x = 1.0f; vb.vertices[0].y = 2.0f; vb.vertices[0].z = 3.0f;
        vb.vertices[0].nx = 1.0f; vb.vertices[0].ny = 0.0f; vb.vertices[0].nz = 0.0f;
        vb.vertices[1].x = 2.0f; vb.vertices[2].y = 1.0f;
        vb.vertices[3].x = 4.0f; vb.vertices[3].y = 5.0f; vb.vertices[3].z = 6.0f;
        vb.vertices[3].nx = 0.0f; vb.vertices[3].ny = 1.0f; vb.vertices[3].nz = 0.0f;
        vb.vertices[4].x = 5.0f; vb.vertices[5].y = 6.0f;
        frame.indices = { 0, 1, 2, 0, 1, 2 };

        GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &texA, NULL, 3, 0, 3);
        c0.buffer = &vb;
        c0.firstVertexByte = 0;
        GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 2, &texA, NULL, 3, 3, 3);
        c1.buffer = &vb;
        c1.firstVertexByte = 3 * sizeof(GfxVertex);
        frame.commands.push_back(c0);
        frame.commands.push_back(c1);
        std::vector<Gles2BatchInput> in = { { 1, 0 }, { 2, 3 } };

        std::vector<GfxVertex> bakedVerts;
        Gles2BatchStats bstats;
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 100u, &bstats);
        Check(outCmds.size() == 1, "2 small opaque 3D draws with different worlds/VB bake into 1 draw");
        Check(bstats.bakedDraws == 2 && bstats.bakedGroups == 1 && bstats.bakedVerts == 6,
              "bake stats record 2 draws -> 1 group / 6 vertices");
        Check(bakedVerts.size() == 6, "baked vertex buffer has 6 vertices");
        if (outCmds.size() == 1 && bakedVerts.size() == 6) {
            Check(outCmds[0].bufferId == 0 && outCmds[0].baseVertex == 0u && outCmds[0].hasRange &&
                      outCmds[0].minVertex == 100u && outCmds[0].maxVertex == 105u && outCmds[0].cmd.world == 0,
                  "baked draw uses dynamic buffer 0, inline range 100..105, and canonical Identity world");
            // v0=(1,2,3) * rotTranslate -> (-2+10, 1+20, 3+30) = (8, 21, 33); n=(1,0,0) -> (0,1,0)
            Check(bakedVerts[0].x == 8.0f && bakedVerts[0].y == 21.0f && bakedVerts[0].z == 33.0f,
                  "baked vertex 0 position matches row-vector affine world transform");
            Check(bakedVerts[0].nx == 0.0f && bakedVerts[0].ny == 1.0f && bakedVerts[0].nz == 0.0f,
                  "baked vertex 0 normal matches upper 3x3 world transform");
            // v3=(4,5,6) * Translate(-5) -> (-1, 5, 6); n=(0,1,0) -> (0,1,0)
            Check(bakedVerts[3].x == -1.0f && bakedVerts[3].y == 5.0f && bakedVerts[3].z == 6.0f,
                  "baked vertex 3 position matches translation world transform");
            const uint16_t expIdx[6] = { 100, 101, 102, 103, 104, 105 };
            Check(memcmp(&outIdx[outCmds[0].cmd.firstIndex], expIdx, sizeof(expIdx)) == 0,
                  "baked draw indices include bakedVertexBase cleanly");
        }
    }

    // 7. Consecutive alpha-blended small 3D draws (e.g. deferred roadside trees)
    //    world-bake in strict far-to-near order and NEVER reorder across another texture.
    {
        GfxFrame frame;
        GfxDrawState blended = BaseState();
        blended.alphaBlend = true;
        blended.lighting = false;
        frame.states.push_back(blended);
        frame.worlds = { Translate(1.0f), Translate(2.0f), Translate(3.0f), Translate(4.0f) };
        GfxVertexBuffer vb;
        vb.vertices.resize(3);
        memset(vb.vertices.data(), 0, sizeof(GfxVertex) * 3);
        vb.vertices[0].x = 1.0f;
        frame.indices = { 0, 1, 2 };

        auto mk = [&](uint32_t w, GfxTexture *t) {
            GfxCommand c = MakeDraw(GFX_TRIANGLELIST, false, 0, w, t, NULL, 3, 0, 3);
            c.buffer = &vb;
            return c;
        };
        // Two consecutive texA trees, then texB, then texA -> must produce 3 draws (order preserved).
        frame.commands = { mk(0, &texA), mk(1, &texA), mk(2, &texB), mk(3, &texA) };
        std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } };
        std::vector<GfxVertex> bakedVerts;
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, NULL);
        Check(outCmds.size() == 3, "blended consecutive trees merge without reordering across texB");
        if (outCmds.size() == 3 && bakedVerts.size() == 6) {
            Check(outCmds[0].cmd.world == ~0u, "sentinel ~0u used when frame.worlds has no Identity");
            Check(bakedVerts[0].x == 2.0f && bakedVerts[3].x == 3.0f,
                  "blended trees stay in exact far-to-near emission order");
            Check(outCmds[1].cmd.textures[0] == &texB && outCmds[2].cmd.textures[0] == &texA &&
                      outCmds[2].cmd.world == 3,
                  "standalone blended trees are not baked or reordered");
        }
    }

    // 8. Opaque bucket flush sorts by (state, world, bufferId) to maximize
    //    program and uniform stream cache reuse before switching VBOs.
    {
        GfxFrame frame;
        GfxDrawState s0 = BaseState();
        frame.states.push_back(s0);
        frame.worlds = { Translate(1.0f), Translate(2.0f) };
        frame.indices = { 0, 1, 2 };
        // Interleave (world 1, buf 2), (world 0, buf 2), (world 1, buf 1), (world 0, buf 1).
        frame.commands = {
            MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &texA, NULL, 100, 0, 3),
            MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 100, 0, 3),
            MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &texB, NULL, 100, 0, 3),
            MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texB, NULL, 100, 0, 3),
        };
        std::vector<Gles2BatchInput> in = { { 2, 0 }, { 2, 100 }, { 1, 0 }, { 1, 100 } };
        std::vector<GfxVertex> bakedVerts;
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, NULL);
        Check(outCmds.size() == 4, "4 distinct large opaque draws stay 4 draws");
        if (outCmds.size() == 4) {
            Check(outCmds[0].cmd.world == 0 && outCmds[0].bufferId == 1 &&
                      outCmds[1].cmd.world == 0 && outCmds[1].bufferId == 2 &&
                      outCmds[2].cmd.world == 1 && outCmds[2].bufferId == 1 &&
                      outCmds[3].cmd.world == 1 && outCmds[3].bufferId == 2,
                  "opaque buckets flush grouped by (state, world, bufferId)");
        }
    }

    // 9. Exact screen-space overlap DAG batching for alpha-blended 3D draws:
    //    non-overlapping draws separated by another texture merge into one
    //    world-baked draw, while overlapping draws strictly preserve painter's order.
    {
        GfxFrame frame;
        GfxDrawState blended = BaseState();
        blended.alphaBlend = true;
        blended.lighting = false;
        frame.states.push_back(blended);
        // With view = projection = Identity, world X/Y land directly in NDC.
        // Tree L0 (texA) at x=-0.8..-0.6, Bush R0 (texB) at x=+0.5..+0.7,
        // Tree L1 (texA) at x=-0.7..-0.5 (does NOT overlap R0 -> merges with L0!),
        // Tree R1 (texA) at x=+0.55..+0.75 (OVERLAPS R0 -> must stay AFTER R0!).
        frame.worlds = { Translate(-0.8f), Translate(0.5f), Translate(-0.7f), Translate(0.55f) };
        GfxVertexBuffer vb;
        vb.vertices.resize(3);
        memset(vb.vertices.data(), 0, sizeof(GfxVertex) * 3);
        vb.vertices[0].x = 0.0f; vb.vertices[0].y = 0.0f;
        vb.vertices[1].x = 0.2f; vb.vertices[1].y = 0.0f;
        vb.vertices[2].x = 0.1f; vb.vertices[2].y = 0.2f;
        frame.indices = { 0, 1, 2 };

        auto mk = [&](uint32_t w, GfxTexture *t) {
            GfxCommand c = MakeDraw(GFX_TRIANGLELIST, false, 0, w, t, NULL, 3, 0, 3);
            c.buffer = &vb;
            return c;
        };
        frame.commands = { mk(0, &texA), mk(1, &texB), mk(2, &texA), mk(3, &texA) };
        std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 }, { 1, 0 }, { 1, 0 } };
        std::vector<GfxVertex> bakedVerts;
        Gles2BatchStats bstats;
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, &bstats);
        // Expected order: [L0+L1 (baked texA), R0 (texB), R1 (texA)] -> 3 draws!
        Check(outCmds.size() == 3, "non-overlapping alpha trees merge across texB while overlapping tree stays after");
        if (outCmds.size() == 3) {
            Check(outCmds[0].cmd.textures[0] == &texA && outCmds[0].cmd.indexCount == 6 &&
                      outCmds[1].cmd.textures[0] == &texB && outCmds[2].cmd.textures[0] == &texA &&
                      outCmds[2].cmd.world == 3,
                  "alpha DAG preserves exact painter's order on overlapping draw R1");
            Check(bstats.bakedDraws == 2 && bstats.bakedGroups == 1, "L0+L1 world-baked into 1 group");
        }
    }

    // 10. Alpha-blended 3D draws above kBakeMaxVerts (>64 verts) act as an
    //     in-order barrier and are never CPU-baked or scanned for AABB overlap.
    {
        GfxFrame frame;
        GfxDrawState blended = BaseState();
        blended.alphaBlend = true;
        blended.lighting = false;
        frame.states.push_back(blended);
        frame.worlds = { Translate(-0.8f), Translate(0.5f), Translate(-0.7f) };
        GfxVertexBuffer vbSmall, vbLarge;
        vbSmall.vertices.resize(3);
        memset(vbSmall.vertices.data(), 0, sizeof(GfxVertex) * 3);
        vbSmall.vertices[0].x = 0.0f; vbSmall.vertices[0].y = 0.0f;
        vbSmall.vertices[1].x = 0.2f; vbSmall.vertices[1].y = 0.0f;
        vbSmall.vertices[2].x = 0.1f; vbSmall.vertices[2].y = 0.2f;

        vbLarge.vertices.resize(80);
        memset(vbLarge.vertices.data(), 0, sizeof(GfxVertex) * 80);
        frame.indices = { 0, 1, 2 };

        GfxCommand l0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 0, 3);
        l0.buffer = &vbSmall;
        GfxCommand large = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &texA, NULL, 80, 0, 3);
        large.buffer = &vbLarge;
        GfxCommand l1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 2, &texA, NULL, 3, 0, 3);
        l1.buffer = &vbSmall;

        frame.commands = { l0, large, l1 };
        std::vector<Gles2BatchInput> in = { { 1, 0 }, { 2, 0 }, { 1, 0 } };
        std::vector<GfxVertex> bakedVerts;
        Gles2BatchStats bstats;
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, &bstats);
        Check(outCmds.size() == 3 && bstats.bakedDraws == 0,
              "alpha draw > 64 verts flushes open alpha buckets cleanly without baking");
    }

    // 11. Virtual screen change (screenW/screenH) flushes open alpha buckets
    //     even when viewport is identical, since NDC maps to different pixels.
    {
        GfxFrame frame;
        GfxDrawState sA = BaseState();
        sA.alphaBlend = true;
        sA.lighting = false;
        sA.screenW = 640;
        sA.screenH = 480;
        GfxDrawState sB = sA;
        sB.screenW = 1024;
        sB.screenH = 768;
        frame.states = { sA, sB };
        frame.worlds = { Translate(-0.8f), Translate(0.5f), Translate(-0.7f) };
        GfxVertexBuffer vb;
        vb.vertices.resize(3);
        memset(vb.vertices.data(), 0, sizeof(GfxVertex) * 3);
        vb.vertices[0].x = 0.0f; vb.vertices[0].y = 0.0f;
        vb.vertices[1].x = 0.2f; vb.vertices[1].y = 0.0f;
        vb.vertices[2].x = 0.1f; vb.vertices[2].y = 0.2f;
        frame.indices = { 0, 1, 2 };

        GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &texA, NULL, 3, 0, 3);
        c0.buffer = &vb;
        GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 1, 1, &texB, NULL, 3, 0, 3);
        c1.buffer = &vb;
        GfxCommand c2 = MakeDraw(GFX_TRIANGLELIST, false, 0, 2, &texA, NULL, 3, 0, 3);
        c2.buffer = &vb;
        frame.commands = { c0, c1, c2 };
        std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 }, { 1, 0 } };
        std::vector<GfxVertex> bakedVerts;
        batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, NULL);
        Check(outCmds.size() == 3, "screenW/screenH change flushes open alpha buckets");
    }

    // 12. Texture atlas on bakeable alpha-blended 3D draws:
    //     - two draws with different source textures on the same page merge and
    //       bake with remapped stage-0 UVs (leaving stage-1 UVs untouched);
    //     - an overlapping draw with a different key in between still splits
    //       the merge (painter's order preserved);
    //     - a bucket keyed on the page NEVER takes the index-concatenation
    //       branch even when both draws share the same world and buffer;
    //     - single draws keep their original texture and UVs;
    //     - draws with UVs outside [0, 1] are not remapped;
    //     - opaque draws are untouched by the atlas.
    {
        GfxTexture page = {};
        GfxTexture treeA = {}, treeB = {}, otherTex = {};
        treeA.width = 128; treeA.height = 256; treeA.levels[0][0].resize(16, 1);
        treeB.width = 128; treeB.height = 256; treeB.levels[0][0].resize(16, 2);
        otherTex.width = 256; otherTex.height = 256; otherTex.levels[0][0].resize(16, 3);

        Gles2Atlas atlas(1024, 1024, 1);
        atlas.SetPageTexture(&page);
        Check(atlas.Add(&treeA) != NULL && atlas.Add(&treeB) != NULL, "atlas places treeA and treeB");
        const Gles2AtlasTile tA = *atlas.Find(&treeA);
        const Gles2AtlasTile tB = *atlas.Find(&treeB);

        GfxDrawState treeState = BaseState();
        treeState.alphaBlend = true;
        treeState.lighting = false;
        treeState.stages[0].address = GFX_ADDRESS_CLAMP;
        treeState.stages[0].mipFilter = GFX_MIPFILTER_NONE;

        GfxVertexBuffer vb;
        vb.vertices.resize(6);
        memset(vb.vertices.data(), 0, sizeof(GfxVertex) * 6);
        // Quad/tri 0: UVs in [0, 1], stage-1 UVs = (0.25, 0.75)
        vb.vertices[0].x = 0.0f; vb.vertices[0].y = 0.0f; vb.vertices[0].u0 = 0.0f; vb.vertices[0].v0 = 0.0f;
        vb.vertices[0].u1 = 0.25f; vb.vertices[0].v1 = 0.75f;
        vb.vertices[1].x = 0.2f; vb.vertices[1].y = 0.0f; vb.vertices[1].u0 = 1.0f; vb.vertices[1].v0 = 0.0f;
        vb.vertices[1].u1 = 0.25f; vb.vertices[1].v1 = 0.75f;
        vb.vertices[2].x = 0.1f; vb.vertices[2].y = 0.2f; vb.vertices[2].u0 = 0.5f; vb.vertices[2].v0 = 1.0f;
        vb.vertices[2].u1 = 0.25f; vb.vertices[2].v1 = 0.75f;
        // Quad/tri 1: UVs outside [0, 1] (u0 = 1.5)
        vb.vertices[3] = vb.vertices[0];
        vb.vertices[4] = vb.vertices[1];
        vb.vertices[4].u0 = 1.5f;
        vb.vertices[5] = vb.vertices[2];

        // 12a. Two consecutive tree draws (treeA, treeB) sharing the SAME world and buffer
        //      must STILL bake (never index-concatenate) and remap UVs to page.
        {
            GfxFrame frame;
            frame.states.push_back(treeState);
            frame.worlds = { Identity() };
            frame.indices = { 0, 1, 2 };
            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vb;
            GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeB, NULL, 3, 0, 3);
            c1.buffer = &vb;
            frame.commands = { c0, c1 };
            std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 } };
            std::vector<GfxVertex> bakedVerts;
            Gles2BatchStats bstats;
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, &bstats, &atlas);
            Check(outCmds.size() == 1, "treeA + treeB on same atlas page merge into 1 draw");
            Check(bstats.bakedDraws == 2 && bstats.bakedGroups == 1 && bstats.atlasRemappedDraws == 2,
                  "atlas bucket forces baking even with identical world and buffer");
            if (outCmds.size() == 1 && bakedVerts.size() == 6) {
                Check(outCmds[0].cmd.textures[0] == &page && outCmds[0].bufferId == 0u,
                      "merged atlas draw binds page texture and baked dynamic buffer");
                Check(bakedVerts[0].u0 == tA.u0 && bakedVerts[0].v0 == tA.v0 &&
                          bakedVerts[1].u0 == tA.u0 + tA.uScale && bakedVerts[2].v0 == tA.v0 + tA.vScale,
                      "treeA UVs remapped to slotA");
                Check(bakedVerts[3].u0 == tB.u0 && bakedVerts[3].v0 == tB.v0 &&
                          bakedVerts[4].u0 == tB.u0 + tB.uScale && bakedVerts[5].v0 == tB.v0 + tB.vScale,
                      "treeB UVs remapped to slotB");
                Check(bakedVerts[0].u1 == 0.25f && bakedVerts[0].v1 == 0.75f &&
                          bakedVerts[3].u1 == 0.25f && bakedVerts[3].v1 == 0.75f,
                      "stage-1 UVs left untouched by atlas remapping");
            }
        }

        // 12b. Single atlas-eligible draw keeps its original texture and is not baked.
        {
            GfxFrame frame;
            frame.states.push_back(treeState);
            frame.worlds = { Translate(0.2f) };
            frame.indices = { 0, 1, 2 };
            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vb;
            frame.commands = { c0 };
            std::vector<Gles2BatchInput> in = { { 1, 0 } };
            std::vector<GfxVertex> bakedVerts;
            Gles2BatchStats bstats;
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, &bstats, &atlas);
            Check(outCmds.size() == 1 && outCmds[0].cmd.textures[0] == &treeA && bakedVerts.empty() &&
                      bstats.atlasRemappedDraws == 0,
                  "single atlas-eligible draw keeps original texture and UVs");
        }

        // 12c. Painter's order: overlapping draw with non-atlas texture in between splits the merge.
        {
            GfxFrame frame;
            frame.states.push_back(treeState);
            // All three at the exact same screen region x=0..0.2, y=0..0.2 -> overlapping!
            frame.worlds = { Translate(0.0f), Translate(0.05f), Translate(0.02f) };
            frame.indices = { 0, 1, 2 };
            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vb;
            GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &otherTex, NULL, 3, 0, 3);
            c1.buffer = &vb;
            GfxCommand c2 = MakeDraw(GFX_TRIANGLELIST, false, 0, 2, &treeB, NULL, 3, 0, 3);
            c2.buffer = &vb;
            frame.commands = { c0, c1, c2 };
            std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 }, { 1, 0 } };
            std::vector<GfxVertex> bakedVerts;
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, NULL, &atlas);
            Check(outCmds.size() == 3 && outCmds[0].cmd.textures[0] == &treeA &&
                      outCmds[1].cmd.textures[0] == &otherTex && outCmds[2].cmd.textures[0] == &treeB,
                  "overlapping non-atlas draw between treeA and treeB splits merge to preserve painter's order");
        }

        // 12d. Draw with UVs outside [0, 1] is not remapped into the atlas bucket.
        {
            GfxFrame frame;
            frame.states.push_back(treeState);
            frame.worlds = { Translate(-0.5f), Translate(0.5f) };
            frame.indices = { 0, 1, 2 };
            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vb;
            c0.firstVertexByte = 0;
            GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &treeB, NULL, 3, 0, 3);
            c1.buffer = &vb;
            c1.firstVertexByte = 3 * sizeof(GfxVertex); // u0 = 1.5
            frame.commands = { c0, c1 };
            std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 3 } };
            std::vector<GfxVertex> bakedVerts;
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, NULL, &atlas);
            Check(outCmds.size() == 2 && outCmds[0].cmd.textures[0] == &treeA &&
                      outCmds[1].cmd.textures[0] == &treeB && bakedVerts.empty(),
                  "draw with UV outside [0, 1] stays out of the atlas bucket");
        }

        // 12e. Opaque draws using atlased textures are untouched.
        {
            GfxFrame frame;
            GfxDrawState opaque = treeState;
            opaque.alphaBlend = false;
            frame.states.push_back(opaque);
            frame.worlds = { Translate(-0.5f), Translate(0.5f) };
            frame.indices = { 0, 1, 2 };
            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vb;
            GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &treeB, NULL, 3, 0, 3);
            c1.buffer = &vb;
            frame.commands = { c0, c1 };
            std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 } };
            std::vector<GfxVertex> bakedVerts;
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, NULL, &atlas);
            Check(outCmds.size() == 2 && outCmds[0].cmd.textures[0] != &page &&
                      outCmds[1].cmd.textures[0] != &page,
                  "opaque path is unchanged even for textures present in the atlas");
        }

        // 12f. Non-identity world transforms + lighting (actual FIN01 tree-card hot path)
        //      and non-overlapping interleaving across another texture.
        {
            GfxFrame frame;
            GfxDrawState litTree = treeState;
            litTree.lighting = true;
            frame.states.push_back(litTree);
            // 90 deg rotation around Z + translate (-0.8, 0, 0) for treeA,
            // non-overlapping otherTex at x=+0.5, and translate (-0.5, 0, 0) for treeB.
            GfxMatrix rot = Identity();
            rot._11 = 0.0f; rot._12 = 1.0f;
            rot._21 = -1.0f; rot._22 = 0.0f;
            rot._41 = -0.8f;
            frame.worlds = { rot, Translate(0.5f), Translate(-0.5f) };
            GfxVertexBuffer vbLit;
            vbLit.vertices.resize(3);
            memset(vbLit.vertices.data(), 0, sizeof(GfxVertex) * 3);
            vbLit.vertices[0].x = 0.0f; vbLit.vertices[0].y = 0.0f; vbLit.vertices[0].nx = 1.0f;
            vbLit.vertices[0].u0 = 0.0f; vbLit.vertices[0].v0 = 0.0f; vbLit.vertices[0].u1 = 0.4f; vbLit.vertices[0].v1 = 0.6f;
            vbLit.vertices[1].x = 0.2f; vbLit.vertices[1].y = 0.0f; vbLit.vertices[1].nx = 1.0f;
            vbLit.vertices[1].u0 = 1.0f; vbLit.vertices[1].v0 = 0.5f; vbLit.vertices[1].u1 = 0.4f; vbLit.vertices[1].v1 = 0.6f;
            vbLit.vertices[2].x = 0.1f; vbLit.vertices[2].y = 0.2f; vbLit.vertices[2].nx = 1.0f;
            vbLit.vertices[2].u0 = 0.5f; vbLit.vertices[2].v0 = 1.0f; vbLit.vertices[2].u1 = 0.4f; vbLit.vertices[2].v1 = 0.6f;
            frame.indices = { 0, 1, 2 };

            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vbLit;
            GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &otherTex, NULL, 3, 0, 3);
            c1.buffer = &vbLit;
            GfxCommand c2 = MakeDraw(GFX_TRIANGLELIST, false, 0, 2, &treeB, NULL, 3, 0, 3);
            c2.buffer = &vbLit;
            frame.commands = { c0, c1, c2 };
            std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 }, { 1, 0 } };
            std::vector<GfxVertex> bakedVerts;
            Gles2BatchStats bstats;
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 0u, &bstats, &atlas);
            Check(outCmds.size() == 2 && outCmds[0].cmd.textures[0] == &page &&
                      outCmds[1].cmd.textures[0] == &otherTex && bstats.atlasRemappedDraws == 2,
                  "lit non-identity treeA + treeB merge across non-overlapping otherTex");
            if (bakedVerts.size() == 6) {
                // v1=(0.2,0,0) * rot -> (-0.8, 0.2, 0), n=(1,0,0) -> (0,1,0)
                Check(bakedVerts[1].x == -0.8f && bakedVerts[1].y == 0.2f &&
                          bakedVerts[1].nx == 0.0f && bakedVerts[1].ny == 1.0f &&
                          bakedVerts[1].u0 == tA.u0 + tA.uScale && bakedVerts[1].v0 == tA.v0 + 0.5f * tA.vScale &&
                          bakedVerts[1].u1 == 0.4f && bakedVerts[1].v1 == 0.6f,
                      "lit atlas bake transforms position, normal, and remaps stage-0 UV");
                // treeB v0=(0,0,0) * Translate(-0.5) -> (-0.5, 0, 0), n=(1,0,0)
                Check(bakedVerts[3].x == -0.5f && bakedVerts[3].nx == 1.0f &&
                          bakedVerts[3].u0 == tB.u0 && bakedVerts[3].v0 == tB.v0,
                      "second lit atlas draw transforms position, normal, and remaps stage-0 UV");
            }
        }

        // 12g. EmitBakedSlice straddling 65536 retry path with atlas remapping.
        {
            GfxFrame frame;
            frame.states.push_back(treeState);
            frame.worlds = { Translate(-0.5f), Translate(0.5f) };
            frame.indices = { 0, 1, 2 };
            GfxCommand c0 = MakeDraw(GFX_TRIANGLELIST, false, 0, 0, &treeA, NULL, 3, 0, 3);
            c0.buffer = &vb;
            GfxCommand c1 = MakeDraw(GFX_TRIANGLELIST, false, 0, 1, &treeB, NULL, 3, 0, 3);
            c1.buffer = &vb;
            frame.commands = { c0, c1 };
            std::vector<Gles2BatchInput> in = { { 1, 0 }, { 1, 0 } };
            std::vector<GfxVertex> bakedVerts;
            Gles2BatchStats bstats;
            // bakedVertexBase = 65534 -> first 3-vert draw straddles 65536, triggering retry path.
            batcher.Run(frame, in, 1, outCmds, outIdx, &usedBatched, &bakedVerts, 65534u, &bstats, &atlas);
            Check(outCmds.size() == 2 && outCmds[0].cmd.textures[0] == &page &&
                      outCmds[1].cmd.textures[0] == &page && bstats.atlasRemappedDraws == 2 &&
                      bakedVerts.size() == 6 && bakedVerts[0].u0 == tA.u0 && bakedVerts[3].u0 == tB.u0,
                  "EmitBakedSlice 65536-straddle retry path remaps UVs and binds atlas page");
        }

        // 12h. Ineligible states (WRAP, mipFilter, texCoordIndex, textureTransform) stay unremapped.
        {
            GfxDrawState wrapState = treeState;
            wrapState.stages[0].address = GFX_ADDRESS_WRAP;
            GfxDrawState mipState = treeState;
            mipState.stages[0].mipFilter = GFX_MIPFILTER_LINEAR;
            GfxDrawState tciState = treeState;
            tciState.stages[0].texCoordIndex = 1u;
            GfxDrawState ttffState = treeState;
            ttffState.stages[0].textureTransform = GFX_TTFF_COUNT2;
            Check(!Gles2BatchDetail::IsAtlasEligibleState(wrapState), "WRAP address rejected for atlas");
            Check(!Gles2BatchDetail::IsAtlasEligibleState(mipState), "mipFilter != NONE rejected for atlas");
            Check(!Gles2BatchDetail::IsAtlasEligibleState(tciState), "texCoordIndex != 0 rejected for atlas");
            Check(!Gles2BatchDetail::IsAtlasEligibleState(ttffState), "textureTransform != DISABLE rejected for atlas");
        }
    }

    if (failures == 0)
        std::puts("gles2_batch: ok");
    return failures != 0;
}
