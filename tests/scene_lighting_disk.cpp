// Stage lighting (.tre) decoder (src/port/scene_lighting_disk.cpp). The
// fixture is written through WriteLE*, so the forced big-endian variant
// proves the swap path on the little-endian host.
#include "port/types.h"

#include "LightZone.h"
#include "port/endian.h"
#include "port/scene_lighting_disk.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int Fail(const char *what, int line)
{
    std::printf("scene_lighting_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

typedef std::vector<unsigned char> Bytes;

void Put16(Bytes &b, size_t at, unsigned int v) { WriteLE16(&b[at], (uint16_t)v); }
void Put32(Bytes &b, size_t at, unsigned int v) { WriteLE32(&b[at], (uint32_t)v); }

const size_t kZones = 50;                      // two zones, 0x14 each
const size_t kItems = kZones + 2 * 0x14;       // three items, 0x30 each
const size_t kMeshes = kItems + 3 * 0x30;      // two mesh headers, 0x14 each
const size_t kVertices = kMeshes + 2 * 0x14;   // (1 + 2) * 0x30
const size_t kTriangles = kVertices + 3 * 0x30; // 1 * 0x4c
const size_t kLevels = kTriangles + 0x4c;      // 3 * 4
const size_t kEnd = kLevels + 12;

Bytes MakeTre()
{
    Bytes b(kEnd, 0xab);
    size_t at;
    int i;

    for (i = 0; i < 3; i++)
        Put32(b, (size_t)i * 4, 0x10000u + (unsigned)i);
    for (i = 0; i < 9; i++)
        Put32(b, 12 + (size_t)i * 4, 0xfffe0000u + (unsigned)i);
    Put16(b, 48, 2);
    // zone 0: sector -1, one item; zone 1: sector 5, two items
    Put16(b, kZones + 0, 0xffff); Put16(b, kZones + 2, 1);
    Put32(b, kZones + 4, 111); Put32(b, kZones + 8, 0xffffff00u);
    Put16(b, kZones + 0x14 + 0, 5); Put16(b, kZones + 0x14 + 2, 2);
    Put32(b, kZones + 0x14 + 4, 333); Put32(b, kZones + 0x14 + 8, 444);
    for (i = 0; i < 3; i++) {
        at = kItems + (size_t)i * 0x30;
        Put32(b, at + 0x00, 1000u + (unsigned)i);
        Put32(b, at + 0x04, 2000u + (unsigned)i);
        Put32(b, at + 0x20, 0x10000u + (unsigned)i);
        Put32(b, at + 0x28, 7u + (unsigned)i);
    }
    Put32(b, kMeshes + 0, 1); Put32(b, kMeshes + 4, 1);
    Put32(b, kMeshes + 0x14 + 0, 0); Put32(b, kMeshes + 0x14 + 4, 2);
    for (i = 0; i < 36; i++)
        Put32(b, kVertices + (size_t)i * 4, 0x80000000u + (unsigned)i);
    Put16(b, kTriangles + 0x00, 0x1234);
    for (i = 0; i < 10; i++)
        Put32(b, kTriangles + 4 + (size_t)i * 4, 0xc0000000u + (unsigned)i);
    Put16(b, kTriangles + 0x40, 7); Put16(b, kTriangles + 0x42, 8); Put16(b, kTriangles + 0x44, 9);
    for (i = 0; i < 3; i++)
        Put32(b, kLevels + (size_t)i * 4, 0x7000u + (unsigned)i);
    return b;
}

unsigned int Host32(const unsigned char *p)
{
    unsigned int v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

unsigned int Host16(const unsigned char *p)
{
    unsigned short v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

void Release(SceneLightingDisk &d)
{
    std::free(d.native);
    std::free(d.payload);
    d.native = NULL;
    d.payload = NULL;
}

int TestDecode()
{
    Bytes tre = MakeTre();
    Bytes shifted(tre.size() + 1);
    std::memcpy(&shifted[1], tre.data(), tre.size()); // unaligned on purpose
    SceneLightingDisk d;
    LightZone *zones;
    LightZoneMesh *meshes;
    int i;

    CHECK(SceneLightingDisk_Decode(&shifted[1], (unsigned)tre.size(), &d) == SCENE_LIGHTING_OK);
    CHECK(d.zoneCount == 2 && d.native != NULL && d.payload != NULL);
    CHECK(d.lightDir[0] == 0x10000 && d.lightDir[1] == 0x10001 && d.lightDir[2] == 0x10002);
    for (i = 0; i < 9; i++)
        CHECK(d.basis[i / 3][i % 3] == (int)(0xfffe0000u + (unsigned)i));
    zones = (LightZone *)d.native;
    meshes = (LightZoneMesh *)(zones + 2);
    CHECK(zones[0].sector == -1 && zones[0].itemCount == 1 && zones[0].x == 111 &&
          zones[0].z == (int)0xffffff00u);
    CHECK(zones[1].sector == 5 && zones[1].itemCount == 2 && zones[1].x == 333 && zones[1].z == 444);
    CHECK(zones[0].pMesh == &meshes[0] && zones[1].pMesh == &meshes[1]);
    CHECK(zones[1].pItems == zones[0].pItems + 1);
    CHECK(zones[0].pItems[0].ownerX == 1000 && zones[0].pItems[0].ownerZ == 2000 &&
          zones[0].pItems[0].intensity == 0x10000 && zones[0].pItems[0].vertexCount == 7);
    CHECK(zones[1].pItems[1].ownerX == 1002 && zones[1].pItems[1].vertexCount == 9);
    CHECK(zones[0].pItems[0].pOwner == NULL);
    CHECK(meshes[0].triangleCount == 1 && meshes[0].vertexCount == 1);
    CHECK(meshes[1].triangleCount == 0 && meshes[1].vertexCount == 2);
    CHECK((unsigned char *)meshes[0].pVertices == d.payload);
    CHECK((unsigned char *)meshes[1].pVertices == d.payload + 0x30);
    CHECK((unsigned char *)meshes[0].pTriangles == d.payload + 3 * 0x30);
    CHECK((unsigned char *)meshes[0].pLevels == d.payload + 3 * 0x30 + 0x4c);
    CHECK((unsigned char *)meshes[1].pLevels == d.payload + 3 * 0x30 + 0x4c + 4);
    for (i = 0; i < 36; i++)
        CHECK(Host32(d.payload + i * 4) == 0x80000000u + (unsigned)i);
    {
        const unsigned char *tri = d.payload + 3 * 0x30;
        CHECK(Host16(tri) == 0x1234);
        for (i = 0; i < 10; i++)
            CHECK(Host32(tri + 4 + i * 4) == 0xc0000000u + (unsigned)i);
        CHECK(Host16(tri + 0x40) == 7 && Host16(tri + 0x42) == 8 && Host16(tri + 0x44) == 9);
        CHECK(tri[0x48] == 0xab); // byte fields are copied unchanged
    }
    for (i = 0; i < 3; i++)
        CHECK(Host32(d.payload + 3 * 0x30 + 0x4c + i * 4) == 0x7000u + (unsigned)i);
    Release(d);
    // A longer member is accepted.
    tre.resize(tre.size() + 100, 0);
    CHECK(SceneLightingDisk_Decode(tre.data(), (unsigned)tre.size(), &d) == SCENE_LIGHTING_OK);
    Release(d);
    return 0;
}

int TestRejects()
{
    Bytes tre = MakeTre();
    SceneLightingDisk d;
    unsigned int cut;

    CHECK(SceneLightingDisk_Decode(NULL, 100, &d) == SCENE_LIGHTING_REJECTED && d.native == NULL);
    CHECK(SceneLightingDisk_Decode(tre.data(), 49, &d) == SCENE_LIGHTING_REJECTED);
    // The zone table must fit.
    Put16(tre, 48, 0x7fff);
    CHECK(SceneLightingDisk_Decode(tre.data(), (unsigned)tre.size(), &d) == SCENE_LIGHTING_REJECTED);
    Put16(tre, 48, 2);

    // Every truncation after the zone table keeps the direction and basis but
    // never reports a full decode and never leaks into the zone tables.
    for (cut = 50; cut < tre.size(); cut++) {
        int status = SceneLightingDisk_Decode(tre.data(), cut, &d);
        CHECK(status != SCENE_LIGHTING_OK);
        if (status == SCENE_LIGHTING_HEADER_ONLY) {
            CHECK(d.lightDir[0] == 0x10000);
            CHECK(d.payload == NULL);
            Release(d);
        }
    }
    // Item counts that do not fit stop before any allocation.
    tre = MakeTre();
    Put16(tre, kZones + 2, 0xffff);
    CHECK(SceneLightingDisk_Decode(tre.data(), (unsigned)tre.size(), &d) == SCENE_LIGHTING_HEADER_ONLY);
    CHECK(d.native == NULL && d.payload == NULL);
    // A negative mesh count frees the native block.
    tre = MakeTre();
    Put32(tre, kMeshes + 4, 0xffffffffu);
    CHECK(SceneLightingDisk_Decode(tre.data(), (unsigned)tre.size(), &d) == SCENE_LIGHTING_HEADER_ONLY);
    CHECK(d.native == NULL && d.payload == NULL);
    // Mesh sizes beyond the member free it too; counts near 2^31 must not wrap.
    tre = MakeTre();
    Put32(tre, kMeshes + 4, 0x7fffffffu);
    CHECK(SceneLightingDisk_Decode(tre.data(), (unsigned)tre.size(), &d) == SCENE_LIGHTING_HEADER_ONLY);
    CHECK(d.native == NULL && d.payload == NULL);
    return 0;
}

} // namespace

int main()
{
    int failed = TestDecode();

    if (failed == 0)
        failed = TestRejects();
    if (failed == 0)
        std::printf("scene_lighting_disk: OK\n");
    return failed;
}
