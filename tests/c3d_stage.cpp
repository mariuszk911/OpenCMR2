// Stage C3D layout, bounds, payload decode and mesh range check
// (src/port/c3d_stage.cpp). The fixture is written through WriteLE*, so the
// forced big-endian variant proves the swap path on the little-endian host.
#include "port/types.h"

#include "Mesh.h"
#include "port/c3d_stage.h"
#include "port/endian.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int Fail(const char *what, int line)
{
    std::printf("c3d_stage: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

typedef std::vector<unsigned char> Bytes;

void Put16(Bytes &b, size_t at, unsigned int v) { WriteLE16(&b[at], (uint16_t)v); }
void Put32(Bytes &b, size_t at, unsigned int v) { WriteLE32(&b[at], (uint32_t)v); }

unsigned int Host32(const unsigned char *p)
{
    unsigned int v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// 2 nodes, 1 mesh + 1 object mesh, 1 object, 1 sector with 1 portal,
// 2 triangles, 3 vertices, 1 vertex flag record, 1 mesh record, 1 texture.
const size_t kNodes = 0x30;
const size_t kMeshes = kNodes + 2 * 0x18c;
const size_t kObjects = kMeshes + 2 * 0x120;
const size_t kSectors = kObjects + 0xa0;
const size_t kTriangles = kSectors + 0x88 + 0x1c;
const size_t kVertexData = kTriangles + 2 * 0x4c;
const size_t kLightLevels = kVertexData + 3 * 0x30;
const size_t kVertexFlags = kLightLevels + 3 * 4;
const size_t kRecords = kVertexFlags + 0x14;
const size_t kEnd = kRecords + 0x5c;
const size_t kNames = kEnd + 0x20; // texture records sit between kEnd and here
const size_t kMember = kNames + 0x104;

Bytes MakeC3d()
{
    Bytes b(kMember, 0x5a);
    size_t i;

    Put32(b, 0x04, 6);          // textureFile
    Put32(b, 0x0c, (unsigned)kNames);
    Put32(b, 0x10, 1);          // vertexFlagCount
    Put32(b, 0x14, 3);          // vertexCount
    Put16(b, 0x18, 2);          // nodes
    Put16(b, 0x1a, 1);          // meshes
    Put16(b, 0x1c, 1);          // objects
    Put16(b, 0x1e, 1);          // sectors
    Put32(b, 0x20, 2);          // triangles
    Put16(b, 0x24, 1);          // textures
    Put16(b, 0x26, 1);          // records
    Put16(b, 0x28, 1);          // portals
    for (i = 0; i < 2; i++) {
        size_t t = kTriangles + i * 0x4c;
        Put16(b, t, 0x10 + (unsigned)i);
        Put32(b, t + 4, 3);                // texture 0
        Put32(b, t + 8, 0xffffffffu);      // "none"
        Put32(b, t + 12, 0x200 + (unsigned)i);
    }
    for (i = 0; i < 36; i++)
        Put32(b, kVertexData + i * 4, 0x90000000u + (unsigned)i);
    for (i = 0; i < 3; i++)
        Put32(b, kLightLevels + i * 4, 0x300 + (unsigned)i);
    for (i = 0; i < 5; i++)
        Put32(b, kVertexFlags + i * 4, 0xa0000000u + (unsigned)i);
    return b;
}

const C3DStageLimits kRoom = {100, 100, 100, 100, 100};

int TestMeasureAndDecode()
{
    Bytes b = MakeC3d();
    C3DHeaderValues header;
    C3DStageLayout layout;
    C3DStageArrays arrays;
    Bytes payload;
    int i;

    CHECK(C3DStage_Measure(b.data(), (unsigned)b.size(), &kRoom, &header, &layout) == C3D_STAGE_OK);
    CHECK(header.nodeCount == 2 && header.meshCount == 1 && header.objectCount == 1);
    CHECK(layout.nodes == b.data() + kNodes && layout.meshes == b.data() + kMeshes);
    CHECK(layout.objects == b.data() + kObjects && layout.sectors == b.data() + kSectors);
    CHECK(layout.triangles == b.data() + kTriangles && layout.vertexData == b.data() + kVertexData);
    CHECK(layout.lightLevels == b.data() + kLightLevels && layout.vertexFlags == b.data() + kVertexFlags);
    CHECK(layout.records == b.data() + kRecords && layout.end == b.data() + kEnd);
    CHECK((unsigned char *)layout.textureRecords == b.data() + kNames);
    CHECK(layout.triangleBytes == 152 && layout.vertexBytes == 144);
    CHECK(layout.lightBytes == 16 && layout.flagBytes == 24 && layout.payloadBytes == 336);
    // An unknown extent skips the member checks only.
    CHECK(C3DStage_Measure(b.data(), 0, &kRoom, &header, &layout) == C3D_STAGE_OK);
    CHECK(C3DStage_Measure(b.data(), (unsigned)b.size() + 50, &kRoom, &header, &layout) == C3D_STAGE_OK);

    CHECK(C3DStage_Measure(b.data(), (unsigned)b.size(), &kRoom, &header, &layout) == C3D_STAGE_OK);
    payload.assign((size_t)layout.payloadBytes, 0xee);
    C3DStage_DecodePayload(payload.data(), &layout, &header, 100, &arrays);
    CHECK((unsigned char *)arrays.triangles == payload.data());
    CHECK(arrays.vertexData == payload.data() + 152);
    CHECK((unsigned char *)arrays.lightLevels == payload.data() + 152 + 144);
    CHECK(arrays.vertexFlags == payload.data() + 152 + 144 + 16);
    CHECK(arrays.triangles[0].flags == 0x10 && arrays.triangles[1].flags == 0x11);
    CHECK(arrays.triangles[0].textures[0] == 103 && arrays.triangles[1].textures[0] == 103);
    CHECK(arrays.triangles[0].textures[1] == -1);          // "none" is not rebased
    CHECK(arrays.triangles[1].textures[2] == 0x201 + 100);
    for (i = 0; i < 36; i++)
        CHECK(Host32(arrays.vertexData + i * 4) == 0x90000000u + (unsigned)i);
    for (i = 0; i < 3; i++)
        CHECK(arrays.lightLevels[i] == 0x300 + i);
    for (i = 0; i < 5; i++)
        CHECK(Host32(arrays.vertexFlags + i * 4) == 0xa0000000u + (unsigned)i);
    // The padding after the light levels is not touched.
    CHECK(payload[152 + 144 + 12] == 0xee && payload[152 + 144 + 15] == 0xee);
    return 0;
}

int TestRejects()
{
    Bytes b = MakeC3d();
    C3DHeaderValues header;
    C3DStageLayout layout;
    C3DStageLimits limits;
    const unsigned int size = (unsigned)b.size();

    CHECK(C3DStage_Measure(NULL, size, &kRoom, &header, &layout) == C3D_STAGE_BAD_HEADER);
    CHECK(C3DStage_Measure(b.data(), 0x2f, &kRoom, &header, &layout) == C3D_STAGE_SHORT_MEMBER);
    // Each registration table has its own limit.
    limits = kRoom; limits.nodes = 1;
    CHECK(C3DStage_Measure(b.data(), size, &limits, &header, &layout) == C3D_STAGE_CAPACITY);
    limits = kRoom; limits.meshes = 0;
    CHECK(C3DStage_Measure(b.data(), size, &limits, &header, &layout) == C3D_STAGE_CAPACITY);
    limits = kRoom; limits.objects = 0;
    CHECK(C3DStage_Measure(b.data(), size, &limits, &header, &layout) == C3D_STAGE_CAPACITY);
    limits = kRoom; limits.records = 0;
    CHECK(C3DStage_Measure(b.data(), size, &limits, &header, &layout) == C3D_STAGE_CAPACITY);
    limits = kRoom; limits.sectors = 0;
    CHECK(C3DStage_Measure(b.data(), size, &limits, &header, &layout) == C3D_STAGE_CAPACITY);
    limits = kRoom; limits.nodes = 2;
    CHECK(C3DStage_Measure(b.data(), size, &limits, &header, &layout) == C3D_STAGE_OK);
    // Member extent: the names block must fit and sit after the records.
    CHECK(C3DStage_Measure(b.data(), size - 1, &kRoom, &header, &layout) == C3D_STAGE_EXCEEDS_MEMBER);
    CHECK(C3DStage_Measure(b.data(), (unsigned)kNames, &kRoom, &header, &layout) == C3D_STAGE_EXCEEDS_MEMBER);
    Put32(b, 0x0c, (unsigned)kEnd - 1);
    CHECK(C3DStage_Measure(b.data(), size, &kRoom, &header, &layout) == C3D_STAGE_EXCEEDS_MEMBER);
    Put32(b, 0x0c, (unsigned)kNames);
    Put16(b, 0x24, 0xffff);
    CHECK(C3DStage_Measure(b.data(), size, &kRoom, &header, &layout) == C3D_STAGE_EXCEEDS_MEMBER);
    Put16(b, 0x24, 1);
    // Huge counts walk past the member.
    Put32(b, 0x20, 0x00ffffffu);
    CHECK(C3DStage_Measure(b.data(), size, &kRoom, &header, &layout) == C3D_STAGE_EXCEEDS_MEMBER);
    CHECK(std::strcmp(C3DStage_StatusText(C3D_STAGE_CAPACITY), "ok") != 0);
    return 0;
}

int TestMeshArrays()
{
    Bytes b = MakeC3d();
    C3DHeaderValues header;
    C3DStageLayout layout;
    C3DStageArrays arrays;
    Bytes payload;
    Mesh mesh;

    CHECK(C3DStage_Measure(b.data(), (unsigned)b.size(), &kRoom, &header, &layout) == C3D_STAGE_OK);
    payload.assign((size_t)layout.payloadBytes, 0);
    C3DStage_DecodePayload(payload.data(), &layout, &header, 0, &arrays);

    std::memset(&mesh, 0, sizeof(mesh));
    mesh.pTriangles = arrays.triangles + 1;
    mesh.triangleCount = 1;
    mesh.pVertexData = (DWORD *)(arrays.vertexData + 0x30);
    mesh.field_0x10 = 2;
    mesh.pLightLevels = arrays.lightLevels + 1;
    CHECK(C3DStage_MeshArraysValid(&mesh, &arrays, &header) == 1);

    mesh.triangleCount = 2;                      // one triangle too many
    CHECK(C3DStage_MeshArraysValid(&mesh, &arrays, &header) == 0);
    mesh.triangleCount = 1;
    mesh.field_0x10 = 3;                         // vertices run past the array
    CHECK(C3DStage_MeshArraysValid(&mesh, &arrays, &header) == 0);
    mesh.field_0x10 = 2;
    mesh.pLightLevels = (int *)((unsigned char *)arrays.lightLevels + 2); // misaligned
    CHECK(C3DStage_MeshArraysValid(&mesh, &arrays, &header) == 0);
    mesh.pLightLevels = arrays.lightLevels + 1;
    mesh.pTriangles = NULL;
    CHECK(C3DStage_MeshArraysValid(&mesh, &arrays, &header) == 0);
    mesh.pTriangles = arrays.triangles;
    mesh.pVertexData = NULL;
    CHECK(C3DStage_MeshArraysValid(&mesh, &arrays, &header) == 0);
    return 0;
}

} // namespace

int main()
{
    int failed = TestMeasureAndDecode();

    if (failed == 0)
        failed = TestRejects();
    if (failed == 0)
        failed = TestMeshArrays();
    if (failed == 0)
        std::printf("c3d_stage: OK\n");
    return failed;
}
