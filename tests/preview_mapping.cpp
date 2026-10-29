#include "port/types.h"
#include "GameInfo.h"
#include "DeformGeometry.h"
#include "OptionPreviewGeometry.h"
#include "Mesh.h"
#include "SceneNode.h"

#include <cstdio>
#include <cstring>

extern DeformVertex **g_previewOriginalVertices[16];
extern BYTE *g_previewOriginalNodeTypes[16];
extern OptionPreviewMeshRecord g_previewMeshRecords[16];
extern BYTE OptionPreview_ReleaseSessionResources(void);
void OptionPreview_AssignConvertedVertexBuffers(int param1, INT_PTR param2, OptionPreviewMeshRecord *param3);

static int expect_int(int actual, int expected, const char *what, int mesh, int vertex)
{
    if (actual != expected) {
        std::fprintf(stderr, "preview mapping: %s mesh %d vertex %d: got %d, want %d\n",
                     what, mesh, vertex, actual, expected);
        return 1;
    }
    return 0;
}

int main()
{
    // The per-slot converted source arrays are indexed by param1; slot 0 is not
    // used by any other fixture, so start from a clean record and free it below.
    CGameInfo::m_gameInfo.field_0x14 = 0;
    CGameInfo::SetPreviewMode(1);

    OptionPreviewMeshRecord &record = g_previewMeshRecords[0];
    std::memset(&record, 0, sizeof(record));

    Mesh meshes[3];
    SceneNode nodes[3];
    DeformFloatVertex source[3][2];
    std::memset(meshes, 0, sizeof(meshes));
    std::memset(nodes, 0, sizeof(nodes));
    std::memset(source, 0, sizeof(source));

    const unsigned int flags[3] = { 0x00001005, 0x00001006, 0x3200100a };
    const unsigned int colour[3] = { 0x20B090C0, 0x40112233, 0x80A0B0C0 };
    const int counts[3] = { 2, 2, 2 };

    for (int i = 0; i < 3; ++i) {
        nodes[i].flags = flags[i];
        meshes[i].pVertexData = reinterpret_cast<DWORD *>(source[i]);
        meshes[i].field_0x10 = counts[i];
        source[i][0].pos[0] = 1.5f;
        source[i][0].pos[1] = -2.25f;
        source[i][0].pos[2] = 0.5f;
        source[i][0].normal[0] = 0.25f;
        source[i][0].normal[1] = -0.75f;
        source[i][0].normal[2] = 0.125f;
        source[i][0].colour = colour[i];
        source[i][1].pos[0] = -0.5f;
        source[i][1].pos[1] = 3.0f;
        source[i][1].pos[2] = -1.25f;
        source[i][1].normal[0] = -0.5f;
        source[i][1].normal[1] = 0.5f;
        source[i][1].normal[2] = -1.0f;
        source[i][1].colour = colour[i];
        record.geometry.meshes[i] = &meshes[i];
        record.geometry.nodes[i] = &nodes[i];
        record.vertexCount[i] = static_cast<WORD>(counts[i]);
    }
    record.meshCount = 3;

    OptionPreview_AssignConvertedVertexBuffers(0, 0, &record);

    int failures = 0;
    if (record.verticesAssigned != 1) {
        std::fprintf(stderr, "preview mapping: verticesAssigned = %d, want 1\n",
                     record.verticesAssigned);
        ++failures;
    }

    if (g_previewOriginalNodeTypes[0] == nullptr) {
        std::fprintf(stderr, "preview mapping: node type array not allocated\n");
        return 1;
    }
    for (int i = 0; i < 3; ++i)
        failures += expect_int(g_previewOriginalNodeTypes[0][i],
                               static_cast<int>(flags[i] & 0xff),
                               "node type", i, 0);

    const int expectedPos[2][3] = {
        { 98304, -147456, 32768 },
        { -32768, 196608, -81920 },
    };
    const int expectedNormal[2][3] = {
        { 16384, -49152, 8192 },
        { -32768, 32768, -65536 },
    };

    for (int i = 0; i < 3; ++i) {
        const DeformVertex *block = g_previewOriginalVertices[0][i];
        if (record.geometry.vertices[i] != block) {
            std::fprintf(stderr, "preview mapping: mesh %d is not linked to its converted block\n", i);
            ++failures;
        }
        for (int v = 0; v < counts[i]; ++v) {
            failures += expect_int(block[v].pos.x, expectedPos[v][0], "pos.x", i, v);
            failures += expect_int(block[v].pos.y, expectedPos[v][1], "pos.y", i, v);
            failures += expect_int(block[v].pos.z, expectedPos[v][2], "pos.z", i, v);
            failures += expect_int(block[v].normal.x, expectedNormal[v][0], "normal.x", i, v);
            failures += expect_int(block[v].normal.y, expectedNormal[v][1], "normal.y", i, v);
            failures += expect_int(block[v].normal.z, expectedNormal[v][2], "normal.z", i, v);
            const int blue = static_cast<int>(colour[i] & 0xff);
            failures += expect_int(block[v].rawNormal[2], blue - 0x80, "rawNormal[2]", i, v);
        }
    }

    OptionPreview_ReleaseSessionResources();
    if (g_previewOriginalVertices[0] != nullptr || g_previewOriginalNodeTypes[0] != nullptr) {
        std::fprintf(stderr, "preview mapping: converted blocks were not released\n");
        ++failures;
    }

    // Duplicate node types make the converted-source mapping ambiguous. The
    // record must not claim the converted buffers were assigned.
    std::memset(&record, 0, sizeof(record));
    nodes[1].flags = flags[0];
    for (int i = 0; i < 3; ++i) {
        record.geometry.meshes[i] = &meshes[i];
        record.geometry.nodes[i] = &nodes[i];
        record.vertexCount[i] = static_cast<WORD>(counts[i]);
    }
    record.meshCount = 3;

    OptionPreview_AssignConvertedVertexBuffers(0, 0, &record);

    if (record.verticesAssigned != 0) {
        std::fprintf(stderr, "preview mapping: duplicate keys set verticesAssigned = %d, want 0\n",
                     record.verticesAssigned);
        ++failures;
    }
    if (record.geometry.vertices[0] != nullptr || record.geometry.vertices[1] != nullptr) {
        std::fprintf(stderr, "preview mapping: duplicate keys linked an ambiguous block\n");
        ++failures;
    }

    OptionPreview_ReleaseSessionResources();
    if (g_previewOriginalVertices[0] != nullptr || g_previewOriginalNodeTypes[0] != nullptr) {
        std::fprintf(stderr, "preview mapping: duplicate-key blocks were not released\n");
        ++failures;
    }

    if (failures != 0)
        return 1;
    std::puts("preview mapping: keys, positions, normals, colour bytes and duplicate-key rejection passed");
    return 0;
}
